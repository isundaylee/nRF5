#include "access_config.h"
#include "boards.h"
#include "log.h"
#include "mesh_adv.h"
#include "mesh_app_utils.h"
#include "mesh_provisionee.h"
#include "mesh_stack.h"
#include "net_state.h"
#include "nrf_mesh_config_examples.h"
#include "nrf_mesh_configure.h"

#include "nrf_delay.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "proxy.h"
#include "sdk_config.h"

#include "custom_log.h"

#define APP_DEVICE_NAME "PROXYTEST"

#define APP_PIN_LED_ERROR 27

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
  error_info_t *error_info = (error_info_t *)info;

  nrf_gpio_cfg_output(APP_PIN_LED_ERROR);
  nrf_gpio_pin_set(APP_PIN_LED_ERROR);

  LOG_ERROR("Encountered error %d on line %d in file %s", error_info->err_code,
            error_info->line_num, error_info->p_file_name);

  NRF_BREAKPOINT_COND;
  while (1) {
  }
}

static void on_sd_soc_evt(uint32_t sd_evt, void *p_context) {
  (void)nrf_mesh_on_sd_evt(sd_evt);
}

NRF_SDH_SOC_OBSERVER(sdh_soc_observer, NRF_SDH_BLE_STACK_OBSERVER_PRIO,
                     on_sd_soc_evt, NULL);

static void init_softdevice() {
  APP_ERROR_CHECK(nrf_sdh_enable_request());

  uint32_t ram_start = 0;
  APP_ERROR_CHECK(
      nrf_sdh_ble_default_cfg_set(MESH_SOFTDEVICE_CONN_CFG_TAG, &ram_start));
  APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram_start));
}

static void init_mesh(void) {
  nrf_clock_lf_cfg_t lfc_cfg = {.source = NRF_CLOCK_LF_SRC_XTAL,
                                .rc_ctiv = 0,
                                .rc_temp_ctiv = 0,
                                .accuracy = NRF_CLOCK_LF_ACCURACY_20_PPM};
  mesh_stack_init_params_t init_params = {.core.irq_priority =
                                              NRF_MESH_IRQ_PRIORITY_LOWEST,
                                          .core.lfclksrc = lfc_cfg};
  ERROR_CHECK(mesh_stack_init(&init_params, NULL));
}

static void init_gap_params(void) {
  ble_gap_conn_sec_mode_t sec_mode;

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

  APP_ERROR_CHECK(sd_ble_gap_device_name_set(
      &sec_mode, (const uint8_t *)APP_DEVICE_NAME, strlen(APP_DEVICE_NAME)));
}

static void initialize(void) {
  __LOG_INIT(LOG_SRC_APP | LOG_SRC_ACCESS | LOG_SRC_BEARER, LOG_LEVEL_DBG1,
             log_callback_custom);
  LOG_INFO("Bluetooth Mesh relay node is initializing. ");

  init_softdevice();
  init_mesh();
  init_gap_params();
}

static void prov_complete_cb(void) {
  dsm_local_unicast_address_t node_address;
  LOG_INFO("Successfully provisioned. ");
  dsm_local_unicast_addresses_get(&node_address);
  LOG_INFO("Node Address: 0x%04x. ", node_address.address_start);
}

static void start() {
  LOG_INFO("Bluetooth Mesh relay node is starting up. ");

  APP_ERROR_CHECK(nrf_mesh_enable());

  if (!mesh_stack_is_device_provisioned()) {
    LOG_INFO("Starting the provisioning process. ");

    static const uint8_t static_auth_data[] = {
        0x6E, 0x6F, 0x72, 0x64, 0x69, 0x63, 0x5F, 0x65,
        0x78, 0x61, 0x6D, 0x70, 0x6C, 0x65, 0x5F, 0x31};
    mesh_provisionee_start_params_t prov_start_params = {
        .p_static_data = static_auth_data,
        .prov_complete_cb = prov_complete_cb};
    ERROR_CHECK(mesh_provisionee_prov_start(&prov_start_params));
  } else {
    LOG_INFO("Node is already provisioned. ");

    proxy_enable();
  }
}

int main(void) {
  initialize();
  execution_start(start);

  for (;;) {
    (void)sd_app_evt_wait();
  }
}
