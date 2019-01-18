#pragma once

#include <stdint.h>

#include "access.h"
#include "access_reliable.h"

#include "battery_level_common.h"
#include "battery_level_messages.h"

#define BATTERY_LEVEL_CLIENT_MODEL_ID 0x0002

typedef struct __battery_level_client_t battery_level_client_t;

typedef void (*battery_level_status_cb_t)(
    battery_level_client_t const *client, access_message_rx_meta_t const *meta,
    battery_level_status_params_t const *in);

typedef struct {
  bool force_segmented;
  nrf_mesh_transmic_size_t transmic_size;

  battery_level_status_cb_t status_cb;
} battery_level_client_settings_t;

struct __battery_level_client_t {
  access_model_handle_t model_handle;

  battery_level_client_settings_t settings;
};

uint32_t battery_level_client_init(battery_level_client_t *client,
                                   uint8_t element_index);
