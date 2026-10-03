# DS4Arduino Research Notes

Authoritative sources: Linux `drivers/hid/hid-playstation.c`, `drivers/hid/hid-sony.c`, Chromium `device/gamepad/dualshock4_controller.cc`, PSDevWiki DS4-BT, DSRemap reverse-engineering docs, padctl device definitions, Espressif `esp_hidh` / `esp_hid_host` docs + example, and the old ESP32 host libraries (`pablomarquez76/PS4_Controller_Host`, `aed3/PS4-esp32`, `StryderUK/BluetoothHID`, `pink0D/btd_vhci`).

Do not blindly copy any of them — what follows is the synthesis that drives this library's design.

## 1. DS4 Bluetooth architecture

- Transport: Bluetooth Classic BR/EDR, HID profile.
- Two L2CAP channels: **control (PSM 0x11)** and **interrupt (PSM 0x13)**.
- HID transaction header on the wire: `0xA1` (BT HID DATA/INPUT) + report ID. ESP-IDF HID-host strips the `0xA1` framing and delivers reports starting at the report ID.
- Controller: Sony DualShock 4 CUH-ZCT1x (`054c:05c4`; ZCT2 `054c:09cc` behaves the same in HID mode).
- Host role: the ESP32 is the HID **host**; the DS4 is the HID **device**. No PS4 console involved.
- Report rate is high (order of 1 kHz bursts); the host must drain quickly and never block the BT task.

## 2. HID channels

| Channel | PSM | Purpose |
|---|---|---|
| Control | 0x11 | SDP queries, SET/GET_REPORT (feature + output), virtual cable plug/unplug |
| Interrupt | 0x13 | Asynchronous input reports (`0x01` minimal, `0x11` full) |

Phase-3 serial banner maps to HID-host `OPEN`: control connected first, then interrupt, then `DS4 connected`. ESP-IDF HID-host abstracts the two PSM connects into one host connection event, but both channels must be up before input flows.

## 3. Pairing / authentication

Three stories, newest first:

1. **SHARE+PS + SSP "just works" (preferred, this library).** Hold SHARE+PS until the light bar flashes white → controller is discoverable/pairable as `Wireless Controller`. Any modern host (phone, PC, ESP-IDF with SSP + bonding) can inquire, connect the HID profile, bond, and store the link key in NVS. No USB tool needed. Reconnects reuse the stored key.
2. **Legacy master-MAC write (old ESP32 libs).** `aed3/PS4-esp32` and its fork `pablomarquez76/PS4_Controller_Host` require the controller's stored master address to equal the ESP32 MAC — set via SixaxisPairTool over USB or by spoofing the ESP32 MAC (`PS4.begin("aa:bb:...")`). This mirrors how a DS4 binds to one PS4 (USB pairs the console MAC into the controller). It works but forces Windows/USB steps and breaks the "flash → SHARE+PS → drive" UX, so it is kept as a documented fallback only.
3. **DS3-style PIN `0000`.** Applies to DualShock 3, not DS4. Do not implement for DS4.

Implications for ESP32:

- Enable Classic BT + Bluedroid, GAP connectable/discoverable, SSP IO-capability that accepts "just works", and bonding with NVS persistence.
- Inquiry scan filters: Bluetooth name `Wireless Controller` and/or Class-of-Device peripheral/joystick/gamepad + HID service hint. Name match alone is the most robust across clones.
- Honest status: until GAP + HID-host + bonding is validated against a real controller on the target core version, the library reports `Scanning...` / `No DS4 found` / `Pairing failed` instead of pretending.

## 4. Report IDs

| ID | Size (incl. ID) | Transport | Meaning |
|---|---|---|---|
| `0x01` | 64 | USB | Full state immediately |
| `0x01` | 10 | BT (initial/minimal) | Sticks + buttons + triggers only |
| `0x11` | 78 | BT (full) | Full state: sticks, buttons, IMU, touchpad, battery, CRC |
| `0x05` | 32 | USB | Output: rumble + LED |
| `0x11` | 78 | BT | Output: rumble + LED + poll interval + CRC |
| `0x02` / 37B | USB | Feature | IMU calibration |
| `0x05` / 41B | BT | Feature | IMU calibration (with CRC) |
| `0x12` / 16B | both | Feature | Pairing info (MAC) |
| `0xA3` / 49B | both | Feature | Firmware info |

