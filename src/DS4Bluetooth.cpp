// DS4Bluetooth implementation: native Classic HID host over raw L2CAP.
//
// Why raw L2CAP: Arduino-ESP32 3.x ships esp_hidh_api.h but its prebuilt
// libbt.a omits the esp_bt_hid_host_* implementation (verified with nm).
// The L2CA_* core used by SPP/A2DP *is* present, so this file drives the
// HID profile directly (control PSM 0x11 + interrupt PSM 0x13) — the same
// architecture as the classic PS4-host libraries. See DS4L2Cap.h and
// docs/RESEARCH.md.
//
// Non-ESP32 (or BLE-only) builds keep a stub so the parser stays
// host-testable; begin() reports NOT_SUPPORTED there.
//
// Pairing model:
//  - SHARE+PS (flashing white) -> General Inquiry match on GAP name
//    "Wireless Controller" (joystick Class-of-Device as backup).
//  - SSP "just works" auto-confirmed; link key persists in NVS.
//  - L2CAP control connect -> config accept -> interrupt connect ->
//    HID SET_REPORT (output 0x11) enables full input reports.

#include "DS4Bluetooth.h"

#if defined(ARDUINO_ARCH_ESP32)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "DS4L2Cap.h"
#if defined(__has_include)
#if __has_include("esp32-hal-bt.h")
#include "esp32-hal-bt.h"
#define DS4_HAVE_HAL_BT 1
#endif
// Keep Classic controller RAM: without this, initArduino() frees the
// Classic BT memory at boot (no BT library detected) and every later
// controller init fails. Same mechanism BluetoothSerial uses.
#if __has_include("esp32-hal-alloc-bt-classic-mem.h")
#include "esp32-hal-alloc-bt-classic-mem.h"
#endif
#endif
// osi_malloc_func/osi_free_func live in libbt.a; allocator.h itself is not
// on the Arduino include path, so declare the pair directly.
extern "C" {
void* osi_malloc_func(size_t size);
void osi_free_func(void* ptr);
}
#define DS4_BT_MALLOC(s) osi_malloc_func(s)
#define DS4_BT_FREE(p) osi_free_func(p)
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif
#if defined(ARDUINO)
#include <Arduino.h>
#include <stdio.h>
#endif
#endif

// BLE-only ESP32 variants have no Classic BR/EDR radio.
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || \
    defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32H2)
#define DS4_NO_CLASSIC_BT 1
#endif

namespace DS4 {

// Expected GAP device name in SHARE+PS pairing mode.
static const char kDs4GapName[] = "Wireless Controller";

DS4Bluetooth::DS4Bluetooth()
    : state_(BTState::Idle), lastError_(DS4_OK), hasAddr_(false)
#if defined(ARDUINO_ARCH_ESP32)
      ,
      queue_(nullptr)
#endif
{
    for (int i = 0; i < 6; i++) lastAddr_[i] = 0;
}

DS4Bluetooth::~DS4Bluetooth() { end(); }

void DS4Bluetooth::setState(BTState s) { state_ = s; }
void DS4Bluetooth::setError(DS4Error e) { lastError_ = e; }

void DS4Bluetooth::clearQueue() {
#if defined(ARDUINO_ARCH_ESP32)
    if (queue_ != nullptr) {
        xQueueReset((QueueHandle_t)queue_);
    }
#else
    head_ = tail_ = count_ = 0;
#endif
}

bool DS4Bluetooth::pushFromCallback(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0 || len > sizeof(RawReport::data)) {
        return false;
    }
#if defined(ARDUINO_ARCH_ESP32)
    if (queue_ == nullptr) {
        return false;
    }
    RawReport r;
    r.len = (uint8_t)len;
    for (size_t i = 0; i < len; i++) r.data[i] = data[i];
    // Never block inside the BT callback: drop oldest on overflow.
    if (xQueueSend((QueueHandle_t)queue_, &r, 0) != pdTRUE) {
        RawReport drop;
        xQueueReceive((QueueHandle_t)queue_, &drop, 0);
        return xQueueSend((QueueHandle_t)queue_, &r, 0) == pdTRUE;
    }
    return true;
#else
    if (count_ >= QUEUE_DEPTH) {
        // Drop oldest.
        tail_ = (tail_ + 1) % QUEUE_DEPTH;
        count_--;
    }
    ring_[head_].len = (uint8_t)len;
    for (size_t i = 0; i < len; i++) ring_[head_].data[i] = data[i];
    head_ = (head_ + 1) % QUEUE_DEPTH;
    count_++;
    return true;
#endif
}

