#pragma once

// DS4L2Cap: minimal internal Bluedroid L2CAP declarations.
//
// Background: Arduino-ESP32 3.x ships the public L2CAP/HID-host headers
// but its prebuilt libbt.a omits the esp_bt_l2cap_* / esp_bt_hid_host_*
// implementations (verified with nm: GAP/SPP present, HIDH absent).
// The L2CA_* core used by SPP/A2DP *is* present, so a basic-mode HID
// host is built directly on it — the same architecture as the classic
// PS4-host libraries.
//
// Declarations below mirror ESP-IDF v5.x
// components/bt/host/bluedroid/stack/include/stack/l2c_api.h EXACTLY
// (callback order, arity). The appl struct keeps two trailing reserved
// slots so it is never smaller than the stack's own definition.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// BT_HDR: every L2CAP data buffer. Payload lives at data()+offset,
// length len. (sizeof == 8.)
// ---------------------------------------------------------------------------
typedef struct {
    uint16_t event;
    uint16_t len;
    uint16_t offset;
    uint16_t layer_specific;
    uint8_t data[];
} DS4BtHdr;

// ---------------------------------------------------------------------------
// Callback table (order matches tL2CAP_APPL_INFO).
// ---------------------------------------------------------------------------
struct DS4L2Cfg;  // opaque: never dereferenced, only forwarded / zeroed.

typedef void DS4L2ConnIndCb(const uint8_t* bd_addr, uint16_t lcid, uint16_t psm, uint8_t id);
typedef void DS4L2ConnCfmCb(uint16_t lcid, uint16_t result);
typedef void DS4L2ConnPndCb(uint16_t lcid);
typedef void DS4L2CfgIndCb(uint16_t lcid, struct DS4L2Cfg* cfg);
typedef void DS4L2CfgCfmCb(uint16_t lcid, struct DS4L2Cfg* cfg);
typedef void DS4L2DiscIndCb(uint16_t lcid, uint8_t ack_needed);
typedef void DS4L2DiscCfmCb(uint16_t lcid, uint16_t result);
typedef void DS4L2QosViolCb(const uint8_t* bd_addr);
typedef void DS4L2DataIndCb(uint16_t lcid, DS4BtHdr* buf);
typedef void DS4L2CongCb(uint16_t lcid, uint8_t congested);
typedef void DS4L2TxCompleteCb(uint16_t lcid, uint16_t num_sdu);

typedef struct {
    DS4L2ConnIndCb* connect_ind;
    DS4L2ConnCfmCb* connect_cfm;
    DS4L2ConnPndCb* connect_pnd;
    DS4L2CfgIndCb* config_ind;
    DS4L2CfgCfmCb* config_cfm;
    DS4L2DiscIndCb* disconnect_ind;
    DS4L2DiscCfmCb* disconnect_cfm;
    DS4L2QosViolCb* qos_violation;
    DS4L2DataIndCb* data_ind;
    DS4L2CongCb* congestion;
    DS4L2TxCompleteCb* tx_complete;
    void* reserved0;
    void* reserved1;
} DS4L2AppliInfo;

// ---------------------------------------------------------------------------
// BTM security registration (btm_api.h). Tells BTM which PSMs need
// authentication/encryption so *it* runs SSP pairing (via our GAP
// confirm callback) instead of failing LMP auth with no retry.
// ---------------------------------------------------------------------------
#define DS4_BTM_SEC_NONE 0x0000
#define DS4_BTM_SEC_IN_AUTHENTICATE 0x0002
#define DS4_BTM_SEC_IN_ENCRYPT 0x0004
#define DS4_BTM_SEC_OUT_AUTHENTICATE 0x0010
#define DS4_BTM_SEC_OUT_ENCRYPT 0x0020
#define DS4_BTM_SEC_PROTO_L2CAP 0
#define DS4_BTM_SEC_SERVICE_HIDH_CTRL 32
#define DS4_BTM_SEC_SERVICE_HIDH_INTR 34

uint8_t BTM_SetSecurityLevel(uint8_t is_originator, const char* name, uint8_t service_id,
                             uint16_t sec_level, uint16_t psm, uint32_t mx_proto_id,
                             uint32_t mx_chan_id);

// ---------------------------------------------------------------------------
// Results.
// ---------------------------------------------------------------------------
#define DS4_L2CAP_CONN_OK 0
#define DS4_L2CAP_CFG_OK 0
#define DS4_L2CAP_DW_SUCCESS 1  // L2CAP_DW_SUCCESS == TRUE (nonzero!)

// Transmit headroom before the payload (L2CAP header + margin).
#define DS4_L2_TX_OFFSET 16

// Returns the PSM, or 0 on error.
uint16_t L2CA_Register(uint16_t psm, DS4L2AppliInfo* cb_info);
// TRUE for success.
uint8_t L2CA_ConnectRsp(const uint8_t* bd_addr, uint8_t id, uint16_t lcid, uint16_t result,
                        uint16_t status);
uint8_t L2CA_ConfigReq(uint16_t cid, void* cfg);
uint8_t L2CA_ConfigRsp(uint16_t cid, void* cfg);
uint8_t L2CA_DisconnectReq(uint16_t cid);
uint8_t L2CA_DisconnectRsp(uint16_t cid);
// Returns DS4_L2CAP_DW_SUCCESS (1) on success; stack takes the buffer.
uint16_t L2CA_ConnectReq(uint16_t psm, const uint8_t* bd_addr);
uint8_t L2CA_DataWrite(uint16_t cid, DS4BtHdr* buf);

#ifdef __cplusplus
}
#endif
