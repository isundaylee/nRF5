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

#include "ring_packet_queue.h"

#include "custom_log.h"

////////////////////////////////////////////////////////////////////////////////

#define TX_QUEUE_SIZE 4096
#define TX_PRIORITY_QUEUE_SIZE 1024

RING_PACKET_QUEUE_DEFINE(tx_queue, TX_QUEUE_SIZE);
RING_PACKET_QUEUE_DEFINE(tx_prio_queue, TX_PRIORITY_QUEUE_SIZE);

ring_packet_queue_t *pending_tx_queue = NULL;

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
    LOG_INFO("noproto\0Protocol: Power detected.");
    if (!nrf_drv_usbd_is_enabled()) {
      app_usbd_enable();
    }
    break;

  case APP_USBD_EVT_POWER_REMOVED:
    LOG_INFO("noproto\0Protocol: Power removed.");
    app_usbd_stop();
    break;

  case APP_USBD_EVT_POWER_READY:
    LOG_INFO("noproto\0Protocol: Ready.");
    app_usbd_start();
    break;

  default:
    LOG_INFO("noproto\0Protocol: Event %d", event);
    break;
  }
}

static void drain_tx_queue() {
  ring_packet_queue_packet_t *packet;
  ring_packet_queue_t *chosen_queue;

  if (!ring_packet_queue_is_empty(&tx_prio_queue)) {
    packet = ring_packet_queue_get_head(&tx_prio_queue);
    chosen_queue = &tx_prio_queue;
  } else if (!ring_packet_queue_is_empty(&tx_queue)) {
    packet = ring_packet_queue_get_head(&tx_queue);
    chosen_queue = &tx_queue;
  } else {
    return;
  }

  // TODO: Plz investigate this further...
  for (int i = 0; i < 10000; i++)
    __asm__ volatile("nop");

  uint32_t err =
      app_usbd_cdc_acm_write(&app_cdc_acm, packet->data, packet->len);
  if (err == NRF_SUCCESS) {
    pending_tx_queue = chosen_queue;
  } else {
    if (err != NRF_ERROR_INVALID_STATE) {
      LOG_ERROR("Protocol: TX error %d.", err);
    }
  }
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
    ASSERT(pending_tx_queue != NULL);
    ring_packet_queue_free_packet(pending_tx_queue,
                                  ring_packet_queue_get_head(pending_tx_queue));
    pending_tx_queue = NULL;
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

bool protocol_send_raw(char const *data, size_t len, bool prio) {
  bool success = true;

  ring_packet_queue_t *queue_to_use = (prio ? &tx_prio_queue : &tx_queue);
  ring_packet_queue_packet_t *packet =
      ring_packet_queue_alloc_packet(queue_to_use, len);

  if (packet != NULL) {
    memcpy(packet->data, data, len);
  } else {
    LOG_ERROR("noproto\0Protocol: Dropped message due to TX queue overflow. "
              "Prio: %d.",
              prio);
    success = false;
  }

  if (pending_tx_queue == NULL) {
    drain_tx_queue();
  }

  return success;
}

#endif
