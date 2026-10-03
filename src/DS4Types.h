#pragma once

// DS4Types: shared constants, report layouts and the stable DS4State
// structure exposed to Arduino users.
//
// Design rules:
//  - No Arduino.h dependency here so the parser can be unit-tested on a PC.
//  - Fixed-size structures only, no dynamic allocation.
//  - Offsets follow Linux drivers/hid/hid-playstation.c and
//    Chromium device/gamepad/dualshock4_controller.cc.
//    See docs/RESEARCH.md for the full analysis.

#include <stdint.h>
#include <stddef.h>

namespace DS4 {

// ---------------------------------------------------------------------------
// Device identity
// ---------------------------------------------------------------------------

static const uint16_t DS4_VID = 0x054c;
static const uint16_t DS4_PID_CUH_ZCT1 = 0x05c4;  // CUH-ZCT1x (also covers most ZCT2 in HID mode)
static const uint16_t DS4_PID_ZCT2 = 0x09cc;      // CUH-ZCT2x second revision

// Expected Bluetooth device name in SHARE+PS pairing mode.
static const char DS4_BT_NAME[] = "Wireless Controller";

// ---------------------------------------------------------------------------
// HID report IDs and sizes (bytes, INCLUDING the report-ID byte)
// ---------------------------------------------------------------------------

static const uint8_t DS4_INPUT_REPORT_USB = 0x01;
static const size_t DS4_INPUT_REPORT_USB_SIZE = 64;

static const uint8_t DS4_INPUT_REPORT_BT_MINIMAL = 0x01;
static const size_t DS4_INPUT_REPORT_BT_MINIMAL_SIZE = 10;

static const uint8_t DS4_INPUT_REPORT_BT = 0x11;
static const size_t DS4_INPUT_REPORT_BT_SIZE = 78;

static const uint8_t DS4_OUTPUT_REPORT_USB = 0x05;
static const size_t DS4_OUTPUT_REPORT_USB_SIZE = 32;

static const uint8_t DS4_OUTPUT_REPORT_BT = 0x11;
static const size_t DS4_OUTPUT_REPORT_BT_SIZE = 78;

// Feature reports (calibration / pairing / firmware).
static const uint8_t DS4_FEATURE_CALIBRATION_BT = 0x05;
static const size_t DS4_FEATURE_CALIBRATION_BT_SIZE = 41;
static const uint8_t DS4_FEATURE_CALIBRATION_USB = 0x02;
static const size_t DS4_FEATURE_CALIBRATION_USB_SIZE = 37;

// L2CAP PSMs for the HID profile (for documentation / raw L2CAP fallback).
static const uint16_t DS4_PSM_HID_CONTROL = 0x11;
static const uint16_t DS4_PSM_HID_INTERRUPT = 0x13;

// CRC seeds used on Bluetooth (Linux hid-playstation.c).
static const uint8_t DS4_CRC_SEED_INPUT = 0xA1;
static const uint8_t DS4_CRC_SEED_OUTPUT = 0xA2;
static const uint8_t DS4_CRC_SEED_FEATURE = 0xA3;

// ---------------------------------------------------------------------------
// D-pad hat encoding (low nibble of buttons byte 0)
// ---------------------------------------------------------------------------

enum DS4Dpad : uint8_t {
    DS4_DPAD_N = 0,
    DS4_DPAD_NE = 1,
    DS4_DPAD_E = 2,
    DS4_DPAD_SE = 3,
    DS4_DPAD_S = 4,
    DS4_DPAD_SW = 5,
    DS4_DPAD_W = 6,
    DS4_DPAD_NW = 7,
    DS4_DPAD_RELEASED = 8
};

// ---------------------------------------------------------------------------
// Errors reported through DS4Controller::lastError()
// ---------------------------------------------------------------------------

enum DS4Error : uint8_t {
    DS4_OK = 0,
    DS4_ERR_BT_INIT_FAILED,
    DS4_ERR_NO_DS4_FOUND,
    DS4_ERR_PAIRING_FAILED,
    DS4_ERR_HID_CONNECT_FAILED,
    DS4_ERR_DISCONNECTED,
    DS4_ERR_INVALID_REPORT,
    DS4_ERR_UNSUPPORTED_REPORT,
    DS4_ERR_NO_MEMORY,
    DS4_ERR_NOT_SUPPORTED,  // e.g. BLE-only chip, BT stack unavailable
    DS4_ERR_OUTPUT_FAILED
};

inline const char* DS4ErrorString(DS4Error e) {
    switch (e) {
        case DS4_OK: return "OK";
        case DS4_ERR_BT_INIT_FAILED: return "Bluetooth initialization failed";
        case DS4_ERR_NO_DS4_FOUND: return "No DS4 found";
        case DS4_ERR_PAIRING_FAILED: return "Pairing failed";
        case DS4_ERR_HID_CONNECT_FAILED: return "HID connection failed";
        case DS4_ERR_DISCONNECTED: return "Controller disconnected";
        case DS4_ERR_INVALID_REPORT: return "Invalid HID report";
        case DS4_ERR_UNSUPPORTED_REPORT: return "Unsupported report ID";
        case DS4_ERR_NO_MEMORY: return "Out of memory";
        case DS4_ERR_NOT_SUPPORTED: return "Not supported on this hardware/core";
        case DS4_ERR_OUTPUT_FAILED: return "Output report failed";
        default: return "Unknown error";
    }
}

// ---------------------------------------------------------------------------
// Stable input state. Populated by DS4Parser, consumed by sketches.
// Raw protocol bytes never leak past the parser.
// ---------------------------------------------------------------------------

struct DS4TouchPoint {
    bool active = false;
    uint8_t id = 0;        // contact id (0..127)
    uint16_t x = 0;        // 0..1920
    uint16_t y = 0;        // 0..942
};

struct DS4State {
    // Sticks and triggers: raw 0..255, centre 128.
    uint8_t leftX = 128;
    uint8_t leftY = 128;
    uint8_t rightX = 128;
    uint8_t rightY = 128;
    uint8_t l2Analog = 0;
    uint8_t r2Analog = 0;

