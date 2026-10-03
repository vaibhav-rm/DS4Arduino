// DS4Parser implementation. See DS4Parser.h and docs/RESEARCH.md.

#include "DS4Parser.h"

namespace DS4 {

bool DS4Parser::parse(const uint8_t* report, size_t len, DS4State& out) {
    if (report == nullptr || len == 0) {
        return false;
    }
    uint8_t id = report[0];
    if (id == DS4_INPUT_REPORT_BT && len >= DS4_INPUT_REPORT_BT_SIZE) {
        return parseBt11(report, len, out);
    }
    if (id == DS4_INPUT_REPORT_USB) {
        if (len >= DS4_INPUT_REPORT_USB_SIZE) {
            return parseUsb01(report, len, out);
        }
        if (len >= DS4_INPUT_REPORT_BT_MINIMAL_SIZE) {
            // Bluetooth minimal mode also uses ID 0x01 with ~10 bytes.
            return parseBtMinimal01(report, len, out);
        }
        return false;
    }
    return false;  // unsupported report ID
}

bool DS4Parser::parseBt11(const uint8_t* report, size_t len, DS4State& out) {
    // Layout (hid-playstation.c dualshock4_input_report_bt, 78 bytes):
    //   [0]    0x11
    //   [1..2] reserved
    //   [3..34] common (32 bytes)
    //   [35]   num touch frames
    //   [36..71] 4 x 9-byte touch frames
    //   [72..73] reserved
    //   [74..77] CRC32 LE (seed 0xA1)
    if (report == nullptr || len < DS4_INPUT_REPORT_BT_SIZE) {
        return false;
    }
    if (report[0] != DS4_INPUT_REPORT_BT) {
        return false;
    }

    // Copy the common payload without assuming alignment.
    DS4InputCommon c;
    const uint8_t* p = report + 3;
    c.x = p[0];
    c.y = p[1];
    c.rx = p[2];
    c.ry = p[3];
    c.buttons[0] = p[4];
    c.buttons[1] = p[5];
    c.buttons[2] = p[6];
    c.z = p[7];
    c.rz = p[8];
    c.sensorTimestamp = readU16LE(p + 9);
    c.sensorTemperature = p[11];
    c.gyro[0] = readI16LE(p + 12);
    c.gyro[1] = readI16LE(p + 14);
    c.gyro[2] = readI16LE(p + 16);
    c.accel[0] = readI16LE(p + 18);
    c.accel[1] = readI16LE(p + 20);
    c.accel[2] = readI16LE(p + 22);
    for (int i = 0; i < 5; i++) c.reserved2[i] = p[24 + i];
    c.status[0] = p[29];
    c.status[1] = p[30];
    c.reserved3 = p[31];

    parseCommon(c, out);

    // Newest touch frame is the first one (index 0). Older frames follow.
    // Each frame: [0] timestamp, [1..4] point0, [5..8] point1.
    const uint8_t* touch = report + 36;
    decodeTouch(touch, out.touch0, out.touch1);

    return true;
}

bool DS4Parser::parseUsb01(const uint8_t* report, size_t len, DS4State& out) {
    // Layout (dualshock4_input_report_usb, 64 bytes):
    //   [0]    0x01
    //   [1..32] common (32 bytes)
    //   [33]   num touch frames
    //   [34..60] 3 x 9-byte touch frames
    //   [61..63] reserved
    if (report == nullptr || len < DS4_INPUT_REPORT_USB_SIZE) {
        return false;
    }
    if (report[0] != DS4_INPUT_REPORT_USB) {
        return false;
    }
    DS4InputCommon c;
    const uint8_t* p = report + 1;
    c.x = p[0];
    c.y = p[1];
    c.rx = p[2];
    c.ry = p[3];
    c.buttons[0] = p[4];
    c.buttons[1] = p[5];
    c.buttons[2] = p[6];
    c.z = p[7];
    c.rz = p[8];
    c.sensorTimestamp = readU16LE(p + 9);
    c.sensorTemperature = p[11];
    c.gyro[0] = readI16LE(p + 12);
    c.gyro[1] = readI16LE(p + 14);
    c.gyro[2] = readI16LE(p + 16);
    c.accel[0] = readI16LE(p + 18);
    c.accel[1] = readI16LE(p + 20);
    c.accel[2] = readI16LE(p + 22);
    for (int i = 0; i < 5; i++) c.reserved2[i] = p[24 + i];
    c.status[0] = p[29];
    c.status[1] = p[30];
    c.reserved3 = p[31];

    parseCommon(c, out);

    const uint8_t* touch = report + 34;
    decodeTouch(touch, out.touch0, out.touch1);
    return true;
}

bool DS4Parser::parseBtMinimal01(const uint8_t* report, size_t len, DS4State& out) {
    // Minimal Bluetooth mode before the host enables full reports:
    // sticks + buttons + triggers only, no IMU/touch/battery.
    //   [0]=0x01 [1]=LX [2]=LY [3]=RX [4]=RY
    //   [5]=buttons0 [6]=buttons1 [7]=buttons2 [8]=L2 [9]=R2
    if (report == nullptr || len < DS4_INPUT_REPORT_BT_MINIMAL_SIZE) {
        return false;
    }
    if (report[0] != 0x01) {
        return false;
    }
    // Preserve fields the minimal report does not carry.
    DS4State keep = out;
    out.leftX = report[1];
    out.leftY = report[2];
    out.rightX = report[3];
    out.rightY = report[4];
    uint8_t b0 = report[5];
    uint8_t b1 = report[6];
    uint8_t b2 = report[7];
    out.dpad = (DS4Dpad)(b0 & 0x0F);
    out.square = (b0 & 0x10) != 0;
    out.cross = (b0 & 0x20) != 0;
    out.circle = (b0 & 0x40) != 0;
    out.triangle = (b0 & 0x80) != 0;
    out.l1 = (b1 & 0x01) != 0;
    out.r1 = (b1 & 0x02) != 0;
    out.l2Btn = (b1 & 0x04) != 0;
    out.r2Btn = (b1 & 0x08) != 0;
    out.share = (b1 & 0x10) != 0;
    out.options = (b1 & 0x20) != 0;
    out.l3 = (b1 & 0x40) != 0;
    out.r3 = (b1 & 0x80) != 0;
    out.ps = (b2 & 0x01) != 0;
    out.touchpadBtn = (b2 & 0x02) != 0;
    out.packetCounter = (uint8_t)((b2 >> 2) & 0x3F);
    out.l2Analog = report[8];
    out.r2Analog = report[9];
    // D-pad direction flags.
    out.dpadUp = out.dpadDown = out.dpadLeft = out.dpadRight = false;
    switch (out.dpad) {
        case DS4_DPAD_N: out.dpadUp = true; break;
        case DS4_DPAD_NE: out.dpadUp = out.dpadRight = true; break;
        case DS4_DPAD_E: out.dpadRight = true; break;
        case DS4_DPAD_SE: out.dpadRight = out.dpadDown = true; break;
        case DS4_DPAD_S: out.dpadDown = true; break;
        case DS4_DPAD_SW: out.dpadDown = out.dpadLeft = true; break;
        case DS4_DPAD_W: out.dpadLeft = true; break;
        case DS4_DPAD_NW: out.dpadUp = out.dpadLeft = true; break;
        default: break;
    }
    // Restore non-carried fields.
    out.gyroX = keep.gyroX;
    out.gyroY = keep.gyroY;
    out.gyroZ = keep.gyroZ;
    out.accelX = keep.accelX;
    out.accelY = keep.accelY;
    out.accelZ = keep.accelZ;
    out.sensorTimestamp = keep.sensorTimestamp;
    out.touch0 = keep.touch0;
    out.touch1 = keep.touch1;
    out.battery = keep.battery;
    out.charging = keep.charging;
    out.cableConnected = keep.cableConnected;
    return true;
}

void DS4Parser::parseCommon(const DS4InputCommon& c, DS4State& out) {
    out.leftX = c.x;
    out.leftY = c.y;
    out.rightX = c.rx;
    out.rightY = c.ry;

    uint8_t b0 = c.buttons[0];
    uint8_t b1 = c.buttons[1];
    uint8_t b2 = c.buttons[2];

    out.dpad = (DS4Dpad)(b0 & 0x0F);
    out.square = (b0 & 0x10) != 0;
    out.cross = (b0 & 0x20) != 0;
    out.circle = (b0 & 0x40) != 0;
    out.triangle = (b0 & 0x80) != 0;

    out.l1 = (b1 & 0x01) != 0;
    out.r1 = (b1 & 0x02) != 0;
    out.l2Btn = (b1 & 0x04) != 0;
    out.r2Btn = (b1 & 0x08) != 0;
    out.share = (b1 & 0x10) != 0;
    out.options = (b1 & 0x20) != 0;
    out.l3 = (b1 & 0x40) != 0;
    out.r3 = (b1 & 0x80) != 0;

    out.ps = (b2 & 0x01) != 0;
    out.touchpadBtn = (b2 & 0x02) != 0;
    out.packetCounter = (uint8_t)((b2 >> 2) & 0x3F);

    out.l2Analog = c.z;
    out.r2Analog = c.rz;

    out.dpadUp = out.dpadDown = out.dpadLeft = out.dpadRight = false;
    switch (out.dpad) {
        case DS4_DPAD_N: out.dpadUp = true; break;
        case DS4_DPAD_NE: out.dpadUp = out.dpadRight = true; break;
        case DS4_DPAD_E: out.dpadRight = true; break;
        case DS4_DPAD_SE: out.dpadRight = out.dpadDown = true; break;
        case DS4_DPAD_S: out.dpadDown = true; break;
        case DS4_DPAD_SW: out.dpadDown = out.dpadLeft = true; break;
        case DS4_DPAD_W: out.dpadLeft = true; break;
        case DS4_DPAD_NW: out.dpadUp = out.dpadLeft = true; break;
        default: break;
    }

    out.sensorTimestamp = c.sensorTimestamp;
    out.gyroX = c.gyro[0];
    out.gyroY = c.gyro[1];
    out.gyroZ = c.gyro[2];
    out.accelX = c.accel[0];
    out.accelY = c.accel[1];
    out.accelZ = c.accel[2];

    // Battery: low nibble of status[0] is 0..10 (cable adds offsets),
    // bit 4 is cable state. Matches hid-playstation semantics.
    uint8_t batt = (uint8_t)(c.status[0] & 0x0F);
    out.cableConnected = (c.status[0] & 0x10) != 0;
    out.charging = false;
    if (batt <= 10) {
        out.battery = batt;
    } else if (batt == 11) {
        out.battery = 10;
        out.charging = false;  // full
    } else {
        // Charging / error states: clamp, flag charging.
        out.battery = 10;
        out.charging = true;
    }
}

void DS4Parser::decodeTouch(const uint8_t* frame9, DS4TouchPoint& t0, DS4TouchPoint& t1) {
    // 9-byte frame: [0]=timestamp, then two 4-byte points:
    //   [0]=contact (bit7 inactive), [1]=x_lo, [2]=x_hi(lo nibble)+y_lo(hi nibble), [3]=y_hi
    // Touch resolution 1920x942.
    const uint8_t* p0 = frame9 + 1;
    const uint8_t* p1 = frame9 + 5;
    t0.active = (p0[0] & 0x80) == 0;
    t0.id = (uint8_t)(p0[0] & 0x7F);
    t0.x = (uint16_t)(p0[1] | ((uint16_t)(p0[2] & 0x0F) << 8));
    t0.y = (uint16_t)(((p0[2] >> 4) & 0x0F) | ((uint16_t)p0[3] << 4));
    t1.active = (p1[0] & 0x80) == 0;
    t1.id = (uint8_t)(p1[0] & 0x7F);
    t1.x = (uint16_t)(p1[1] | ((uint16_t)(p1[2] & 0x0F) << 8));
    t1.y = (uint16_t)(((p1[2] >> 4) & 0x0F) | ((uint16_t)p1[3] << 4));
}

// Standard reflected CRC-32 (poly 0xEDB88320), no final xor here;
// callers apply the seed + inversion per hid-playstation.
uint32_t DS4Parser::crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint32_t DS4Parser::inputCrc(const uint8_t* report, size_t lenWithoutCrc) {
    // CRC covers seed byte 0xA1 + report bytes, then inverted.
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t seed = DS4_CRC_SEED_INPUT;
    const uint8_t* p = &seed;
    for (size_t i = 0; i < 1; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    for (size_t i = 0; i < lenWithoutCrc; i++) {
        crc ^= report[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

bool DS4Parser::verifyBt11Crc(const uint8_t* report, size_t len) {
    if (report == nullptr || len < DS4_INPUT_REPORT_BT_SIZE) {
        return false;
    }
    uint32_t got = (uint32_t)report[len - 4] | ((uint32_t)report[len - 3] << 8) |
                   ((uint32_t)report[len - 2] << 16) | ((uint32_t)report[len - 1] << 24);
    uint32_t want = inputCrc(report, len - 4);
    return got == want;
}

}  // namespace DS4
