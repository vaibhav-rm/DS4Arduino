// DS4HID implementation. Layouts from hid-playstation.c.

#include "DS4HID.h"
#include "DS4Parser.h"

namespace DS4 {

DS4Error DS4HID::validateInput(const uint8_t* report, size_t len) {
    if (report == nullptr || len == 0) {
        return DS4_ERR_INVALID_REPORT;
    }
    switch (report[0]) {
        case DS4_INPUT_REPORT_BT:
            if (len < DS4_INPUT_REPORT_BT_SIZE) {
                return DS4_ERR_INVALID_REPORT;
            }
            return DS4_OK;
        case DS4_INPUT_REPORT_USB:
            if (len >= DS4_INPUT_REPORT_USB_SIZE) {
                return DS4_OK;
            }
            if (len >= DS4_INPUT_REPORT_BT_MINIMAL_SIZE) {
                return DS4_OK;  // BT minimal 0x01
            }
            return DS4_ERR_INVALID_REPORT;
        default:
            return DS4_ERR_UNSUPPORTED_REPORT;
    }
}

void DS4HID::buildOutputUsb(uint8_t* out,
                            uint8_t motorSmall, uint8_t motorLarge,
                            uint8_t r, uint8_t g, uint8_t b,
                            uint8_t blinkOn, uint8_t blinkOff,
                            bool setLed, bool setRumble) {
    // dualshock4_output_report_usb (32 bytes):
    //   [0]=0x05, common=[1..10]: valid0, valid1, reserved,
    //   motorSmall(right), motorLarge(left), R,G,B, blinkOn, blinkOff,
    //   then 21 reserved bytes.
    for (size_t i = 0; i < DS4_OUTPUT_REPORT_USB_SIZE; i++) out[i] = 0;
    out[0] = DS4_OUTPUT_REPORT_USB;
    uint8_t valid0 = 0;
    if (setRumble) valid0 |= 0x01;
    if (setLed) valid0 |= 0x02 | 0x04;
    out[1] = valid0;
    out[2] = 0x00;  // valid1 (unused on DS4)
    out[3] = 0x00;
    out[4] = motorSmall;
    out[5] = motorLarge;
    out[6] = r;
    out[7] = g;
    out[8] = b;
    out[9] = blinkOn;
    out[10] = blinkOff;
}

void DS4HID::buildOutputBt(uint8_t* out,
                           uint8_t motorSmall, uint8_t motorLarge,
                           uint8_t r, uint8_t g, uint8_t b,
                           uint8_t blinkOn, uint8_t blinkOff,
                           bool setLed, bool setRumble,
                           uint8_t pollIntervalMs) {
    // dualshock4_output_report_bt (78 bytes):
    //   [0]=0x11, [1]=hw_control, [2]=audio, common=[3..12],
    //   reserved[61], CRC32 LE.
    for (size_t i = 0; i < DS4_OUTPUT_REPORT_BT_SIZE; i++) out[i] = 0;
    out[0] = DS4_OUTPUT_REPORT_BT;
    uint8_t interval = pollIntervalMs;
    if (interval < 1) interval = 1;
    if (interval > 62) interval = 62;
    out[1] = (uint8_t)(0x80 | 0x40 | (interval & 0x3F));  // HID + CRC + poll
    out[2] = 0x00;  // audio control (mute default)
    uint8_t valid0 = 0;
    if (setRumble) valid0 |= 0x01;
    if (setLed) valid0 |= 0x02 | 0x04;
    out[3] = valid0;
    out[4] = 0x00;
    out[5] = 0x00;
    out[6] = motorSmall;
    out[7] = motorLarge;
    out[8] = r;
    out[9] = g;
    out[10] = b;
    out[11] = blinkOn;
    out[12] = blinkOff;

    uint32_t crc = outputCrc(out, DS4_OUTPUT_REPORT_BT_SIZE - 4);
    out[74] = (uint8_t)(crc & 0xFF);
    out[75] = (uint8_t)((crc >> 8) & 0xFF);
    out[76] = (uint8_t)((crc >> 16) & 0xFF);
    out[77] = (uint8_t)((crc >> 24) & 0xFF);
}

uint32_t DS4HID::crc32Step(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return crc;
}

uint32_t DS4HID::outputCrc(const uint8_t* report, size_t lenWithoutCrc) {
    uint8_t seed = DS4_CRC_SEED_OUTPUT;
    uint32_t crc = crc32Step(0xFFFFFFFFu, &seed, 1);
    crc = crc32Step(crc, report, lenWithoutCrc);
    return ~crc;
}

}  // namespace DS4
