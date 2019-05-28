#pragma once

#include "app_state.h"

typedef void (*prov_start_cb_t)(uint16_t addr);
typedef void (*prov_success_cb_t)(uint16_t addr);
typedef void (*prov_failure_cb_t)();

void prov_init(app_state_t *app_state, prov_start_cb_t start_cb,
               prov_success_cb_t success_cb, prov_failure_cb_t failure_cb);
uint32_t prov_start_scan();
