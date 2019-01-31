#include "protocol_config_client.h"

#include <stdlib.h>
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
  uint32_t addr;
  protocol_config_client_request_type_t type;

  union {
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

static bool parse_uint32(char *str, uint32_t *out, int base) {
  char *tok = strtok(str, " ");
  if (tok == NULL) {
    return false;
  }

  LOG_INFO("TOK: %s", tok);

  *out = strtol(tok, NULL, base);

  return true;
}

static bool parse_string(char *str, char **out) {
  char *tok = strtok(str, " ");
  if (tok == NULL) {
    return false;
  }

  LOG_INFO("TOK: %s", tok);
  *out = tok;

  return true;
}

// Returns whether PARSING is successful
bool parse_and_start_request(char const *op, char *params) {
  if (!parse_uint32(params, &pending_request.addr, 16)) {
    return false;
  }

  char *config_op;
  if (!parse_string(NULL, &config_op)) {
    return false;
  }

  if (strcmp(config_op, "reset") == 0) {
    pending_request.type = PROTOCOL_CONFIG_CLIENT_RESET_REQUEST;
  } else {
    return false;
  }

  uint32_t err = conf_start(pending_request.addr, conf_client_step_builder);
  if (err != NRF_SUCCESS) {
    pending_request.type = PROTOCOL_CONFIG_CLIENT_NO_REQUEST;
    protocol_reply(err, "failed to start configuration");
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////

uint32_t protocol_config_client_init() { return NRF_SUCCESS; }

bool protocol_config_client_handle_request(char const *op, char *params) {
  if (strcmp(op, "config") != 0) {
    return false;
  }

  if (!parse_and_start_request(op, params)) {
    protocol_reply(NRF_ERROR_INVALID_PARAM, "cannot parse request");
  }

  return true;
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
  return true;
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
  return true;
}
