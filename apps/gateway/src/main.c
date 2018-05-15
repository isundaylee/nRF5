#include <stdio.h>

#include "nrf_delay.h"
#include "nrf_gpio.h"

#include "access_config.h"

#include "mesh_app_utils.h"
#include "mesh_softdevice_init.h"
#include "mesh_stack.h"

#include "nrf_mesh_events.h"

#include "net_state.h"

#include "custom_log.h"
#include "provisioner.h"

#include "ecare_server.h"

#define PIN_LED_ERROR 27
#define PIN_LED_INDICATION 28

app_state_t app_state;

ecare_server_t ecare_server;

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
  error_info_t *error_info = (error_info_t *)info;

  nrf_gpio_cfg_output(PIN_LED_ERROR);
  nrf_gpio_pin_set(PIN_LED_ERROR);

  LOG_ERROR("Encountered error %d on line %d in file %s", error_info->err_code,
            error_info->line_num, error_info->p_file_name);

  NRF_BREAKPOINT_COND;
  while (1) {
  }
}

static void init_leds() {
  nrf_gpio_cfg_output(PIN_LED_ERROR);
  nrf_gpio_cfg_output(PIN_LED_INDICATION);
}

static void init_logging() {
  LOG_INIT();
  LOG_INFO("Hello, world!");
}

static void config_server_evt_cb(config_server_evt_t const *evt) {}

static void init_mesh() {
  // Initialize the softdevice
  nrf_clock_lf_cfg_t lfc_cfg = {.source = NRF_CLOCK_LF_SRC_XTAL,
                                .rc_ctiv = 0,
                                .rc_temp_ctiv = 0,
                                .accuracy = NRF_CLOCK_LF_ACCURACY_20_PPM};
  APP_ERROR_CHECK(mesh_softdevice_init(lfc_cfg));
  LOG_INFO("Mesh soft device initialized.");

  // Initialize the Mesh stack
  mesh_stack_init_params_t mesh_init_params = {
      .core.irq_priority = NRF_MESH_IRQ_PRIORITY_LOWEST,
      .core.lfclksrc = lfc_cfg,
      .models.config_server_cb = config_server_evt_cb};
  bool provisioned;
  APP_ERROR_CHECK(mesh_stack_init(&mesh_init_params, &provisioned));

  LOG_INFO("Mesh stack initialized.");

  // Print out the device MAC address
  ble_gap_addr_t addr;
  APP_ERROR_CHECK(sd_ble_gap_addr_get(&addr));

  LOG_INFO("Device address is %2x:%2x:%2x:%2x:%2x:%2x", addr.addr[0],
           addr.addr[1], addr.addr[2], addr.addr[3], addr.addr[4],
           addr.addr[5]);
}

static void ecare_server_cb(ecare_server_t const *server, ecare_state_t state) {
  LOG_INFO("Received new Ecare state: fallen = %d, x = %d, y = %d, z = %d",
           state.fallen, state.x, state.y, state.z);
}

static void prov_init_complete_cb() {
  // Initializes ecare server
  ecare_server.set_cb = ecare_server_cb;
  APP_ERROR_CHECK(ecare_server_init(&ecare_server, 0));
  LOG_INFO("Ecare server initialized. ");

  APP_ERROR_CHECK(access_model_application_bind(ecare_server.model_handle,
                                                app_state.appkey_handle));
  APP_ERROR_CHECK(access_model_publish_application_set(
      ecare_server.model_handle, app_state.appkey_handle));
  LOG_INFO("Ecare server appkey bound successfully. ");

  prov_start_scan();
}

static void start() {
  APP_ERROR_CHECK(mesh_stack_start());
  LOG_INFO("Mesh stack started.");

  if (mesh_stack_is_device_provisioned()) {
    nrf_gpio_pin_set(PIN_LED_INDICATION);

    LOG_ERROR("We have already been provisioned. ");
    LOG_ERROR("Will clear all config and reset in 1s. ");

    mesh_stack_config_clear();
    nrf_delay_ms(1000);
    mesh_stack_device_reset();
    while (1) {
    }
  }

  prov_init(&app_state, prov_init_complete_cb);
}

int main(void) {
  init_leds();
  init_logging();
  init_mesh();

  execution_start(start);
}
