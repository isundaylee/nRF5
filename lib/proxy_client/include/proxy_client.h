#pragma once

#include <stdint.h>

#include "ble.h"
#include "ble_db_discovery.h"

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

typedef struct {
  proxy_client_evt_handler_t evt_handler;

  uint16_t conn_handle;
  proxy_client_handles_t handles;
} proxy_client_t;

uint32_t proxy_client_init(proxy_client_t *client,
                           proxy_client_evt_handler_t evt_handler);
void proxy_client_conn_handle_assign(proxy_client_t *client,
                                     uint16_t conn_handle);
void proxy_client_db_discovery_evt_handler(proxy_client_t *client,
                                           ble_db_discovery_evt_t *evt);
void proxy_client_ble_evt_handler(ble_evt_t const *ble_evt, void *context);
uint32_t proxy_client_send(proxy_client_t *client, uint8_t *data, uint16_t len);
