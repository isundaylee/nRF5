#include <stdio.h>

#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
  nrf_gpio_cfg_output(7);
  nrf_gpio_pin_set(7);

  NRF_BREAKPOINT_COND;
}

int main(void) {
  int err = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(err);

  NRF_LOG_DEFAULT_BACKENDS_INIT();

  NRF_LOG_INFO("Hello, world!");

  while (1) {
    NRF_LOG_PROCESS();
  }
}
