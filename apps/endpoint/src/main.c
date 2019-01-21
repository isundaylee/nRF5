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

#include "generic_onoff_client.h"

#include "battery_level_server.h"

#include "nrf_delay.h"
#include "nrf_drv_saadc.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"

#include "sdk_config.h"

#include "custom_log.h"

#include "app_button.h"

#define APP_DEVICE_NAME "PROXYTEST"

#define APP_PIN_LED_ERROR 23
#define APP_PIN_LED_INDICATION 24
#define APP_PIN_CLEAR_CONFIG 20

#define APP_PIN_USER_BUTTON 20

#define APP_LED_BLINK_PERIOD_MS 3000
#define APP_LED_BLINK_ON_MS 5
#define APP_LED_BLINK_OFF_MS (APP_LED_BLINK_PERIOD_MS - APP_LED_BLINK_ON_MS)

#define APP_ATTENTION_LED_BLINK_PERIOD_MS 1000
#define APP_ATTENTION_LED_BLINK_ON_MS 500
#define APP_ATTENTION_LED_BLINK_OFF_MS                                         \
  (APP_ATTENTION_LED_BLINK_PERIOD_MS - APP_ATTENTION_LED_BLINK_ON_MS)

APP_TIMER_DEF(reset_timer);
APP_TIMER_DEF(led_blink_timer);
APP_TIMER_DEF(initiate_friendship_timer);
APP_TIMER_DEF(dummy_timer);
APP_TIMER_DEF(attention_led_blink_timer);

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

bool led_blink_value;

void set_friendship_status(bool established) {
  ASSERT(established != friendship_established);

  friendship_established = established;

  if (established) {
    health_server_fault_clear(mesh_stack_health_server_get(),
                              APP_FAULT_ID_FRIENDLESS);
    nrf_gpio_pin_clear(APP_PIN_LED_ERROR);
    APP_ERROR_CHECK(app_timer_stop(led_blink_timer));
  } else {
    health_server_fault_register(mesh_stack_health_server_get(),
                                 APP_FAULT_ID_FRIENDLESS);
    led_blink_value = false;
    APP_ERROR_CHECK(app_timer_start(led_blink_timer, APP_TIMER_TICKS(1), NULL));
  }

  health_server_send_fault_status(mesh_stack_health_server_get());
}

void led_blink_timer_handler(void *context) {
  if (friendship_established) {
    // TODO: Figure out whether we can error out here.
    return;
  }

  uint32_t next_timeout;

  if (led_blink_value) {
    nrf_gpio_pin_clear(APP_PIN_LED_ERROR);
    next_timeout = APP_LED_BLINK_OFF_MS;
  } else {
    nrf_gpio_pin_set(APP_PIN_LED_ERROR);
    next_timeout = APP_LED_BLINK_ON_MS;
  }

  led_blink_value = !led_blink_value;

  APP_ERROR_CHECK(
      app_timer_start(led_blink_timer, APP_TIMER_TICKS(next_timeout), NULL));
}

