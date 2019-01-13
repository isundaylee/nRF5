#include "access_config.h"
#include "app_timer.h"
#include "ble_softdevice_support.h"
#include "log.h"
#include "mesh_adv.h"
#include "mesh_app_utils.h"
#include "mesh_provisionee.h"
#include "mesh_stack.h"
#include "net_state.h"
#include "nrf_gpio.h"
#include "nrf_mesh_config_examples.h"
#include "nrf_mesh_configure.h"

#include "mesh_friendship_types.h"
#include "mesh_lpn.h"

#include "generic_onoff_server.h"

#include "nrf_delay.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "sdk_config.h"

#include "custom_log.h"

#define APP_DEVICE_NAME "PROXYTEST"

#define APP_PIN_LED_ERROR 23
#define APP_PIN_LED_INDICATION 24
#define APP_PIN_CLEAR_CONFIG 20

APP_TIMER_DEF(reset_timer);
APP_TIMER_DEF(initiate_friendship_timer);

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
  nrf_gpio_cfg_output(APP_PIN_LED_ERROR);
  nrf_gpio_pin_set(APP_PIN_LED_ERROR);

  error_info_t *error_info = (error_info_t *)info;
  assert_info_t *assert_info = (assert_info_t *)info;

  switch (id) {
  case NRF_FAULT_ID_SDK_ERROR:
    LOG_ERROR("Encountered error %d on line %d in file %s",
              error_info->err_code, error_info->line_num,
              error_info->p_file_name);
    break;
  case NRF_FAULT_ID_SDK_ASSERT:
    LOG_ERROR("Encountered assertion error on line %d in file %s on pc 0x%x",
              assert_info->line_num, assert_info->p_file_name, pc);
    break;
  }

  NRF_BREAKPOINT_COND;
  while (1) {
  }
}

void schedule_friend_request(uint32_t delay_ms) {
  APP_ERROR_CHECK(app_timer_start(initiate_friendship_timer,
                                  APP_TIMER_TICKS(delay_ms), NULL));
}

bool friendship_established = false;

void set_friendship_status(bool established) {
  ASSERT(established != friendship_established);

  friendship_established = established;
  nrf_gpio_pin_write(APP_PIN_LED_ERROR, !established);

  if (established) {
    health_server_fault_clear(mesh_stack_health_server_get(),
                              APP_FAULT_ID_FRIENDLESS);
  } else {
    health_server_fault_register(mesh_stack_health_server_get(),
                                 APP_FAULT_ID_FRIENDLESS);
  }
}

void mesh_assertion_handler(uint32_t pc) {
  assert_info_t assert_info = {.line_num = 0, .p_file_name = (uint8_t *)""};

  app_error_fault_handler(NRF_FAULT_ID_SDK_ASSERT, pc,
                          (uint32_t)(&assert_info));

  UNUSED_VARIABLE(assert_info);
}

