// DS4Controller implementation.

#include "DS4Controller.h"

#if defined(ARDUINO)
#include <Arduino.h>
#include <stdio.h>
#endif

DS4Controller::DS4Controller() = default;
DS4Controller::~DS4Controller() { end(); }

bool DS4Controller::begin() {
#if defined(ARDUINO)
    Serial.println(F("DS4Arduino starting..."));
#endif
    DS4::DS4Error e = bt_.begin();
    beginError_ = e;
    if (e != DS4::DS4_OK) {
        setError(e);
#if defined(ARDUINO)
        Serial.print(F("Bluetooth initialization failed: "));
        Serial.println(DS4::DS4ErrorString(e));
#endif
        return false;
    }
#if defined(ARDUINO)
    uint8_t mac[6];
    if (bt_.localBtMac(mac)) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        Serial.print(F("ESP32 BT MAC: "));
        Serial.println(buf);
    }
    Serial.println(F("Bluetooth initialized"));
    Serial.println(F("Scanning..."));
    Serial.println(F("Hold SHARE + PS until the light bar flashes white."));
    prevBtState_ = bt_.state();
    lastRetryMs_ = millis();
#endif
    setError(DS4::DS4_OK);
    return true;
}

void DS4Controller::end() { bt_.end(); }

bool DS4Controller::connected() const { return bt_.connected(); }

const char* DS4Controller::lastErrorText() const {
    if (lastError_ != DS4::DS4_OK) {
        return DS4::DS4ErrorString(lastError_);
    }
    return DS4::DS4ErrorString(bt_.lastError());
}

const char* DS4Controller::statusText() const {
    switch (bt_.state()) {
        case DS4::BTState::Idle:
            return "Idle";
        case DS4::BTState::Initializing:
            return "Starting Bluetooth...";
        case DS4::BTState::Scanning:
#if defined(ARDUINO)
            snprintf(scanMsg_, sizeof(scanMsg_),
                     "Scanning... hold SHARE+PS (bd=%d ctl=%d inq=0x%x hits=%lu begin=%d step=%d)",
                     bt_.diagBd(), bt_.diagCtl(), bt_.diagInq(),
                     (unsigned long)bt_.discResCount(), (int)beginError_,
                     bt_.initStep());
            return scanMsg_;
#else
            return "Scanning... hold SHARE+PS";
#endif
        case DS4::BTState::Found:
            return "DS4 found";
        case DS4::BTState::Connecting:
#if defined(ARDUINO)
            snprintf(scanMsg_, sizeof(scanMsg_),
                     "Connecting... (cfmres=%d ssp=%d pin=%d auth=%d match=%02X:%02X:%02X:%02X:%02X:%02X)",
                     bt_.lastCtrlRes_, bt_.cfmReqs_, bt_.pinReqs_, bt_.authStat_,
                     bt_.matchAddr_[0], bt_.matchAddr_[1], bt_.matchAddr_[2],
                     bt_.matchAddr_[3], bt_.matchAddr_[4], bt_.matchAddr_[5]);
            return scanMsg_;
#else
            return "Connecting...";
#endif
        case DS4::BTState::Connected:
#if defined(ARDUINO)
            snprintf(scanMsg_, sizeof(scanMsg_), "Connected (send=%d rx=%d pkts=%lu cfgI=%d cfgC=%d)",
                     bt_.lastSendRes_, bt_.dataIndCount_, (unsigned long)state_.sequence,
                     bt_.cfgIndCount_, bt_.cfgCfmCount_);
            return scanMsg_;
#else
            return "Connected";
#endif
        case DS4::BTState::Disconnected:
            return "Controller disconnected";
        case DS4::BTState::Failed:
#if defined(ARDUINO)
            snprintf(scanMsg_, sizeof(scanMsg_), "%s (cfmres=%d ssp=%d pin=%d auth=%d)",
                     lastErrorText(), bt_.lastCtrlRes_, bt_.cfmReqs_, bt_.pinReqs_,
                     bt_.authStat_);
            return scanMsg_;
#else
            return lastErrorText();
#endif
    }
    return "?";
}

#if defined(ARDUINO)
void DS4Controller::onBtStateChange(DS4::BTState st) {
    switch (st) {
        case DS4::BTState::Found: {
            const uint8_t* a = bt_.lastAddress();
            char buf[20];
            snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                     a[0], a[1], a[2], a[3], a[4], a[5]);
            Serial.print(F("DS4 found: "));
            Serial.println(buf);
            break;
        }
        case DS4::BTState::Connecting:
            Serial.println(F("Connecting..."));
            break;
        case DS4::BTState::Connected:
            // Both HID L2CAP channels are up when esp_hidh reports OPEN.
            Serial.println(F("L2CAP control channel connected"));
            Serial.println(F("L2CAP interrupt channel connected"));
            Serial.println(F("DS4 connected"));
            fullModeSent_ = false;
            sendFullModeEnable();
            break;
        case DS4::BTState::Disconnected:
            Serial.println(F("Controller disconnected"));
            fullModeSent_ = false;
            break;
        case DS4::BTState::Failed:
            Serial.print(F("Connection failed: "));
            Serial.println(lastErrorText());
            break;
        default:
            break;
    }
}

void DS4Controller::sendFullModeEnable() {
    // The DS4 ships minimal 0x01 reports until the host sends an output
    // report; that flips it into full 0x11 mode (sticks+IMU+touch+battery).
    // A dim blue light bar also confirms the link on the controller.
    uint8_t out[DS4::DS4_OUTPUT_REPORT_BT_SIZE];
    DS4::DS4HID::buildOutputBt(out, 0, 0, 0, 0, 255, 0, 0,
                               true /*led*/, false /*rumble*/);
    if (bt_.sendOutputReport(out, sizeof(out))) {
        fullModeSent_ = true;
    }
}
#endif

