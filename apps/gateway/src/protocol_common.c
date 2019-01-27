#include "protocol_internal.h"

#include <stdarg.h>
#include <stdlib.h>

#include "nrf_mesh_assert.h"

#include "custom_log.h"

static bool request_pending = false;

protocol_request_handler_t protocol_request_handler;

static bool send_vararg(bool guaranteed, char const *fmt, va_list args) {
  static char buf[128];

  int len = vsnprintf(buf, sizeof(buf) - 2, fmt, args);
  if ((len < 0) || (len >= sizeof(buf) - 2)) {
    return false;
  }

  buf[len++] = '\r';
  buf[len++] = '\n';

  return protocol_send_raw(buf, len, guaranteed);
}

bool protocol_send(char const *fmt, ...) {
  bool success = protocol_send_raw("sta ", 4, false);

  va_list args;
  va_start(args, fmt);
  success &= send_vararg(false, fmt, args);
  va_end(args);

  return success;
}

void protocol_reply(uint32_t err, char const *fmt, ...) {
  static char buf[10];

  bool success = true;

  success &= protocol_send_raw("rep ", 4, true);

  itoa(err, buf, 10);
  success &= protocol_send_raw(buf, strlen(buf), true);
  success &= protocol_send_raw(" ", 1, true);

  va_list args;
  va_start(args, fmt);
  success &= send_vararg(true, fmt, args);
  va_end(args);

  request_pending = false;

  NRF_MESH_ASSERT(success);
}

void protocol_post_rx_char(char c) {
  static char buf[256];
  static char *cursor = buf;

  if (request_pending) {
    // We are currently processing a request.
    // Just discard what we just read.

    LOG_INFO("Protocol: Discarding byte '%c' due to pending request.", c);

    if (c == '\n') {
      protocol_reply(NRF_ERROR_INVALID_STATE, "last request still pending");
    }

    return;
  }

  *cursor = c;

  if (*cursor == '\n') {
    *cursor = '\0';

    if ((buf[0] == 'r') && (buf[1] == 'e') && (buf[2] == 'q') &&
        (buf[3] == ' ')) {
      request_pending = true;
      LOG_INFO("Protocol: Received command \"%s\"", buf + 4);

      char *space = strchr(buf + 4, ' ');

      if (space == NULL) {
        // Only opcode, no params
        protocol_request_handler(buf + 4, cursor);
      } else {
        // Both opcode and params
        *space = '\0';
        protocol_request_handler(buf + 4, space + 1);
      }
    } else {
      LOG_ERROR("Protocol: Received unexpected message \"%s\"", buf);
    }

    cursor = buf;

    return;
  }

  cursor++;

  if (cursor == buf + sizeof(buf)) {
    // Out of space! Just clear the buffer and start over.
    cursor = buf;

    LOG_INFO("Protocol: RX buffer overflown.", buf);
  }
}
