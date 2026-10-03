#pragma once

// DS4HID: HID report validation and output-report construction.
//
// Pure logic (no Bluetooth stack). Builds the exact byte layouts from
// Linux drivers/hid/hid-playstation.c so the controller recognises them:
//
//  - USB output 0x05 (32 bytes)
//  - BT  output 0x11 (78 bytes, CRC-32 seed 0xA2)
//
// Also validates incoming report IDs / lengths and produces the
// Phase-4 style diagnostics ("REPORT ID: 0x11 / LENGTH: ...").

#include "DS4Types.h"

namespace DS4 {

class DS4HID {
public:
    // Validate an incoming report. Returns DS4_OK or a specific error.
    static DS4Error validateInput(const uint8_t* report, size_t len);

    // Build a USB output report (0x05, 32 bytes) into `out` (must hold 32B).
    // validFlag: bit0 motor, bit1 LED, bit2 LED blink.
    static void buildOutputUsb(uint8_t* out,
                               uint8_t motorSmall, uint8_t motorLarge,
                               uint8_t r, uint8_t g, uint8_t b,
                               uint8_t blinkOn, uint8_t blinkOff,
                               bool setLed, bool setRumble);

    // Build a Bluetooth output report (0x11, 78 bytes) into `out` (must hold 78B).
    // hwControl top bits select CRC/HID framing; pollIntervalMs 1..62.
    static void buildOutputBt(uint8_t* out,
                              uint8_t motorSmall, uint8_t motorLarge,
                              uint8_t r, uint8_t g, uint8_t b,
                              uint8_t blinkOn, uint8_t blinkOff,
                              bool setLed, bool setRumble,
                              uint8_t pollIntervalMs = 4);

    // CRC for a Bluetooth OUTPUT report (seed 0xA2 over bytes except last 4).
    static uint32_t outputCrc(const uint8_t* report, size_t lenWithoutCrc);

private:
    static uint32_t crc32Step(uint32_t crc, const uint8_t* data, size_t len);
};

}  // namespace DS4
