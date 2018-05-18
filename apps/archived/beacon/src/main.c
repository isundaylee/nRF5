#include <stdio.h>

#include "nrf_delay.h"
#include "nrf_gpio.h"

#include "mesh_app_utils.h"
#include "mesh_provisionee.h"
#include "mesh_softdevice_init.h"
#include "mesh_stack.h"

#include "custom_log.h"

#include "debug_pins.h"

#define PIN_LED_ERROR 27
#define PIN_LED_INDICATION 28
#define PIN_RESET_NETWORK_CONFIG 4

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

static void config_server_evt_cb(config_server_evt_t const *evt) {
  LOG_INFO("Received config server event. ");

  nrf_gpio_pin_set(PIN_LED_INDICATION);
}

static void provision_complete_cb(void) {
  LOG_INFO("We have been successfully provisioned! ");
}

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
  APP_ERROR_CHECK(mesh_stack_init(&mesh_init_params, NULL));
  LOG_INFO("Mesh stack initialized.");

  ble_gap_addr_t addr;
  APP_ERROR_CHECK(sd_ble_gap_addr_get(&addr));

  LOG_INFO("Device address is %2x:%2x:%2x:%2x:%2x:%2x", addr.addr[0],
           addr.addr[1], addr.addr[2], addr.addr[3], addr.addr[4],
           addr.addr[5]);
}

static void start() {
  APP_ERROR_CHECK(mesh_stack_start());
  LOG_INFO("Mesh stack started.");

  if (!mesh_stack_is_device_provisioned()) {
    const uint8_t static_data[] = {0x6E, 0x6F, 0x72, 0x64, 0x69, 0x63,
                                   0x5F, 0x65, 0x78, 0x61, 0x6D, 0x70,
                                   0x6C, 0x65, 0x5F, 0x31};
    mesh_provisionee_start_params_t prov_start_params = {
        .p_static_data = static_data,
        .prov_complete_cb = provision_complete_cb,
        .p_device_uri = "beacon",
    };
    APP_ERROR_CHECK(mesh_provisionee_prov_start(&prov_start_params));

    nrf_gpio_pin_clear(PIN_LED_INDICATION);

    LOG_ERROR("Provisioning initiated. ");
  } else {
    LOG_ERROR("We have already been provisioned. ");

    nrf_gpio_pin_set(PIN_LED_INDICATION);
    nrf_gpio_cfg_input(PIN_RESET_NETWORK_CONFIG, NRF_GPIO_PIN_PULLDOWN);
    bool should_reset = (nrf_gpio_pin_read(PIN_RESET_NETWORK_CONFIG) != 0);

    LOG_INFO("Reset pin value read as %d", should_reset);

    if (should_reset) {
      LOG_INFO("Will clear all config and reset in 1s. ");

      mesh_stack_config_clear();
      nrf_delay_ms(1000);
      mesh_stack_device_reset();
    } else {
      LOG_ERROR("Will reuse the existing network config. ");
    }
  }
}

int main(void) {
  DEBUG_PINS_INIT();

  init_leds();
  init_logging();
  init_mesh();

  execution_start(start);

  while (1) {
  }
}
