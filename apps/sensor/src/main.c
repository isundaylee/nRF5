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

#include "ble_advdata.h"
#include "nrf_ble_gatt.h"
#include "nrf_delay.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "proxy.h"
#include "sdk_config.h"

#include "custom_log.h"

#define APP_DEVICE_NAME "PROXYCLIENT"

#define APP_PIN_LED_ERROR 27

static ble_gap_scan_params_t const scan_params = {
    .active = 1,
    .interval = MSEC_TO_UNITS(100, UNIT_0_625_MS),
    .window = MSEC_TO_UNITS(50, UNIT_0_625_MS),
    .timeout = 0x0000,
    .scan_phys = BLE_GAP_PHY_1MBPS,
    .filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL,
};

static ble_gap_conn_params_t const conn_params = {
    .min_conn_interval = MSEC_TO_UNITS(250, UNIT_1_25_MS),
    .max_conn_interval = MSEC_TO_UNITS(1000, UNIT_1_25_MS),
    .slave_latency = MSEC_TO_UNITS(0, UNIT_1_25_MS),
    .conn_sup_timeout = MSEC_TO_UNITS(4000, UNIT_1_25_MS),
};

static uint8_t gap_scan_buffer_data[BLE_GAP_SCAN_BUFFER_MIN];
static ble_data_t gap_scan_buffer = {gap_scan_buffer_data,
                                     BLE_GAP_SCAN_BUFFER_MIN};

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

#define MESH_PROXY_SERVICE_UUID 0x1828

static void ble_evt_cb(ble_evt_t const *ble_evt, void *context) {
  ble_gap_evt_t const *gap_evt = &ble_evt->evt.gap_evt;

  switch (ble_evt->header.evt_id) {
  case BLE_GAP_EVT_ADV_REPORT: //
  {
    uint16_t *service_uuid = (uint16_t *)ble_advdata_parse(
        gap_evt->params.adv_report.data.p_data,
        gap_evt->params.adv_report.data.len,
        BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_COMPLETE);

    if (service_uuid != NULL && *service_uuid == MESH_PROXY_SERVICE_UUID) {
      LOG_INFO("Found node with Mesh Proxy service. ");

      uint32_t err = sd_ble_gap_connect(&gap_evt->params.adv_report.peer_addr,
                                        &scan_params, &conn_params,
                                        MESH_SOFTDEVICE_CONN_CFG_TAG);

      if (err == NRF_SUCCESS) {
        LOG_INFO("Connecting to target... ", err);
      } else {
        LOG_ERROR("Error on connecting: %d", err);
      }
    } else {
      APP_ERROR_CHECK(sd_ble_gap_scan_start(NULL, &gap_scan_buffer));
    }

    break;
  }

  case BLE_GAP_EVT_CONNECTED: //
  {
    LOG_INFO("Connection established. ");
    break;
  }

  default: //
  {
    LOG_INFO("Received unhandled BLE event: %d", ble_evt->header.evt_id);
    break;
  }
  }
}

static void init_softdevice() {
  APP_ERROR_CHECK(nrf_sdh_enable_request());

  uint32_t ram_start = 0;
  APP_ERROR_CHECK(
      nrf_sdh_ble_default_cfg_set(MESH_SOFTDEVICE_CONN_CFG_TAG, &ram_start));
  APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram_start));

  NRF_SDH_BLE_OBSERVER(ble_observer, 3, ble_evt_cb, NULL);
}

static void init_gap_params(void) {
  ble_gap_conn_sec_mode_t sec_mode;

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

  APP_ERROR_CHECK(sd_ble_gap_device_name_set(
      &sec_mode, (const uint8_t *)APP_DEVICE_NAME, strlen(APP_DEVICE_NAME)));
}

NRF_BLE_GATT_DEF(gatt);

static void gatt_evt_cb(nrf_ble_gatt_t *gatt, nrf_ble_gatt_evt_t const *evt) {
  switch (evt->evt_id) {
  case NRF_BLE_GATT_EVT_ATT_MTU_UPDATED: //
  {
    LOG_INFO("GATT attribute MTU changed to %d", evt->params.att_mtu_effective);
    break;
  }

  default: //
  {
    LOG_INFO("Received unhandled GATT event: %d", evt->evt_id);
    break;
  }
  }
}

static void init_gatt() {
  APP_ERROR_CHECK(nrf_ble_gatt_init(&gatt, gatt_evt_cb));
}

static void initialize(void) {
  __LOG_INIT(LOG_SRC_APP | LOG_SRC_ACCESS | LOG_SRC_BEARER, LOG_LEVEL_DBG1,
             log_callback_custom);
  LOG_INFO("Bluetooth Mesh relay node is initializing. ");

  init_softdevice();
  init_gap_params();
  init_gatt();
}

static void start() {
  LOG_INFO("Bluetooth Mesh sensor node is scanning for relay nodes. ");

  APP_ERROR_CHECK(sd_ble_gap_scan_start(&scan_params, &gap_scan_buffer));
}

int main(void) {
  initialize();
  execution_start(start);

  for (;;) {
    (void)sd_app_evt_wait();
  }
}
