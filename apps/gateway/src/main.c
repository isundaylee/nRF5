#include <stdio.h>

#include "app_error.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"

#include "access_config.h"

#include "ble_softdevice_support.h"
#include "mesh_app_utils.h"
#include "mesh_stack.h"

#include "nrf_mesh_events.h"

#include "net_state.h"

#include "configurator.h"
#include "provisioner.h"

#include "custom_log.h"
#include "rtt_input.h"

#include "app_timer.h"

#include "generic_onoff_client.h"
#include "health_client.h"

#include "debug_pins.h"

#define PIN_LED_ERROR 27
#define PIN_LED_INDICATION 28

APP_TIMER_DEF(onoff_client_toggle_timer);

health_client_t health_client;

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

void mesh_assertion_handler(uint32_t pc) {
  assert_info_t assert_info = {.line_num = 0, .p_file_name = (uint8_t *)""};

  app_error_fault_handler(NRF_FAULT_ID_SDK_ASSERT, pc,
                          (uint32_t)(&assert_info));

  UNUSED_VARIABLE(assert_info);
}

static void init_leds() {
  nrf_gpio_cfg_output(PIN_LED_ERROR);
  nrf_gpio_cfg_output(PIN_LED_INDICATION);
}

static void init_logging() {
  __LOG_INIT(LOG_SRC_APP | LOG_SRC_BEARER | LOG_SRC_ACCESS, LOG_LEVEL_DBG1,
             log_callback_custom);
  LOG_INFO("Hello, world!");
}

static generic_onoff_client_t onoff_client;

void send_onoff_request(bool value) {
  static uint8_t tid = 0;
  generic_onoff_set_params_t params;

  params.tid = tid++;
  params.on_off = (value ? 1 : 0);

  APP_ERROR_CHECK(
      generic_onoff_client_set_unack(&onoff_client, &params, NULL, 2));
}

static void rtt_input_handler(int key) {
  switch (key) {
  case '0':
    LOG_INFO("Turning it OFF!");
    send_onoff_request(false);
    break;

  case '1':
    LOG_INFO("Turning it ON!");
    send_onoff_request(true);
    break;
  }
}

static void config_server_evt_cb(config_server_evt_t const *evt) {}

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

static void health_client_event_handler(health_client_t const *client,
                                        health_client_evt_t const *event) {
  switch (event->type) {
  case HEALTH_CLIENT_EVT_TYPE_CURRENT_STATUS_RECEIVED: //
  {
    LOG_INFO("Health client: current status. %d fault(s).",
             event->data.fault_status.fault_array_length);
    break;
  }

  case HEALTH_CLIENT_EVT_TYPE_FAULT_STATUS_RECEIVED: //
  {
    LOG_INFO("Health client: fault status");
    break;
  }

  case HEALTH_CLIENT_EVT_TYPE_PERIOD_STATUS_RECEIVED: //
  {
    LOG_INFO("Health client: period status");
    break;
  }

  case HEALTH_CLIENT_EVT_TYPE_ATTENTION_STATUS_RECEIVED: //
  {
    LOG_INFO("Health client: attention status");
    break;
  }

  default: //
  {
    LOG_ERROR("Unexpected health client event type: %d", event->type);
    break;
  }
  }
}

static void init_models(void) {
  APP_ERROR_CHECK(
      health_client_init(&health_client, 0, health_client_event_handler));
  LOG_INFO("Health client initialized.");

  static const generic_onoff_client_callbacks_t cbs = {
      .onoff_status_cb = generic_onoff_client_status_cb,
      .ack_transaction_status_cb = generic_onoff_client_transaction_status_cb,
      .periodic_publish_cb = generic_onoff_client_publish_interval_cb};

  onoff_client.settings.p_callbacks = &cbs;
  onoff_client.settings.timeout = 0;
  onoff_client.settings.force_segmented = false;
  onoff_client.settings.transmic_size = NRF_MESH_TRANSMIC_SIZE_SMALL;

  APP_ERROR_CHECK(generic_onoff_client_init(&onoff_client, 0));
  LOG_INFO("OnOff client initialized.");
}