bool DS4Bluetooth::popReport(RawReport& out) {
#if defined(ARDUINO_ARCH_ESP32)
    if (queue_ == nullptr) {
        return false;
    }
    return xQueueReceive((QueueHandle_t)queue_, &out, 0) == pdTRUE;
#else
    if (count_ == 0) {
        return false;
    }
    out = ring_[tail_];
    tail_ = (tail_ + 1) % QUEUE_DEPTH;
    count_--;
    return true;
#endif
}

void DS4Bluetooth::onHidConnected() {
    setError(DS4_OK);
    setState(BTState::Connected);
}

void DS4Bluetooth::onHidDisconnected() {
    setError(DS4_ERR_DISCONNECTED);
    if (state_ == BTState::Connected || state_ == BTState::Connecting) {
        setState(BTState::Disconnected);
    }
}

void DS4Bluetooth::onDeviceFound(const uint8_t addr[6]) {
    for (int i = 0; i < 6; i++) lastAddr_[i] = addr[i];
    hasAddr_ = true;
    setState(BTState::Found);
}

void DS4Bluetooth::onHidFailure(DS4Error e) {
    setError(e);
    setState(BTState::Failed);
}

void DS4Bluetooth::onOutputSent(DS4Error e) {
    if (e != DS4_OK) {
        setError(DS4_ERR_OUTPUT_FAILED);
    }
}

void DS4Bluetooth::setCtrlCid(uint16_t cid) { ctrlCid_ = cid; }
void DS4Bluetooth::setIntrCid(uint16_t cid) { intrCid_ = cid; }

