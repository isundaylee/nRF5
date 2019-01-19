#include "ble_softdevice_support.h"
#include "mesh_stack.h"

#include "app_timer.h"

#include "sdk_config.h"

#include "log.h"

APP_TIMER_DEF(repro_timer);

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
  while (true) {
  }
}
void mesh_assertion_handler(uint32_t pc) {}

static void repro_timer_handler(void *ctx) {
  static bool is_first_time = true;

  if (is_first_time) {
    is_first_time = false;
    APP_ERROR_CHECK(
        app_timer_start(repro_timer, APP_TIMER_TICKS(500000), NULL));
    __LOG(LOG_SRC_APP, LOG_LEVEL_ERROR, "Started the second trigger.");
  } else {
    APP_ERROR_CHECK(app_timer_start(repro_timer, APP_TIMER_TICKS(1000), NULL));
    __LOG(LOG_SRC_APP, LOG_LEVEL_ERROR, "Started a periodic trigger.");
  }
}

static void config_server_event_handler(config_server_evt_t const *event) {}

int main(void) {
  // Initialize logging
  __LOG_INIT(LOG_SRC_APP | LOG_SRC_ACCESS | LOG_SRC_BEARER, LOG_LEVEL_INFO,
             LOG_CALLBACK_DEFAULT);
  __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Hello, world!\n");

  // Initialize SD
  ble_stack_init();
  __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Mesh soft device initialized.\n");

  // Initialize Mesh
  nrf_clock_lf_cfg_t lfc_cfg = {.source = NRF_CLOCK_LF_SRC_XTAL,
                                .rc_ctiv = 0,
                                .rc_temp_ctiv = 0,
                                .accuracy = NRF_CLOCK_LF_ACCURACY_20_PPM};
  mesh_stack_init_params_t init_params = {
      .core.irq_priority = NRF_MESH_IRQ_PRIORITY_LOWEST,
      .core.lfclksrc = lfc_cfg,
      .models.config_server_cb = config_server_event_handler,
  };
  APP_ERROR_CHECK(mesh_stack_init(&init_params, NULL));
  __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Mesh stack initialized.\n");

  // Initialize timer
  APP_ERROR_CHECK(app_timer_create(&repro_timer, APP_TIMER_MODE_SINGLE_SHOT,
                                   repro_timer_handler));

  // Start timer
  APP_ERROR_CHECK(app_timer_start(repro_timer, APP_TIMER_TICKS(30000), NULL));
  __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "All great!\n");

  // Start mesh
  APP_ERROR_CHECK(mesh_stack_start());
  __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Mesh stack started.\n");

  // Main loop
  for (;;) {
    (void)sd_app_evt_wait();
  }
}
