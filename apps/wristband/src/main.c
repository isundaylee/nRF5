#include <stdio.h>

#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "mesh_softdevice_init.h"
#include "mesh_stack.h"

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
  error_info_t *error_info = (error_info_t *)info;

  nrf_gpio_cfg_output(8);
  nrf_gpio_pin_set(8);

  NRF_LOG_INFO("Encountered error %d on line %d in file %s",
               error_info->err_code, error_info->line_num,
               error_info->p_file_name);
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

static void config_server_evt_cb(const config_server_evt_t *evt) {}

static void init_mesh() {
  // Initialize the softdevice
  nrf_clock_lf_cfg_t lfc_cfg = {.source = NRF_CLOCK_LF_SRC_XTAL,
                                .rc_ctiv = 0,
                                .rc_temp_ctiv = 0,
                                .accuracy = NRF_CLOCK_LF_ACCURACY_20_PPM};

  int err = mesh_softdevice_init(lfc_cfg);
  APP_ERROR_CHECK(err);

  NRF_LOG_INFO("Mesh soft device initialized.");

  // Initialize the Mesh stack
  mesh_stack_init_params_t mesh_init_params = {
      .core.irq_priority = NRF_MESH_IRQ_PRIORITY_LOWEST,
      .core.lfclksrc = lfc_cfg,
      .models.config_server_cb = config_server_evt_cb};
  bool provisioned;
  err = mesh_stack_init(&mesh_init_params, &provisioned);
  APP_ERROR_CHECK(err);

  NRF_LOG_INFO("Mesh stack initialized.");
}

int main(void) {
  init_logging();
  init_mesh();

  while (1) {
    NRF_LOG_PROCESS();
  }
}
