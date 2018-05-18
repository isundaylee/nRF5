#pragma once

#include "access.h"
#include <stdint.h>

#include "ecare_common.h"

/** Simple OnOff Client model ID. */
#define ECARE_CLIENT_MODEL_ID (0x0001)

typedef struct __ecare_client ecare_client_t;

typedef void (*ecare_status_cb_t)(const ecare_client_t *self,
                                  ecare_state_t state);
typedef void (*ecare_timeout_cb_t)(access_model_handle_t handle, void *self);

struct __ecare_client {
  access_model_handle_t model_handle;

  ecare_status_cb_t status_cb;
  ecare_timeout_cb_t timeout_cb;

  struct {
    bool reliable_transfer_active;
    ecare_msg_set_t data;
  } state;
};

uint32_t ecare_client_init(ecare_client_t *client, uint16_t element_index);
uint32_t ecare_client_set(ecare_client_t *client, ecare_state_t state);
uint32_t ecare_client_set_unreliable(ecare_client_t *client,
                                     ecare_state_t state);

void ecare_client_pending_msg_cancel(ecare_client_t *client);