bool DS4Bluetooth::localBtMac(uint8_t mac[6]) {
#if defined(ARDUINO_ARCH_ESP32) && !defined(DS4_NO_CLASSIC_BT)
    return esp_read_mac(mac, ESP_MAC_BT) == ESP_OK;
#else
    (void)mac;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// ESP32 Classic BT implementation (raw L2CAP HID host)
// ---------------------------------------------------------------------------

#if defined(ARDUINO_ARCH_ESP32) && !defined(DS4_NO_CLASSIC_BT)

namespace {

// HID L2CAP channels and framing.
static_assert(sizeof(DS4BtHdr) == 8, "BT_HDR layout mismatch");
const uint16_t kPsmCtrl = 0x11;
const uint16_t kPsmIntr = 0x13;
// HID transaction headers on the L2CAP payload.
const uint8_t kHidSetReportOutput = 0x52;  // SET_REPORT | OUTPUT
const uint8_t kHidDataInput = 0xA1;        // DATA | INPUT

DS4Bluetooth* s_inst = nullptr;

bool addrEqual(const uint8_t a[6], const uint8_t b[6]) {
    for (int i = 0; i < 6; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

#if defined(ARDUINO)
#define DS4_BT_LOG(x) Serial.println(x)
#else
#define DS4_BT_LOG(x) (void)0
#endif

// GAP name is not NUL-terminated; compare exactly.
bool gapNameIsDs4(const void* val, int len) {
    const char* want = kDs4GapName;
    int wantLen = 0;
    while (want[wantLen] != '\0') wantLen++;
    if (len != wantLen) return false;
    const char* got = (const char*)val;
    for (int i = 0; i < wantLen; i++) {
        if (got[i] != want[i]) return false;
    }
    return true;
}

bool codIsJoystick(uint32_t cod) {
    uint32_t major = (cod >> 8) & 0x1F;
    uint32_t minor = (cod >> 2) & 0x3F;
    return major == 0x05 && minor == 0x02;
}

// Discovery debug: remember recently seen addresses to log each once.
uint8_t s_seen[10][6];
int s_seenCount = 0;

bool alreadySeen(const uint8_t bda[6]) {
    for (int i = 0; i < s_seenCount; i++) {
        if (addrEqual(s_seen[i], bda)) return true;
    }
    return false;
}

void markSeen(const uint8_t bda[6]) {
    if (s_seenCount < 10) {
        for (int i = 0; i < 6; i++) s_seen[s_seenCount][i] = bda[i];
        s_seenCount++;
    }
}

#if defined(ARDUINO)
void printAddr(const char* prefix, const uint8_t bda[6]) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1], bda[2],
             bda[3], bda[4], bda[5]);
    Serial.print(prefix);
    Serial.println(buf);
}
#endif

uint8_t emptyCfg64[64];  // zeroed basic-mode config shadow (result=OK).

// Config request with an explicit MTU (some HID devices stall when the
// request carries no options at all). Layout mirrors tL2CAP_CFG_INFO:
// result u16 @0, mtu_present u8 @2, mtu u16le @4.
uint8_t mtuCfg64[64];
bool mtuCfgInit_ = false;

void ensureMtuCfg() {
    if (!mtuCfgInit_) {
        mtuCfgInit_ = true;
        for (int i = 0; i < 64; i++) mtuCfg64[i] = 0;
        mtuCfg64[2] = 1;      // mtu_present
        mtuCfg64[4] = 0xA0;   // mtu = 672 LE
        mtuCfg64[5] = 0x02;
    }
}

// --- forward declarations of channel handlers (defined below) ---
void ctrlConnectCfm(uint16_t lcid, uint16_t result);
void intrConnectCfm(uint16_t lcid, uint16_t result);
void onChannelData(uint16_t lcid, DS4BtHdr* buf, bool isCtrl);
void onChannelDisc(uint16_t lcid, bool ack_needed, bool isCtrl);

void ctrlConnectInd(const uint8_t* bda, uint16_t lcid, uint16_t psm, uint8_t id) {
    (void)psm;
    DS4Bluetooth* self = s_inst;
    if (self == nullptr) return;
    self->acceptIncoming(bda, id, lcid, true);
}
void intrConnectInd(const uint8_t* bda, uint16_t lcid, uint16_t psm, uint8_t id) {
    (void)psm;
    DS4Bluetooth* self = s_inst;
    if (self == nullptr) return;
    self->acceptIncoming(bda, id, lcid, false);
}
void ctrlConnectCfmW(uint16_t lcid, uint16_t result) { ctrlConnectCfm(lcid, result); }
void intrConnectCfmW(uint16_t lcid, uint16_t result) { intrConnectCfm(lcid, result); }
void ctrlCfgInd(uint16_t lcid, DS4L2Cfg* cfg) {
    (void)cfg;
    if (s_inst != nullptr) s_inst->cfgIndCount_++;
    L2CA_ConfigRsp(lcid, emptyCfg64);
}
void intrCfgInd(uint16_t lcid, DS4L2Cfg* cfg) {
    (void)cfg;
    if (s_inst != nullptr) s_inst->cfgIndCount_++;
    L2CA_ConfigRsp(lcid, emptyCfg64);
}
void cfgCfm(uint16_t lcid, DS4L2Cfg* cfg) {
    (void)lcid;
    (void)cfg;
    if (s_inst != nullptr) s_inst->cfgCfmCount_++;
}
void ctrlDiscInd(uint16_t lcid, uint8_t ack) { onChannelDisc(lcid, ack != 0, true); }
void intrDiscInd(uint16_t lcid, uint8_t ack) { onChannelDisc(lcid, ack != 0, false); }
void discCfm(uint16_t lcid, uint16_t result) {
    (void)result;
    onChannelDisc(lcid, false, false);
}
void ctrlDataInd(uint16_t lcid, DS4BtHdr* buf) { onChannelData(lcid, buf, true); }
void intrDataInd(uint16_t lcid, DS4BtHdr* buf) { onChannelData(lcid, buf, false); }

DS4L2AppliInfo kCtrlAppli = {ctrlConnectInd, ctrlConnectCfmW, nullptr, ctrlCfgInd,
                            cfgCfm,          ctrlDiscInd,      discCfm, nullptr,
                            ctrlDataInd,     nullptr,          nullptr, nullptr,
                            nullptr};
DS4L2AppliInfo kIntrAppli = {intrConnectInd, intrConnectCfmW, nullptr, intrCfgInd,
                            cfgCfm,          intrDiscInd,      discCfm, nullptr,
                            intrDataInd,     nullptr,          nullptr, nullptr,
                            nullptr};

void ctrlConnectCfm(uint16_t lcid, uint16_t result) {
    DS4Bluetooth* self = s_inst;
    if (self == nullptr) return;
    self->lastCtrlRes_ = (int)result;
#if defined(ARDUINO)
    Serial.print("CTRL cfm lcid=");
    Serial.print(lcid);
    Serial.print(" result=");
    Serial.println(result);
#endif
    if (result != DS4_L2CAP_CONN_OK) {
        self->onHidFailure(DS4_ERR_HID_CONNECT_FAILED);
        return;
    }
    self->setCtrlCid(lcid);
    DS4_BT_LOG("CTRL connected, opening INTR...");
    ensureMtuCfg();
    uint8_t cfgr = L2CA_ConfigReq(lcid, mtuCfg64);
#if defined(ARDUINO)
    Serial.print("CTRL ConfigReq rc=");
    Serial.println(cfgr);
#endif
    uint16_t intr = L2CA_ConnectReq(kPsmIntr, self->lastAddress());
    if (intr == 0) {
        self->onHidFailure(DS4_ERR_HID_CONNECT_FAILED);
    }
}

void intrConnectCfm(uint16_t lcid, uint16_t result) {
    DS4Bluetooth* self = s_inst;
    if (self == nullptr) return;
    if (result != DS4_L2CAP_CONN_OK) {
        self->onHidFailure(DS4_ERR_HID_CONNECT_FAILED);
        return;
    }
    self->setIntrCid(lcid);
    DS4_BT_LOG("INTR connected");
    ensureMtuCfg();
    uint8_t cfgr = L2CA_ConfigReq(lcid, mtuCfg64);
#if defined(ARDUINO)
    Serial.print("INTR ConfigReq rc=");
    Serial.println(cfgr);
#endif
    self->onHidConnected();
}

void onChannelData(uint16_t lcid, DS4BtHdr* buf, bool isCtrl) {
    DS4Bluetooth* self = s_inst;
    if (buf == nullptr) return;
    uint8_t* payload = buf->data + buf->offset;
    uint16_t len = buf->len;
    if (self != nullptr) {
        self->dataIndCount_++;
#if defined(ARDUINO)
        if (self->dataIndCount_ <= 3) {
            Serial.print(isCtrl ? "CTRL data len=" : "INTR data len=");
            Serial.print(len);
            Serial.print(" b0=0x");
            Serial.print(len > 0 ? payload[0] : 0, HEX);
            Serial.print(" b1=0x");
            Serial.println(len > 1 ? payload[1] : 0, HEX);
        }
#endif
    }
    if (self != nullptr && len >= 2 && payload[0] == kHidDataInput && !isCtrl) {
        // Interrupt DATA: strip 0xA1 header, queue the HID report.
        self->pushFromCallback(payload + 1, len - 1);
    }
    // Control-channel DATA (GET_REPORT answers) is Phase-7 work; ignored.
    DS4_BT_FREE(buf);
}

void onChannelDisc(uint16_t lcid, bool ack_needed, bool isCtrl) {
    DS4Bluetooth* self = s_inst;
    if (ack_needed) {
        L2CA_DisconnectRsp(lcid);
    }
    if (self == nullptr) return;
    if (isCtrl) {
        self->setCtrlCid(0);
    } else {
        self->setIntrCid(0);
    }
    // Either channel dropping ends the session; also close the sibling.
    self->closeChannels();
    self->onHidDisconnected();
}

void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
    DS4Bluetooth* self = s_inst;
    if (self == nullptr || param == nullptr) return;

    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            self->noteDiscRes();
            if (self->state() != BTState::Scanning) break;
            const char* name = nullptr;
            int nameLen = 0;
            bool haveCod = false;
            uint32_t cod = 0;
            for (int i = 0; i < param->disc_res.num_prop; i++) {
                esp_bt_gap_dev_prop_t* p = &param->disc_res.prop[i];
                if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME && p->val != nullptr) {
                    name = (const char*)p->val;
                    nameLen = p->len;
                } else if (p->type == ESP_BT_GAP_DEV_PROP_COD && p->val != nullptr &&
                           p->len >= (int)sizeof(uint32_t)) {
                    cod = *(uint32_t*)p->val;
                    haveCod = true;
                }
            }
