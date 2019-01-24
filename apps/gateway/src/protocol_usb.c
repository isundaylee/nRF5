#ifdef NRF52840_XXAA

#include "protocol.h"

#include <stdarg.h>
#include <stdlib.h>

#include <nrf.h>
#include <nrf_drv_usbd.h>

#include <app_usbd.h>
#include <app_usbd_cdc_acm.h>
#include <app_usbd_core.h>
#include <app_usbd_serial_num.h>
#include <app_usbd_string_desc.h>

#include "custom_log.h"

static bool request_pending = false;

static protocol_request_handler_t request_handler;

////////////////////////////////////////////////////////////////////////////////

static void usbd_user_ev_handler(app_usbd_event_type_t event) {
  LOG_INFO("USB: Event %d", event);

  switch (event) {
  case APP_USBD_EVT_DRV_SUSPEND:
    break;
  case APP_USBD_EVT_DRV_RESUME:
    break;
  case APP_USBD_EVT_STARTED:
    break;
  case APP_USBD_EVT_STOPPED:
    break;
  case APP_USBD_EVT_POWER_DETECTED:
    LOG_INFO("USB: Power detected.");

    if (!nrf_drv_usbd_is_enabled()) {
      app_usbd_enable();
    }
    break;
  case APP_USBD_EVT_POWER_REMOVED:
    LOG_INFO("USB: Power removed.");
    app_usbd_stop();
    break;
  case APP_USBD_EVT_POWER_READY:
    LOG_INFO("USB: Ready.");
    app_usbd_start();
    break;
  default:
    break;
  }
}

////////////////////////////////////////////////////////////////////////////////

uint32_t protocol_init(uint32_t tx_pin, uint32_t rx_pin,
                       protocol_request_handler_t req_handler) {
  request_handler = req_handler;

  // Initialize usbd
  static const app_usbd_config_t usbd_config = {
      .ev_state_proc = usbd_user_ev_handler,
  };

  app_usbd_serial_num_generate();

  APP_ERROR_CHECK(app_usbd_init(&usbd_config));
  LOG_INFO("USB: Initialized.");

  return NRF_SUCCESS;
}

uint32_t protocol_start() {
  APP_ERROR_CHECK(app_usbd_power_events_enable());
  LOG_INFO("USB: Power events enabled.");

  return NRF_SUCCESS;
}

void protocol_send(char const *fmt, ...) {}

void protocol_reply(uint32_t err, char const *fmt, ...) {}

void protocol_process() {
  while (app_usbd_event_queue_process()) {
    // Live life
  }
}

#endif
