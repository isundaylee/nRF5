#pragma once

#include <stdint.h>

#include "app_config.h"
#include "mesh_stack.h"

typedef struct {
  uint8_t netkey[NRF_MESH_KEY_SIZE];
  uint8_t appkey[NRF_MESH_KEY_SIZE];
  uint8_t devkey[NRF_MESH_KEY_SIZE];

  dsm_handle_t netkey_handle;
  dsm_handle_t devkey_handle;
  dsm_handle_t appkey_handle;

  dsm_handle_t beacon_addr_handle;

  uint16_t next_addr;
  uint16_t provisioned_addrs[APP_MAX_PROVISIONEES];
  uint8_t provisioned_uuids[APP_MAX_PROVISIONEES][NRF_MESH_UUID_SIZE];
} app_state_t;