#if defined(ARDUINO)
            if (!alreadySeen(param->disc_res.bda)) {
                markSeen(param->disc_res.bda);
                printAddr("SCAN: ", param->disc_res.bda);
                if (haveCod) {
                    Serial.print("  COD=0x");
                    Serial.println(cod, HEX);
                }
                if (name != nullptr) {
                    Serial.print("  NAME=");
                    for (int i = 0; i < nameLen; i++) Serial.print(name[i]);
                    Serial.println();
                }
            }
#endif
            bool match = (name != nullptr && gapNameIsDs4(name, nameLen)) ||
                         (haveCod && codIsJoystick(cod));
            if (match) {
                for (int i = 0; i < 6; i++) self->matchAddr_[i] = param->disc_res.bda[i];
                self->haveMatch_ = true;
                self->onDeviceFound(param->disc_res.bda);
                self->openControl();
            }
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
            // Inquiry window ended with no match: scan again while the user
            // is still holding SHARE+PS.
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED &&
                self->state() == BTState::Scanning) {
                esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
            }
            break;
        }
        case ESP_BT_GAP_CFM_REQ_EVT:
            // SSP "just works": accept without a PIN or display.
            self->cfmReqs_++;
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;
        case ESP_BT_GAP_KEY_NOTIF_EVT:
            break;
        case ESP_BT_GAP_PIN_REQ_EVT:
            // DS4 uses SSP, not legacy PIN. Reject so discovery continues.
            self->pinReqs_++;
            esp_bt_gap_pin_reply(param->pin_req.bda, false, 0, nullptr);
            break;
        case ESP_BT_GAP_AUTH_CMPL_EVT: {
            self->authStat_ = (int)param->auth_cmpl.stat;
            if (param->auth_cmpl.stat != ESP_BT_STATUS_SUCCESS &&
                (self->state() == BTState::Connecting ||
                 self->state() == BTState::Found)) {
                self->onHidFailure(DS4_ERR_PAIRING_FAILED);
            }
            break;
        }
        default:
            break;
    }
}

}  // namespace

