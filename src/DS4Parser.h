#pragma once

// DS4Parser: converts raw DualShock 4 HID reports into DS4::DS4State.
//
// Handles:
//  - Bluetooth full report  0x11 (78 bytes)
//  - Bluetooth minimal report 0x01 (10 bytes, sticks+buttons only)
//  - USB full report 0x01 (64 bytes)
//
// Pure logic, no Arduino/Bluetooth dependency. Safe to call from the
// Arduino loop; never call directly from a Bluetooth callback — drain the
// report queue in update() first (see DS4Controller).

#include "DS4Types.h"

namespace DS4 {

class DS4Parser {
public:
    // Parse one raw HID report (report[0] must be the report ID).
    // Returns true and fills `out` on success, false on invalid/
    // unsupported reports. `out.sequence` is incremented by the caller.
    static bool parse(const uint8_t* report, size_t len, DS4State& out);

    // Parse helpers exposed for unit tests.
    static bool parseBt11(const uint8_t* report, size_t len, DS4State& out);
    static bool parseUsb01(const uint8_t* report, size_t len, DS4State& out);
    static bool parseBtMinimal01(const uint8_t* report, size_t len, DS4State& out);

    // Decode the common 32-byte payload shared by USB/BT full reports.
    static void parseCommon(const DS4InputCommon& c, DS4State& out);

    // CRC-32 (IEEE, reflected) used for Bluetooth reports.
    // Seed byte (0xA1 input / 0xA2 output) is prepended per hid-playstation.
    static uint32_t crc32(const uint8_t* data, size_t len);
    static uint32_t inputCrc(const uint8_t* report, size_t lenWithoutCrc);
    static bool verifyBt11Crc(const uint8_t* report, size_t len);

private:
    static inline int16_t readI16LE(const uint8_t* p) {
        return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    }
    static inline uint16_t readU16LE(const uint8_t* p) {
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    }
    static void decodeTouch(const uint8_t* frame9, DS4TouchPoint& t0, DS4TouchPoint& t1);
};

}  // namespace DS4