Key trap: **USB ≠ Bluetooth layout**, and Bluetooth **starts minimal** (`0x01`/10B) and switches to full (`0x11`/78B) only after the host sends an output report (Chromium) / reads calibration (other docs). Implementations that assume one layout misparse everything. This library parses all three input shapes and treats `0x01`-length as the discriminator.

## 5. Input report structure (full)

Common 32-byte payload (`dualshock4_input_report_common`, hid-playstation.c), at USB offset 1 / BT offset 3:

```
x, y, rx, ry, buttons[3], z(L2), rz(R2),
sensor_timestamp LE16, sensor_temp,
gyro[3] LE16, accel[3] LE16,
reserved[5], status[2], reserved
```

- `buttons[0]`: hat `0..7` + `8`=released (low nibble), square `0x10`, cross `0x20`, circle `0x40`, triangle `0x80`.
- `buttons[1]`: L1 `0x01`, R1 `0x02`, L2-btn `0x04`, R2-btn `0x08`, Share `0x10`, Options `0x20`, L3 `0x40`, R3 `0x80`.
- `buttons[2]`: PS `0x01`, touchpad-click `0x02`, packet counter bits `2..7` (6-bit, 0..63).
- `status[0]`: battery low nibble `0..10` (cable adds context), cable bit `0x10`.
- Touch: BT has a count byte + **4×9B frames**, USB **3×9B frames**; newest frame first. Each frame: timestamp + two 4B points `contact(bit7 inactive)+id`, `x_lo`, `x_hi/y_lo`, `y_hi` (1920×942).
- BT `0x11` framing: `[0]=0x11, [1..2]=reserved, [3..34]=common, [35]=touch count, [36..71]=touches, [72..73]=reserved, [74..77]=CRC32-LE (seed 0xA1)`.
- USB `0x01` framing: `[0]=0x01, [1..32]=common, [33]=touch count, [34..60]=touches, [61..63]=reserved`.

Minimal BT `0x01`/10B: `[1..4]` sticks, `[5..7]` the three button bytes, `[8..9]` L2/R2 analog. No IMU/touch/battery/timestamp.

## 6. Output report structure

Common 10-byte suffix: `valid0, valid1, reserved, motorSmall(right), motorLarge(left), R, G, B, blinkOn, blinkOff`.

- USB `0x05`/32B: `[0]=0x05` + common + 21 reserved.
- BT `0x11`/78B: `[0]=0x11, [1]=hw_control, [2]=audio, common, reserved[61], CRC32-LE (seed 0xA2)`.
  - `valid0`: motor `0x01`, LED `0x02`, LED-blink `0x04`.
  - `hw_control`: `0x80` HID + `0x40` CRC + poll interval `0x01..0x3E` (1..62 ms; default 4 ms); `0x3F` disables.
- Chromium BT vibration example fills `0xC0 0x20 ...` style headers plus volume bytes; hid-playstation is authoritative for the generic layout used here.

Sending any BT output report is also what flips most controllers from minimal `0x01` into full `0x11` mode — output and input enablement are coupled.

## 7. Feature reports

- `0x05`/41B (BT, CRC seed `0xA3`) / `0x02`/37B (USB): gyro/accel bias + sensitivity. Needed for calibrated deg/s and g units; this library currently exposes raw int16 and leaves calibration to a later phase.
- `0x12`/16B: pairing info (stored master MAC) — the bytes SixaxisPairTool rewrites.
- `0xA3`/49B: firmware info.

## 8. ESP32 Bluetooth APIs used

Target: original ESP32 (BR/EDR capable) + Arduino-ESP32 2.x/3.x, via ESP-IDF under Arduino:

