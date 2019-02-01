#include "protocol_config_client.h"

#include <stdlib.h>
#include <string.h>

#include <nrf_mesh_utils.h>

#include "configurator.h"

#include "protocol.h"

#include "custom_log.h"

////////////////////////////////////////////////////////////////////////////////

typedef enum {
  PROTOCOL_CONFIG_CLIENT_RESET_REQUEST = 1,
  PROTOCOL_CONFIG_CLIENT_PUB_SET_REQUEST,

  PROTOCOL_CONFIG_CLIENT_NO_REQUEST = 100,
} protocol_config_client_request_type_t;

typedef struct {
  uint32_t element_addr;
  uint32_t company_id;
  uint32_t model_id;
  uint32_t pub_addr;
  uint32_t ttl;
  uint32_t period_100ms;
} protocol_config_client_pub_set_request_t;

typedef struct {
  uint32_t addr;
  protocol_config_client_request_type_t type;

  union {
    protocol_config_client_pub_set_request_t pub_set;
  } request;
} protocol_config_client_request_t;

static protocol_config_client_request_t pending_request = {
    .type = PROTOCOL_CONFIG_CLIENT_NO_REQUEST};

////////////////////////////////////////////////////////////////////////////////

static nrf_mesh_address_t get_mesh_address(uint32_t addr) {
  nrf_mesh_address_t mesh_addr = {
      .type = nrf_mesh_address_type_get(addr),
      .value = addr,
  };

  return mesh_addr;
}

static access_publish_period_t get_publish_period(uint32_t period_100ms) {
  access_publish_period_t period;

  if (period_100ms <= 31) {
    period.step_res = ACCESS_PUBLISH_RESOLUTION_100MS;
    period.step_num = period_100ms;

    return period;
  }

  uint32_t period_1s = period_100ms / 10;
  if (period_1s <= 31) {
    period.step_res = ACCESS_PUBLISH_RESOLUTION_1S;
    period.step_num = period_1s;

    return period;
  }

  uint32_t period_10s = period_1s / 10;
  if (period_10s <= 31) {
    period.step_res = ACCESS_PUBLISH_RESOLUTION_10S;
    period.step_num = period_10s;

    return period;
  }

  uint32_t period_10min = period_10s / 60;
  if (period_10min <= 31) {
    period.step_res = ACCESS_PUBLISH_RESOLUTION_10MIN;
    period.step_num = period_10min;

    return period;
  }

  period.step_res = ACCESS_PUBLISH_RESOLUTION_100MS;
  period.step_num = 0;

  return period;
}

static void conf_client_step_builder(
    uint16_t addr, config_msg_composition_data_status_t const *composition_data,
    conf_step_t *steps_out) {
  conf_step_t *cursor = steps_out;

  if (pending_request.type == PROTOCOL_CONFIG_CLIENT_RESET_REQUEST) {
    cursor->type = CONF_STEP_TYPE_NODE_RESET;
    cursor++;
  } else if (pending_request.type == PROTOCOL_CONFIG_CLIENT_PUB_SET_REQUEST) {
    protocol_config_client_pub_set_request_t req =
        pending_request.request.pub_set;

    cursor->type = CONF_STEP_TYPE_MODEL_PUBLICATION_SET;
    cursor->params.model_publication_set.element_addr = req.element_addr;
    cursor->params.model_publication_set.model_id.company_id = req.company_id;
    cursor->params.model_publication_set.model_id.model_id = req.model_id;
    cursor->params.model_publication_set.publish_address =
        get_mesh_address(req.pub_addr);
    cursor->params.model_publication_set.appkey_index = APP_APPKEY_IDX;
    cursor->params.model_publication_set.publish_ttl = req.ttl;
    cursor->params.model_publication_set.publish_period =
        get_publish_period(req.period_100ms);
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
  } else if (strcmp(config_op, "pub_set") == 0) {
    pending_request.type = PROTOCOL_CONFIG_CLIENT_PUB_SET_REQUEST;

    if (!parse_uint32(NULL, &pending_request.request.pub_set.element_addr,
                      16) ||
        !parse_uint32(NULL, &pending_request.request.pub_set.company_id, 16) ||
        !parse_uint32(NULL, &pending_request.request.pub_set.model_id, 16) ||
        !parse_uint32(NULL, &pending_request.request.pub_set.pub_addr, 16) ||
        !parse_uint32(NULL, &pending_request.request.pub_set.ttl, 10) ||
        !parse_uint32(NULL, &pending_request.request.pub_set.period_100ms,
                      10)) {
      return false;
    }
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
  case PROTOCOL_CONFIG_CLIENT_RESET_REQUEST:   //
  case PROTOCOL_CONFIG_CLIENT_PUB_SET_REQUEST: //
  {
    protocol_reply(NRF_SUCCESS, "success");
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
  case PROTOCOL_CONFIG_CLIENT_RESET_REQUEST:   //
  case PROTOCOL_CONFIG_CLIENT_PUB_SET_REQUEST: //
  {
    protocol_reply(err, "failed");
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
