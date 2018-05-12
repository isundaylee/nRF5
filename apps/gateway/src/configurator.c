#include "configurator.h"

#include <stdlib.h>

#include "config_client.h"
#include "device_state_manager.h"

#include "custom_log.h"

typedef enum {
  CONF_STATE_IDLE,
} conf_state_t;

typedef enum {
  CONF_IDLE,
  CONF_STEP_COMPOSITION_GET,
  CONF_STEP_APPKEY_ADD,
  CONF_STEP_APPKEY_BIND_HEALTH,
  CONF_DONE,
} conf_step_t;

typedef struct {
  conf_state_t state;

  uint16_t node_addr;

  conf_step_t *steps;
  conf_step_t *current_step;

  uint32_t expected_opcode;
  uint32_t expected_n;
  uint8_t const *expected_list;

  bool status_checked;
} conf_t;

conf_t conf;

void conf_init() { conf.state = CONF_STATE_IDLE; }

void conf_evt_handler(config_client_event_type_t event_type,
                      const config_client_event_t *p_event, uint16_t length) {
  LOG_INFO("Received config client event with type %d and length %d",
           event_type, length);
}

void conf_set_expected_status(uint32_t opcode, uint32_t n,
                              uint8_t const *list) {
  if (n > 0) {
    NRF_MESH_ASSERT(list != NULL);
  }

  conf.expected_opcode = opcode;
  conf.expected_n = n;
  conf.expected_list = list;
  conf.status_checked = false;
}

void conf_execute_step() {
  switch (*conf.current_step) {
  case CONF_STEP_COMPOSITION_GET: //
  {
    LOG_INFO("Configurator is getting composition data. ");
    APP_ERROR_CHECK(config_client_composition_data_get(0x00));
    conf_set_expected_status(CONFIG_OPCODE_COMPOSITION_DATA_STATUS, 0, NULL);
    break;
  }

  default:
    break;
  }
}

void conf_start(uint16_t node_addr, uint8_t const *appkey,
                uint16_t appkey_idx) {
  LOG_INFO("Starting to configure node at address %d", node_addr);

  if (conf.state != CONF_STATE_IDLE) {
    LOG_INFO("Configurator currently busy. ");
    return;
  }

  conf.node_addr = node_addr;

  // Setting up config_client
  dsm_handle_t addr_handle = DSM_HANDLE_INVALID;
  dsm_handle_t devkey_handle = DSM_HANDLE_INVALID;
  nrf_mesh_address_t addr;

  addr.type = NRF_MESH_ADDRESS_TYPE_UNICAST;
  addr.value = node_addr;

  APP_ERROR_CHECK(dsm_address_handle_get(&addr, &addr_handle));
  APP_ERROR_CHECK(dsm_devkey_handle_get(addr.value, &devkey_handle));

  APP_ERROR_CHECK(config_client_init(conf_evt_handler));

  APP_ERROR_CHECK(config_client_server_bind(devkey_handle));
  APP_ERROR_CHECK(config_client_server_set(devkey_handle, addr_handle));

  // Setting up the steps necessary for configuration
  const conf_step_t steps[] = {
      CONF_STEP_COMPOSITION_GET,
      CONF_STEP_APPKEY_ADD,
      CONF_STEP_APPKEY_BIND_HEALTH,
      CONF_DONE,
  };
  conf.steps = (conf_step_t *)malloc(sizeof(steps));
  conf.current_step = conf.steps;
  memcpy(conf.steps, steps, sizeof(steps));

  LOG_INFO("Configurator successfully set up to talk to node at addr %d. ",
           node_addr);

  conf_execute_step();
}
