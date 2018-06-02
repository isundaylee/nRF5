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

#include "app_timer.h"
#include "bearer_handler.h"
#include "ble_advdata.h"
#include "ble_db_discovery.h"
#include "health_client.h"
#include "nrf_ble_gatt.h"
#include "nrf_delay.h"
#include "nrf_drv_clock.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "proxy.h"
#include "rand.h"
#include "sdk_config.h"

#include "custom_log.h"
#include "proxy_client.h"

#define APP_DEVICE_NAME "PROXYCLIENT"

#define APP_PIN_FORCE_RESET 20

#define APP_PIN_LED_ERROR 23
#define APP_PIN_LED_INDICATION 24

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

static ble_gap_scan_params_t const scan_params = {
    .active = 1,
    .interval = MSEC_TO_UNITS(100, UNIT_0_625_MS),
    .window = MSEC_TO_UNITS(50, UNIT_0_625_MS),
    .timeout = 0x0000,
    .scan_phys = BLE_GAP_PHY_1MBPS,
    .filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL,
};

static ble_gap_conn_params_t const conn_params = {
    .min_conn_interval = MSEC_TO_UNITS(25, UNIT_1_25_MS),
    .max_conn_interval = MSEC_TO_UNITS(100, UNIT_1_25_MS),
    .slave_latency = MSEC_TO_UNITS(0, UNIT_1_25_MS),
    .conn_sup_timeout = MSEC_TO_UNITS(4000, UNIT_10_MS),
};

static uint8_t gap_scan_buffer_data[BLE_GAP_SCAN_BUFFER_MIN];
static ble_data_t gap_scan_buffer = {gap_scan_buffer_data,
                                     BLE_GAP_SCAN_BUFFER_MIN};

BLE_DB_DISCOVERY_DEF(db_discovery);
NRF_BLE_GATT_DEF(gatt);
PROXY_CLIENT_DEF(proxy_client);
health_client_t health_client;

////////////////////////////////////////////////////////////////////////////////
// App error handler
////////////////////////////////////////////////////////////////////////////////

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

////////////////////////////////////////////////////////////////////////////////
// Callbacks
////////////////////////////////////////////////////////////////////////////////

static void ble_evt_handler(ble_evt_t const *ble_evt, void *context) {
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

    APP_ERROR_CHECK(
        ble_db_discovery_start(&db_discovery, gap_evt->conn_handle));
    proxy_client_conn_handle_assign(&proxy_client, gap_evt->conn_handle);

    break;
  }

  case BLE_GAP_EVT_DISCONNECTED: //
  {
    LOG_INFO("Connection terminated. Reason: %d",
             gap_evt->params.disconnected.reason);

    break;
  }

  case BLE_GATTC_EVT_WRITE_RSP: //
  {
    uint16_t status = ble_evt->evt.gattc_evt.gatt_status;
    if (status != BLE_GATT_STATUS_SUCCESS) {
      LOG_ERROR("GATT client write error: %d", status);
    }

    break;
  }

  case BLE_GATTC_EVT_HVX:                   //
  case BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP:    //
  case BLE_GATTC_EVT_CHAR_DISC_RSP:         //
  case BLE_GATTC_EVT_EXCHANGE_MTU_RSP:      //
  case BLE_GATTC_EVT_DESC_DISC_RSP:         //
  case BLE_GATTC_EVT_WRITE_CMD_TX_COMPLETE: //
  {
    // Events that we don't care about.
    break;
  }

  default: //
  {
    LOG_INFO("Received unhandled BLE event: %d", ble_evt->header.evt_id);
    break;
  }
  }
}

static void db_discovery_evt_handler(ble_db_discovery_evt_t *evt) {
  LOG_INFO("Received DB discovery event: %d", evt->evt_type);

  proxy_client_db_discovery_evt_handler(&proxy_client, evt);
}

static void gatt_evt_handler(nrf_ble_gatt_t *gatt,
                             nrf_ble_gatt_evt_t const *evt) {
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

static void proxy_client_evt_handler(proxy_client_evt_t *evt) {
  switch (evt->type) {
  case PROXY_CLIENT_EVT_DATA_IN: //
  {
    break;
  }

  default: //
  {
    break;
  }
  }
}

static void prov_complete_cb(void) {
  dsm_local_unicast_address_t node_address;
  LOG_INFO("Successfully provisioned. ");
  dsm_local_unicast_addresses_get(&node_address);
  LOG_INFO("Node Address: 0x%04x. ", node_address.address_start);

  mesh_stack_disable_radio();
  APP_ERROR_CHECK(sd_ble_gap_scan_start(&scan_params, &gap_scan_buffer));
}

static void config_server_evt_cb(config_server_evt_t const *evt) {

  switch (evt->type) {
  case CONFIG_SERVER_EVT_APPKEY_ADD: //
  {
    LOG_INFO("App key has been added. ");

    // APP_ERROR_CHECK(access_model_application_bind(
    //     health_client.model_handle, evt->params.appkey_add.appkey_handle));
    // dsm_handle_t sub_addr_handle;
    // APP_ERROR_CHECK(dsm_address_subscription_add(0xCAFE, &sub_addr_handle));
    // APP_ERROR_CHECK(access_model_subscription_add(health_client.model_handle,
    //                                               sub_addr_handle));

    break;
  }

  case CONFIG_SERVER_EVT_MODEL_PUBLICATION_SET: //
  {
    LOG_INFO("Model publication is set. ");

    break;
  }

  default: //
  {
    LOG_INFO("Received unhandled config server evt: %d", evt->type);
    break;
  }
  }
}

static void health_client_evt_handler(const health_client_t *client,
                                      const health_client_evt_t *event) {
  LOG_INFO("Event from src %d", event->p_meta_data->src.value);
}

////////////////////////////////////////////////////////////////////////////////
// Initialization routines
////////////////////////////////////////////////////////////////////////////////

static void init_db_discovery() {
  APP_ERROR_CHECK(ble_db_discovery_init(db_discovery_evt_handler));
}

static void init_softdevice() {
  APP_ERROR_CHECK(nrf_sdh_enable_request());

  uint32_t ram_start = 0;
  APP_ERROR_CHECK(
      nrf_sdh_ble_default_cfg_set(MESH_SOFTDEVICE_CONN_CFG_TAG, &ram_start));
  APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram_start));

  NRF_SDH_BLE_OBSERVER(ble_observer, 3, ble_evt_handler, NULL);
}

