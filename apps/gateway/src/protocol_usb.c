#ifdef NRF52840_XXAA

#include "protocol.h"

#include <stdarg.h>
#include <stdlib.h>

#include "custom_log.h"

static bool request_pending = false;

static protocol_request_handler_t request_handler;

uint32_t protocol_init(uint32_t tx_pin, uint32_t rx_pin,
                       protocol_request_handler_t req_handler) {
  request_handler = req_handler;
  return NRF_SUCCESS;
}

void protocol_send(char const *fmt, ...) {
}

void protocol_reply(uint32_t err, char const *fmt, ...) {
}

#endif
