#pragma once

#include <stdint.h>

#include "device_state_manager.h"

#include "app_state.h"

typedef void (*conf_success_cb_t)(uint16_t node_addr);
typedef void (*conf_failure_cb_t)(uint16_t node_addr);

void conf_init(app_state_t *app_state, conf_success_cb_t success_cb,
               conf_failure_cb_t failure_cb);
void conf_start(uint16_t node_addr, uint8_t const *appkey, uint16_t appkey_idx);
