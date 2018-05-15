#include "configurator.h"

#include <stdlib.h>

#include "access_config.h"
#include "access_status.h"
#include "config_client.h"
#include "device_state_manager.h"
#include "health_client.h"
#include "health_common.h"

#include "app_config.h"
#include "custom_log.h"

const conf_step_t CONF_STEPS_BEACON[] = {
    CONF_STEP_COMPOSITION_GET,
    CONF_STEP_APPKEY_ADD,
    CONF_STEP_APPKEY_BIND_HEALTH,
    CONF_STEP_PUBLICATION_HEALTH,
    CONF_DONE,
};

const conf_step_t CONF_STEPS_WRISTBAND[] = {
    CONF_STEP_COMPOSITION_GET,
    CONF_STEP_APPKEY_ADD,
    CONF_STEP_APPKEY_BIND_HEALTH,
    CONF_STEP_PUBLICATION_HEALTH,
    CONF_STEP_APPKEY_BIND_HEALTH_CLIENT,
    CONF_STEP_APPKEY_SUBSCRIBE_HEALTH_CLIENT,
    CONF_DONE,
};

const conf_step_t CONF_STEPS_GATEWAY[] = {
    CONF_STEP_APPKEY_BIND_HEALTH,
    CONF_STEP_PUBLICATION_HEALTH,
    CONF_DONE,
};

typedef enum {
  CONF_STATE_IDLE,
  CONF_STATE_EXECUTING,
} conf_state_t;

typedef struct {
  conf_state_t state;

  app_state_t *app_state;

  uint8_t appkey[NRF_MESH_KEY_SIZE];

  uint16_t node_addr;

  conf_success_cb_t success_cb;
  conf_failure_cb_t failure_cb;

  conf_step_t const *steps;
  conf_step_t const *current_step;

  uint32_t expected_opcode;
  uint32_t expected_n;
  uint8_t const *expected_list;

  health_client_t health_client;

  bool status_checked;
} conf_t;

typedef enum {
  CONF_CHECK_RESULT_PASS,
  CONF_CHECK_RESULT_LATER,
  CONF_CHECK_RESULT_FAIL,
} conf_check_result_t;

conf_t conf;

void conf_execute_step();
void conf_config_client_evt_cb(config_client_event_type_t evt_type,
                               const config_client_event_t *evt,
                               uint16_t length);

void conf_health_client_evt_cb(health_client_t const *client,
                               health_client_evt_t const *event) {
  // LOG_INFO("Received health client event from %04x to %04x with RSSI: %d. ",
  //          event->p_meta_data->src.value, event->p_meta_data->dst.value,
  //          event->p_meta_data->p_core_metadata->params.scanner.rssi);
}

void conf_self_configure() {
  // Subscribe health client to the beacon address
  APP_ERROR_CHECK(access_model_subscription_add(
      conf.health_client.model_handle, conf.app_state->beacon_addr_handle));
}

void conf_init(app_state_t *app_state, conf_success_cb_t success_cb,
               conf_failure_cb_t failure_cb) {
  conf.state = CONF_STATE_IDLE;
  conf.app_state = app_state;
  memcpy(conf.appkey, app_state->appkey, NRF_MESH_KEY_SIZE);

  conf.success_cb = success_cb;
  conf.failure_cb = failure_cb;

  APP_ERROR_CHECK(config_client_init(conf_config_client_evt_cb));

  APP_ERROR_CHECK(
      health_client_init(&conf.health_client, 0, conf_health_client_evt_cb));

  APP_ERROR_CHECK(access_model_application_bind(conf.health_client.model_handle,
                                                app_state->appkey_handle));
  APP_ERROR_CHECK(access_model_publish_application_set(
      conf.health_client.model_handle, app_state->appkey_handle));

  conf_self_configure();
}

conf_check_result_t conf_check_status(uint32_t opcode,
                                      config_msg_t const *msg) {
  if (opcode != conf.expected_opcode) {
    LOG_ERROR("Unexpected opcode: %d vs %d. Ignoring the message.", opcode,
              conf.expected_opcode);
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
    LOG_ERROR("Unknown opcode: %d", opcode);
    APP_ERROR_CHECK(NRF_ERROR_NOT_FOUND);
  }

  if (conf.expected_n == 0) {
    return CONF_CHECK_RESULT_PASS;
  }

  for (int i = 0; i < conf.expected_n; i++) {
    if (conf.expected_list[i] == status) {
      return CONF_CHECK_RESULT_PASS;
    }
  }

  LOG_INFO("Unexpected configuration result status: %d", status);

  return CONF_CHECK_RESULT_FAIL;
}

void conf_succeed() {
  conf.state = CONF_STATE_IDLE;
  conf.success_cb(conf.node_addr);
}

void conf_fail() {
  conf.state = CONF_STATE_IDLE;
  conf.failure_cb(conf.node_addr);
}

