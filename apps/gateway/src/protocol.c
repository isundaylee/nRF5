#include "protocol_internal.h"

#include <stdarg.h>
#include <stdlib.h>

#include "custom_log.h"

#ifndef NRF52840_XXAA

static void uart_rx() {
  for (;;) {
    char c;
    uint32_t err = app_uart_get(&c);

    if (err == NRF_ERROR_NOT_FOUND) {
      // We'll be back -- later.
      return;
    }

    protocol_post_rx_char(c);
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
  APP_UART_FIFO_INIT(&comm_params, 512, 512, uart_error_handler, 6, err_code);

  return err_code;
}

void protocol_send_raw(char const *buf, size_t len) {
  for (int i = 0; i < len; i++) {
    uint32_t err_code = app_uart_put(buf[i]);
    if (err_code == NRF_ERROR_NO_MEM) {
      return;
    }

    APP_ERROR_CHECK(err_code);
  }
}

void protocol_process() {}

uint32_t protocol_start() { return NRF_SUCCESS; }

#endif
