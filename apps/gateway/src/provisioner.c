#include "provisioner.h"

#include "mesh_stack.h"

#include "nrf_mesh_configure.h"
#include "nrf_mesh_prov.h"
#include "nrf_mesh_prov_bearer_adv.h"

#include "config_server.h"
#include "device_state_manager.h"

#include "rand.h"

#include "address_book.h"
#include "app_config.h"
#include "custom_log.h"

typedef enum {
  PROV_STATE_IDLE,
  PROV_STATE_SCANNING,
  PROV_STATE_PROV,
  PROV_STATE_COMPLETE,
} prov_state_t;

typedef struct {
  nrf_mesh_prov_ctx_t ctx;
  nrf_mesh_prov_bearer_adv_t bearer;

  prov_state_t state;
  uint16_t device_addr;
  uint8_t device_uuid[NRF_MESH_UUID_SIZE];

  uint8_t public_key[NRF_MESH_PROV_PUBKEY_SIZE];
  uint8_t private_key[NRF_MESH_PROV_PRIVKEY_SIZE];

  prov_success_cb_t success_cb;
  prov_failure_cb_t failure_cb;

  app_state_t *app_state;
} prov_t;

prov_t prov;

void prov_reset_state() {
  prov.device_addr = 0;
  prov.state = PROV_STATE_IDLE;
}

void prov_evt_handler(nrf_mesh_prov_evt_t const *evt) {
  switch (evt->type) {
  case NRF_MESH_PROV_EVT_UNPROVISIONED_RECEIVED: // 0
  {
    LOG_INFO("Provisioner: Detected unprovisioned device.");

    if (prov.state == PROV_STATE_SCANNING) {
      prov.device_addr =
          prov.app_state->persistent.network.next_provisionee_addr++;
      memcpy(prov.device_uuid, evt->params.unprov.device_uuid,
             NRF_MESH_UUID_SIZE);
      address_book_remove(prov.device_uuid);
      app_state_save();

      LOG_INFO("Provisioner: Starting to provision a device to address %d.",
               prov.device_addr);

      nrf_mesh_prov_provisioning_data_t prov_data = {
          .netkey_index = APP_NETKEY_IDX,
          .iv_index = 0,
          .address = prov.device_addr,
          .flags.iv_update = false,
          .flags.key_refresh = false};
      memcpy(prov_data.netkey, prov.app_state->persistent.network.netkey,
             NRF_MESH_KEY_SIZE);

      APP_ERROR_CHECK(
          nrf_mesh_prov_provision(&prov.ctx, evt->params.unprov.device_uuid,
                                  &prov_data, NRF_MESH_PROV_BEARER_ADV));
      prov.state = PROV_STATE_PROV;
    }

    break;
  }

  case NRF_MESH_PROV_EVT_LINK_ESTABLISHED: // 1
  {
    // We don't care
    break;
  }

  case NRF_MESH_PROV_EVT_LINK_CLOSED: // 2
  {
    if (prov.state == PROV_STATE_PROV) {
      LOG_ERROR("Provisioner: Provisioning failed. Code: %d.",
                evt->params.link_closed.close_reason);

      prov_reset_state();
      prov.failure_cb();
    } else if (prov.state == PROV_STATE_COMPLETE) {
      LOG_INFO("Provisioner: Provisioning complete. ");

      uint16_t device_addr = prov.device_addr;
      prov_reset_state();
      prov.success_cb(device_addr);
    }

    break;
  }

  case NRF_MESH_PROV_EVT_STATIC_REQUEST: // 5
  {
    const uint8_t static_data[NRF_MESH_KEY_SIZE] = {
        0x6E, 0x6F, 0x72, 0x64, 0x69, 0x63, 0x5F, 0x65,
        0x78, 0x61, 0x6D, 0x70, 0x6C, 0x65, 0x5F, 0x31};

    APP_ERROR_CHECK(nrf_mesh_prov_auth_data_provide(&prov.ctx, static_data,
                                                    NRF_MESH_KEY_SIZE));
    LOG_INFO("Provisioner: Static authentication provided successfully.");

    break;
  }

  case NRF_MESH_PROV_EVT_CAPS_RECEIVED: // 7
  {
    LOG_INFO("Provisioner: Received capabilities from device with %d elements.",
             evt->params.oob_caps_received.oob_caps.num_elements);

    uint32_t ret = nrf_mesh_prov_oob_use(
        &prov.ctx, NRF_MESH_PROV_OOB_METHOD_STATIC, 0, NRF_MESH_KEY_SIZE);

    if (ret != NRF_SUCCESS) {
      LOG_ERROR("Provisioner: Static OOB rejected. ");

      prov_reset_state();
      prov.failure_cb();
    } else {
      LOG_INFO("Provisioner: Static OOB chosen successfully. ");
    }

    break;
  }

  case NRF_MESH_PROV_EVT_COMPLETE: // 8
  {

    prov.state = PROV_STATE_COMPLETE;

    address_book_add(prov.device_uuid, prov.device_addr,
                     evt->params.complete.p_devkey);

    break;
  }

  case NRF_MESH_PROV_EVT_FAILED: // 10
  {
    LOG_ERROR("Provisioner: Provisioning failed: %d",
              evt->params.failed.failure_code);
  }

  default:
    LOG_ERROR("Provisioner: Received unhandled provisioning event: %d",
              evt->type);

    break;
  }
}

