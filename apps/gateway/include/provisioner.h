#pragma once

#include "app_state.h"

typedef void (*prov_init_complete_cb_t)();

void prov_init(app_state_t *app_state, prov_init_complete_cb_t complete_cb);
void prov_start_scan();