    // Face buttons.
    bool cross = false;
    bool circle = false;
    bool square = false;
    bool triangle = false;

    // Shoulders / stick clicks.
    bool l1 = false;
    bool r1 = false;
    bool l2Btn = false;  // digital press of the trigger
    bool r2Btn = false;
    bool l3 = false;
    bool r3 = false;

    // System buttons.
    bool share = false;
    bool options = false;
    bool ps = false;
    bool touchpadBtn = false;

    // D-pad.
    DS4Dpad dpad = DS4_DPAD_RELEASED;
    bool dpadUp = false;
    bool dpadRight = false;
    bool dpadDown = false;
    bool dpadLeft = false;

    // Motion sensors: raw signed 16-bit as delivered by the controller.
    // Calibration (feature report 0x05/0x02) is applied by the host app
    // if required; raw values are exposed so behaviour matches hid-sony.
    int16_t gyroX = 0;
    int16_t gyroY = 0;
    int16_t gyroZ = 0;
    int16_t accelX = 0;
    int16_t accelY = 0;
    int16_t accelZ = 0;
    uint16_t sensorTimestamp = 0;

    // Touchpad (first two contacts of the newest touch frame).
    DS4TouchPoint touch0;
    DS4TouchPoint touch1;

    // Battery: 0..10 (Linux hid-playstation semantics). cable==true when on USB.
    uint8_t battery = 0;
    bool charging = false;
    bool cableConnected = false;

    // 6-bit packet counter from the input report (0..63).
    uint8_t packetCounter = 0;

    // Monotonic count of successfully parsed full reports.
    uint32_t sequence = 0;
};

// ---------------------------------------------------------------------------
// Common (transport-independent) input payload.
//
// Mirrors `struct dualshock4_input_report_common` in
// Linux drivers/hid/hid-playstation.c (32 bytes):
//
//   x, y, rx, ry, buttons[3], z, rz, sensor_timestamp, sensor_temp,
//   gyro[3], accel[3], reserved[5], status[2], reserved
// ---------------------------------------------------------------------------

struct DS4InputCommon {
    uint8_t x;
    uint8_t y;
    uint8_t rx;
    uint8_t ry;
    uint8_t buttons[3];
    uint8_t z;  // L2 analog
    uint8_t rz;  // R2 analog
    uint16_t sensorTimestamp;  // little-endian
    uint8_t sensorTemperature;
    int16_t gyro[3];   // little-endian signed
    int16_t accel[3];  // little-endian signed
    uint8_t reserved2[5];
    uint8_t status[2];
    uint8_t reserved3;
} __attribute__((packed));
static_assert(sizeof(DS4InputCommon) == 32, "DS4InputCommon must be 32 bytes");

}  // namespace DS4