static void mesh_core_event_handler(nrf_mesh_evt_t const *event) {
  switch (event->type) {
  case NRF_MESH_EVT_LPN_FRIEND_OFFER: {
    const nrf_mesh_evt_lpn_friend_offer_t *offer = &event->params.friend_offer;

    LOG_INFO("Received friend offer from 0x%04X.", offer->src);
    LOG_INFO("Receive window: %d ms, RSSI: %d", offer->offer.receive_window_ms,
             offer->offer.measured_rssi);

    APP_ERROR_CHECK(mesh_lpn_friend_accept(offer));
    LOG_INFO("Friendship offer successfully accepted.");

    break;
  }

  case NRF_MESH_EVT_LPN_FRIEND_REQUEST_TIMEOUT: {
    // TODO: Prove that we'll be in S_IDLE
    ASSERT(!friendship_established);
    schedule_friend_request(1000);
    LOG_ERROR("Friend request timed out.");
    break;
  }

  case NRF_MESH_EVT_FRIENDSHIP_ESTABLISHED: {
    set_friendship_status(true);
    LOG_INFO("Friendship established with 0x%04X.",
             event->params.friendship_established.friend_src);
    break;
  }

  case NRF_MESH_EVT_FRIENDSHIP_TERMINATED: {
    // TODO: Prove that we'll be in S_IDLE
    set_friendship_status(false);
    schedule_friend_request(1000);
    LOG_ERROR("Friendship terminated with 0x%04X. Reason: %d.",
              event->params.friendship_terminated.friend_src,
              event->params.friendship_terminated.reason);
    break;
  }

  case NRF_MESH_EVT_LPN_FRIEND_UPDATE: {
    LOG_INFO("Received a Friend Update.");
    break;
  }

  case NRF_MESH_EVT_LPN_FRIEND_POLL_COMPLETE: {
    LOG_INFO("Friend poll completed.");
    break;
  }

  case NRF_MESH_EVT_MESSAGE_RECEIVED:
  case NRF_MESH_EVT_TX_COMPLETE:
  case NRF_MESH_EVT_FLASH_STABLE:
  case NRF_MESH_EVT_NET_BEACON_RECEIVED:
    break;

  default: {
    LOG_INFO("Unexpected event: %d", event->type);
    break;
  }
  }
}

static bool is_on = false;
static generic_onoff_server_t onoff_server;

static void generic_onoff_state_get_cb(const generic_onoff_server_t *p_self,
                                       const access_message_rx_meta_t *p_meta,
                                       generic_onoff_status_params_t *p_out) {
  ASSERT(p_out != NULL);

  p_out->present_on_off = is_on;
  p_out->target_on_off = is_on;
  p_out->remaining_time_ms = 0;
}

static void
generic_onoff_state_set_cb(const generic_onoff_server_t *p_self,
                           const access_message_rx_meta_t *p_meta,
                           const generic_onoff_set_params_t *p_in,
                           const model_transition_t *p_in_transition,
                           generic_onoff_status_params_t *p_out) {
  LOG_INFO("Received LED set request: %s", (p_in->on_off ? "ON" : "OFF"));

  is_on = p_in->on_off;

  nrf_gpio_pin_write(APP_PIN_LED_INDICATION, is_on);

  if (p_out != NULL) {
    p_out->present_on_off = is_on;
    p_out->target_on_off = is_on;
    p_out->remaining_time_ms = 0;
  }
}

static void init_models(void) {
  static generic_onoff_server_callbacks_t cbs = {
      .onoff_cbs.set_cb = generic_onoff_state_set_cb,
      .onoff_cbs.get_cb = generic_onoff_state_get_cb,
  };

  onoff_server.settings.force_segmented = false;
  onoff_server.settings.transmic_size = NRF_MESH_TRANSMIC_SIZE_SMALL;
  onoff_server.settings.p_callbacks = &cbs;

  APP_ERROR_CHECK(generic_onoff_server_init(&onoff_server, 0));

  LOG_INFO("OnOff server initialized.");
}

static void start_friendship() {
  LOG_INFO("Starting making friends....");

  mesh_lpn_friend_request_t req;

  req.friend_criteria.friend_queue_size_min_log =
      MESH_FRIENDSHIP_MIN_FRIEND_QUEUE_SIZE_16;
  req.friend_criteria.receive_window_factor =
      MESH_FRIENDSHIP_RECEIVE_WINDOW_FACTOR_1_0;
  req.friend_criteria.rssi_factor = MESH_FRIENDSHIP_RSSI_FACTOR_2_0;
  req.poll_timeout_ms = SEC_TO_MS(10);
  req.receive_delay_ms = 100;

  APP_ERROR_CHECK(
      mesh_lpn_friend_request(req, MESH_LPN_FRIEND_REQUEST_TIMEOUT_MAX_MS));
  LOG_INFO("Friend request sent.");
}

static void reset_timer_handler(void *context) {
  LOG_INFO("Clearing config and resetting...");

  mesh_stack_config_clear();
  // This function may return if there are ongoing flash operations.
  mesh_stack_device_reset();

  while (true) {
  }
}

