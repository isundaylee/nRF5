#include "ecare_client.h"
#include "ecare_common.h"

#include <stddef.h>
#include <stdint.h>

#include "access.h"
#include "access_config.h"
#include "access_reliable.h"
#include "device_state_manager.h"
#include "nrf_mesh.h"
#include "nrf_mesh_assert.h"

#include "custom_log.h"

/*****************************************************************************
 * Static variables
 *****************************************************************************/

/** Keeps a single global TID for all transfers. */
static uint8_t tid;

/*****************************************************************************
 * Static functions
 *****************************************************************************/

static void reliable_status_cb(access_model_handle_t model_handle, void *p_args,
                               access_reliable_status_t status) {
  ecare_client_t *p_client = p_args;
  NRF_MESH_ASSERT(p_client->status_cb != NULL);

  p_client->state.reliable_transfer_active = false;

  switch (status) {
  case ACCESS_RELIABLE_TRANSFER_SUCCESS:
    break;
  case ACCESS_RELIABLE_TRANSFER_TIMEOUT:
    LOG_ERROR("Encountered timeout in Ecare client.")
    break;
  case ACCESS_RELIABLE_TRANSFER_CANCELLED:
    LOG_ERROR("Encountered cancellation in Ecare client.")
    break;
  default:
    NRF_MESH_ASSERT(false);
    break;
  }
}

/** Returns @c true if the message received was from the address corresponding
 * to the clients publish address. */
static bool is_valid_source(const ecare_client_t *p_client,
                            const access_message_rx_t *p_message) {
  /* Check the originator of the status. */
  dsm_handle_t publish_handle;
  nrf_mesh_address_t publish_address;
  if (access_model_publish_address_get(p_client->model_handle,
                                       &publish_handle) != NRF_SUCCESS ||
      publish_handle == DSM_HANDLE_INVALID ||
      dsm_address_get(publish_handle, &publish_address) != NRF_SUCCESS ||
      publish_address.value != p_message->meta_data.src.value) {
    return false;
  } else {
    return true;
  }
}

static uint32_t send_reliable_message(const ecare_client_t *p_client,
                                      ecare_opcode_t opcode,
                                      const uint8_t *p_data, uint16_t length) {
  access_reliable_t reliable;

  reliable.model_handle = p_client->model_handle;
  reliable.message.p_buffer = p_data;
  reliable.message.length = length;
  reliable.message.opcode.opcode = opcode;
  reliable.message.opcode.company_id = ECARE_COMPANY_ID;
  reliable.message.force_segmented = false;
  reliable.message.transmic_size = NRF_MESH_TRANSMIC_SIZE_DEFAULT;
  reliable.reply_opcode.opcode = ECARE_OPCODE_STATUS;
  reliable.reply_opcode.company_id = ECARE_COMPANY_ID;
  reliable.timeout = ACCESS_RELIABLE_TIMEOUT_MIN;
  reliable.status_cb = reliable_status_cb;

  return access_model_reliable_publish(&reliable);
}

/*****************************************************************************
 * Opcode handler callback(s)
 *****************************************************************************/

static void handle_status_cb(access_model_handle_t handle,
                             const access_message_rx_t *message, void *args) {
  ecare_client_t *client = args;
  NRF_MESH_ASSERT(client->status_cb != NULL);

  if (!is_valid_source(client, message)) {
    return;
  }

  ecare_msg_status_t *msg = (ecare_msg_status_t *)message->p_data;
  ecare_state_t state = msg->state;
  client->status_cb(client, state);
}

static const access_opcode_handler_t opcode_handlers[] = {
    {{ECARE_OPCODE_STATUS, ECARE_COMPANY_ID}, handle_status_cb}};

static void handle_publish_timeout(access_model_handle_t handle, void *args) {
  ecare_client_t *client = args;

  if (client->timeout_cb != NULL) {
    client->timeout_cb(handle, args);
  }
}
/*****************************************************************************
 * Public API
 *****************************************************************************/

uint32_t ecare_client_init(ecare_client_t *client, uint16_t element_index) {
  if (client == NULL || client->status_cb == NULL) {
    return NRF_ERROR_NULL;
  }

  access_model_add_params_t init_params;

  init_params.model_id.model_id = ECARE_CLIENT_MODEL_ID;
  init_params.model_id.company_id = ECARE_COMPANY_ID;
  init_params.element_index = element_index;
  init_params.p_opcode_handlers = &opcode_handlers[0];
  init_params.opcode_count =
      sizeof(opcode_handlers) / sizeof(opcode_handlers[0]);
  init_params.p_args = client;
  init_params.publish_timeout_cb = handle_publish_timeout;

  return access_model_add(&init_params, &client->model_handle);
}

uint32_t ecare_client_set(ecare_client_t *client, ecare_state_t state) {
  if (client == NULL || client->status_cb == NULL) {
    return NRF_ERROR_NULL;
  } else if (client->state.reliable_transfer_active) {
    return NRF_ERROR_INVALID_STATE;
  }

  static ecare_msg_set_t msg;

  msg.state = state;
  msg.tid = tid++;

  uint32_t status = send_reliable_message(client, ECARE_OPCODE_SET,
                                          (const uint8_t *)&msg, sizeof(msg));
  if (status == NRF_SUCCESS) {
    client->state.reliable_transfer_active = true;
  }
  return status;
}

void ecare_client_pending_msg_cancel(ecare_client_t *client) {
  (void)access_model_reliable_cancel(client->model_handle);
}
