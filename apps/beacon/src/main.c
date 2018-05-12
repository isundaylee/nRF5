#include <stdio.h>

#include "nrf_delay.h"
#include "nrf_gpio.h"

#include "mesh_app_utils.h"
#include "mesh_provisionee.h"
#include "mesh_softdevice_init.h"
#include "mesh_stack.h"

#include "custom_log.h"

#define PIN_LED_ERROR 7
#define PIN_LED_INDICATION 2

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
  error_info_t *error_info = (error_info_t *)info;

  nrf_gpio_cfg_output(PIN_LED_ERROR);
  nrf_gpio_pin_set(PIN_LED_ERROR);

  LOG_INFO("Encountered error %d on line %d in file %s", error_info->err_code,
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

/**
 * The callback function for incoming packets (seems like all Bluetooth Mesh
 * packets are in the format of Bluetooth advertisement packets -- hence the
 * parameter type).
 */
static void packet_rx_cb(nrf_mesh_adv_packet_rx_data_t const *packet) {
  // LOG_INFO("Received packet on channel %d with RSSI %d",
  //          packet->p_metadata->params.scanner.channel,
  // packet->p_metadata->params.scanner.rssi);
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

  // Set the packet RX callback
  nrf_mesh_rx_cb_set(packet_rx_cb);
  LOG_INFO("Packet RX callback set.");
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
    };
    APP_ERROR_CHECK(mesh_provisionee_prov_start(&prov_start_params));

    LOG_INFO("Provisioning initiated. ");
  } else {
    LOG_INFO("We have already been provisioned. ");
    LOG_INFO("Will clear all config and reset in 1s. ");

    mesh_stack_config_clear();
    nrf_delay_ms(1000);
    mesh_stack_device_reset();
  }
}

int main(void) {
  init_leds();
  init_logging();
  init_mesh();

  execution_start(start);

  while (1) {
  }
}
