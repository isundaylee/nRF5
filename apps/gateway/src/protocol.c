#include "protocol.h"

#include <stdarg.h>

static void uart_error_handler(app_uart_evt_t *event) {
  if (event->evt_type == APP_UART_COMMUNICATION_ERROR) {
    APP_ERROR_HANDLER(event->data.error_communication);
  } else if (event->evt_type == APP_UART_FIFO_ERROR) {
    APP_ERROR_HANDLER(event->data.error_code);
  }
}

uint32_t protocol_init(uint32_t tx_pin, uint32_t rx_pin) {
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

void protocol_send(char const *fmt, ...) {
  static char buf[128];

  va_list args;
  va_start(args, fmt);

  int len = vsnprintf(buf, sizeof(buf) - 2, fmt, args);
  if ((len < 0) || (len >= sizeof(buf) - 2)) {
    va_end(args);
    return;
  }

  buf[len++] = '\r';
  buf[len++] = '\n';

  for (int i = 0; i < len; i++) {
    uint32_t err_code = app_uart_put(buf[i]);
    if (err_code == NRF_ERROR_NO_MEM) {
      va_end(args);
      return;
    }

    APP_ERROR_CHECK(err_code);
  }

  va_end(args);
}