static void init_mesh() {
  // Initialize the softdevice
  ble_stack_init();
  LOG_INFO("Mesh soft device initialized.");

  // Initialize the Mesh stack
  nrf_clock_lf_cfg_t lfc_cfg = {.source = NRF_CLOCK_LF_SRC_XTAL,
                                .rc_ctiv = 0,
                                .rc_temp_ctiv = 0,
                                .accuracy = NRF_CLOCK_LF_ACCURACY_20_PPM};
  mesh_stack_init_params_t mesh_init_params = {
      .core.irq_priority = NRF_MESH_IRQ_PRIORITY_LOWEST,
      .core.lfclksrc = lfc_cfg,
      .models.config_server_cb = config_server_evt_cb,
      .models.models_init_cb = init_models};
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

static void prov_start_cb(uint16_t addr) {
  nrf_gpio_pin_set(PIN_LED_INDICATION);
}

static void prov_success_cb(uint16_t addr) {
  nrf_gpio_pin_clear(PIN_LED_INDICATION);

  static conf_step_t steps[] = {
      {
          .type = CONF_STEP_TYPE_COMPOSITION_GET,
          .params.composition_get.page_number = 0x00,
      },
      {
          .type = CONF_STEP_TYPE_APPKEY_ADD,
          .params.appkey_add.netkey_index = APP_NETKEY_IDX,
          .params.appkey_add.appkey_index = APP_APPKEY_IDX,
          .params.appkey_add.appkey = app_state.persistent.network.appkey,
      },
      {
          .type = CONF_STEP_TYPE_MODEL_APP_BIND,
          .params.model_app_bind.element_addr = 0x00,
          .params.model_app_bind.model_id.company_id = ACCESS_COMPANY_ID_NONE,
          .params.model_app_bind.model_id.model_id = HEALTH_SERVER_MODEL_ID,
          .params.model_app_bind.appkey_index = APP_APPKEY_IDX,
      },
      {
          .type = CONF_STEP_TYPE_MODEL_PUBLICATION_SET,
          .params.model_publication_set.element_addr = 0x00,
          .params.model_publication_set.model_id.company_id =
              ACCESS_COMPANY_ID_NONE,
          .params.model_publication_set.model_id.model_id =
              HEALTH_SERVER_MODEL_ID,
          .params.model_publication_set.publish_address.type =
              NRF_MESH_ADDRESS_TYPE_UNICAST,
          .params.model_publication_set.publish_address.value = 0x0001,
          .params.model_publication_set.appkey_index = APP_APPKEY_IDX,
          .params.model_publication_set.publish_ttl = 7,
          .params.model_publication_set.publish_period.step_num = 10,
          .params.model_publication_set.publish_period.step_res =
              ACCESS_PUBLISH_RESOLUTION_1S,
      },
      {
          .type = CONF_STEP_TYPE_MODEL_APP_BIND,
          .params.model_app_bind.element_addr = 0x00,
          .params.model_app_bind.model_id.company_id = ACCESS_COMPANY_ID_NONE,
          .params.model_app_bind.model_id.model_id =
              0x1000, // Generic OnOff Server
          .params.model_app_bind.appkey_index = APP_APPKEY_IDX,
      },
      {
          .type = CONF_STEP_TYPE_MODEL_PUBLICATION_SET,
          .params.model_publication_set.element_addr = 0x00,
          .params.model_publication_set.model_id.company_id =
              ACCESS_COMPANY_ID_NONE,
          .params.model_publication_set.model_id.model_id =
              0x1000, // Generic OnOff Server
          .params.model_publication_set.publish_address.type =
              NRF_MESH_ADDRESS_TYPE_UNICAST,
          .params.model_publication_set.publish_address.value = 0x0001,
          .params.model_publication_set.appkey_index = APP_APPKEY_IDX,
          .params.model_publication_set.publish_ttl = 7,
          .params.model_publication_set.publish_period.step_num = 0,
          .params.model_publication_set.publish_period.step_res =
              ACCESS_PUBLISH_RESOLUTION_1S,
      },
      {
          .type = CONF_STEP_TYPE_DONE,
      }};

  steps[2].params.model_app_bind.element_addr = addr;
  steps[3].params.model_publication_set.element_addr = addr;
  steps[4].params.model_app_bind.element_addr = addr;
  steps[5].params.model_publication_set.element_addr = addr;

  conf_start(addr, steps);
}

static void prov_failure_cb() {
  nrf_gpio_pin_clear(PIN_LED_INDICATION);

  prov_start_scan();
}

void self_config(uint16_t node_addr) {
  static conf_step_t steps[] = {
      {
          .type = CONF_STEP_TYPE_MODEL_PUBLICATION_SET,
          .params.model_publication_set.element_addr = APP_GATEWAY_ADDR,
          .params.model_publication_set.model_id.company_id =
              ACCESS_COMPANY_ID_NONE,
          .params.model_publication_set.model_id.model_id =
              GENERIC_ONOFF_CLIENT_MODEL_ID,
          .params.model_publication_set.publish_address.type =
              NRF_MESH_ADDRESS_TYPE_UNICAST,
          .params.model_publication_set.publish_address.value = 0x0000,
          .params.model_publication_set.appkey_index = APP_APPKEY_IDX,
          .params.model_publication_set.publish_ttl = 7,
          .params.model_publication_set.publish_period.step_num = 1,
          .params.model_publication_set.publish_period.step_res =
              ACCESS_PUBLISH_RESOLUTION_1S,
      },
      {
          .type = CONF_STEP_TYPE_MODEL_APP_BIND,
          .params.model_app_bind.element_addr = APP_GATEWAY_ADDR,
          .params.model_app_bind.model_id.company_id = ACCESS_COMPANY_ID_NONE,
          .params.model_app_bind.model_id.model_id = HEALTH_CLIENT_MODEL_ID,
          .params.model_app_bind.appkey_index = APP_APPKEY_IDX,
      },
      {
          .type = CONF_STEP_TYPE_DONE,
      }};

  steps[0].params.model_publication_set.publish_address.value = node_addr;

  conf_start(APP_GATEWAY_ADDR, steps);
}

static void conf_success_cb(uint16_t addr) {
  if (addr == APP_GATEWAY_ADDR) {
    // We finished self-config. On to the next!

    // APP_ERROR_CHECK(app_timer_start(onoff_client_toggle_timer,
    //                                 APP_TIMER_TICKS(1000), NULL));

    prov_start_scan();
  } else {
    // We should do self-config.
    self_config(addr);
  }
}

static void conf_failure_cb(uint16_t addr) { prov_start_scan(); }

static void onoff_client_toggle_timer_handler(void *context) {
  static bool value = false;

  send_onoff_request(value);
  value = !value;
}

static void start() {
  rtt_input_enable(rtt_input_handler, 100);

  APP_ERROR_CHECK(mesh_stack_start());
  LOG_INFO("Mesh stack started.");

  app_state_init();

  if (mesh_stack_is_device_provisioned()) {
    LOG_ERROR("We have already been provisioned. ");

    nrf_gpio_cfg_input(8, NRF_GPIO_PIN_PULLDOWN);
    bool should_reset = (nrf_gpio_pin_read(8) != 0);

    if (should_reset) {
      LOG_ERROR("Will clear all config and reset in 1s. ");

      mesh_stack_config_clear();
      nrf_delay_ms(1000);
      mesh_stack_device_reset();
      while (1) {
      }
    } else {
      LOG_ERROR("Will reuse the existing network config. ");
      app_state_load();
    }
  } else {
    app_state_clear();
  }

  prov_init(&app_state, prov_start_cb, prov_success_cb, prov_failure_cb);
  conf_init(&app_state, conf_success_cb, conf_failure_cb);

  APP_ERROR_CHECK(app_timer_create(&onoff_client_toggle_timer,
                                   APP_TIMER_MODE_REPEATED,
                                   onoff_client_toggle_timer_handler));

  prov_start_scan();
}

int main(void) {
  DEBUG_PINS_INIT();

  init_leds();
  init_logging();
  init_mesh();

  start();

  while (true) {
    (void)sd_app_evt_wait();
  }
}
