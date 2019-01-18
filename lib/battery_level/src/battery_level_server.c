#include <stdint.h>

#include "battery_level_common.h"
#include "battery_level_messages.h"
#include "battery_level_server.h"

#include "access.h"
#include "access_config.h"
#include "nordic_common.h"
#include "nrf_mesh_assert.h"
#include "nrf_mesh_utils.h"

#include "log.h"

static uint32_t status_send(battery_level_server_t *server,
                            access_message_rx_t const *message,
                            battery_level_status_params_t const *params) {
  battery_level_status_msg_pkt_t msg_pkt;

  msg_pkt.level = params->level;

  access_message_tx_t reply = {
      .opcode = ACCESS_OPCODE_VENDOR(BATTERY_LEVEL_OPCODE_STATUS,
                                     BATTERY_LEVEL_COMPANY_ID),
      .p_buffer = (const uint8_t *)&msg_pkt,
      .length = BATTERY_LEVEL_STATUS_MINLEN,
      .force_segmented = server->settings.force_segmented,
      .transmic_size = server->settings.transmic_size};

  if (message == NULL) {
    return access_model_publish(server->model_handle, &reply);
  } else {
    return access_model_reply(server->model_handle, message, &reply);
  }
}

static void periodic_publish_cb(access_model_handle_t handle, void *args) {
  battery_level_server_t *server = (battery_level_server_t *)args;
  battery_level_status_params_t out = {0};

  server->settings.get_cb(server, NULL, &out);
  APP_ERROR_CHECK(status_send(server, NULL, &out));
}

uint32_t battery_level_server_init(battery_level_server_t *server,
                                   uint8_t element_index) {
  if (server == NULL || server->settings.get_cb == NULL) {
    return NRF_ERROR_NULL;
  }

  access_model_add_params_t init_params = {
      .model_id = ACCESS_MODEL_VENDOR(BATTERY_LEVEL_SERVER_MODEL_ID,
                                      BATTERY_LEVEL_COMPANY_ID),
      .element_index = element_index,
      .p_opcode_handlers = NULL,
      .opcode_count = 0,
      .p_args = server,
      .publish_timeout_cb = periodic_publish_cb};

  uint32_t status = access_model_add(&init_params, &server->model_handle);

  if (status == NRF_SUCCESS) {
    status = access_model_subscription_list_alloc(server->model_handle);
  }

  return status;
}
