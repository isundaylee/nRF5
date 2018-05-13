#pragma once

#include <stdint.h>

#include "mesh_stack.h"

typedef struct {
  uint8_t netkey[NRF_MESH_KEY_SIZE];
  uint8_t appkey[NRF_MESH_KEY_SIZE];
  uint8_t devkey[NRF_MESH_KEY_SIZE];

  dsm_handle_t netkey_handle;
  dsm_handle_t devkey_handle;
  dsm_handle_t appkey_handle;

  dsm_handle_t beacon_addr_handle;
} app_state_t;
