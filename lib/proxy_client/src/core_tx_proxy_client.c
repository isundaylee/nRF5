#include "core_tx_proxy_client.h"

#include "proxy_client.h"

// Forward declarations
static core_tx_alloc_result_t
packet_alloc(core_tx_bearer_t *p_bearer,
             const core_tx_alloc_params_t *p_params);
static void packet_send(core_tx_bearer_t *p_bearer, const uint8_t *p_packet,
                        uint32_t packet_length);
static void packet_discard(core_tx_bearer_t *p_bearer);

static proxy_client_t *bearer_client;
static core_tx_bearer_t bearer;
static core_tx_bearer_interface_t bearer_interface = {
    .packet_alloc = packet_alloc,
    .packet_send = packet_send,
    .packet_discard = packet_discard,
};

static proxy_client_packet_t *current_alloc = NULL;

// Core TX interface functions

static core_tx_alloc_result_t
packet_alloc(core_tx_bearer_t *proxy_client_bearer,
             const core_tx_alloc_params_t *params) {
  NRF_MESH_ASSERT(proxy_client_bearer == &bearer);
  NRF_MESH_ASSERT(current_alloc == NULL);

  current_alloc =
      proxy_client_network_packet_alloc(bearer_client, params->net_packet_len);

  if (current_alloc == NULL) {
    return CORE_TX_ALLOC_FAIL_NO_MEM;
  }

  return CORE_TX_ALLOC_SUCCESS;
}

static void packet_send(core_tx_bearer_t *proxy_client_bearer,
                        const uint8_t *packet, uint32_t packet_length) {
  NRF_MESH_ASSERT(proxy_client_bearer == &bearer);
  NRF_MESH_ASSERT(current_alloc != NULL);

  memcpy(current_alloc->pdu, packet, packet_length);
  proxy_client_network_packet_send(bearer_client, current_alloc, packet_length);
  current_alloc = NULL;
}

static void packet_discard(core_tx_bearer_t *proxy_client_bearer) {
  NRF_MESH_ASSERT(proxy_client_bearer == &bearer);
  NRF_MESH_ASSERT(current_alloc != NULL);

  proxy_client_network_packet_discard(bearer_client, current_alloc);
}

void core_tx_proxy_client_init(proxy_client_t *client) {
  bearer_client = client;

  core_tx_bearer_add(&bearer, &bearer_interface,
                     CORE_TX_BEARER_TYPE_GATT_CLIENT);
}
