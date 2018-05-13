#pragma once

#include <stdint.h>

#include "device_state_manager.h"

typedef void (*conf_success_cb_t)(void);
typedef void (*conf_failure_cb_t)(void);

void conf_init(uint8_t const *appkey, dsm_handle_t appkey_handle,
               conf_success_cb_t success_cb, conf_failure_cb_t failure_cb);
void conf_start(uint16_t node_addr, uint8_t const *appkey, uint16_t appkey_idx);
