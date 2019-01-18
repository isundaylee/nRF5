#include "battery_level_client.h"

#include "model_common.h"

#include "access.h"
#include "access_config.h"
#include "nordic_common.h"
#include "nrf_mesh_assert.h"
#include "nrf_mesh_utils.h"
#include <stdint.h>

#include "log.h"

static void status_handle(access_model_handle_t handle,
                          const access_message_rx_t *rx_msg, void *args) {
  battery_level_client_t *client = (battery_level_client_t *)args;
  battery_level_status_params_t in_data = {0};

  if (rx_msg->length != BATTERY_LEVEL_STATUS_MAXLEN) {
    return;
  }

  battery_level_status_msg_pkt_t *msg =
      (battery_level_status_msg_pkt_t *)rx_msg->p_data;

  in_data.level = msg->level;

  client->settings.status_cb(client, &rx_msg->meta_data, &in_data);
}

static const access_opcode_handler_t opcode_handlers[] = {
    {ACCESS_OPCODE_VENDOR(BATTERY_LEVEL_OPCODE_STATUS,
                          BATTERY_LEVEL_COMPANY_ID),
     status_handle},
};

uint32_t battery_level_client_init(battery_level_client_t *client,
                                   uint8_t element_index) {
  if (client == NULL || client->settings.status_cb == NULL) {
    return NRF_ERROR_NULL;
  }

  access_model_add_params_t add_params = {
      .model_id = ACCESS_MODEL_VENDOR(BATTERY_LEVEL_CLIENT_MODEL_ID,
                                      BATTERY_LEVEL_COMPANY_ID),
      .element_index = element_index,
      .p_opcode_handlers = &opcode_handlers[0],
      .opcode_count = ARRAY_SIZE(opcode_handlers),
      .p_args = client};

  uint32_t status = access_model_add(&add_params, &client->model_handle);

  if (status == NRF_SUCCESS) {
    status = access_model_subscription_list_alloc(client->model_handle);
  }

  return status;
}
