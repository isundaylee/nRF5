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

#include "nrf_delay.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "sdk_config.h"

#include "custom_log.h"

#define APP_DEVICE_NAME "PROXYTEST"

#define APP_PIN_LED_ERROR 27
#define APP_PIN_CLEAR_CONFIG 7

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

void mesh_assertion_handler(uint32_t pc) {
  assert_info_t assert_info = {.line_num = 0, .p_file_name = (uint8_t *)""};

  app_error_fault_handler(NRF_FAULT_ID_SDK_ASSERT, pc,
                          (uint32_t)(&assert_info));

  UNUSED_VARIABLE(assert_info);
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
  mesh_stack_init_params_t init_params = {.core.irq_priority =
                                              NRF_MESH_IRQ_PRIORITY_LOWEST,
                                          .core.lfclksrc = lfc_cfg};
  APP_ERROR_CHECK(mesh_stack_init(&init_params, NULL));
  LOG_INFO("Mesh stack initialized.");
}

static void prov_complete_cb(void) {
  dsm_local_unicast_address_t node_address;
  LOG_INFO("Successfully provisioned. ");
  dsm_local_unicast_addresses_get(&node_address);
  LOG_INFO("Node Address: 0x%04x. ", node_address.address_start);
}

APP_TIMER_DEF(reset_timer);

static void reset_timer_handler(void *context) {
  LOG_INFO("Clearing config and resetting...");

  mesh_stack_config_clear();
  // This function may return if there are ongoing flash operations.
  mesh_stack_device_reset();

  while (true) {
  }
}

static void start() {
  nrf_gpio_cfg_input(APP_PIN_CLEAR_CONFIG, NRF_GPIO_PIN_PULLDOWN);

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

    if (nrf_gpio_pin_read(APP_PIN_CLEAR_CONFIG)) {
      APP_ERROR_CHECK(app_timer_create(&reset_timer, APP_TIMER_MODE_SINGLE_SHOT,
                                       reset_timer_handler));
      APP_ERROR_CHECK(app_timer_start(reset_timer, APP_TIMER_TICKS(100), NULL));
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
