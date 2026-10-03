#pragma once

// DS4Bluetooth: Bluetooth Classic initialisation, discovery, connection,
// reconnection and L2CAP/HID channel management.
//
// Threading: the Bluedroid/HIDH callbacks run on the BT task. They must
// never block or do heavy work. Incoming reports are copied into a
// fixed-size FreeRTOS queue; DS4Controller::update() drains that queue
// on the Arduino loop task and parses there.
//
// Transport: Classic BR/EDR HID host (PSM 0x11 control + 0x13 interrupt,
// abstracted by ESP-IDF esp_hid HID-host). BLE-only chips
// (S3/C3/C6) are rejected with DS4_ERR_NOT_SUPPORTED.

#include "DS4Types.h"

namespace DS4 {

enum class BTState : uint8_t {
    Idle = 0,
    Initializing,
    Scanning,
    Found,
    Connecting,
    Connected,
    Disconnected,
    Failed
};

struct RawReport {
    uint8_t len = 0;
    uint8_t data[78];  // max BT report size
};

class DS4Bluetooth {
public:
    static const size_t QUEUE_DEPTH = 6;

    DS4Bluetooth();
    ~DS4Bluetooth();

    // Init BT stack + start inquiry scan for a DS4 in SHARE+PS mode.
    // Returns DS4_OK or a DS4Error code. Non-blocking: connection
    // completes asynchronously; poll state()/hasReport().
    DS4Error begin();
    void end();

    // (Re)start General Inquiry scan for a DS4. Safe to call when already
    // scanning; returns the GAP call result mapped to DS4Error.
    DS4Error startScan();

    // Send an output report (LED/rumble) over the HID control channel.
    // Returns false when not connected; true means handed to the stack
    // (completion reported via HIDH events, failures via lastError).
    bool sendOutputReport(const uint8_t* data, size_t len);

    // Phase 9: attempt reconnect using the last known address.
    DS4Error reconnect();

    BTState state() const { return state_; }
    bool connected() const { return state_ == BTState::Connected; }
    DS4Error lastError() const { return lastError_; }

    const uint8_t* lastAddress() const { return lastAddr_; }
    bool hasAddress() const { return hasAddr_; }

    // Diagnostics: inquiry results seen since boot.
    uint32_t discResCount() const { return discResCount_; }
    void noteDiscRes() { discResCount_++; }

    // Last seen stack/inquiry codes for the status line.
    int diagBd() const { return diagBd_; }
    int diagCtl() const { return diagCtl_; }
    int diagInq() const { return diagInq_; }
    int initStep() const { return initStep_; }

    // Queue API: called from BT callbacks (push) and loop task (pop).
    // pushFromCallback is ISR/BT-task safe (never blocks).
    bool pushFromCallback(const uint8_t* data, size_t len);
    bool popReport(RawReport& out);  // returns false when empty
    void clearQueue();

    // Test helper: inject a report without hardware.
    bool injectReport(const uint8_t* data, size_t len) { return pushFromCallback(data, len); }

    // ESP32 Classic BT address of this board (base MAC + 2). False on stubs.
    bool localBtMac(uint8_t mac[6]);

    // Called by the (static) BT stack callbacks.
    void onHidConnected();
    void onHidDisconnected();
    void onDeviceFound(const uint8_t addr[6]);
    void onHidFailure(DS4Error e);
    void onOutputSent(DS4Error e);
    void setState(BTState s);
    void setError(DS4Error e);

    // L2CAP channel management (ESP32; no-ops elsewhere).
    bool openControl();
    void acceptIncoming(const uint8_t bda[6], uint8_t id, uint16_t lcid, bool isCtrl);
    void closeChannels();
    void setCtrlCid(uint16_t cid);
    void setIntrCid(uint16_t cid);

    // Repeating diagnostics (written from BT callbacks, read from loop()).
    int lastCtrlRes_ = -1;
    int authStat_ = 999;
    int pinReqs_ = 0;
    int cfmReqs_ = 0;
    uint8_t matchAddr_[6] = {0, 0, 0, 0, 0, 0};
    bool haveMatch_ = false;
    int lastSendRes_ = -1;
    int dataIndCount_ = 0;
    int cfgIndCount_ = 0;
    int cfgCfmCount_ = 0;

private:
    volatile BTState state_;
    volatile DS4Error lastError_;
    uint8_t lastAddr_[6];
    bool hasAddr_;
    volatile uint16_t ctrlCid_ = 0;
    volatile uint16_t intrCid_ = 0;
    volatile uint32_t discResCount_ = 0;
    int diagBd_ = -1;
    int diagCtl_ = -1;
    int diagInq_ = 0;
    int initStep_ = 0;

    bool sendHid(uint16_t cid, const uint8_t* payload, size_t len);

#if defined(ARDUINO_ARCH_ESP32)
    void* queue_ = nullptr;  // FreeRTOS QueueHandle_t, void* to keep header portable
#else
    // Host-side fallback ring buffer (PC unit tests, non-ESP32 builds).
    RawReport ring_[QUEUE_DEPTH];
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
#endif
};

}  // namespace DS4