void selftest_check_friend_status(health_server_t *server, uint16_t company_id,
                                  uint8_t test_id) {
  LOG_INFO("RUNNING SELF TESTS!!!");
  health_server_fault_register(server, 1);
}

static void initialize(void) {
  __LOG_INIT(LOG_SRC_APP | LOG_SRC_ACCESS | LOG_SRC_BEARER, LOG_LEVEL_DBG1,
             log_callback_custom);
  LOG_INFO("Mesh relay node is initializing. ");

  // Initialize the softdevice
  ble_stack_init();
  LOG_INFO("Mesh soft device initialized.");

  // Initialize the Mesh stack
  nrf_clock_lf_cfg_t lfc_cfg = {.source = NRF_CLOCK_LF_SRC_XTAL,
                                .rc_ctiv = 0,
                                .rc_temp_ctiv = 0,
                                .accuracy = NRF_CLOCK_LF_ACCURACY_20_PPM};
  mesh_stack_init_params_t init_params = {
      .core.irq_priority = NRF_MESH_IRQ_PRIORITY_LOWEST,
      .core.lfclksrc = lfc_cfg,
      .models.models_init_cb = init_models,
  };
  APP_ERROR_CHECK(mesh_stack_init(&init_params, NULL));
  LOG_INFO("Mesh stack initialized.");

  // Add event handler
  static nrf_mesh_evt_handler_t event_handler = {.evt_cb =
                                                     mesh_core_event_handler};
  nrf_mesh_evt_handler_add(&event_handler);

  // Initialize timers
  APP_ERROR_CHECK(app_timer_create(&reset_timer, APP_TIMER_MODE_SINGLE_SHOT,
                                   reset_timer_handler));
  APP_ERROR_CHECK(app_timer_create(&initiate_friendship_timer,
                                   APP_TIMER_MODE_SINGLE_SHOT,
                                   start_friendship));

  // Initialize LPN
  mesh_lpn_init();
}

static void prov_complete_cb(void) {
  dsm_local_unicast_address_t node_address;
  LOG_INFO("Successfully provisioned. ");
  dsm_local_unicast_addresses_get(&node_address);
  LOG_INFO("Node Address: 0x%04x. ", node_address.address_start);

  start_friendship();
}

static void start() {
  nrf_gpio_cfg_output(APP_PIN_LED_ERROR);
  nrf_gpio_cfg_output(APP_PIN_LED_INDICATION);
  nrf_gpio_cfg_input(APP_PIN_CLEAR_CONFIG, NRF_GPIO_PIN_PULLUP);

  set_friendship_status(false);

  if (!mesh_stack_is_device_provisioned()) {
    LOG_INFO("Starting the provisioning process. ");

    static const uint8_t static_auth_data[] = {
        0x6E, 0x6F, 0x72, 0x64, 0x69, 0x63, 0x5F, 0x65,
        0x78, 0x61, 0x6D, 0x70, 0x6C, 0x65, 0x5F, 0x31};
    mesh_provisionee_start_params_t prov_start_params = {
        .p_static_data = static_auth_data,
        .prov_complete_cb = prov_complete_cb};
    APP_ERROR_CHECK(mesh_provisionee_prov_start(&prov_start_params));
  } else {
    LOG_INFO("Node is already provisioned. ");

    if (!nrf_gpio_pin_read(APP_PIN_CLEAR_CONFIG)) {
      APP_ERROR_CHECK(app_timer_start(reset_timer, APP_TIMER_TICKS(100), NULL));
    } else {
      APP_ERROR_CHECK(app_timer_start(initiate_friendship_timer,
                                      APP_TIMER_TICKS(100), NULL));
    }
  }

  APP_ERROR_CHECK(mesh_stack_start());
  LOG_INFO("Mesh stack started.");
}

int main(void) {
  initialize();
  start();

  for (;;) {
    (void)sd_app_evt_wait();
  }
}
