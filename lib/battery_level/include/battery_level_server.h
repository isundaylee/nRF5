#pragma once

#include <stdint.h>

#include "access.h"
#include "model_common.h"

#include "battery_level_common.h"

#define BATTERY_LEVEL_SERVER_MODEL_ID 0x0001

typedef struct __battery_level_server_t battery_level_server_t;

typedef void (*battery_level_state_get_cb_t)(
    battery_level_server_t const *server, access_message_rx_meta_t const *meta,
    battery_level_status_params_t *out);

typedef struct {
  bool force_segmented;
  nrf_mesh_transmic_size_t transmic_size;

  battery_level_state_get_cb_t get_cb;
} battery_level_server_settings_t;

struct __battery_level_server_t {
  access_model_handle_t model_handle;
  tid_tracker_t tid_tracker;

  battery_level_server_settings_t settings;
};

uint32_t battery_level_server_init(battery_level_server_t *server,
                                   uint8_t element_index);
