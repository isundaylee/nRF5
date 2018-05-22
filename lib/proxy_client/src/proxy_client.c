#include "proxy_client.h"

#include "ble_db_discovery.h"
#include "device_state_manager.h"
#include "mesh_gatt.h"
#include "net_packet.h"
#include "net_state.h"
#include "nrf_error.h"
#include "nrf_mesh_assert.h"
#include "nrf_mesh_externs.h"
#include "proxy_config_packet.h"
#include "proxy_filter.h"

#include "custom_log.h"

uint32_t proxy_client_init(proxy_client_t *client,
                           proxy_client_evt_handler_t evt_handler) {
  client->evt_handler = evt_handler;

  client->conn_handle = BLE_CONN_HANDLE_INVALID;

  client->handles.data_in_handle = BLE_GATT_HANDLE_INVALID;
  client->handles.data_out_handle = BLE_GATT_HANDLE_INVALID;
  client->handles.data_out_cccd_handle = BLE_GATT_HANDLE_INVALID;

  client->beacon_seen = false;

  ble_uuid_t service_uuid = {
      .uuid = MESH_PROXY_SERVICE_UUID,
      .type = BLE_UUID_TYPE_BLE,
  };

  packet_buffer_init(&client->tx.packet_buffer, client->tx.packet_buffer_data,
                     PROXY_CLIENT_PACKET_BUFFER_SIZE);

  uint32_t status = ble_db_discovery_evt_register(&service_uuid);
  if (status != NRF_SUCCESS) {
    return status;
  }

  return NRF_SUCCESS;
}

void proxy_client_conn_handle_assign(proxy_client_t *client,
                                     uint16_t conn_handle) {
  client->conn_handle = conn_handle;
}

void proxy_client_db_discovery_evt_handler(proxy_client_t *client,
                                           ble_db_discovery_evt_t *evt) {
  if ((evt->evt_type == BLE_DB_DISCOVERY_COMPLETE) &&
      (evt->params.discovered_db.srv_uuid.uuid == MESH_PROXY_SERVICE_UUID) &&
      (evt->params.discovered_db.srv_uuid.type == BLE_UUID_TYPE_BLE)) {
    LOG_INFO("Proxy Client: Found Mesh Proxy service");

    for (size_t i = 0; i < evt->params.discovered_db.char_count; i++) {
      ble_gatt_db_char_t ch = evt->params.discovered_db.charateristics[i];

      if (ch.characteristic.uuid.uuid == MESH_PROXY_DATA_IN_CHAR_UUID) {
        LOG_INFO("Proxy Client: Found Mesh Proxy Data In");
        client->handles.data_in_handle = ch.characteristic.handle_value;
      } else if (ch.characteristic.uuid.uuid == MESH_PROXY_DATA_OUT_CHAR_UUID) {
        LOG_INFO("Proxy Client: Found Mesh Proxy Data Out");
        client->handles.data_out_handle = ch.characteristic.handle_value;
        client->handles.data_out_cccd_handle = ch.cccd_handle;
      } else {
        LOG_INFO("Proxy Client: Found unexpected characteristic: %04x",
                 ch.characteristic.uuid.uuid);
      }
    }

    // Subscribe to the Mesh Proxy Data Out characteristic.
    uint8_t buf[] = {BLE_GATT_HVX_NOTIFICATION, 0};
    ble_gattc_write_params_t const write_params = {
        .write_op = BLE_GATT_OP_WRITE_REQ,
        .flags = BLE_GATT_EXEC_WRITE_FLAG_PREPARED_WRITE,
        .handle = client->handles.data_out_cccd_handle,
        .offset = 0,
        .len = sizeof(buf),
        .p_value = buf};
    APP_ERROR_CHECK(sd_ble_gattc_write(client->conn_handle, &write_params));
    LOG_INFO("Proxy Client: Subscribed to Mesh Proxy Data Out. ");

    proxy_client_evt_t pending_evt = {
        .type = PROXY_CLIENT_EVT_DISCOVERY_COMPLETE,
        .params.discovery_complete.conn_handle = evt->conn_handle,
    };

    client->evt_handler(&pending_evt);
  }
}

