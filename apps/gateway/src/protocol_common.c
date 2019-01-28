#include "protocol_internal.h"

#include <stdarg.h>
#include <stdlib.h>

#include "nrf_mesh_assert.h"

#include "custom_log.h"

#define MESSAGE_BUFFER_SIZE 256

static bool request_pending = false;

protocol_request_handler_t protocol_request_handler;

static char message_buffer[MESSAGE_BUFFER_SIZE];
char *message_buffer_cursor;

void buffer_reset() { message_buffer_cursor = message_buffer; }

size_t buffer_space_left() {
  return (message_buffer + MESSAGE_BUFFER_SIZE - message_buffer_cursor - 1);
}

bool buffer_append_string(char const *string) {
  size_t len = strlen(string);

  if (len > buffer_space_left()) {
    return false;
  }

  strcpy(message_buffer_cursor, string);
  message_buffer_cursor += len;

  return true;
}

bool buffer_append_vararg(char const *fmt, va_list args) {
  int limit = buffer_space_left() + 1;

  int len = vsnprintf(message_buffer_cursor, limit, fmt, args);
  if ((len < 0) || (len >= limit)) {
    return false;
  }

  message_buffer_cursor += len;

  return true;
}

bool protocol_send(char const *fmt, ...) {
  buffer_reset();

  if (!buffer_append_string("sta ")) {
    return false;
  }

  va_list args;
  va_start(args, fmt);
  if (!buffer_append_vararg(fmt, args)) {
    va_end(args);
    return false;
  }
  va_end(args);

  if (!buffer_append_string("\r\n")) {
    return false;
  }

  return protocol_send_raw(message_buffer,
                           message_buffer_cursor - message_buffer, false);
}

void protocol_reply(uint32_t err, char const *fmt, ...) {
  request_pending = false;

  buffer_reset();

  if (!buffer_append_string("rep ")) {
    NRF_MESH_ASSERT(false);
    return;
  }

  static char err_buf[10];
  itoa(err, err_buf, sizeof(err_buf));
  if (!buffer_append_string(err_buf)) {
    NRF_MESH_ASSERT(false);
    return;
  }

  if (!buffer_append_string(" ")) {
    NRF_MESH_ASSERT(false);
    return;
  }

  va_list args;
  va_start(args, fmt);
  if (!buffer_append_vararg(fmt, args)) {
    va_end(args);
    NRF_MESH_ASSERT(false);
    return;
  }
  va_end(args);

  if (!buffer_append_string("\r\n")) {
    NRF_MESH_ASSERT(false);
    return;
  }

  bool success = protocol_send_raw(
      message_buffer, message_buffer_cursor - message_buffer, true);
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
