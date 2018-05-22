#include "proxy_client.h"

#include "ble_db_discovery.h"
#include "nrf_error.h"
#include "nrf_mesh_assert.h"

#include "custom_log.h"

uint32_t proxy_client_init(proxy_client_t *client,
                           proxy_client_evt_handler_t evt_handler) {
  client->evt_handler = evt_handler;

  client->conn_handle = BLE_CONN_HANDLE_INVALID;

  client->handles.data_in_handle = BLE_GATT_HANDLE_INVALID;
  client->handles.data_out_handle = BLE_GATT_HANDLE_INVALID;
  client->handles.data_out_cccd_handle = BLE_GATT_HANDLE_INVALID;

  ble_uuid_t service_uuid = {
      .uuid = MESH_PROXY_SERVICE_UUID,
      .type = BLE_UUID_TYPE_BLE,
  };

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
    LOG_INFO("Proxy Client: Found Mesh Proxy service. ");

    for (size_t i = 0; i < evt->params.discovered_db.char_count; i++) {
      ble_gatt_db_char_t ch = evt->params.discovered_db.charateristics[i];

      if (ch.characteristic.uuid.uuid == MESH_PROXY_DATA_IN_CHAR_UUID) {
        LOG_INFO("Proxy Client: Found Mesh Proxy Data In. ");
        client->handles.data_in_handle = ch.characteristic.handle_value;
      } else if (ch.characteristic.uuid.uuid == MESH_PROXY_DATA_OUT_CHAR_UUID) {
        LOG_INFO("Proxy Client: Found Mesh Proxy Data Out. ");
        client->handles.data_out_handle = ch.characteristic.handle_value;
        client->handles.data_out_cccd_handle = ch.cccd_handle;
      } else {
        LOG_INFO("Proxy Client: Found unexpected characteristic: %04x. ",
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

void proxy_client_ble_evt_handler(ble_evt_t const *ble_evt, void *context) {
  proxy_client_t *client = (proxy_client_t *)context;

  switch (ble_evt->header.evt_id) {
  case BLE_GATTC_EVT_HVX: //
  {
    const ble_gattc_evt_hvx_t hvx = ble_evt->evt.gattc_evt.params.hvx;

    if (hvx.handle != client->handles.data_out_handle) {
      break;
    }

    proxy_client_evt_t pending_evt = {
        .type = PROXY_CLIENT_EVT_DATA_IN,
        .params.data_in.data = (uint8_t *)hvx.data,
        .params.data_in.len = hvx.len,
    };

    client->evt_handler(&pending_evt);

    break;
  }

  default: //
  {
    break;
  }
  }
}