bool DS4Bluetooth::openControl() {
    esp_bt_gap_cancel_discovery();
    setState(BTState::Connecting);
    DS4_BT_LOG("Opening L2CAP control channel...");
    // Drop any stale link key for this address (e.g. from an earlier life
    // of this board): a mismatched key fails LMP auth with no SSP retry.
    esp_bt_gap_remove_bond_device(lastAddr_);
    uint16_t cid = L2CA_ConnectReq(kPsmCtrl, lastAddr_);
#if defined(ARDUINO)
    Serial.print("CTRL ConnectReq cid=");
    Serial.println(cid);
#endif
    if (cid == 0) {
        onHidFailure(DS4_ERR_HID_CONNECT_FAILED);
        return false;
    }
    return true;
}

void DS4Bluetooth::acceptIncoming(const uint8_t bda[6], uint8_t id, uint16_t lcid, bool isCtrl) {
    // DS4-initiated reconnect (it pages us when woken with a stored master).
    if (!hasAddr_ || addrEqual(lastAddr_, bda)) {
        onDeviceFound(bda);
    } else if (state_ != BTState::Scanning) {
        L2CA_ConnectRsp(bda, id, lcid, 1 /*refused*/, 0);
        return;
    } else {
        onDeviceFound(bda);
    }
    L2CA_ConnectRsp(bda, id, lcid, DS4_L2CAP_CONN_OK, 0);
    if (isCtrl) {
        setCtrlCid(lcid);
        setState(BTState::Connecting);
    } else {
        setIntrCid(lcid);
        if (ctrlCid_ != 0) {
            onHidConnected();
        } else {
            // Interrupt arrived first: open control to complete the pair.
            setState(BTState::Connecting);
            uint16_t cid = L2CA_ConnectReq(kPsmCtrl, lastAddr_);
            if (cid == 0) {
                onHidFailure(DS4_ERR_HID_CONNECT_FAILED);
            }
        }
    }
}

