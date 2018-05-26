#pragma once

#include <stdint.h>

#include "ble.h"
#include "ble_db_discovery.h"
#include "packet_buffer.h"

#define MESH_PROXY_SERVICE_UUID 0x1828

#define MESH_PROXY_DATA_IN_CHAR_UUID 0x2ADD
#define MESH_PROXY_DATA_OUT_CHAR_UUID 0x2ADE

#define PROXY_CLIENT_DEF(_name)                                                \
  static proxy_client_t _name;                                                 \
  NRF_SDH_BLE_OBSERVER(_name##_obs, 2, proxy_client_ble_evt_handler, &_name)

typedef struct {
  uint16_t data_in_handle;
  uint16_t data_out_handle;
  uint16_t data_out_cccd_handle;
} proxy_client_handles_t;

typedef enum {
  PROXY_CLIENT_EVT_DISCOVERY_COMPLETE,
  PROXY_CLIENT_EVT_DATA_IN,
} proxy_client_evt_type_t;

typedef struct {
  proxy_client_evt_type_t type;

  union {
    struct {
      uint16_t conn_handle;
    } discovery_complete;

    struct {
      uint8_t *data;
      uint16_t len;
    } data_in;
  } params;
} proxy_client_evt_t;

typedef void (*proxy_client_evt_handler_t)(proxy_client_evt_t *evt);

typedef struct __attribute((packed)) {
  uint8_t pdu_type : 6;
  uint8_t sar_type : 2;
  uint8_t pdu[];
} proxy_client_packet_t;

#define PROXY_CLIENT_PACKET_BUFFER_SIZE 300

struct proxy_client_t {
  proxy_client_evt_handler_t evt_handler;

  uint16_t conn_handle;
  proxy_client_handles_t handles;
  bool beacon_seen;

  struct {
    packet_buffer_t packet_buffer;
    uint8_t packet_buffer_data[PROXY_CLIENT_PACKET_BUFFER_SIZE];

    packet_buffer_packet_t *current_packet;
  } tx;
};

typedef struct proxy_client_t proxy_client_t;

uint32_t proxy_client_init(proxy_client_t *client,
                           proxy_client_evt_handler_t evt_handler);
void proxy_client_conn_handle_assign(proxy_client_t *client,
                                     uint16_t conn_handle);
void proxy_client_db_discovery_evt_handler(proxy_client_t *client,
                                           ble_db_discovery_evt_t *evt);
void proxy_client_ble_evt_handler(ble_evt_t const *ble_evt, void *context);
uint32_t proxy_client_send_raw(proxy_client_t *client, uint8_t *data,
                               uint16_t len);

proxy_client_packet_t *proxy_client_network_packet_alloc(proxy_client_t *client,
                                                         size_t pdu_size);
void proxy_client_network_packet_send(proxy_client_t *client,
                                      proxy_client_packet_t *packet,
                                      uint32_t pdu_len);
void proxy_client_network_packet_discard(proxy_client_t *client,
                                         proxy_client_packet_t *packet);