static void init_gap_params(void) {
  ble_gap_conn_sec_mode_t sec_mode;

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

  APP_ERROR_CHECK(sd_ble_gap_device_name_set(
      &sec_mode, (const uint8_t *)APP_DEVICE_NAME, strlen(APP_DEVICE_NAME)));
}

static void init_gatt() {
  APP_ERROR_CHECK(nrf_ble_gatt_init(&gatt, gatt_evt_handler));
  APP_ERROR_CHECK(nrf_ble_gatt_att_mtu_central_set(&gatt, 69));
}

static void init_proxy_client() {
  APP_ERROR_CHECK(proxy_client_init(&proxy_client, proxy_client_evt_handler));
}

static void init_models(void) {
  health_client_init(&health_client, 0, health_client_evt_handler);
  // (void) &health_client_evt_handler;
}

APP_TIMER_DEF(app_timer_gatt_scan_id);

static void app_timer_gatt_scan_cb(void *context) {
  LOG_INFO("Bluetooth Mesh sensor node is scanning for relay nodes. ");
  APP_ERROR_CHECK(sd_ble_gap_scan_start(&scan_params, &gap_scan_buffer));
}

static uint32_t app_timer_ms_to_ticks(uint32_t ms) {
  return (2048 * ms) / 1000;
}

static void init_timers() {
  APP_ERROR_CHECK(app_timer_init());

  app_timer_create(&app_timer_gatt_scan_id, APP_TIMER_MODE_SINGLE_SHOT,
                   app_timer_gatt_scan_cb);
}

static void init_mesh() {
  nrf_clock_lf_cfg_t lfc_cfg = {.source = NRF_CLOCK_LF_SRC_XTAL,
                                .rc_ctiv = 0,
                                .rc_temp_ctiv = 0,
                                .accuracy = NRF_CLOCK_LF_ACCURACY_20_PPM};
  mesh_stack_init_params_t init_params = {
      .core.irq_priority = NRF_MESH_IRQ_PRIORITY_LOWEST,
      .core.lfclksrc = lfc_cfg,
      .models.config_server_cb = config_server_evt_cb,
      .models.models_init_cb = init_models};
  APP_ERROR_CHECK(mesh_stack_init(&init_params, NULL));

  LOG_INFO("Mesh stack initialized");
}

static void initialize(void) {
  __LOG_INIT(LOG_SRC_APP | LOG_SRC_ACCESS | LOG_SRC_BEARER, LOG_LEVEL_DBG1,
             log_callback_custom);
  LOG_INFO("Bluetooth Mesh sensor node is initializing. ");

  nrf_gpio_cfg_input(APP_PIN_FORCE_RESET, NRF_GPIO_PIN_PULLUP);

  nrf_gpio_cfg_output(APP_PIN_LED_ERROR);
  nrf_gpio_cfg_output(APP_PIN_LED_INDICATION);

  init_db_discovery();
  init_softdevice();
  init_gap_params();
  init_gatt();
  init_proxy_client();
  init_timers();

  init_mesh();
}

static void start() {
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
    if (!nrf_gpio_pin_read(APP_PIN_FORCE_RESET)) {
      LOG_INFO("Already provisioned. Resetting... ");
      mesh_stack_config_clear();
      nrf_delay_ms(500);
      mesh_stack_device_reset();
      while (1) {
      }
    } else {
      LOG_INFO("Node is already provisioned. Disabling the radio. ");
      mesh_stack_disable_radio();
    }
  }
}

int main(void) {
  initialize();
  execution_start(start);

  if (mesh_stack_is_device_provisioned()) {
    // Delay a bit before starting to scan for GATT proxy in order to give the
    // mesh stack a bit of time to finish initializing tasks (e.g. flash op for
    // net state seqnum block allocation).
    app_timer_start(app_timer_gatt_scan_id, app_timer_ms_to_ticks(100), NULL);
  }

  for (;;) {
    (void)sd_app_evt_wait();
    bearer_handler_restart_timeslot_if_needed();
  }
}
