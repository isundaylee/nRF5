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

#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "proxy.h"
#include "sdk_config.h"

#include "custom_log.h"

#define APP_DEVICE_NAME "PROXYTEST"
#define GAP_MIN_CONN_INTERVAL MSEC_TO_UNITS(250, UNIT_1_25_MS)
#define GAP_MAX_CONN_INTERVAL MSEC_TO_UNITS(1000, UNIT_1_25_MS)
#define GAP_SLAVE_LATENCY 0
#define GAP_CONN_SUP_TIMEOUT MSEC_TO_UNITS(4000, UNIT_10_MS)

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

static void on_sd_evt(uint32_t sd_evt, void *p_context) {
  (void)nrf_mesh_on_sd_evt(sd_evt);
}

NRF_SDH_SOC_OBSERVER(mesh_observer, NRF_SDH_BLE_STACK_OBSERVER_PRIO, on_sd_evt,
                     NULL);

static bool provisioned;

static void provisioning_complete_cb(void) {
  dsm_local_unicast_address_t node_address;
  LOG_INFO("Successfully provisioned. ");
  dsm_local_unicast_addresses_get(&node_address);
  LOG_INFO("Node Address: 0x%04x. ", node_address.address_start);
}

static void node_reset(void) {
  LOG_INFO("----- Node reset -----");

  // This function may return if there are ongoing flash operations.
  mesh_stack_device_reset();
}

static void config_server_evt_cb(const config_server_evt_t *p_evt) {
  if (p_evt->type == CONFIG_SERVER_EVT_NODE_RESET) {
    node_reset();
  }
}

static void mesh_init(void) {
  mesh_stack_init_params_t init_params = {
      .core.irq_priority = NRF_MESH_IRQ_PRIORITY_LOWEST,
      .core.lfclksrc = DEV_BOARD_LF_CLK_CFG,
      .models.config_server_cb = config_server_evt_cb};
  ERROR_CHECK(mesh_stack_init(&init_params, &provisioned));
}

static void gap_params_init(void) {
  uint32_t err_code;
  ble_gap_conn_params_t gap_conn_params;
  ble_gap_conn_sec_mode_t sec_mode;

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

  err_code = sd_ble_gap_device_name_set(
      &sec_mode, (const uint8_t *)APP_DEVICE_NAME, strlen(APP_DEVICE_NAME));
  APP_ERROR_CHECK(err_code);

  memset(&gap_conn_params, 0, sizeof(gap_conn_params));

  gap_conn_params.min_conn_interval = GAP_MIN_CONN_INTERVAL;
  gap_conn_params.max_conn_interval = GAP_MAX_CONN_INTERVAL;
  gap_conn_params.slave_latency = GAP_SLAVE_LATENCY;
  gap_conn_params.conn_sup_timeout = GAP_CONN_SUP_TIMEOUT;

  err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
  APP_ERROR_CHECK(err_code);
}

static void initialize(void) {
  __LOG_INIT(LOG_SRC_APP | LOG_SRC_ACCESS, LOG_LEVEL_DBG1, log_callback_custom);
  LOG_INFO("----- BLE Mesh Light Switch Proxy Server Demo -----");

  uint32_t err_code = nrf_sdh_enable_request();
  APP_ERROR_CHECK(err_code);

  uint32_t ram_start = 0;
  /* Set the default configuration (as defined through sdk_config.h). */
  err_code =
      nrf_sdh_ble_default_cfg_set(MESH_SOFTDEVICE_CONN_CFG_TAG, &ram_start);
  APP_ERROR_CHECK(err_code);

  err_code = nrf_sdh_ble_enable(&ram_start);
  APP_ERROR_CHECK(err_code);

  gap_params_init();

  mesh_init();
}

static void start(void) {
  ERROR_CHECK(mesh_stack_start());

  if (!provisioned) {
    LOG_INFO("Starting the provisioning process. ");
    static const uint8_t static_auth_data[] = {
        0x6E, 0x6F, 0x72, 0x64, 0x69, 0x63, 0x5F, 0x65,
        0x78, 0x61, 0x6D, 0x70, 0x6C, 0x65, 0x5F, 0x31};
    mesh_provisionee_start_params_t prov_start_params = {
        .p_static_data = static_auth_data,
        .prov_complete_cb = provisioning_complete_cb};
    ERROR_CHECK(mesh_provisionee_prov_start(&prov_start_params));
  } else {
    LOG_INFO("Node is already provisioned. ");
  }

  const uint8_t *uuid = nrf_mesh_configure_device_uuid_get();
  __LOG_XB(LOG_SRC_APP, LOG_LEVEL_INFO, "Device UUID ", uuid,
           NRF_MESH_UUID_SIZE);
}

int main(void) {
  initialize();
  execution_start(start);

  for (;;) {
    // (void)sd_app_evt_wait();
  }
}
