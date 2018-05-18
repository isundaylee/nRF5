#pragma once

#include "access.h"
#include <stdbool.h>
#include <stdint.h>

#include "ecare_common.h"

#define ECARE_SERVER_MODEL_ID (0x0000)

typedef struct __ecare_server ecare_server_t;

typedef void (*ecare_server_set_cb_t)(ecare_server_t const *server,
                                      ecare_state_t state);

struct __ecare_server {
  access_model_handle_t model_handle;

  ecare_state_t state;

  ecare_server_set_cb_t set_cb;
};

uint32_t ecare_server_init(ecare_server_t *server, uint16_t element_index);