uint32_t proxy_client_send(proxy_client_t *client, uint8_t *data,
                           uint16_t len) {
  if ((client->conn_handle == BLE_CONN_HANDLE_INVALID) ||
      (client->handles.data_in_handle == BLE_GATT_HANDLE_INVALID)) {
    return NRF_ERROR_INVALID_STATE;
  }

  ble_gattc_write_params_t const write_params = {
      .write_op = BLE_GATT_OP_WRITE_CMD,
      .flags = BLE_GATT_EXEC_WRITE_FLAG_PREPARED_WRITE,
      .handle = client->handles.data_in_handle,
      .offset = 0,
      .len = len,
      .p_value = data};
  return sd_ble_gattc_write(client->conn_handle, &write_params);
}

static packet_mesh_net_packet_t *proxy_packet_alloc(proxy_client_t *client,
                                                    uint8_t pdu_type,
                                                    uint8_t sar_type,
                                                    uint16_t proxy_pdu_size) {
  NRF_MESH_ASSERT(client->tx.current_packet == NULL);

  packet_buffer_packet_t *packet;
  uint32_t status = packet_buffer_reserve(&client->tx.packet_buffer, &packet,
                                          sizeof(proxy_client_proxy_packet_t) +
                                              proxy_pdu_size);

  if (status != NRF_SUCCESS) {
    NRF_MESH_ASSERT(status == NRF_ERROR_NO_MEM);
    return NULL;
  }

  proxy_client_proxy_packet_t *proxy_packet =
      (proxy_client_proxy_packet_t *)packet;

  proxy_packet->pdu_type = pdu_type;
  proxy_packet->sar_type = sar_type;

  client->tx.current_packet = proxy_packet;

  return (packet_mesh_net_packet_t *)proxy_packet->pdu;
}

static proxy_config_msg_t *config_packet_alloc(proxy_client_t *client,
                                               uint16_t params_size) {
  uint16_t msg_len = PROXY_CONFIG_PARAM_OVERHEAD + params_size;
  uint16_t proxy_pdu_size =
      PACKET_MESH_NET_PDU_OFFSET + msg_len + net_packet_mic_size_get(true);
  packet_mesh_net_packet_t *net_packet = proxy_packet_alloc(
      client, MESH_GATT_PDU_TYPE_PROXY_CONFIG, 0, proxy_pdu_size);

  if (net_packet == NULL) {
    return NULL;
  }

  return (proxy_config_msg_t *)net_packet_payload_get(net_packet);
}

static uint32_t packet_send(proxy_client_t *client, uint16_t proxy_pdu_size) {
  NRF_MESH_ASSERT(client->tx.current_packet != NULL);

  if ((client->conn_handle == BLE_CONN_HANDLE_INVALID) ||
      (client->handles.data_in_handle == BLE_GATT_HANDLE_INVALID)) {
    return NRF_ERROR_INVALID_STATE;
  }

  ble_gattc_write_params_t const write_params = {
      .write_op = BLE_GATT_OP_WRITE_CMD,
      .flags = BLE_GATT_EXEC_WRITE_FLAG_PREPARED_WRITE,
      .handle = client->handles.data_in_handle,
      .offset = 0,
      .len = sizeof(proxy_client_proxy_packet_t) + proxy_pdu_size,
      .p_value = (uint8_t *)client->tx.current_packet};

  return sd_ble_gattc_write(client->conn_handle, &write_params);
}

