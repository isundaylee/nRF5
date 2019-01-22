#include "protocol.h"

#include <stdarg.h>
#include <stdlib.h>

#include "custom_log.h"

static bool request_pending = false;

static protocol_request_handler_t request_handler;

static void uart_rx() {
  static char buf[256];
  static char *cursor = buf;

  for (;;) {
    uint32_t err = app_uart_get((uint8_t *)cursor);

    if (err == NRF_ERROR_NOT_FOUND) {
      // We'll be back -- later.
      return;
    }

    if (request_pending) {
      // We are currently processing a request.
      // Just discard what we just read.

      LOG_INFO("Protocol: Discarding byte '%c' due to pending request.",
               *cursor);

      if (*cursor == '\n') {
        protocol_reply(NRF_ERROR_INVALID_STATE, "last request still pending");
      }

      continue;
    }

    if (*cursor == '\n') {
      *cursor = '\0';

      if ((buf[0] == 'r') && (buf[1] == 'e') && (buf[2] == 'q') &&
          (buf[3] == ' ')) {
        request_pending = true;
        LOG_INFO("Protocol: Received command \"%s\"", buf + 4);

        char *space = strchr(buf + 4, ' ');

        if (space == NULL) {
          // Only opcode, no params
          request_handler(buf + 4, cursor);
        } else {
          // Both opcode and params
          *space = '\0';
          request_handler(buf + 4, space + 1);
        }
      } else {
        LOG_ERROR("Protocol: Received unexpected message \"%s\"", buf);
      }

      cursor = buf;

      continue;
    }

    cursor++;

    if (cursor == buf + sizeof(buf)) {
      // Out of space! Just clear the buffer and start over.
      cursor = buf;

      LOG_INFO("Protocol: RX buffer overflown.", buf);
    }
  }
}

static void uart_error_handler(app_uart_evt_t *event) {
  if (event->evt_type == APP_UART_COMMUNICATION_ERROR) {
    APP_ERROR_HANDLER(event->data.error_communication);
  } else if (event->evt_type == APP_UART_FIFO_ERROR) {
    APP_ERROR_HANDLER(event->data.error_code);
  } else if (event->evt_type == APP_UART_DATA_READY) {
    uart_rx();
  }
}

uint32_t protocol_init(uint32_t tx_pin, uint32_t rx_pin,
                       protocol_request_handler_t req_handler) {
  request_handler = req_handler;

  const app_uart_comm_params_t comm_params = {rx_pin,
                                              tx_pin,
                                              0,
                                              0,
                                              APP_UART_FLOW_CONTROL_DISABLED,
                                              false,
                                              NRF_UART_BAUDRATE_115200};

  uint32_t err_code;
  APP_UART_FIFO_INIT(&comm_params, 512, 512, uart_error_handler,
                     APP_IRQ_PRIORITY_LOWEST, err_code);

  return err_code;
}

static void send_raw(char const *buf, size_t len) {
  for (int i = 0; i < len; i++) {
    uint32_t err_code = app_uart_put(buf[i]);
    if (err_code == NRF_ERROR_NO_MEM) {
      return;
    }

    APP_ERROR_CHECK(err_code);
  }
}

static void send_vararg(char const *fmt, va_list args) {
  static char buf[128];

  int len = vsnprintf(buf, sizeof(buf) - 2, fmt, args);
  if ((len < 0) || (len >= sizeof(buf) - 2)) {
    return;
  }

  buf[len++] = '\r';
  buf[len++] = '\n';

  send_raw(buf, len);
}

void protocol_send(char const *fmt, ...) {
  send_raw("sta ", 4);

  va_list args;
  va_start(args, fmt);
  send_vararg(fmt, args);
  va_end(args);
}

void protocol_reply(uint32_t err, char const *fmt, ...) {
  static char buf[10];

  send_raw("rep ", 4);

  itoa(err, buf, 10);
  send_raw(buf, strlen(buf));
  send_raw(" ", 1);

  va_list args;
  va_start(args, fmt);
  send_vararg(fmt, args);
  va_end(args);

  request_pending = false;
}
