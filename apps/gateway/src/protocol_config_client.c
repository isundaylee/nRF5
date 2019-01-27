#include "protocol_config_client.h"

#include <string.h>

#include "configurator.h"

#include "protocol.h"

#include "custom_log.h"

////////////////////////////////////////////////////////////////////////////////

typedef enum {
  PROTOCOL_CONFIG_CLIENT_RESET_REQUEST = 1,

  PROTOCOL_CONFIG_CLIENT_NO_REQUEST = 100,
} protocol_config_client_request_type_t;

typedef struct {
  int addr;
} protocol_config_client_reset_request_t;

typedef struct {
  protocol_config_client_request_type_t type;
  union {
    protocol_config_client_reset_request_t reset_request;
  } request;
} protocol_config_client_request_t;

static protocol_config_client_request_t pending_request = {
    .type = PROTOCOL_CONFIG_CLIENT_NO_REQUEST};

////////////////////////////////////////////////////////////////////////////////

static void conf_client_step_builder(
    uint16_t addr, config_msg_composition_data_status_t const *composition_data,
    conf_step_t *steps_out) {
  conf_step_t *cursor = steps_out;

  if (pending_request.type == PROTOCOL_CONFIG_CLIENT_RESET_REQUEST) {
    cursor->type = CONF_STEP_TYPE_NODE_RESET;
    cursor++;
  } else {
    LOG_ERROR("Protocol Config Client: Unexpected invocation of "
              "conf_client_step_builder.");
  }

  cursor->type = CONF_STEP_TYPE_DONE;
}

////////////////////////////////////////////////////////////////////////////////

uint32_t protocol_config_client_init() { return NRF_SUCCESS; }

bool protocol_config_client_handle_request(char const *op, char const *params) {
  if (strcmp(op, "reset") == 0) {
    uint32_t addr = strtol(params, NULL, 16);
    uint32_t err = conf_start(addr, conf_client_step_builder);

    if (err != NRF_SUCCESS) {
      protocol_reply(err, "failed to start configuration");
    } else {
      pending_request.type = PROTOCOL_CONFIG_CLIENT_RESET_REQUEST;
      pending_request.request.reset_request.addr = addr;
    }

    return true;
  }

  return false;
}

bool protocol_config_client_handle_conf_success(uint16_t addr) {
  switch (pending_request.type) {
  case PROTOCOL_CONFIG_CLIENT_RESET_REQUEST: //
  {
    protocol_reply(NRF_SUCCESS, "node successfully reset");
    pending_request.type = PROTOCOL_CONFIG_CLIENT_NO_REQUEST;
    return true;
  }

  case PROTOCOL_CONFIG_CLIENT_NO_REQUEST: //
  {
    return false;
  }
  }

  NRF_MESH_ASSERT(false);
}

bool protocol_config_client_handle_conf_failure(uint16_t addr, uint32_t err) {
  switch (pending_request.type) {
  case PROTOCOL_CONFIG_CLIENT_RESET_REQUEST: //
  {
    protocol_reply(err, "failed to reset node");
    pending_request.type = PROTOCOL_CONFIG_CLIENT_NO_REQUEST;
    return true;
  }

  case PROTOCOL_CONFIG_CLIENT_NO_REQUEST: //
  {
    return false;
  }
  }

  NRF_MESH_ASSERT(false);
}
