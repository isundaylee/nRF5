#include <stdio.h>

#include "nrf_delay.h"
#include "nrf_gpio.h"

#include "mesh_app_utils.h"
#include "mesh_softdevice_init.h"
#include "mesh_stack.h"

#include "nrf_mesh_events.h"

#include "net_state.h"

#include "custom_log.h"
#include "provisioner.h"

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

static void mesh_evt_cb(nrf_mesh_evt_t const *evt) {
  // LOG_INFO("Got Mesh event with type: %d", evt->type);
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
  bool provisioned;
  APP_ERROR_CHECK(mesh_stack_init(&mesh_init_params, &provisioned));

  LOG_INFO("Mesh stack initialized.");

  // Set the packet RX callback
  nrf_mesh_rx_cb_set(packet_rx_cb);
  LOG_INFO("Packet RX callback set.");

  nrf_mesh_evt_handler_t evt_handler_params = {.evt_cb = mesh_evt_cb,
                                               .p_next = NULL};
  nrf_mesh_evt_handler_add(&evt_handler_params);

  // Print out the device MAC address
  ble_gap_addr_t addr;
  APP_ERROR_CHECK(sd_ble_gap_addr_get(&addr));

  LOG_INFO("Device address is %2x:%2x:%2x:%2x:%2x:%2x", addr.addr[0],
           addr.addr[1], addr.addr[2], addr.addr[3], addr.addr[4],
           addr.addr[5]);
}

static void start() {
  APP_ERROR_CHECK(mesh_stack_start());
  LOG_INFO("Mesh stack started.");

  prov_start_scan();
}

int main(void) {
  init_leds();
  init_logging();
  init_mesh();

  prov_init();

  execution_start(start);

  while (1) {
  }
}