void prov_self_provision() {
  dsm_local_unicast_address_t local_address = {APP_GATEWAY_ADDR,
                                               ACCESS_ELEMENT_COUNT};
  APP_ERROR_CHECK(dsm_local_unicast_addresses_set(&local_address));

  // Set next provisionee addr to start at 2
  prov.app_state->persistent.network.next_provisionee_addr = 2;

  // Generate keys
  rand_hw_rng_get(prov.app_state->persistent.network.netkey, NRF_MESH_KEY_SIZE);
  rand_hw_rng_get(prov.app_state->persistent.network.appkey, NRF_MESH_KEY_SIZE);
  app_state_save();

  uint8_t devkey[NRF_MESH_KEY_SIZE];
  rand_hw_rng_get(devkey, NRF_MESH_KEY_SIZE);

  // Add generated keys
  APP_ERROR_CHECK(
      dsm_subnet_add(APP_NETKEY_IDX, prov.app_state->persistent.network.netkey,
                     &prov.app_state->ephemeral.network.netkey_handle));
  APP_ERROR_CHECK(dsm_appkey_add(
      APP_APPKEY_IDX, prov.app_state->ephemeral.network.netkey_handle,
      prov.app_state->persistent.network.appkey,
      &prov.app_state->ephemeral.network.appkey_handle));
  APP_ERROR_CHECK(dsm_devkey_add(
      APP_GATEWAY_ADDR, prov.app_state->ephemeral.network.netkey_handle, devkey,
      &prov.app_state->ephemeral.network.devkey_handle));

  // Add the gateway's address to DSM
  dsm_handle_t addr_handle;
  APP_ERROR_CHECK(dsm_address_publish_add(APP_GATEWAY_ADDR, &addr_handle));

  // Bind config server to use the devkey
  APP_ERROR_CHECK(
      config_server_bind(prov.app_state->ephemeral.network.devkey_handle));

  LOG_INFO("Provisioner: Self-provisioning finished. ");
}

void prov_restore() {
  // Restore the netkey handle
  uint32_t count = 1;
  APP_ERROR_CHECK(dsm_subnet_get_all(
      &prov.app_state->ephemeral.network.netkey_handle, &count));
  NRF_MESH_ASSERT(count == 1);

  // Restore the appkey handle
  APP_ERROR_CHECK(dsm_appkey_get_all(
      prov.app_state->ephemeral.network.netkey_handle,
      &prov.app_state->ephemeral.network.appkey_handle, &count));
  NRF_MESH_ASSERT(count == 1);

  // Restore the devkey handle
  dsm_local_unicast_address_t local_addr;
  dsm_local_unicast_addresses_get(&local_addr);
  APP_ERROR_CHECK(
      dsm_devkey_handle_get(local_addr.address_start,
                            &prov.app_state->ephemeral.network.devkey_handle));

  LOG_INFO("Provisioner: Provisioner restored. ");
}

void prov_init(app_state_t *app_state, prov_success_cb_t success_cb,
               prov_failure_cb_t failure_cb) {
  nrf_mesh_prov_oob_caps_t caps =
      NRF_MESH_PROV_OOB_CAPS_DEFAULT(ACCESS_ELEMENT_COUNT);

  prov.state = PROV_STATE_IDLE;
  prov.app_state = app_state;
  prov.success_cb = success_cb;
  prov.failure_cb = failure_cb;

  // Initialize the provisioner and adv bearer
  APP_ERROR_CHECK(
      nrf_mesh_prov_generate_keys(prov.public_key, prov.private_key));
  APP_ERROR_CHECK(nrf_mesh_prov_init(
      &prov.ctx, prov.public_key, prov.private_key, &caps, prov_evt_handler));
  APP_ERROR_CHECK(nrf_mesh_prov_bearer_add(
      &prov.ctx, nrf_mesh_prov_bearer_adv_interface_get(&prov.bearer)));

  if (mesh_stack_is_device_provisioned()) {
    prov_restore();
  } else {
    prov_self_provision();
  }

  __LOG_XB(LOG_SRC_APP, LOG_LEVEL_INFO,
           "netkey is: ", prov.app_state->persistent.network.netkey, 16);

  address_book_init(prov.app_state);
  app_state_save();

  LOG_INFO("Provisioner: Provisioning initialized. ");
}

void prov_start_scan() {
  prov.state = PROV_STATE_SCANNING;

  APP_ERROR_CHECK(nrf_mesh_prov_scan_start(prov_evt_handler));
  LOG_INFO("Provisioner: Provisioning started scanning.");
}