void conf_config_client_evt_cb(config_client_event_type_t evt_type,
                               const config_client_event_t *evt,
                               uint16_t length) {
  switch (evt_type) {
  case CONFIG_CLIENT_EVENT_TYPE_TIMEOUT: //
  {
    LOG_ERROR("Received config timeout message. ");
    conf_fail();

    break;
  }

  case CONFIG_CLIENT_EVENT_TYPE_MSG: //
  {
    if (conf.state != CONF_STATE_EXECUTING) {
      LOG_ERROR("Config client message received while the configurator is not "
                "executing. ");
      break;
    }

    if (conf.status_checked || *conf.current_step == CONF_DONE) {
      LOG_ERROR(
          "Config client message received when it has already been checked");
      break;
    }

    conf_check_result_t result = conf_check_status(evt->opcode, evt->p_msg);

    if (result == CONF_CHECK_RESULT_PASS) {
      conf.current_step++;
      if (*conf.current_step == CONF_DONE) {
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
    LOG_ERROR(
        "Received unexpected config client event with type %d and length %d",
        evt_type, length);

    break;
  }
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

  case CONF_STEP_APPKEY_ADD: //
  {
    LOG_INFO("Configurator is adding app key. ");
    APP_ERROR_CHECK(config_client_appkey_add(0, 0, conf.appkey));
    static const uint8_t expected_statuses[] = {
        ACCESS_STATUS_SUCCESS, ACCESS_STATUS_KEY_INDEX_ALREADY_STORED};
    conf_set_expected_status(CONFIG_OPCODE_APPKEY_STATUS,
                             sizeof(expected_statuses), expected_statuses);
    break;
  }

  case CONF_STEP_APPKEY_BIND_HEALTH: //
  {
    LOG_INFO("Configurator is adding key binding to the health server. ");
    access_model_id_t model_id = {
        .company_id = ACCESS_COMPANY_ID_NONE,
        .model_id = HEALTH_SERVER_MODEL_ID,
    };
    APP_ERROR_CHECK(
        config_client_model_app_bind(conf.node_addr, APP_APPKEY_IDX, model_id));
    static const uint8_t expected_statuses[] = {ACCESS_STATUS_SUCCESS};
    conf_set_expected_status(CONFIG_OPCODE_MODEL_APP_STATUS,
                             sizeof(expected_statuses), expected_statuses);
    break;
  }

  case CONF_STEP_PUBLICATION_HEALTH: //
  {
    LOG_INFO(
        "Configurator is setting publication schedule for the health server. ");

    config_publication_state_t pub_state = {
        .element_address = conf.node_addr,
        .publish_address.type = NRF_MESH_ADDRESS_TYPE_GROUP,
        .publish_address.value = APP_BEACON_PUBLISH_ADDRESS,
        .appkey_index = 0,
        .frendship_credential_flag = false,
        .publish_ttl = 1,
        .publish_period.step_num = 2,
        .publish_period.step_res = ACCESS_PUBLISH_RESOLUTION_100MS,
        .retransmit_count = 1,
        .retransmit_interval = 0,
        .model_id.company_id = ACCESS_COMPANY_ID_NONE,
        .model_id.model_id = HEALTH_SERVER_MODEL_ID,
    };

    APP_ERROR_CHECK(config_client_model_publication_set(&pub_state));
    static const uint8_t expected_statuses[] = {ACCESS_STATUS_SUCCESS};
    conf_set_expected_status(CONFIG_OPCODE_MODEL_PUBLICATION_STATUS,
                             sizeof(expected_statuses), expected_statuses);
    break;
  }

  case CONF_STEP_APPKEY_BIND_HEALTH_CLIENT: //
  {
    LOG_INFO("Configurator is adding key binding to the health client. ");

    access_model_id_t model_id = {
        .company_id = ACCESS_COMPANY_ID_NONE,
        .model_id = HEALTH_CLIENT_MODEL_ID,
    };
    APP_ERROR_CHECK(
        config_client_model_app_bind(conf.node_addr, APP_APPKEY_IDX, model_id));
    static const uint8_t expected_statuses[] = {ACCESS_STATUS_SUCCESS};
    conf_set_expected_status(CONFIG_OPCODE_MODEL_APP_STATUS,
                             sizeof(expected_statuses), expected_statuses);

    break;
  }

  case CONF_STEP_APPKEY_SUBSCRIBE_HEALTH_CLIENT: //
  {
    LOG_INFO(
        "Configurator is adding subscription address for the health client. ");

    access_model_id_t model_id = {
        .company_id = ACCESS_COMPANY_ID_NONE,
        .model_id = HEALTH_CLIENT_MODEL_ID,
    };
    nrf_mesh_address_t address = {.type = NRF_MESH_ADDRESS_TYPE_GROUP,
                                  .value = APP_BEACON_PUBLISH_ADDRESS};
    APP_ERROR_CHECK(config_client_model_subscription_add(conf.node_addr,
                                                         address, model_id));
    static const uint8_t expected_statuses[] = {ACCESS_STATUS_SUCCESS};
    conf_set_expected_status(CONFIG_OPCODE_MODEL_SUBSCRIPTION_STATUS,
                             sizeof(expected_statuses), expected_statuses);

    break;
  }

  default:
    APP_ERROR_CHECK(NRF_ERROR_NOT_FOUND);
    break;
  }
}

void conf_start(uint16_t node_addr, conf_step_t const *steps) {
  LOG_INFO("Starting to configure node at address %d", node_addr);

  if (conf.state != CONF_STATE_IDLE) {
    LOG_ERROR("Configurator currently busy. ");
    return;
  }

  conf.state = CONF_STATE_EXECUTING;
  conf.node_addr = node_addr;

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

  LOG_INFO("Config client bound and set. devkey_handle = %d, addr_handle = %d",
           devkey_handle, addr_handle);

  conf.steps = steps;
  conf.current_step = conf.steps;

  LOG_INFO("Configurator successfully set up to talk to node at addr %d. ",
           node_addr);

  conf_execute_step();
}