static uint32_t config_packet_send(proxy_client_t *client,
                                   uint16_t params_size) {
  NRF_MESH_ASSERT(client->tx.current_packet != NULL);

  nrf_mesh_network_secmat_t const *secmat;
  mesh_key_index_t netkey_index;

  // Get netkey index
  uint32_t subnet_count = 1;
  APP_ERROR_CHECK(dsm_subnet_get_all(&netkey_index, &subnet_count));
  NRF_MESH_ASSERT(subnet_count == 1);

  // Get network secmat
  APP_ERROR_CHECK(dsm_net_secmat_from_keyindex_get(netkey_index, &secmat));

  // Generate packet metadata
  network_packet_metadata_t tx_net_meta = {
      .dst = {.type = NRF_MESH_ADDRESS_TYPE_INVALID, .value = 0},
      .ttl = 0,
      .control_packet = true,
      .internal = {.iv_index = net_state_tx_iv_index_get()},
      .p_security_material = secmat};

  // Fill in packet src address
  uint16_t unicast_addr_count = 1;
  nrf_mesh_unicast_address_get(&tx_net_meta.src, &unicast_addr_count);
  NRF_MESH_ASSERT(unicast_addr_count == 1);

  // Allocate seqnum
  APP_ERROR_CHECK(
      net_state_seqnum_alloc(&tx_net_meta.internal.sequence_number));

  // Fill in packet header
  packet_mesh_net_packet_t *net_packet =
      (packet_mesh_net_packet_t *)client->tx.current_packet->pdu;
  net_packet_header_set(net_packet, &tx_net_meta);

  // Encrypt packet
  uint16_t msg_len = PROXY_CONFIG_PARAM_OVERHEAD + params_size;
  net_packet_encrypt(&tx_net_meta, msg_len, net_packet,
                     NET_PACKET_KIND_PROXY_CONFIG);

  // Finally, send it!
  uint16_t proxy_pdu_size =
      PACKET_MESH_NET_PDU_OFFSET + msg_len + net_packet_mic_size_get(true);
  return packet_send(client, proxy_pdu_size);
}

uint32_t proxy_client_send_filter_type_set(proxy_client_t *client,
                                           uint8_t filter_type) {
  LOG_INFO("Proxy Client: Setting filter type to %d", filter_type);

  uint16_t params_size = sizeof(proxy_config_params_filter_type_set_t);
  proxy_config_msg_t *config_packet = config_packet_alloc(client, params_size);

  if (config_packet == NULL) {
    return NRF_ERROR_NO_MEM;
  }

  config_packet->opcode = PROXY_CONFIG_OPCODE_FILTER_TYPE_SET;
  config_packet->params.filter_type_set.filter_type = filter_type;

  return config_packet_send(client, params_size);
}

void proxy_client_packet_in(proxy_client_t *client, uint8_t *data,
                            uint16_t len) {
  proxy_client_proxy_packet_t *proxy_packet =
      (proxy_client_proxy_packet_t *)data;

  switch (proxy_packet->pdu_type) {
  case MESH_GATT_PDU_TYPE_NETWORK_PDU: // Network PDU
  {
    proxy_client_evt_t pending_evt = {
        .type = PROXY_CLIENT_EVT_DATA_IN,
        .params.data_in.data = data,
        .params.data_in.len = len,
    };

    client->evt_handler(&pending_evt);

    break;
  }

  case MESH_GATT_PDU_TYPE_MESH_BEACON: // Mesh Beacon
  {
    if (!client->beacon_seen) {
      client->beacon_seen = true;
      APP_ERROR_CHECK(proxy_client_send_filter_type_set(
          client, PROXY_FILTER_TYPE_BLACKLIST));
    }

    break;
  }

  case MESH_GATT_PDU_TYPE_PROXY_CONFIG: // Proxy Configuration
  {
    break;
  }

  default: //
  {
    LOG_INFO("Proxy Client: Unsupported packet with type %02x ignored",
             data[0] & 0x3F);
    break;
  }
  }
}

void proxy_client_ble_evt_handler(ble_evt_t const *ble_evt, void *context) {
  proxy_client_t *client = (proxy_client_t *)context;

  switch (ble_evt->header.evt_id) {
  case BLE_GATTC_EVT_HVX: //
  {
    const ble_gattc_evt_hvx_t hvx = ble_evt->evt.gattc_evt.params.hvx;

    if (hvx.handle != client->handles.data_out_handle) {
      break;
    }

    proxy_client_packet_in(client, (uint8_t *)hvx.data, hvx.len);

    break;
  }

  default: //
  {
    break;
  }
  }
}