- `nvs_flash_init` (bonding persistence), `esp_bt_controller_init/enable(BT_MODE_CLASSIC_BT)`, `esp_bluedroid_init/enable`.
- GAP: `esp_bt_gap_start_discovery(GENERAL_INQUIRY)`, `DISC_RES_EVT` (filter name/COD), `esp_bt_gap_cancel_discovery`, scan-mode + SSP/bonding security params.
- HID-host: `esp_hidh_api.h` (`esp_bt_hid_host_init/connect/disconnect/register_callback`, events `OPEN/CLOSE/DATA_IND`) or the newer `esp_hid_host` wrapper + `bluetooth/esp_hid_host` example pattern. Header names moved between IDF 4.4 and 5.x — the library isolates this in `DS4Bluetooth.cpp` behind `__has_include` + `CONFIG_BT_ENABLED` guards so core drift needs a shim, not a fork.
- FreeRTOS `xQueue` (depth 6 × 78B) from BT callbacks → `DS4Controller::update()` on the loop task. No `String`, no `delay`, no Serial I/O inside callbacks.

## 9. Differences between the old PS4 libraries

| Project | Approach | Verdict |
|---|---|---|
| `aed3/PS4-esp32` | Raw L2CAP + fixed master-MAC pairing, old core | Obsolete pairing UX; protocol offsets partly right but tied to old IDF |
| `pablomarquez76/PS4_Controller_Host` | Fork of above + gyro | Same obsolescence; useful as offset cross-check only |
| `StryderUK/BluetoothHID` | Newer HID-host attempt incl. DS4 example | Closest to the right architecture; validate against hid-playstation before reuse |
| `pink0D/btd_vhci` | Custom VHCI/BT stack builds | Requires custom IDF — explicitly out of scope for "normal Arduino" |
| Bluepad32 | Full multi-controller stack | Excellent but a runtime dependency — excluded by requirement |
| ESP-HID host example | Generic HID host (BT/BLE/USB) | Reference for API shape, not DS4 parsing |

Nothing is copied verbatim; offsets/CRCs are taken from kernel + Chromium, transport from ESP-IDF docs.

## 10. Compatibility problems with modern Arduino-ESP32

1. **HID-host header drift** (IDF 4.4 `esp_hidh_api.h` vs 5.x `esp_hid_host` naming): solved with a guarded shim in one file.
2. **BT Classic availability:** only the original ESP32 (and a few others) have BR/EDR; S3/C3/C6 are BLE-only and must fail fast with `NOT_SUPPORTED`.
3. **Bluedroid vs NimBLE:** Classic HID-host needs Bluedroid; a NimBLE-only app config breaks discovery. The library requires `CONFIG_BT_ENABLED` + Classic.
4. **NVS / bonding:** link keys must persist or every boot re-pairs; Arduino cores that skip `nvs_flash_init` need the shim to call it.
5. **BT task starvation:** Serial prints and motor PWM inside callbacks cause drops; hence the queue + parse-in-`loop()` rule.
6. **Minimal→full switch:** sketches that only read sticks will "work" on `0x01` and then misreport battery/IMU as zero — the library preserves last-known IMU/touch/battery across minimal reports and documents the output-report enable step instead of faking data.

## Open hardware validation checklist

- [ ] Inquiry sees `Wireless Controller` in SHARE+PS mode (log address).
- [ ] HID OPEN yields control + interrupt channels.
- [ ] First interrupt reports arrive (`REPORT ID: 0x11 LENGTH: 78`).
- [ ] `LX/LY/RX/RY/L2/R2` track the sticks.
- [ ] Buttons, D-pad, battery, IMU sane.
- [ ] Output `0x11` flips minimal→full and drives LED/rumble.
- [ ] Reconnect after power-cycle without re-pairing.

## 11. Hardware validation (2026-10-02, Fedora PC + BlueZ)

Controller proven good over Bluetooth Classic HID — the same profile the
ESP32 firmware will use, so these values are the ground truth for the
ESP32 bring-up:

