#include <stdio.h>

#include "nrf_delay.h"
#include "nrf_gpio.h"

#include "mesh_app_utils.h"
#include "mesh_provisionee.h"
#include "mesh_softdevice_init.h"
#include "mesh_stack.h"

#include "app_timer.h"

#include "ecare_client.h"
#include "health_client.h"
#include "localization.h"

#include "app_config.h"
#include "custom_log.h"
#include "fall_detection.h"
#include "localization.h"
#include "lsm9ds1.h"
#include "vector.h"

#include "debug_pins.h"

#define PIN_LED_ERROR 27
#define PIN_LED_INDICATION 28

typedef struct {
  health_client_t health_client;
  ecare_client_t ecare_client;
} app_t;

app_t app;

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

static void init_leds() {
  nrf_gpio_cfg_output(PIN_LED_ERROR);
  nrf_gpio_cfg_output(PIN_LED_INDICATION);
}

static void init_logging() {
  LOG_INIT();
  LOG_INFO("Hello, world!");
}

static void config_server_evt_cb(config_server_evt_t const *evt) {
  LOG_INFO("Received config server event. ");

  nrf_gpio_pin_set(PIN_LED_INDICATION);
}

static void provision_complete_cb(void) {
  LOG_INFO("We have been successfully provisioned! ");
}

static void health_client_evt_cb(const health_client_t *client,
                                 const health_client_evt_t *event) {
  if (event->p_meta_data->p_core_metadata->source ==
      NRF_MESH_RX_SOURCE_LOOPBACK) {
    return;
  }

  LOG_INFO("Received health client event from %d. ",
           event->p_meta_data->src.value);
}

void ecare_status_cb(const ecare_client_t *self, ecare_state_t state) {
  LOG_INFO("Received new Ecare state: fallen = %d, x = %d, y = %d, z = %d",
           state.fallen, state.x, state.y, state.z);
}

void ecare_timeout_cb(access_model_handle_t handle, void *self) {
  LOG_ERROR("Received timeout on Ecare client. ");
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

  // Set up health client
  APP_ERROR_CHECK(
      health_client_init(&app.health_client, 0, health_client_evt_cb));

  // Set up ecare client
  app.ecare_client.status_cb = ecare_status_cb;
  app.ecare_client.timeout_cb = ecare_timeout_cb;
  APP_ERROR_CHECK(ecare_client_init(&app.ecare_client, 0));

  // Print out device address
  ble_gap_addr_t addr;
  APP_ERROR_CHECK(sd_ble_gap_addr_get(&addr));
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
        .p_device_uri = "wristband",
    };
    APP_ERROR_CHECK(mesh_provisionee_prov_start(&prov_start_params));

    nrf_gpio_pin_clear(PIN_LED_INDICATION);

    LOG_INFO("Provisioning initiated. ");
  } else {
    LOG_ERROR("We have already been provisioned. ");

    nrf_gpio_pin_set(PIN_LED_INDICATION);
    bool should_reset = true;

    if (should_reset) {
      LOG_ERROR("Will clear all config and reset in 1s. ");

      mesh_stack_config_clear();
      nrf_delay_ms(1000);
      mesh_stack_device_reset();
    } else {
      LOG_ERROR("Will reuse the existing network config. ");
    }
  }
}

static void init_timer() { APP_ERROR_CHECK(app_timer_init()); }

int main(void) {
  DEBUG_PINS_INIT();

  init_leds();
  init_logging();
  init_mesh();
  init_timer();

  execution_start(start);

  while (true) {
    (void)sd_app_evt_wait();
  }
}
