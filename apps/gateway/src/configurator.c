#include "configurator.h"

#include <stdlib.h>

#include "app_error.h"
#include "nrf_gpio.h"

#include "access_config.h"
#include "access_status.h"
#include "config_client.h"
#include "device_state_manager.h"
#include "health_client.h"
#include "health_common.h"

#include "app_config.h"
#include "custom_log.h"

typedef enum {
  CONF_STATE_IDLE,
  CONF_STATE_EXECUTING,
} conf_state_t;

typedef struct {
  app_state_t *app_state;

  conf_success_cb_t success_cb;
  conf_failure_cb_t failure_cb;

  // Information about the current configuration process
  conf_state_t state;

  uint16_t node_addr;
  conf_step_t const *steps;
  conf_step_t const *current_step;

  uint32_t expected_opcode;
  uint32_t expected_n;
  uint8_t const *expected_list;

  bool status_checked;
} conf_t;

typedef enum {
  CONF_CHECK_RESULT_PASS,
  CONF_CHECK_RESULT_LATER,
  CONF_CHECK_RESULT_FAIL,
} conf_check_result_t;

conf_t conf;

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

conf_check_result_t conf_check_status(uint32_t opcode,
                                      config_msg_t const *msg) {
  if (opcode != conf.expected_opcode) {
    LOG_ERROR(
        "Configurator: Unexpected opcode: %d vs %d. Ignoring the message.",
        opcode, conf.expected_opcode);
    return CONF_CHECK_RESULT_LATER;
  }

  conf.status_checked = true;

  uint8_t status = 0;
  switch (opcode) {
  case CONFIG_OPCODE_COMPOSITION_DATA_STATUS:
    break;
  case CONFIG_OPCODE_MODEL_APP_STATUS:
    status = msg->app_status.status;
    break;
  case CONFIG_OPCODE_MODEL_PUBLICATION_STATUS:
    status = msg->publication_status.status;
    break;
  case CONFIG_OPCODE_MODEL_SUBSCRIPTION_STATUS:
    status = msg->subscription_status.status;
    break;
  case CONFIG_OPCODE_APPKEY_STATUS:
    status = msg->appkey_status.status;
    break;

  default:
    LOG_ERROR("Configurator: Unknown opcode: %d", opcode);
    NRF_MESH_ASSERT(false);
  }

  if (conf.expected_n == 0) {
    return CONF_CHECK_RESULT_PASS;
  }

  for (int i = 0; i < conf.expected_n; i++) {
    if (conf.expected_list[i] == status) {
      return CONF_CHECK_RESULT_PASS;
    }
  }

  LOG_ERROR("Configurator: Unexpected configuration result status: %d", status);

  return CONF_CHECK_RESULT_FAIL;
}

void conf_bind(uint16_t node_addr) {
  // Setting up config_client
  dsm_handle_t addr_handle = DSM_HANDLE_INVALID;
  dsm_handle_t devkey_handle = DSM_HANDLE_INVALID;
  nrf_mesh_address_t addr;

  addr.type = NRF_MESH_ADDRESS_TYPE_UNICAST;
  addr.value = node_addr;

  APP_ERROR_CHECK(dsm_address_handle_get(&addr, &addr_handle));
  APP_ERROR_CHECK(dsm_devkey_handle_get(addr.value, &devkey_handle));

  APP_ERROR_CHECK(config_client_server_bind(devkey_handle));
  APP_ERROR_CHECK(config_client_server_set(devkey_handle, addr_handle));

  LOG_INFO("Configurator: Config client bound and set for node %d. ",
           node_addr);
}

void conf_succeed() {
  LOG_INFO("Configurator: Configuration succeeded for node %d. ",
           conf.node_addr);

  // Bind the config client back to the gateway to avoid reboot crash in case a
  // previously provisioned device is removed from DSM.
  conf_bind(APP_GATEWAY_ADDR);
  conf.state = CONF_STATE_IDLE;
  conf.success_cb(conf.node_addr);
}

void conf_fail() {
  LOG_INFO("Configurator: Configuration failed for node %d. ",
           conf.node_addr);

  // Bind the config client back to the gateway to avoid reboot crash in case a
  // previously provisioned device is removed from DSM.
  conf_bind(APP_GATEWAY_ADDR);
  conf.state = CONF_STATE_IDLE;
  conf.failure_cb(conf.node_addr);
}