void DS4Bluetooth::closeChannels() {
    if (ctrlCid_ != 0) {
        L2CA_DisconnectReq(ctrlCid_);
        ctrlCid_ = 0;
    }
    if (intrCid_ != 0) {
        L2CA_DisconnectReq(intrCid_);
        intrCid_ = 0;
    }
}

bool DS4Bluetooth::sendHid(uint16_t cid, const uint8_t* payload, size_t len) {
    if (cid == 0 || payload == nullptr || len == 0 || len > 256) {
        return false;
    }
    DS4BtHdr* h =
        (DS4BtHdr*)DS4_BT_MALLOC(sizeof(DS4BtHdr) + DS4_L2_TX_OFFSET + len);
    if (h == nullptr) {
        return false;
    }
    h->event = 0;
    h->len = (uint16_t)len;
    h->offset = DS4_L2_TX_OFFSET;
    h->layer_specific = 0;
    uint8_t* dst = h->data + h->offset;
    for (size_t i = 0; i < len; i++) dst[i] = payload[i];
    uint8_t rc = L2CA_DataWrite(cid, h);
    if (s_inst != nullptr) {
        s_inst->lastSendRes_ = (int)rc;
    }
#if defined(ARDUINO)
    Serial.print("DataWrite cid=");
    Serial.print(cid);
    Serial.print(" rc=");
    Serial.println(rc);
#endif
    if (rc != 0) {
        return true;  // stack owns the buffer now (DW_SUCCESS or CONGESTED)
    }
    DS4_BT_FREE(h);
    return false;
}

DS4Error DS4Bluetooth::begin() {
    if (state_ == BTState::Scanning || state_ == BTState::Connected ||
        state_ == BTState::Connecting) {
        return DS4_OK;
    }
    setState(BTState::Initializing);
    setError(DS4_OK);
    s_inst = this;
    initStep_ = 1;

    if (queue_ == nullptr) {
        queue_ = (void*)xQueueCreate(QUEUE_DEPTH, sizeof(RawReport));
        if (queue_ == nullptr) {
            setError(DS4_ERR_NO_MEMORY);
            setState(BTState::Failed);
            return DS4_ERR_NO_MEMORY;
        }
    } else {
        xQueueReset((QueueHandle_t)queue_);
    }

    initStep_ = 2;
    // Bond keys persist in NVS; initialise it (Arduino may have already).
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        setError(DS4_ERR_BT_INIT_FAILED);
        setState(BTState::Failed);
        return DS4_ERR_BT_INIT_FAILED;
    }

    initStep_ = 3;
#if defined(DS4_HAVE_HAL_BT)
    // Use Arduino's own starter: sets cfg.mode correctly, releases the
    // other mode's memory (tracked), and waits for status transitions.
    // Hand-rolling controller init here fails (INVALID_STATE) because the
    // default config's mode does not match a Classic-only enable.
    if (!btStarted() && !btStartMode(BT_MODE_CLASSIC_BT)) {
        diagCtl_ = -10;
        setError(DS4_ERR_BT_INIT_FAILED);
        setState(BTState::Failed);
        return DS4_ERR_BT_INIT_FAILED;
    }
