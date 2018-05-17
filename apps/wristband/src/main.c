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
#define PIN_RESET_NETWORK_CONFIG 4

typedef struct {
  health_client_t health_client;
  ecare_client_t ecare_client;
} app_t;

app_t app;

APP_TIMER_DEF(imu_timer_id);
APP_TIMER_DEF(fallen_led_timer_id);

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
  error_info_t *error_info = (error_info_t *)info;

  app_timer_stop(fallen_led_timer_id);
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

// static void toggle_indication_led(void) {
//   static bool on = false;
//
//   if (on) {
//     nrf_gpio_pin_clear(PIN_LED_INDICATION);
//   } else {
//     nrf_gpio_pin_set(PIN_LED_INDICATION);
//   }
//
//   on = !on;
// }

static void health_client_evt_cb(const health_client_t *client,
                                 const health_client_evt_t *event) {
  if (event->p_meta_data->p_core_metadata->source ==
      NRF_MESH_RX_SOURCE_LOOPBACK) {
    return;
  }

  static int t = 0;
  static int8_t rssi_by_channel[3][APP_MAX_PROVISIONEES] = {0};
  static int reading_timestamp[3][APP_MAX_PROVISIONEES] = {0};

  uint8_t ch = event->p_meta_data->p_core_metadata->params.scanner.channel;
  uint16_t addr = event->p_meta_data->src.value;
  int8_t rssi = event->p_meta_data->p_core_metadata->params.scanner.rssi;

  if (addr == 1) {
    return;
  }

  ++t;
  rssi_by_channel[ch - 37][addr] = rssi;
  reading_timestamp[ch - 37][addr] = t;

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < APP_MAX_PROVISIONEES; j++) {
      if (reading_timestamp[i][j] < t - 50) {
        reading_timestamp[i][j] = 0;
        rssi_by_channel[i][j] = 0;
      }
    }
  }

  bool best = true;
  for (int i = 2; i < APP_MAX_PROVISIONEES; i++) {
    if (rssi_by_channel[ch - 37][i] != 0 &&
        rssi_by_channel[ch - 37][i] > rssi) {
      best = false;
      break;
    }
  }

  static int best_history[10] = {0};
  static int best_history_index = 0;
  static int published_best = -1;

  if (best) {
    best_history[best_history_index] = addr;
    best_history_index++;
    best_history_index %= 10;

    int best_count = 0;
    for (int i = 0; i < sizeof(best_history) / sizeof(best_history[0]); i++) {
      if (best_history[i] == addr) {
        best_count++;
      }
    }

    if (best_count >= 8) {
      if (addr != published_best) {
        published_best = addr;

        ecare_state_t state = {
            .fallen = false, .x = 1, .y = published_best, .z = 0};

        ecare_client_set(&app.ecare_client, state);

        LOG_INFO("Published %d", published_best);
      }
    }
  }
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

static void send_fallen_state_update();

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
    nrf_gpio_cfg_input(PIN_RESET_NETWORK_CONFIG, NRF_GPIO_PIN_PULLDOWN);
    bool should_reset = (nrf_gpio_pin_read(PIN_RESET_NETWORK_CONFIG) != 0);

    LOG_INFO("should_reset is read as %d", should_reset);
    LOG_INFO("should_reset is read as %d", should_reset);

    if (should_reset) {
      LOG_ERROR("Will clear all config and reset in 1s. ");

      mesh_stack_config_clear();
      nrf_delay_ms(1000);
      mesh_stack_device_reset();
    } else {
      LOG_ERROR("Will reuse the existing network config. ");
      send_fallen_state_update();
    }
  }
}

static const nrf_drv_twi_t twi = NRF_DRV_TWI_INSTANCE(0);
lsm9ds1_t imu;

static void init_imu() {
  const nrf_drv_twi_config_t config = {.scl = 8,
                                       .sda = 7,
                                       .frequency = NRF_DRV_TWI_FREQ_100K,
                                       .interrupt_priority =
                                           APP_IRQ_PRIORITY_HIGH,
                                       .clear_bus_init = true};

  APP_ERROR_CHECK(nrf_drv_twi_init(&twi, &config, NULL, NULL));
  nrf_drv_twi_enable(&twi);
  LOG_INFO("TWI successfully initialized. ");

  lsm9ds1_init(&imu, &twi);
  LOG_INFO("IMU successfully initialized. ");

  fall_detection_init();
}

bool fallen = false;
static void send_fallen_state_update() {
  ecare_state_t state = {
      .fallen = fallen,
      .x = 0,
      .y = 0,
      .z = 0,
  };

  ecare_client_set(&app.ecare_client, state);
}

static void imu_timer_handler(void *context) {
  float ax, ay, az;

  lsm9ds1_accel_read_all(&imu, &ax, &ay, &az);

  bool lying = fall_detection_update(ax, ay, az);

  if (lying != fallen) {
    fallen = lying;
    send_fallen_state_update();
  }
}

static void fallen_led_timer_handler(void *context) {
  if (fallen) {
    nrf_gpio_pin_clear(PIN_LED_INDICATION);
    nrf_gpio_pin_toggle(PIN_LED_ERROR);
  } else {
    nrf_gpio_pin_clear(PIN_LED_ERROR);
  }
}

static void init_timer() {
  APP_ERROR_CHECK(app_timer_init());

  APP_ERROR_CHECK(app_timer_create(&imu_timer_id, APP_TIMER_MODE_REPEATED,
                                   imu_timer_handler));
  APP_ERROR_CHECK(app_timer_start(imu_timer_id,
                                  APP_TIMER_TICKS(APP_IMU_INTERVAL_MS), NULL));

  APP_ERROR_CHECK(app_timer_create(
      &fallen_led_timer_id, APP_TIMER_MODE_REPEATED, fallen_led_timer_handler));
  APP_ERROR_CHECK(app_timer_start(
      fallen_led_timer_id, APP_TIMER_TICKS(APP_FALLEN_LED_INTERVAL_MS), NULL));
}

int main(void) {
  DEBUG_PINS_INIT();

  init_leds();
  init_logging();
  init_mesh();

  init_timer();
  init_imu();

  execution_start(start);

  while (true) {
    (void)sd_app_evt_wait();
  }
}
