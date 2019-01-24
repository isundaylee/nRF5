#ifdef NRF52840_XXAA

#include "protocol_internal.h"

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

#define TX_QUEUE_SIZE 8

static bool tx_pending = false;
static size_t tx_queue_head = 0;
static size_t tx_queue_tail = 0;
static char tx_buffers[TX_QUEUE_SIZE][256];
static size_t tx_lengths[TX_QUEUE_SIZE];

static void cdc_acm_user_event_handler(app_usbd_class_inst_t const *inst,
                                       app_usbd_cdc_acm_user_event_t event);

APP_USBD_CDC_ACM_GLOBAL_DEF(app_cdc_acm, cdc_acm_user_event_handler, 0, 1,
                            NRF_DRV_USBD_EPIN2, NRF_DRV_USBD_EPIN1,
                            NRF_DRV_USBD_EPOUT1,
                            APP_USBD_CDC_COMM_PROTOCOL_AT_V250);

////////////////////////////////////////////////////////////////////////////////

static void usbd_user_ev_handler(app_usbd_event_type_t event) {

  switch (event) {
  case APP_USBD_EVT_POWER_DETECTED:
    LOG_INFO("Protocol: Power detected.");
    if (!nrf_drv_usbd_is_enabled()) {
      app_usbd_enable();
    }
    break;

  case APP_USBD_EVT_POWER_REMOVED:
    LOG_INFO("Protocol: Power removed.");
    app_usbd_stop();
    break;

  case APP_USBD_EVT_POWER_READY:
    LOG_INFO("Protocol: Ready.");
    app_usbd_start();
    break;

  default:
    LOG_INFO("Protocol: Event %d", event);
    break;
  }
}

static void drain_tx_queue() {
  if (tx_queue_head == tx_queue_tail) {
    return;
  }

  uint32_t err = app_usbd_cdc_acm_write(&app_cdc_acm, tx_buffers[tx_queue_head],
                                        tx_lengths[tx_queue_head]);
  if (err == NRF_SUCCESS) {
    tx_pending = true;
  } else {
    if (err != NRF_ERROR_INVALID_STATE) {
      LOG_INFO("Protocol: TX error %d.", err);
    }
  }

  tx_queue_head = (tx_queue_head + 1) % TX_QUEUE_SIZE;
}

static void cdc_acm_user_event_handler(app_usbd_class_inst_t const *inst,
                                       app_usbd_cdc_acm_user_event_t event) {
  static char rx_char;
  app_usbd_cdc_acm_t const *cdc_acm = app_usbd_cdc_acm_class_get(inst);

  switch (event) {
  case APP_USBD_CDC_ACM_USER_EVT_PORT_OPEN: //
  {
    LOG_INFO("Protocol: Port opened.");
    app_usbd_cdc_acm_read(cdc_acm, &rx_char, 1);
    break;
  }

  case APP_USBD_CDC_ACM_USER_EVT_PORT_CLOSE: //
  {
    LOG_INFO("Protocol: Port closed.");
    break;
  }

  case APP_USBD_CDC_ACM_USER_EVT_TX_DONE: //
  {
    tx_pending = false;
    drain_tx_queue();
    break;
  }

  case APP_USBD_CDC_ACM_USER_EVT_RX_DONE: //
  {
    do {
      protocol_post_rx_char(rx_char);
    } while (app_usbd_cdc_acm_read(cdc_acm, &rx_char, 1) == NRF_SUCCESS);

    break;
  }

  default:
    LOG_INFO("Protocol: Unknown event %d.", event);
    break;
  }
}

////////////////////////////////////////////////////////////////////////////////

uint32_t protocol_init(uint32_t tx_pin, uint32_t rx_pin,
                       protocol_request_handler_t request_handler) {
  protocol_request_handler = request_handler;

  // Initialize usbd
  static const app_usbd_config_t usbd_config = {
      .ev_state_proc = usbd_user_ev_handler,
  };

  app_usbd_serial_num_generate();

  APP_ERROR_CHECK(app_usbd_init(&usbd_config));
  LOG_INFO("Protocol: Initialized.");

  app_usbd_class_inst_t const *class_cdc_acm =
      app_usbd_cdc_acm_class_inst_get(&app_cdc_acm);
  APP_ERROR_CHECK(app_usbd_class_append(class_cdc_acm));

  return NRF_SUCCESS;
}

uint32_t protocol_start() {
  APP_ERROR_CHECK(app_usbd_power_events_enable());
  LOG_INFO("Protocol: Power events enabled.");

  return NRF_SUCCESS;
}

void protocol_process() {
  while (app_usbd_event_queue_process()) {
    // Live life
  }
}

void protocol_send_raw(char const *data, size_t len) {
  if (len > sizeof(tx_buffers[0])) {
    LOG_ERROR("Protocol: Dropped message due to TX buffer size limit.");
    return;
  }

  size_t new_tx_queue_tail = (tx_queue_tail + 1) % TX_QUEUE_SIZE;
  if (new_tx_queue_tail == tx_queue_head) {
    LOG_ERROR("Protocol: Dropped message due to TX queue overflow.");
    return;
  }

  memcpy(tx_buffers[tx_queue_tail], data, len);
  tx_lengths[tx_queue_tail] = len;

  tx_queue_tail = new_tx_queue_tail;

  if (!tx_pending) {
    drain_tx_queue();
  }
}

#endif
