#include "ecare_server.h"
#include "ecare_common.h"

#include <stddef.h>
#include <stdint.h>

#include "access.h"
#include "nrf_mesh_assert.h"

/*****************************************************************************
 * Static functions
 *****************************************************************************/

static void reply_status(const ecare_server_t *server,
                         const access_message_rx_t *message) {
  ecare_msg_status_t status = {.state = server->state};
  access_message_tx_t reply;

  reply.opcode.opcode = ECARE_OPCODE_STATUS;
  reply.opcode.company_id = ECARE_COMPANY_ID;
  reply.p_buffer = (const uint8_t *)&status;
  reply.length = sizeof(status);
  reply.force_segmented = false;
  reply.transmic_size = NRF_MESH_TRANSMIC_SIZE_DEFAULT;

  (void)access_model_reply(server->model_handle, message, &reply);
}

/*****************************************************************************
 * Opcode handler callbacks
 *****************************************************************************/

static void handle_set_cb(access_model_handle_t handle,
                          const access_message_rx_t *message, void *args) {
  ecare_server_t *server = args;
  ecare_state_t state = ((ecare_msg_set_t *)message->p_data)->state;

  server->state = state;
  server->set_cb(server, state);

  reply_status(server, message);
}

static const access_opcode_handler_t opcode_handlers[] = {
    {ACCESS_OPCODE_VENDOR(ECARE_OPCODE_SET, ECARE_COMPANY_ID), handle_set_cb}};

/*****************************************************************************
 * Public API
 *****************************************************************************/

uint32_t ecare_server_init(ecare_server_t *server, uint16_t element_index) {
  if (server == NULL || server->set_cb == NULL) {
    return NRF_ERROR_NULL;
  }

  access_model_add_params_t init_params;

  init_params.element_index = element_index;
  init_params.model_id.model_id = ECARE_SERVER_MODEL_ID;
  init_params.model_id.company_id = ECARE_COMPANY_ID;
  init_params.p_opcode_handlers = &opcode_handlers[0];
  init_params.opcode_count =
      sizeof(opcode_handlers) / sizeof(opcode_handlers[0]);
  init_params.p_args = server;
  init_params.publish_timeout_cb = NULL;

  return access_model_add(&init_params, &server->model_handle);
}
