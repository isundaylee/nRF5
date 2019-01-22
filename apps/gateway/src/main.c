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

#include "protocol.h"

#include "composition_data.h"

#include "custom_log.h"
#include "rtt_input.h"

#include "app_timer.h"

#include "battery_level_client.h"
#include "battery_level_server.h"
#include "generic_onoff_client.h"
#include "health_client.h"

#include "app_button.h"

#include "app_uart.h"
#include "nrf_uart.h"

#include "debug_pins.h"

#define PIN_LED_ERROR 27
#define PIN_LED_INDICATION 28

#define APP_PIN_USER_BUTTON 7

#define APP_PIN_UART_TX 3
#define APP_PIN_UART_RX 2

APP_TIMER_DEF(onoff_client_toggle_timer);
APP_TIMER_DEF(self_config_timer);
APP_TIMER_DEF(pong_timer);

health_client_t health_client;

typedef struct {
  int addr;
} protocol_id_request_t;

typedef enum {
  PROTOCOL_ID_REQUEST = 0,

  PROTOCOL_NO_REQUEST = 100,
} protocol_request_type_t;

typedef struct {
  protocol_request_type_t type;
  union {
    protocol_id_request_t id_request;
  } request;
} protocol_request_t;

protocol_request_t pending_request = {.type = PROTOCOL_NO_REQUEST};

__attribute__((optimize(0))) void
app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
  nrf_gpio_cfg_output(PIN_LED_ERROR);
  nrf_gpio_pin_set(PIN_LED_ERROR);

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

void mesh_assertion_handler(uint32_t pc) {
  assert_info_t assert_info = {.line_num = 0, .p_file_name = (uint8_t *)""};

  app_error_fault_handler(NRF_FAULT_ID_SDK_ASSERT, pc,
                          (uint32_t)(&assert_info));

  UNUSED_VARIABLE(assert_info);
}

static generic_onoff_client_t onoff_client;

static bool onoff_value = false;

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

static void init_buttons() {
  // Initialize buttons
  static app_button_cfg_t buttons[] = {{
      .pin_no = APP_PIN_USER_BUTTON,
      .active_state = APP_BUTTON_ACTIVE_HIGH,
      .pull_cfg = NRF_GPIO_PIN_PULLDOWN,
      .button_handler = button_handler,
  }};

  APP_ERROR_CHECK(app_button_init(buttons, sizeof(buttons) / sizeof(buttons[0]),
                                  APP_TIMER_TICKS(50)));
  APP_ERROR_CHECK(app_button_enable());
}

static void pong_timer_handler(void *ctx) {
  protocol_reply(NRF_SUCCESS, "pong");
}

static void health_client_bind_step_builder(
    uint16_t addr, config_msg_composition_data_status_t const *composition_data,
    conf_step_t *steps_out) {
  conf_step_t *cursor = steps_out;

  if (pending_request.type == PROTOCOL_ID_REQUEST) {
    cursor->type = CONF_STEP_TYPE_MODEL_PUBLICATION_SET;
    cursor->params.model_publication_set.element_addr = addr;
    cursor->params.model_publication_set.model_id.company_id =
        ACCESS_COMPANY_ID_NONE;
    cursor->params.model_publication_set.model_id.model_id =
        HEALTH_CLIENT_MODEL_ID;
    cursor->params.model_publication_set.publish_address.type =
        NRF_MESH_ADDRESS_TYPE_UNICAST;
    cursor->params.model_publication_set.publish_address.value =
        pending_request.request.id_request.addr;
    cursor->params.model_publication_set.appkey_index = APP_APPKEY_IDX;
    cursor->params.model_publication_set.publish_ttl = 7;
    cursor->params.model_publication_set.publish_period.step_num = 0;
    cursor->params.model_publication_set.publish_period.step_res =
        ACCESS_PUBLISH_RESOLUTION_100MS;
    cursor++;
  } else {
    LOG_ERROR("Unexpected invocation of health_client_bind_step_builder.");
  }

  cursor->type = CONF_STEP_TYPE_DONE;
}

