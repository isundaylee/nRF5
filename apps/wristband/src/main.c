#include <stdio.h>

#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "mesh_softdevice_init.h"

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
  nrf_gpio_cfg_output(8);
  nrf_gpio_pin_set(8);

  NRF_LOG_FINAL_FLUSH();

  NRF_BREAKPOINT_COND;
  while (1) {
  }
}

static void init_logging() {
  int err = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(err);

  NRF_LOG_DEFAULT_BACKENDS_INIT();

  NRF_LOG_INFO("Hello, world!");
}

static void init_mesh() {
  nrf_clock_lf_cfg_t lfc_cfg = {.source = NRF_CLOCK_LF_SRC_XTAL,
                                .rc_ctiv = 0,
                                .rc_temp_ctiv = 0,
                                .accuracy = NRF_CLOCK_LF_ACCURACY_20_PPM};

  int err = mesh_softdevice_init(lfc_cfg);
  APP_ERROR_CHECK(err);

  NRF_LOG_INFO("Mesh soft device initialized.");
}

int main(void) {
  init_logging();
  init_mesh();

  while (1) {
    NRF_LOG_PROCESS();
  }
}