void DS4Controller::update() {
#if defined(ARDUINO)
    DS4::BTState st = bt_.state();
    if (st != prevBtState_) {
        prevBtState_ = st;
        onBtStateChange(st);
    }
    // Phase 9: keep trying, then get out of the way. We page a few times
    // for a controller that is awake but dropped; afterwards we only
    // listen, because a woken DS4 pages us itself and simultaneous
    // paging from both ends collides forever.
    if ((st == DS4::BTState::Disconnected || st == DS4::BTState::Failed) &&
        (millis() - lastRetryMs_ > 3000)) {
        lastRetryMs_ = millis();
        if (bt_.hasAddress() && reconnTries_ < 5) {
            reconnTries_++;
            bt_.reconnect();
        } else if (!bt_.hasAddress()) {
            bt_.startScan();
        }
        // else: known address, out of page attempts -> listen for PS wake.
    }
    if (st == DS4::BTState::Found || st == DS4::BTState::Connected) {
        reconnTries_ = 0;
    }
    // If inquiry is up but completely silent, the start call may have been
    // lost to a stack race: re-issue it (prints its result code).
    if (st == DS4::BTState::Scanning) {
        if (scanRetryMs_ == 0) {
            scanRetryMs_ = millis();
            lastDiscSeen_ = bt_.discResCount();
        } else if (millis() - scanRetryMs_ > 15000) {
            scanRetryMs_ = millis();
            if (bt_.discResCount() == lastDiscSeen_) {
                Serial.print(F("Still silent ("));
                Serial.print(bt_.discResCount());
                Serial.println(F(" inquiry hits), restarting inquiry..."));
                bt_.startScan();
            }
            lastDiscSeen_ = bt_.discResCount();
        }
    } else {
        scanRetryMs_ = 0;
    }
    // Half-open recovery: control up but interrupt gone (e.g. after a
    // sleep/wake cycle) means no input will ever arrive. Reopen it.
    if (st == DS4::BTState::Connected && !bt_.intrUp() &&
        (millis() - lastIntrRetryMs_ > 5000)) {
        lastIntrRetryMs_ = millis();
        bt_.openIntr();
    }
    if (st == DS4::BTState::Connected && bt_.intrUp()) {
        lastIntrRetryMs_ = millis();
    }
    // If the enable-output raced the connection, retry once it is up.
    // Keep re-sending until input actually flows (some units ignore the
    // first SET_REPORT if config is still settling).
    if (st == DS4::BTState::Connected &&
        (millis() - lastRetryMs_ > 3000)) {
        lastRetryMs_ = millis();
        if (state_.sequence == 0) {
            sendFullModeEnable();
        }
    }
#endif

    DS4::RawReport raw;
    bool gotAny = false;
    while (bt_.popReport(raw)) {
        gotAny = true;
        lastReportId_ = raw.data[0];
        lastReportLen_ = raw.len;

        DS4::DS4Error v = DS4::DS4HID::validateInput(raw.data, raw.len);
        if (v != DS4::DS4_OK) {
            setError(v);
#if defined(ARDUINO)
            if (v == DS4::DS4_ERR_UNSUPPORTED_REPORT) {
                Serial.print(F("Unsupported report ID: 0x"));
                Serial.println(raw.data[0], HEX);
            } else {
                Serial.print(F("Invalid HID report (id=0x"));
                Serial.print(raw.data[0], HEX);
                Serial.print(F(" len="));
                Serial.print(raw.len);
                Serial.println(F(")"));
            }
#endif
            continue;
        }
        DS4::DS4State next = state_;
        if (!DS4::DS4Parser::parse(raw.data, raw.len, next)) {
            setError(DS4::DS4_ERR_INVALID_REPORT);
#if defined(ARDUINO)
            Serial.println(F("Invalid HID report"));
#endif
            continue;
        }
        next.sequence = state_.sequence + 1;
        state_ = next;
        setError(DS4::DS4_OK);
    }

    // Reflect transport-level disconnects sticky.
    if (!gotAny && !bt_.connected() && bt_.lastError() == DS4::DS4_ERR_DISCONNECTED) {
        setError(DS4::DS4_ERR_DISCONNECTED);
    }
}

bool DS4Controller::setLED(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t out[DS4::DS4_OUTPUT_REPORT_BT_SIZE];
    DS4::DS4HID::buildOutputBt(out, 0, 0, r, g, b, 0, 0, true, false);
#if defined(ARDUINO_ARCH_ESP32)
    if (connected()) {
        return bt_.sendOutputReport(out, sizeof(out));
    }
#endif
    setError(DS4::DS4_OK);
    return connected();
}

bool DS4Controller::setRumble(uint8_t small, uint8_t large) {
    uint8_t out[DS4::DS4_OUTPUT_REPORT_BT_SIZE];
    DS4::DS4HID::buildOutputBt(out, small, large, 0, 0, 0, 0, 0, false, true);
#if defined(ARDUINO_ARCH_ESP32)
    if (connected()) {
        return bt_.sendOutputReport(out, sizeof(out));
    }
#endif
    setError(DS4::DS4_OK);
    return connected();
}

bool DS4Controller::injectReport(const uint8_t* data, size_t len) {
    return bt_.injectReport(data, len);
}