static void protocol_request_handler(char const *op, char const *params) {
  if (strcmp(op, "ping") == 0) {
    if (strlen(params) == 0) {
      protocol_reply(NRF_SUCCESS, "pong");
    } else {
      int delay_ms = atoi(params);
      uint32_t err =
          app_timer_start(pong_timer, APP_TIMER_TICKS(delay_ms), NULL);

      if (err != NRF_SUCCESS) {
        protocol_reply(err, "delayed ping failed");
      }
    }
  } else if (strcmp(op, "id") == 0) {
    uint32_t addr = strtol(params, NULL, 16);
    uint32_t err =
        conf_start(APP_GATEWAY_ADDR, health_client_bind_step_builder);

    if (err != NRF_SUCCESS) {
      protocol_reply(err, "health client config failed");
    } else {
      pending_request.type = PROTOCOL_ID_REQUEST;
      pending_request.request.id_request.addr = addr;
    }
  } else {
    protocol_reply(NRF_ERROR_NOT_FOUND, "invalid op '%s'", op);
  }
}

static void init_uart() {
  APP_ERROR_CHECK(protocol_init(APP_PIN_UART_TX, APP_PIN_UART_RX,
                                protocol_request_handler));

  LOG_INFO("UART initialized.");
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
generic_onoff_client_status_cb(const generic_onoff_client_t *self,
                               const access_message_rx_meta_t *meta,
                               const generic_onoff_status_params_t *in) {
  LOG_INFO("OnOff server on 0x%04x is: %s.", meta->src.value,
           (in->present_on_off ? "ON" : "OFF"));

  nrf_mesh_rx_metadata_t const *core_metadata = meta->p_core_metadata;

  if (core_metadata->source != NRF_MESH_RX_SOURCE_SCANNER) {
    return;
  }

  nrf_mesh_rx_metadata_scanner_t const *scanner_metadata =
      &core_metadata->params.scanner;

  protocol_send("onoff %d %d %d %d", meta->src.value, meta->ttl,
                scanner_metadata->rssi, in->present_on_off);
}

static void
generic_onoff_client_transaction_status_cb(access_model_handle_t model_handle,
                                           void *p_args,
                                           access_reliable_status_t status) {
  // TODO:
}

char const *get_faults_description(health_client_evt_fault_status_t status) {
  if (status.fault_array_length == 0) {
    return "Healthy";
  } else if (status.fault_array_length == 1) {
    switch (status.p_fault_array[0]) {
    case APP_FAULT_ID_FRIENDLESS:
      return "Friendless";
    default:
      return "Unknown Fault";
    }
  } else {
    return "UNIMPLEMENTED YET";
  }
}

char const *to_hex(uint8_t const *data, size_t len) {
  static char buf[259];
  static char hex[17] = "0123456789abcdef";

  size_t actual_len = MIN(len, (sizeof(buf) - 1) / 2);

  buf[0] = '[';

  for (size_t i = 0; i < actual_len; i++) {
    buf[2 * i + 1] = hex[(data[i] & 0xF0) >> 4];
    buf[2 * i + 2] = hex[(data[i] & 0x0F) >> 0];
  }

  buf[2 * actual_len + 1] = ']';
  buf[2 * actual_len + 2] = '\0';

  return buf;
}

static void health_client_event_handler(health_client_t const *client,
                                        health_client_evt_t const *event) {
  switch (event->type) {
  case HEALTH_CLIENT_EVT_TYPE_CURRENT_STATUS_RECEIVED: //
  case HEALTH_CLIENT_EVT_TYPE_FAULT_STATUS_RECEIVED:   //
  {
    nrf_mesh_rx_metadata_t const *core_metadata =
        event->p_meta_data->p_core_metadata;

    if (core_metadata->source != NRF_MESH_RX_SOURCE_SCANNER) {
      break;
    }

    nrf_mesh_rx_metadata_scanner_t const *scanner_metadata =
        &core_metadata->params.scanner;

    protocol_send("health %d %d %d %s", event->p_meta_data->src.value,
                  event->p_meta_data->ttl, scanner_metadata->rssi,
                  to_hex(event->data.fault_status.p_fault_array,
                         event->data.fault_status.fault_array_length));

    break;
  }

  case HEALTH_CLIENT_EVT_TYPE_ATTENTION_STATUS_RECEIVED: //
  {
    if (pending_request.type != PROTOCOL_ID_REQUEST) {
      LOG_ERROR("Unexpected health client attention status received.");
      return;
    }

    protocol_reply(NRF_SUCCESS, "");
    pending_request.type = PROTOCOL_NO_REQUEST;

    break;
  }

  case HEALTH_CLIENT_EVT_TYPE_TIMEOUT: //
  {
    if (pending_request.type != PROTOCOL_ID_REQUEST) {
      LOG_ERROR("Unexpected health client timeout received.");
      return;
    }

    protocol_reply(NRF_ERROR_TIMEOUT, "timed out");
    pending_request.type = PROTOCOL_NO_REQUEST;

    break;
  }

  default: //
  {
    LOG_ERROR("Unexpected health client event type: %d", event->type);
    break;
  }
  }
}

void battery_level_client_status_handler(
    battery_level_client_t const *client, access_message_rx_meta_t const *meta,
    battery_level_status_params_t const *in) {
  nrf_mesh_rx_metadata_t const *core_metadata = meta->p_core_metadata;

  if (core_metadata->source != NRF_MESH_RX_SOURCE_SCANNER) {
    return;
  }

  nrf_mesh_rx_metadata_scanner_t const *scanner_metadata =
      &core_metadata->params.scanner;

  protocol_send("battery %d %d %d %d", meta->src.value, meta->ttl,
                scanner_metadata->rssi, in->level);
}

battery_level_client_t bl_client;

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

  bl_client.settings.force_segmented = false;
  bl_client.settings.transmic_size = NRF_MESH_TRANSMIC_SIZE_SMALL;
  bl_client.settings.status_cb = battery_level_client_status_handler;

  APP_ERROR_CHECK(battery_level_client_init(&bl_client, 0));
  LOG_INFO("Battery Level client initialized.");
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

static void
conf_step_builder(uint16_t addr,
                  config_msg_composition_data_status_t const *composition_data,
                  conf_step_t *steps_out) {
  conf_step_t *cursor = steps_out;
  uint8_t const *data = composition_data->data;

  cursor->type = CONF_STEP_TYPE_APPKEY_ADD;
  cursor->params.appkey_add.netkey_index = APP_NETKEY_IDX;
  cursor->params.appkey_add.appkey_index = APP_APPKEY_IDX;
  cursor->params.appkey_add.appkey = app_state.persistent.network.appkey;
  cursor++;

  // TODO: Assuming 1 element
  config_composition_element_header_t const *element_header =
      (config_composition_element_header_t const
           *)&data[sizeof(config_composition_data_header_t)];
  LOG_INFO("SIG model number: %d. Vendor model number: %d.",
           element_header->sig_model_count, element_header->vendor_model_count);

  uint16_t const *sig_model_ids =
      (uint16_t const *)(&data[sizeof(config_composition_data_header_t) +
                               sizeof(config_composition_element_header_t)]);
  for (uint8_t i = 0; i < element_header->sig_model_count; i++) {
    uint16_t sig_model_id = sig_model_ids[i];

    switch (sig_model_id) {
    case HEALTH_SERVER_MODEL_ID: //
    {
      LOG_INFO("Adding steps for Health Server...");

      cursor->type = CONF_STEP_TYPE_MODEL_APP_BIND;
      cursor->params.model_app_bind.element_addr = addr;
      cursor->params.model_app_bind.model_id.company_id =
          ACCESS_COMPANY_ID_NONE;
      cursor->params.model_app_bind.model_id.model_id = HEALTH_SERVER_MODEL_ID;
      cursor->params.model_app_bind.appkey_index = APP_APPKEY_IDX;
      cursor++;

      cursor->type = CONF_STEP_TYPE_MODEL_PUBLICATION_SET;
      cursor->params.model_publication_set.element_addr = addr;
      cursor->params.model_publication_set.model_id.company_id =
          ACCESS_COMPANY_ID_NONE;
      cursor->params.model_publication_set.model_id.model_id =
          HEALTH_SERVER_MODEL_ID;
      cursor->params.model_publication_set.publish_address.type =
          NRF_MESH_ADDRESS_TYPE_UNICAST;
      cursor->params.model_publication_set.publish_address.value = 0x0001;
      cursor->params.model_publication_set.appkey_index = APP_APPKEY_IDX;
      cursor->params.model_publication_set.publish_ttl = 7;
      cursor->params.model_publication_set.publish_period.step_num = 10;
      cursor->params.model_publication_set.publish_period.step_res =
          ACCESS_PUBLISH_RESOLUTION_1S;
      cursor++;

      break;
    }

    case HEALTH_CLIENT_MODEL_ID: //
    {
      LOG_INFO("Adding steps for Health Client...");

      cursor->type = CONF_STEP_TYPE_MODEL_APP_BIND;
      cursor->params.model_app_bind.element_addr = APP_GATEWAY_ADDR;
      cursor->params.model_app_bind.model_id.company_id =
          ACCESS_COMPANY_ID_NONE;
      cursor->params.model_app_bind.model_id.model_id = HEALTH_CLIENT_MODEL_ID;
      cursor->params.model_app_bind.appkey_index = APP_APPKEY_IDX;
      cursor++;

      break;
    }

    case 0x1000: // Generic OnOff Server
    {
      LOG_INFO("Adding steps for Generic OnOff Server...");

      cursor->type = CONF_STEP_TYPE_MODEL_APP_BIND;
      cursor->params.model_app_bind.element_addr = addr;
      cursor->params.model_app_bind.model_id.company_id =
          ACCESS_COMPANY_ID_NONE;
      cursor->params.model_app_bind.model_id.model_id =
          0x1000, // Generic OnOff Server
          cursor->params.model_app_bind.appkey_index = APP_APPKEY_IDX;
      cursor++;

      cursor->type = CONF_STEP_TYPE_MODEL_PUBLICATION_SET;
      cursor->params.model_publication_set.element_addr = addr;
      cursor->params.model_publication_set.model_id.company_id =
          ACCESS_COMPANY_ID_NONE;
      cursor->params.model_publication_set.model_id.model_id =
          0x1000; // Generic OnOff Server
      cursor->params.model_publication_set.publish_address.type =
          NRF_MESH_ADDRESS_TYPE_UNICAST;
      cursor->params.model_publication_set.publish_address.value =
          APP_GATEWAY_ADDR;
      cursor->params.model_publication_set.appkey_index = APP_APPKEY_IDX;
      cursor->params.model_publication_set.publish_ttl = 7;
      cursor->params.model_publication_set.publish_period.step_num = 0;
      cursor->params.model_publication_set.publish_period.step_res =
          ACCESS_PUBLISH_RESOLUTION_100MS;
      cursor++;

      cursor->type = CONF_STEP_TYPE_MODEL_SUBSCRIPTION_ADD;
      cursor->params.model_subscription_add.element_addr = addr;
      cursor->params.model_subscription_add.model_id.company_id =
          ACCESS_COMPANY_ID_NONE;
      cursor->params.model_subscription_add.model_id.model_id =
          0x1000; // Generic OnOff Server
      cursor->params.model_subscription_add.address.type =
          NRF_MESH_ADDRESS_TYPE_GROUP;
      cursor->params.model_subscription_add.address.value = APP_PROXIMITY_ADDR;
      cursor++;

      break;
    }

    case GENERIC_ONOFF_CLIENT_MODEL_ID: //
    {
      LOG_INFO("Adding steps for Generic OnOff Client...");

      cursor->type = CONF_STEP_TYPE_MODEL_APP_BIND;
      cursor->params.model_app_bind.element_addr = addr;
      cursor->params.model_app_bind.model_id.company_id =
          ACCESS_COMPANY_ID_NONE;
      cursor->params.model_app_bind.model_id.model_id =
          GENERIC_ONOFF_CLIENT_MODEL_ID;
      cursor->params.model_app_bind.appkey_index = APP_APPKEY_IDX;
      cursor++;

      cursor->type = CONF_STEP_TYPE_MODEL_PUBLICATION_SET;
      cursor->params.model_publication_set.element_addr = addr;
      cursor->params.model_publication_set.model_id.company_id =
          ACCESS_COMPANY_ID_NONE;
      cursor->params.model_publication_set.model_id.model_id =
          GENERIC_ONOFF_CLIENT_MODEL_ID;
      cursor->params.model_publication_set.publish_address.type =
          NRF_MESH_ADDRESS_TYPE_GROUP;
      cursor->params.model_publication_set.publish_address.value = APP_LED_ADDR;
      cursor->params.model_publication_set.appkey_index = APP_APPKEY_IDX;
      cursor->params.model_publication_set.publish_ttl = 7;
      cursor->params.model_publication_set.publish_period.step_num = 0;
      cursor->params.model_publication_set.publish_period.step_res =
          ACCESS_PUBLISH_RESOLUTION_100MS;
      cursor++;

      break;
    }

    default: //
    {
      LOG_INFO("Unknown SIG model: %d.", sig_model_id);
      break;
    }
    }
  }

  uint32_t const *vendor_model_ids =
      (uint32_t const
           *)(&data[sizeof(config_composition_data_header_t) +
                    sizeof(config_composition_element_header_t) +
                    element_header->sig_model_count * sizeof(uint16_t)]);
  for (size_t i = 0; i < element_header->vendor_model_count; i++) {
    uint32_t vendor_model_id = vendor_model_ids[i];

    if (vendor_model_id == 0x0001BEEE) {
      LOG_INFO("Adding steps for Battery Level Server...");

      cursor->type = CONF_STEP_TYPE_MODEL_APP_BIND;
      cursor->params.model_app_bind.element_addr = addr;
      cursor->params.model_app_bind.model_id.company_id =
          BATTERY_LEVEL_COMPANY_ID;
      cursor->params.model_app_bind.model_id.model_id =
          BATTERY_LEVEL_SERVER_MODEL_ID;
      cursor->params.model_app_bind.appkey_index = APP_APPKEY_IDX;
      cursor++;

      cursor->type = CONF_STEP_TYPE_MODEL_PUBLICATION_SET;
      cursor->params.model_publication_set.element_addr = addr;
      cursor->params.model_publication_set.model_id.company_id =
          BATTERY_LEVEL_COMPANY_ID;
      cursor->params.model_publication_set.model_id.model_id =
          BATTERY_LEVEL_SERVER_MODEL_ID;
      cursor->params.model_publication_set.publish_address.type =
          NRF_MESH_ADDRESS_TYPE_UNICAST;
      cursor->params.model_publication_set.publish_address.value = 0x0001;
      cursor->params.model_publication_set.appkey_index = APP_APPKEY_IDX;
      cursor->params.model_publication_set.publish_ttl = 7;
      cursor->params.model_publication_set.publish_period.step_num = 10;
      cursor->params.model_publication_set.publish_period.step_res =
          ACCESS_PUBLISH_RESOLUTION_1S;
      cursor++;
    } else if (vendor_model_id == 0x0002BEEE) {
      LOG_INFO("Adding steps for Battery Level Client...");

      cursor->type = CONF_STEP_TYPE_MODEL_APP_BIND;
      cursor->params.model_app_bind.element_addr = APP_GATEWAY_ADDR;
      cursor->params.model_app_bind.model_id.company_id =
          BATTERY_LEVEL_COMPANY_ID;
      cursor->params.model_app_bind.model_id.model_id =
          BATTERY_LEVEL_CLIENT_MODEL_ID;
      cursor->params.model_app_bind.appkey_index = APP_APPKEY_IDX;
      cursor++;
    } else {
      LOG_ERROR("Unknown vendor model: 0x%08x.", vendor_model_id);
    }
  }

  cursor->type = CONF_STEP_TYPE_DONE;
}

static void prov_success_cb(uint16_t addr) {
  nrf_gpio_pin_clear(PIN_LED_INDICATION);

  APP_ERROR_CHECK(conf_start(addr, conf_step_builder));
}

static void prov_failure_cb() {
  nrf_gpio_pin_clear(PIN_LED_INDICATION);

  prov_start_scan();
}

static void conf_success_cb(uint16_t addr) {
  switch (pending_request.type) {
  case PROTOCOL_ID_REQUEST: //
  {
    uint32_t err = health_client_attention_set(&health_client, 10, true);

    if (err != NRF_SUCCESS) {
      protocol_reply(err, "failed to set attention timer");
      return;
    }

    return;
  }

  case PROTOCOL_NO_REQUEST:
  default:
    break;
  }

  prov_start_scan();
}

static void conf_failure_cb(uint16_t addr, uint32_t err) {
  switch (pending_request.type) {
  case PROTOCOL_ID_REQUEST: //
  {
    protocol_reply(err, "failed to config health client");
    return;
  }

  case PROTOCOL_NO_REQUEST:
  default:
    break;
  }

  prov_start_scan();
}

static void onoff_client_toggle_timer_handler(void *context) {
  static bool value = false;

  send_onoff_request(value);
  value = !value;
}

static void self_config_timer_handler(void *context) {
  LOG_INFO("Starting self-configuration...");

  conf_start(APP_GATEWAY_ADDR, conf_step_builder);
}

static void start() {
  rtt_input_enable(rtt_input_handler, 100);

  APP_ERROR_CHECK(mesh_stack_start());
  LOG_INFO("Mesh stack started.");

  app_state_init();

  bool is_provisioned = mesh_stack_is_device_provisioned();

  if (is_provisioned) {
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
  APP_ERROR_CHECK(app_timer_create(&self_config_timer,
                                   APP_TIMER_MODE_SINGLE_SHOT,
                                   self_config_timer_handler));
  APP_ERROR_CHECK(app_timer_create(&pong_timer, APP_TIMER_MODE_SINGLE_SHOT,
                                   pong_timer_handler));

  if (!is_provisioned) {
    APP_ERROR_CHECK(
        app_timer_start(self_config_timer, APP_TIMER_TICKS(1000), NULL));
  } else {
    prov_start_scan();
  }
}

int main(void) {
  DEBUG_PINS_INIT();

  init_leds();
  init_logging();
  init_mesh();
  init_buttons();
  init_uart();

  start();

  while (true) {
    (void)sd_app_evt_wait();
  }
}