void attention_led_blink_timer_handler(void *context) {
  static bool value = true;

  uint32_t next_timeout;

  if (value) {
    nrf_gpio_pin_clear(APP_PIN_LED_INDICATION);
    next_timeout = APP_ATTENTION_LED_BLINK_OFF_MS;
  } else {
    nrf_gpio_pin_set(APP_PIN_LED_INDICATION);
    next_timeout = APP_ATTENTION_LED_BLINK_ON_MS;
  }

  value = !value;

  APP_ERROR_CHECK(app_timer_start(attention_led_blink_timer,
                                  APP_TIMER_TICKS(next_timeout), NULL));
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
static generic_onoff_client_t onoff_client;

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

static void
generic_onoff_client_publish_interval_cb(access_model_handle_t handle,
                                         void *p_self) {
  // TODO:
}
static void
generic_onoff_client_status_cb(const generic_onoff_client_t *p_self,
                               const access_message_rx_meta_t *p_meta,
                               const generic_onoff_status_params_t *p_in) {
  LOG_INFO("LED on node 0x%04x is: %s.", p_meta->src.value,
           (p_in->present_on_off ? "ON" : "OFF"));
}

static void
generic_onoff_client_transaction_status_cb(access_model_handle_t model_handle,
                                           void *p_args,
                                           access_reliable_status_t status) {
  // TODO:
}

battery_level_server_t bl_server;

void battery_level_server_get_handler(battery_level_server_t const *server,
                                      access_message_rx_meta_t const *meta,
                                      battery_level_status_params_t *out) {
  nrf_saadc_value_t vdd_value;
  APP_ERROR_CHECK(nrfx_saadc_sample_convert(0, &vdd_value));

  LOG_INFO("VDD measurement: %d.", vdd_value);

  out->level = vdd_value;
}

static void init_models(void) {
  static generic_onoff_server_callbacks_t onoff_server_cbs = {
      .onoff_cbs.set_cb = generic_onoff_state_set_cb,
      .onoff_cbs.get_cb = generic_onoff_state_get_cb,
  };

  onoff_server.settings.force_segmented = false;
  onoff_server.settings.transmic_size = NRF_MESH_TRANSMIC_SIZE_SMALL;
  onoff_server.settings.p_callbacks = &onoff_server_cbs;

  APP_ERROR_CHECK(generic_onoff_server_init(&onoff_server, 0));

  LOG_INFO("OnOff server initialized.");

  static const generic_onoff_client_callbacks_t onoff_client_cbs = {
      .onoff_status_cb = generic_onoff_client_status_cb,
      .ack_transaction_status_cb = generic_onoff_client_transaction_status_cb,
      .periodic_publish_cb = generic_onoff_client_publish_interval_cb};

  onoff_client.settings.p_callbacks = &onoff_client_cbs;
  onoff_client.settings.timeout = 0;
  onoff_client.settings.force_segmented = false;
  onoff_client.settings.transmic_size = NRF_MESH_TRANSMIC_SIZE_SMALL;

  APP_ERROR_CHECK(generic_onoff_client_init(&onoff_client, 0));
  LOG_INFO("OnOff client initialized.");

  bl_server.settings.force_segmented = false;
  bl_server.settings.transmic_size = NRF_MESH_TRANSMIC_SIZE_SMALL;
  bl_server.settings.get_cb = battery_level_server_get_handler;

  APP_ERROR_CHECK(battery_level_server_init(&bl_server, 0));
  LOG_INFO("Battery Level server initialized.");
}

static void start_friendship() {
  static bool is_first = true;

  if (is_first) {
    set_friendship_status(false);
    is_first = false;
  }

  // Calculating interval
  uint32_t period_ms = 0;
  access_publish_resolution_t res;
  uint8_t steps;

  APP_ERROR_CHECK(access_model_publish_period_get(
      mesh_stack_health_server_get()->model_handle, &res, &steps));

  switch (res) {
  case ACCESS_PUBLISH_RESOLUTION_100MS:
    period_ms = 100 * steps;
    break;
  case ACCESS_PUBLISH_RESOLUTION_1S:
    period_ms = 1000 * steps;
    break;
  case ACCESS_PUBLISH_RESOLUTION_10S:
    period_ms = 10 * 1000 * steps;
    break;
  case ACCESS_PUBLISH_RESOLUTION_10MIN:
    period_ms = 10 * 60 * 1000 * steps;
    break;
  }

  LOG_INFO("Starting making friends....");

  mesh_lpn_friend_request_t req;
  uint32_t poll_timeout_ms = MAX(2 * period_ms, 10 * 1000);

  req.friend_criteria.friend_queue_size_min_log =
      MESH_FRIENDSHIP_MIN_FRIEND_QUEUE_SIZE_16;
  req.friend_criteria.receive_window_factor =
      MESH_FRIENDSHIP_RECEIVE_WINDOW_FACTOR_1_0;
  req.friend_criteria.rssi_factor = MESH_FRIENDSHIP_RSSI_FACTOR_2_0;
  req.poll_timeout_ms = poll_timeout_ms;
  req.receive_delay_ms = 100;

  APP_ERROR_CHECK(
      mesh_lpn_friend_request(req, MESH_LPN_FRIEND_REQUEST_TIMEOUT_MAX_MS));
  LOG_INFO("Friend request sent. Poll timeout: %d ms.", poll_timeout_ms);
}

static void reset_timer_handler(void *context) {
  LOG_INFO("Clearing config and resetting...");

  mesh_stack_config_clear();
  // This function may return if there are ongoing flash operations.
  mesh_stack_device_reset();
}

void selftest_check_friend_status(health_server_t *server, uint16_t company_id,
                                  uint8_t test_id) {
  LOG_INFO("RUNNING SELF TESTS!!!");
  health_server_fault_register(server, 1);
}

bool onoff_value = false;

void send_onoff_request(bool value) {
  static uint8_t tid = 0;
  generic_onoff_set_params_t params;

  params.tid = tid++;
  params.on_off = (value ? 1 : 0);

  onoff_value = value;

  APP_ERROR_CHECK(
      generic_onoff_client_set_unack(&onoff_client, &params, NULL, 2));
}

void button_handler(uint8_t pin_no, uint8_t button_action) {
  if (button_action == 0) {
    return;
  }

  switch (pin_no) {
  case APP_PIN_USER_BUTTON: //
  {
    LOG_INFO("User button pressed.");
    send_onoff_request(!onoff_value);
    break;
  }

  default: //
  {
    LOG_ERROR("Unknown button pressed.");
    break;
  }
  }
}

static void config_server_event_handler(config_server_evt_t const *event) {
  LOG_INFO("Received config server event of type %d.", event->type);

  switch (event->type) {
  case CONFIG_SERVER_EVT_MODEL_PUBLICATION_SET: //
  {
    config_server_evt_model_publication_set_t const *params =
        &event->params.model_publication_set;

    if (params->model_handle != mesh_stack_health_server_get()->model_handle) {
      break;
    }

    LOG_INFO("Health server publication changed. ");

    if (friendship_established) {
      APP_ERROR_CHECK(mesh_lpn_friendship_terminate());
      LOG_INFO("Terminated our precious friendship...");
    }

    break;
  }

  default:
    break;
  }
}

void health_server_publish_timeout_handler(health_server_t *p_server) {
  if (friendship_established) {
    APP_ERROR_CHECK(mesh_lpn_friend_poll(0));
  }
}

void saadc_event_handler(nrf_drv_saadc_evt_t const *event) {
  LOG_INFO("Received SAADC event of type %d.", event->type);
}

static void dummy_timer_handler(void *context) {
  // No need to do anything here.
  // Used only to make sure app_timer tracks m_latest_ticks correctly.
}

void health_server_attention_handler(health_server_t const *server,
                                     bool attention_state) {
  APP_ERROR_CHECK(app_timer_stop(attention_led_blink_timer));
  nrf_gpio_pin_clear(APP_PIN_LED_INDICATION);

  if (attention_state) {
    APP_ERROR_CHECK(
        app_timer_start(attention_led_blink_timer, APP_TIMER_TICKS(1), NULL));
  }
}

bool should_reset = false;

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
      .models.config_server_cb = config_server_event_handler,
      .models.models_init_cb = init_models,
      .models.health_server_attention_cb = health_server_attention_handler,
      .models.health_server_publish_timeout_cb =
          health_server_publish_timeout_handler,
  };
  APP_ERROR_CHECK(mesh_stack_init(&init_params, NULL));
  LOG_INFO("Mesh stack initialized.");

  // Add event handler
  static nrf_mesh_evt_handler_t event_handler = {.evt_cb =
                                                     mesh_core_event_handler};
  nrf_mesh_evt_handler_add(&event_handler);

  // Check reset status
  nrf_gpio_cfg_input(APP_PIN_CLEAR_CONFIG, NRF_GPIO_PIN_PULLUP);
  __asm__ volatile("nop");
  __asm__ volatile("nop");
  __asm__ volatile("nop");
  __asm__ volatile("nop");
  __asm__ volatile("nop");
  __asm__ volatile("nop");
  should_reset = (!nrf_gpio_pin_read(APP_PIN_CLEAR_CONFIG));

  // Initialize buttons
  static app_button_cfg_t buttons[] = {{
      .pin_no = APP_PIN_USER_BUTTON,
      .active_state = APP_BUTTON_ACTIVE_LOW,
      .pull_cfg = NRF_GPIO_PIN_PULLUP,
      .button_handler = button_handler,
  }};
  APP_ERROR_CHECK(app_button_init(buttons, sizeof(buttons) / sizeof(buttons[0]),
                                  APP_TIMER_TICKS(50)));
  APP_ERROR_CHECK(app_button_enable());

  // Initialize timers
  APP_ERROR_CHECK(app_timer_create(&reset_timer, APP_TIMER_MODE_SINGLE_SHOT,
                                   reset_timer_handler));
  APP_ERROR_CHECK(app_timer_create(&initiate_friendship_timer,
                                   APP_TIMER_MODE_SINGLE_SHOT,
                                   start_friendship));
  APP_ERROR_CHECK(app_timer_create(&led_blink_timer, APP_TIMER_MODE_SINGLE_SHOT,
                                   led_blink_timer_handler));
  APP_ERROR_CHECK(app_timer_create(&attention_led_blink_timer,
                                   APP_TIMER_MODE_SINGLE_SHOT,
                                   attention_led_blink_timer_handler));
  APP_ERROR_CHECK(app_timer_create(&dummy_timer, APP_TIMER_MODE_REPEATED,
                                   dummy_timer_handler));

  // Initialize SAADC
  APP_ERROR_CHECK(nrf_drv_saadc_init(NULL, saadc_event_handler));

  nrf_saadc_channel_config_t saadc_channel_config =
      NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_VDD);
  APP_ERROR_CHECK(nrf_drv_saadc_channel_init(0, &saadc_channel_config));

  // Initialize LPN
  mesh_lpn_init();
}

static void prov_complete_cb(void) {
  dsm_local_unicast_address_t node_address;
  LOG_INFO("Successfully provisioned. ");
  dsm_local_unicast_addresses_get(&node_address);
  LOG_INFO("Node Address: 0x%04x. ", node_address.address_start);

  schedule_friend_request(3000);
}

static void start() {
  nrf_gpio_cfg_output(APP_PIN_LED_ERROR);
  nrf_gpio_cfg_output(APP_PIN_LED_INDICATION);

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

    if (should_reset) {
      APP_ERROR_CHECK(app_timer_start(reset_timer, APP_TIMER_TICKS(100), NULL));
    } else {
      APP_ERROR_CHECK(app_timer_start(initiate_friendship_timer,
                                      APP_TIMER_TICKS(100), NULL));
    }
  }

  APP_ERROR_CHECK(app_timer_start(dummy_timer, APP_TIMER_TICKS(200000), NULL));

  APP_ERROR_CHECK(mesh_stack_start());
  LOG_INFO("Mesh stack started.");
}

bool is_in_debug_mode() { return ((CoreDebug->DHCSR & 0x0001) != 0); }

int main(void) {
  initialize();
  start();

  for (;;) {
    (void)sd_app_evt_wait();
  }
}
