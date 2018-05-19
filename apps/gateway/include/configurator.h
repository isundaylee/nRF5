#pragma once

#include <stdint.h>

#include "device_state_manager.h"

#include "app_state.h"

// Parameter structure for different configuration steps

typedef enum {
  CONF_STEP_TYPE_COMPOSITION_GET,
  CONF_STEP_TYPE_APPKEY_ADD,
  CONF_STEP_TYPE_MODEL_APP_BIND,
  CONF_STEP_TYPE_MODEL_PUBLICATION_SET,
  CONF_STEP_TYPE_DONE,
} conf_step_type_t;

typedef struct {
  uint8_t page_number;
} conf_step_composition_get_t;

typedef struct {
  uint16_t netkey_index;
  uint16_t appkey_index;
  uint8_t *appkey;
} conf_step_appkey_add_t;

typedef struct {
  uint16_t element_addr;
  access_model_id_t model_id;
  uint16_t appkey_index;
} conf_step_model_app_bind_t;

typedef struct {
  uint16_t element_addr;
  access_model_id_t model_id;
  nrf_mesh_address_t publish_address;
  uint16_t appkey_index;
  uint8_t publish_ttl;
  access_publish_period_t publish_period;
} conf_step_model_publication_set_t;

// Structure for a configuration step

typedef struct {
  conf_step_type_t type;

  union {
    conf_step_composition_get_t composition_get;
    conf_step_appkey_add_t appkey_add;
    conf_step_model_app_bind_t model_app_bind;
    conf_step_model_publication_set_t model_publication_set;
  } params;
} conf_step_t;

// Configurator interface

typedef void (*conf_success_cb_t)(uint16_t node_addr);
typedef void (*conf_failure_cb_t)(uint16_t node_addr);

void conf_init(app_state_t *app_state, conf_success_cb_t success_cb,
               conf_failure_cb_t failure_cb);
void conf_start(uint16_t node_addr, conf_step_t const *steps);