#else
    // Classic-only: hand BLE controller memory back to the heap.
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    // Controller: converge IDLE/INITED/ENABLED to ENABLED. (A prior
    // half-init, e.g. by other Arduino BT code, must not leave us mute.)
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        initStep_ = 31;
        esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        esp_err_t cerr = esp_bt_controller_init(&cfg);
        if (cerr != ESP_OK && cerr != ESP_ERR_INVALID_STATE) {
            diagCtl_ = (int)cerr;
            setError(DS4_ERR_BT_INIT_FAILED);
            setState(BTState::Failed);
            return DS4_ERR_BT_INIT_FAILED;
        }
        // INVALID_STATE here means someone else already initialized the
        // controller (e.g. Arduino core): proceed to enabling below.
        if (cerr != ESP_OK) {
            initStep_ = 310 + (int)cerr;
        }
    }
    initStep_ = 32;
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_err_t cerr = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
        if (cerr != ESP_OK) {
            diagCtl_ = (int)cerr;
            setError(DS4_ERR_BT_INIT_FAILED);
            setState(BTState::Failed);
            return DS4_ERR_BT_INIT_FAILED;
        }
    }
#endif

    initStep_ = 4;
    initStep_ = 4;
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        esp_err_t berr = esp_bluedroid_init();
        if (berr != ESP_OK && berr != ESP_ERR_INVALID_STATE) {
            setError(DS4_ERR_BT_INIT_FAILED);
            setState(BTState::Failed);
            return DS4_ERR_BT_INIT_FAILED;
        }
    }
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
        if (esp_bluedroid_enable() != ESP_OK) {
            setError(DS4_ERR_BT_INIT_FAILED);
            setState(BTState::Failed);
            return DS4_ERR_BT_INIT_FAILED;
        }
    }
#if defined(ARDUINO)
    Serial.print("bluedroid status: ");
    Serial.println((int)esp_bluedroid_get_status());
    Serial.print("controller status: ");
    Serial.println((int)esp_bt_controller_get_status());
#endif
    diagBd_ = (int)esp_bluedroid_get_status();
    diagCtl_ = (int)esp_bt_controller_get_status();

    // Accept incoming connections (the DS4 pages back after SSP / on wake).
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    // SSP "just works": no display, no keyboard.
    {
        uint8_t iocap = ESP_BT_IO_CAP_NONE;
        esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap));
    }

    initStep_ = 5;
    if (esp_bt_gap_register_callback(gapCallback) != ESP_OK) {
        setError(DS4_ERR_BT_INIT_FAILED);
        setState(BTState::Failed);
        return DS4_ERR_BT_INIT_FAILED;
    }

    initStep_ = 6;
    // Register both HID PSMs for outgoing AND incoming connections.
    if (L2CA_Register(kPsmCtrl, &kCtrlAppli) == 0 ||
        L2CA_Register(kPsmIntr, &kIntrAppli) == 0) {
        setError(DS4_ERR_BT_INIT_FAILED);
        setState(BTState::Failed);
        return DS4_ERR_BT_INIT_FAILED;
    }

    // Require BTM authentication+encryption on both HID channels so the
    // stack runs SSP pairing itself (just-works, confirmed in gapCallback)
    // instead of failing LMP auth with no retry.
    {
        const uint16_t sec = (uint16_t)(DS4_BTM_SEC_IN_AUTHENTICATE | DS4_BTM_SEC_IN_ENCRYPT |
                                        DS4_BTM_SEC_OUT_AUTHENTICATE | DS4_BTM_SEC_OUT_ENCRYPT);
        uint8_t ok1 = BTM_SetSecurityLevel(1, "ds4hid", DS4_BTM_SEC_SERVICE_HIDH_CTRL, sec,
                                           kPsmCtrl, DS4_BTM_SEC_PROTO_L2CAP, kPsmCtrl);
        uint8_t ok2 = BTM_SetSecurityLevel(1, "ds4hid", DS4_BTM_SEC_SERVICE_HIDH_INTR, sec,
                                           kPsmIntr, DS4_BTM_SEC_PROTO_L2CAP, kPsmIntr);
#if defined(ARDUINO)
        Serial.print("BTM seclevel: ");
        Serial.print(ok1);
        Serial.print("/");
        Serial.println(ok2);
#endif
        if (!ok1 || !ok2) {
            setError(DS4_ERR_BT_INIT_FAILED);
            setState(BTState::Failed);
            return DS4_ERR_BT_INIT_FAILED;
        }
    }

    return startScan();
}

