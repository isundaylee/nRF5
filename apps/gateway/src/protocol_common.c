#include "protocol_internal.h"

#include <stdarg.h>
#include <stdlib.h>

#include "custom_log.h"

static bool request_pending = false;

protocol_request_handler_t protocol_request_handler;

static void send_vararg(char const *fmt, va_list args) {
  static char buf[128];

  int len = vsnprintf(buf, sizeof(buf) - 2, fmt, args);
  if ((len < 0) || (len >= sizeof(buf) - 2)) {
    return;
  }

  buf[len++] = '\r';
  buf[len++] = '\n';

  protocol_send_raw(buf, len);
}

void protocol_send(char const *fmt, ...) {
  protocol_send_raw("sta ", 4);

  va_list args;
  va_start(args, fmt);
  send_vararg(fmt, args);
  va_end(args);
}

void protocol_reply(uint32_t err, char const *fmt, ...) {
  static char buf[10];

  protocol_send_raw("rep ", 4);

  itoa(err, buf, 10);
  protocol_send_raw(buf, strlen(buf));
  protocol_send_raw(" ", 1);

  va_list args;
  va_start(args, fmt);
  send_vararg(fmt, args);
  va_end(args);

  request_pending = false;
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