- Controller BT address: `XX:XX:XX:XX:XX:XX` (your unit will differ), name `Wireless Controller`
- Modalais: `usb:v054Cp05C4d0100` (genuine CUH-ZCT1x, VID 054C / PID 05C4)
- Class: `0x00002508` (Peripheral / joystick), `LegacyPairing: no` (SSP),
  HID UUID `00001124`, icon `input-gaming`
- Pairing: SHARE+PS flashing-white → inquiry finds it, SSP just-works
  bonds with no PIN and no SixaxisPairTool. Bond persists in BlueZ.
- On connect, kernel exposes 3 nodes (hid-playstation): gamepad
  (`BTN_SOUTH/EAST/NORTH/WEST`, `ABS_X/Y/RX/RY` 0..255), Motion Sensors,
  Touchpad; plus `ps-controller-battery-*` (read 45%, Discharging).
- Live `evtest`: right stick swept 61..253, gyro/accel streaming.

ESP32 board on `/dev/ttyUSB0`:

- `ESP32-D0WD-V3` (rev 3.1), Wi-Fi + BT (Classic-capable), 4 MB flash.
- Base MAC `XX:XX:XX:XX:XX:XY` → BT MAC `XX:XX:XX:XX:XX:XZ` (base+2),
  matching the address supplied by the user.
- Current flash image is unresponsive after reset (no boot log at
  115200); needs the DS4Arduino firmware flashed. Arduino-ESP32
  toolchain download was ~50 KB/s in this environment, so the ESP32
  RF test is deferred to a machine with normal CDN access.

## 12. ESP32 bring-up findings (2026-10-03, Arduino-ESP32 3.3.11, DOIT DEVKIT V1)

Validated end-to-end on real hardware: SHARE+PS pairing, SSP bond,
L2CAP control+interrupt, config, SET_REPORT output, full 0x11 input at
stick rate. Hard-won facts:

1. **Prebuilt `libbt.a` has no HID-host profile.** `nm` shows GAP/SPP/A2DP
   but zero `esp_bt_hid_host_*` / `esp_bt_l2cap_*` symbols, even though the
   public headers ship. The L2CA_* core (used by SPP) IS present, so the
   library drives HID directly over L2CAP (PSMs 0x11/0x13). Declarations in
   `src/DS4L2Cap.h` were verified field-by-field against IDF v5.4
   `l2c_api.h` / `btm_api.h` after two real bugs: a missing
   QoS-violation slot shifted the callback table, and `L2CA_Register`
   takes 2 args (not 7). `L2CA_DataWrite` returns TRUE(1) on success.
2. **Classic BT RAM is freed at boot.** `initArduino()` releases Classic
   controller memory unless a library is detected. A Classic-BT Arduino
   library MUST `#include "esp32-hal-alloc-bt-classic-mem.h"` (constructor
   sets `_btClassicLibraryInUse`) or every controller init fails.
3. **Use `btStartMode(BT_MODE_CLASSIC_BT)`, not hand-rolled init.** It sets
   `cfg.mode` to match the enable call and waits for status transitions.
   Hand-rolled init with the default config fails with INVALID_STATE.
4. **BTM must own pairing.** With no security registered, L2CAP fails
   with AUTH_FAILURE (result 5) and zero GAP events. `BTM_SetSecurityLevel`
   (OUT+IN auth/encrypt on both PSMs, service IDs 32/34) makes the stack
   run SSP just-works itself; afterwards the DS4 bonds and reconnects
   need no re-pairing. Stale link keys are dropped via
   `esp_bt_gap_remove_bond_device` before connecting.
5. **L2CAP config must carry an MTU.** Empty ConfigReq/ConfigRsp deadlocks:
   channels report connected but the DS4 never sends. Requesting MTU 672
   unblocks both directions (cfg confirm + indications observed).
6. **DS4 sends minimal 0x01 (11B on the wire) before full 0x11 (79B).**
   Strip the 0xA1 HID header per channel; the parser already handles both.
7. **Toolchain gotchas:** `arduino-cli upload` after a separate `compile`
   can flash a stale cache dir — use single `compile --upload` and wipe
   the sketch cache when prints don't match the source. CP210x captures
   need tolerant parsing (duplicated/garbled bursts at 115200).