DS4Error DS4Bluetooth::startScan() {
    if (state_ == BTState::Connected || state_ == BTState::Connecting) {
        return DS4_OK;
    }
    esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
    diagInq_ = (int)err;
#if defined(ARDUINO)
    Serial.print("Inquiry start: ");
    Serial.print(err == ESP_OK ? "OK" : esp_err_to_name(err));
    Serial.print(" (0x");
    Serial.print((int)err, HEX);
    Serial.println(")");
#endif
    (void)err;
    clearQueue();
    setState(BTState::Scanning);
    setError(DS4_OK);
    return DS4_OK;
}

void DS4Bluetooth::end() {
    closeChannels();
    esp_bt_gap_cancel_discovery();
    if (queue_ != nullptr) {
        vQueueDelete((QueueHandle_t)queue_);
        queue_ = nullptr;
    }
    if (s_inst == this) s_inst = nullptr;
    setState(BTState::Idle);
}

DS4Error DS4Bluetooth::reconnect() {
    if (!hasAddr_) {
        return startScan();
    }
    setState(BTState::Connecting);
    setError(DS4_OK);
    DS4_BT_LOG("Reconnecting (control channel)...");
    esp_bd_addr_t ra;
    for (int i = 0; i < 6; i++) ra[i] = lastAddr_[i];
    esp_bt_gap_remove_bond_device(ra);
    uint16_t cid = L2CA_ConnectReq(kPsmCtrl, lastAddr_);
    if (cid == 0) {
        setError(DS4_ERR_HID_CONNECT_FAILED);
        setState(BTState::Failed);
        return DS4_ERR_HID_CONNECT_FAILED;
    }
    return DS4_OK;
}

bool DS4Bluetooth::sendOutputReport(const uint8_t* data, size_t len) {
    // HID SET_REPORT(Output) on the control channel: header + report.
    if (!connected() || ctrlCid_ == 0 || data == nullptr || len == 0 ||
        len > sizeof(RawReport::data)) {
        return false;
    }
    uint8_t framed[1 + sizeof(RawReport::data)];
    framed[0] = kHidSetReportOutput;
    for (size_t i = 0; i < len; i++) framed[1 + i] = data[i];
    return sendHid(ctrlCid_, framed, len + 1);
}

#else

// Non-ESP32 or BLE-only ESP32 variant: compile-safe stub.
bool DS4Bluetooth::openControl() { return false; }
void DS4Bluetooth::acceptIncoming(const uint8_t bda[6], uint8_t id, uint16_t lcid, bool isCtrl) {
    (void)bda;
    (void)id;
    (void)lcid;
    (void)isCtrl;
}
void DS4Bluetooth::closeChannels() {}
bool DS4Bluetooth::sendHid(uint16_t cid, const uint8_t* payload, size_t len) {
    (void)cid;
    (void)payload;
    (void)len;
    return false;
}

DS4Error DS4Bluetooth::begin() {
    setError(DS4_ERR_NOT_SUPPORTED);
    setState(BTState::Failed);
    return DS4_ERR_NOT_SUPPORTED;
}

DS4Error DS4Bluetooth::startScan() {
    setError(DS4_ERR_NOT_SUPPORTED);
    return DS4_ERR_NOT_SUPPORTED;
}

bool DS4Bluetooth::sendOutputReport(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    return false;
}

bool DS4Bluetooth::localBtMac(uint8_t mac[6]) {
    (void)mac;
    return false;
}

void DS4Bluetooth::end() { setState(BTState::Idle); }

DS4Error DS4Bluetooth::reconnect() {
    setError(DS4_ERR_NOT_SUPPORTED);
    return DS4_ERR_NOT_SUPPORTED;
}

#endif

}  // namespace DS4