void conf_execute_step() {
  conf_step_t const *current_step = conf.current_step;

  switch (current_step->type) {
  case CONF_STEP_TYPE_COMPOSITION_GET: //
  {
    LOG_INFO("Configurator: Getting composition data. ");
    APP_ERROR_CHECK(config_client_composition_data_get(
        current_step->params.composition_get.page_number));
    conf_set_expected_status(CONFIG_OPCODE_COMPOSITION_DATA_STATUS, 0, NULL);
    break;
  }

  case CONF_STEP_TYPE_APPKEY_ADD: //
  {
    LOG_INFO("Configurator: Adding app key. ");
    APP_ERROR_CHECK(
        config_client_appkey_add(current_step->params.appkey_add.netkey_index,
                                 current_step->params.appkey_add.appkey_index,
                                 current_step->params.appkey_add.appkey));
    static const uint8_t expected_statuses[] = {
        ACCESS_STATUS_SUCCESS, ACCESS_STATUS_KEY_INDEX_ALREADY_STORED};
    conf_set_expected_status(CONFIG_OPCODE_APPKEY_STATUS,
                             sizeof(expected_statuses), expected_statuses);
    break;
  }

  case CONF_STEP_TYPE_MODEL_APP_BIND: //
  {
    LOG_INFO("Configurator: Adding model key binding. ");
    APP_ERROR_CHECK(config_client_model_app_bind(
        current_step->params.model_app_bind.element_addr,
        current_step->params.model_app_bind.appkey_index,
        current_step->params.model_app_bind.model_id));
    static const uint8_t expected_statuses[] = {ACCESS_STATUS_SUCCESS};
    conf_set_expected_status(CONFIG_OPCODE_MODEL_APP_STATUS,
                             sizeof(expected_statuses), expected_statuses);
    break;
  }

  case CONF_STEP_TYPE_MODEL_PUBLICATION_SET: //
  {
    LOG_INFO("Configurator: Setting publication schedule for model. ");

    config_publication_state_t pub_state = {
        .element_address =
            current_step->params.model_publication_set.element_addr,
        .publish_address =
            current_step->params.model_publication_set.publish_address,
        .appkey_index = current_step->params.model_publication_set.appkey_index,
        .frendship_credential_flag = false,
        .publish_ttl = current_step->params.model_publication_set.publish_ttl,
        .publish_period =
            current_step->params.model_publication_set.publish_period,
        .retransmit_count = 1,
        .retransmit_interval = 0,
        .model_id = current_step->params.model_publication_set.model_id,
    };

    APP_ERROR_CHECK(config_client_model_publication_set(&pub_state));
    static const uint8_t expected_statuses[] = {ACCESS_STATUS_SUCCESS};
    conf_set_expected_status(CONFIG_OPCODE_MODEL_PUBLICATION_STATUS,
                             sizeof(expected_statuses), expected_statuses);
    break;
  }

  case CONF_STEP_TYPE_DONE:
    break;
  }
}

void conf_config_client_evt_cb(config_client_event_type_t evt_type,
                               const config_client_event_t *evt,
                               uint16_t length) {
  switch (evt_type) {
  case CONFIG_CLIENT_EVENT_TYPE_TIMEOUT: //
  {
    LOG_ERROR("Configurator: Received config timeout message. ");
    conf_fail();

    break;
  }

  case CONFIG_CLIENT_EVENT_TYPE_MSG: //
  {
    if (conf.state != CONF_STATE_EXECUTING) {
      LOG_ERROR("Configurator: Config client message received while the "
                "configurator is not "
                "executing. ");
      break;
    }

    if (conf.status_checked || conf.current_step->type == CONF_STEP_TYPE_DONE) {
      LOG_ERROR("Configurator: Config client message received when it has "
                "already been checked");
      break;
    }

    conf_check_result_t result = conf_check_status(evt->opcode, evt->p_msg);

    if (result == CONF_CHECK_RESULT_PASS) {
      conf.current_step++;
      if (conf.current_step->type == CONF_STEP_TYPE_DONE) {
        conf_succeed();
      } else {
        conf_execute_step();
      }
    } else if (result == CONF_CHECK_RESULT_FAIL) {
      conf_fail();
    }

    break;
  }

  default:
    LOG_ERROR("Configurator: Received unexpected config client event with type "
              "%d and length %d",
              evt_type, length);

    break;
  }
}

void conf_start(uint16_t node_addr, conf_step_t const *steps) {
  LOG_INFO("Configurator: Starting to configure node at address %d", node_addr);

  if (conf.state != CONF_STATE_IDLE) {
    LOG_ERROR("Configurator: Configurator currently busy. ");
    NRF_MESH_ASSERT(false);
    return;
  }

  conf.state = CONF_STATE_EXECUTING;
  conf.node_addr = node_addr;

  conf_bind(node_addr);

  conf.steps = steps;
  conf.current_step = conf.steps;

  conf_execute_step();
}

void conf_init(app_state_t *app_state, conf_success_cb_t success_cb,
               conf_failure_cb_t failure_cb) {
  conf.state = CONF_STATE_IDLE;
  conf.app_state = app_state;

  conf.success_cb = success_cb;
  conf.failure_cb = failure_cb;

  APP_ERROR_CHECK(config_client_init(conf_config_client_evt_cb));
}
