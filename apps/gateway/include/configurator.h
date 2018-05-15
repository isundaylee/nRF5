#pragma once

#include <stdint.h>

#include "device_state_manager.h"

#include "app_state.h"

typedef enum {
  CONF_IDLE,
  CONF_STEP_COMPOSITION_GET,
  CONF_STEP_APPKEY_ADD,
  CONF_STEP_APPKEY_BIND_HEALTH,
  CONF_STEP_PUBLICATION_HEALTH,
  CONF_STEP_APPKEY_BIND_HEALTH_CLIENT,
  CONF_STEP_APPKEY_SUBSCRIBE_HEALTH_CLIENT,
  CONF_STEP_APPKEY_BIND_ECARE_CLIENT,
  CONF_STEP_APPKEY_SUBSCRIBE_ECARE_CLIENT,
  CONF_DONE,
} conf_step_t;

extern const conf_step_t CONF_STEPS_BEACON[];
extern const conf_step_t CONF_STEPS_WRISTBAND[];
extern const conf_step_t CONF_STEPS_GATEWAY[];

typedef void (*conf_success_cb_t)(uint16_t node_addr);
typedef void (*conf_failure_cb_t)(uint16_t node_addr);

void conf_init(app_state_t *app_state, conf_success_cb_t success_cb,
               conf_failure_cb_t failure_cb);
void conf_start(uint16_t node_addr, conf_step_t const *steps);
