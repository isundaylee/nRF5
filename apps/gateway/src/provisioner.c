#include "provisioner.h"

#include "mesh_stack.h"

#include "nrf_mesh_prov.h"
#include "nrf_mesh_prov_bearer_adv.h"

#include "config_server.h"
#include "device_state_manager.h"

#include "rand.h"

#include "app_config.h"
#include "configurator.h"
#include "custom_log.h"
#include "enc.h"

typedef enum {
  PROV_STATE_IDLE,
  PROV_STATE_WAIT,
  PROV_STATE_COMPLETE,
  PROV_STATE_PROV,
  PROV_STATE_CONFIG,
} prov_state_t;

typedef struct {
  nrf_mesh_prov_ctx_t ctx;
  nrf_mesh_prov_bearer_adv_t bearer;

  prov_state_t state;

  uint8_t public_key[NRF_MESH_PROV_PUBKEY_SIZE];
  uint8_t private_key[NRF_MESH_PROV_PRIVKEY_SIZE];

  app_state_t *app_state;

  bool init_completed;

  prov_init_complete_cb_t init_complete_cb;
} prov_t;

prov_t prov;

void prov_config_node(uint16_t addr) {}

uint16_t prov_find_usable_addr() {
  for (int i = 2; i < 16; i++) {
    dsm_handle_t handle;
    uint32_t ret = dsm_devkey_handle_get(i, &handle);
    if (ret == NRF_ERROR_NOT_FOUND) {
      return i;
    } else {
      APP_ERROR_CHECK(ret);
    }
  }

  return 0xFFFF;
}

static uint8_t PROV_URI_BEACON[] = {'b', 'e', 'a', 'c', 'o', 'n'};
static uint8_t PROV_URI_WRISTBAND[] = {'w', 'r', 'i', 's', 't',
                                       'b', 'a', 'n', 'd'};

void prov_evt_handler(nrf_mesh_prov_evt_t const *evt) {
  switch (evt->type) {
  case NRF_MESH_PROV_EVT_UNPROVISIONED_RECEIVED: // 0
  {
    LOG_INFO("Detected unprovisioned device.");

    if (prov.state == PROV_STATE_WAIT) {
      uint16_t device_addr = prov_find_usable_addr();

      uint8_t hash_uri_beacon[NRF_MESH_BEACON_UNPROV_URI_HASH_SIZE];
      uint8_t hash_uri_wristband[NRF_MESH_BEACON_UNPROV_URI_HASH_SIZE];

      enc_s1(PROV_URI_BEACON, sizeof(PROV_URI_BEACON), hash_uri_beacon);
      enc_s1(PROV_URI_WRISTBAND, sizeof(PROV_URI_WRISTBAND),
             hash_uri_wristband);

      if (!evt->params.unprov.uri_hash_present) {
        LOG_INFO("Ignored unprovisioned device with no URI hash present. ");
        return;
      }
      if (memcmp(evt->params.unprov.uri_hash, hash_uri_beacon,
                 NRF_MESH_BEACON_UNPROV_URI_HASH_SIZE) != 0) {
        LOG_INFO("Provisioning a new Beacon node. ");
      } else if (memcmp(evt->params.unprov.uri_hash, hash_uri_wristband,
                        NRF_MESH_BEACON_UNPROV_URI_HASH_SIZE) != 0) {
        LOG_INFO("Provisioning a new Wristband node. ");
      } else {
        LOG_INFO("Ignored unprovisioned device with unknown URI hash: %d",
                 evt->params.unprov.uri_hash);
      }

      LOG_INFO("Starting to provision a device. Handing out address %d.",
               device_addr);

      nrf_mesh_prov_provisioning_data_t prov_data = {.netkey_index = 0,
                                                     .iv_index = 0,
                                                     .address = device_addr,
                                                     .flags.iv_update = false,
                                                     .flags.key_refresh =
                                                         false};
      memcpy(prov_data.netkey, prov.app_state->netkey, NRF_MESH_KEY_SIZE);

      APP_ERROR_CHECK(
          nrf_mesh_prov_provision(&prov.ctx, evt->params.unprov.device_uuid,
                                  &prov_data, NRF_MESH_PROV_BEARER_ADV));
      prov.state = PROV_STATE_PROV;
    }

    break;
  }

  case NRF_MESH_PROV_EVT_LINK_CLOSED: // 2
  {
    if (prov.state == PROV_STATE_PROV) {
      LOG_INFO("Provisioning failed. Code: %d.",
               evt->params.link_closed.close_reason);
      prov.state = PROV_STATE_WAIT;
    } else if (prov.state == PROV_STATE_COMPLETE) {
      LOG_INFO("Provisioning complete. ");
      prov.state = PROV_STATE_CONFIG;

      conf_start(evt->params.link_closed.p_context->data.address,
                 prov.app_state->appkey, APP_APPKEY_IDX);
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
    LOG_INFO("Static authentication provided successfully.");

    break;
  }

  case NRF_MESH_PROV_EVT_CAPS_RECEIVED: // 7
  {
    LOG_INFO("Received capabilities from device with %d elements.",
             evt->params.oob_caps_received.oob_caps.num_elements);

    uint32_t ret = nrf_mesh_prov_oob_use(
        &prov.ctx, NRF_MESH_PROV_OOB_METHOD_STATIC, 0, NRF_MESH_KEY_SIZE);

    if (ret != NRF_SUCCESS) {
      LOG_INFO("Static OOB rejected. ");
      prov.state = PROV_STATE_IDLE;
    } else {
      LOG_INFO("Static OOB chosen successfully. ");
    }

    break;
  }

  case NRF_MESH_PROV_EVT_COMPLETE: // 8
  {
    dsm_handle_t addr_handle;
    dsm_handle_t devkey_handle;

    LOG_INFO("Received provisioning completion event. ");

    prov.state = PROV_STATE_COMPLETE;

    APP_ERROR_CHECK(dsm_address_publish_add(
        evt->params.complete.p_prov_data->address, &addr_handle));
    APP_ERROR_CHECK(dsm_devkey_add(evt->params.complete.p_prov_data->address,
                                   prov.app_state->netkey_handle,
                                   evt->params.complete.p_devkey,
                                   &devkey_handle));

    break;
  }

  default:
    LOG_INFO("Received unhandled provisioning event: %d", evt->type);

    break;
  }
}

void prov_self_provision() {
  LOG_INFO("Doing self-provisioning. ");

  dsm_local_unicast_address_t local_address = {APP_GATEWAY_ADDR,
                                               ACCESS_ELEMENT_COUNT};
  APP_ERROR_CHECK(dsm_local_unicast_addresses_set(&local_address));

  // rand_hw_rng_get(prov.app_state->netkey, NRF_MESH_KEY_SIZE);
  // rand_hw_rng_get(prov.app_state->appkey, NRF_MESH_KEY_SIZE);
  rand_hw_rng_get(prov.app_state->devkey, NRF_MESH_KEY_SIZE);

  // Add the generated keys
  APP_ERROR_CHECK(dsm_subnet_add(0, prov.app_state->netkey,
                                 &prov.app_state->netkey_handle));
  APP_ERROR_CHECK(dsm_appkey_add(APP_APPKEY_IDX, prov.app_state->netkey_handle,
                                 prov.app_state->appkey,
                                 &prov.app_state->appkey_handle));
  APP_ERROR_CHECK(
      dsm_devkey_add(APP_GATEWAY_ADDR, prov.app_state->netkey_handle,
                     prov.app_state->devkey, &prov.app_state->devkey_handle));

  // Add the beacon publish address to DSM
  APP_ERROR_CHECK(dsm_address_subscription_add(
      APP_BEACON_PUBLISH_ADDRESS, &prov.app_state->beacon_addr_handle));

  // Add the gateway's address to DSM
  dsm_handle_t addr_handle;
  APP_ERROR_CHECK(dsm_address_publish_add(APP_GATEWAY_ADDR, &addr_handle));

  // Bind config server to use the devkey
  APP_ERROR_CHECK(config_server_bind(prov.app_state->devkey_handle));

  LOG_INFO("Self-provisioning finished. ");
}

void prov_restore() {
  LOG_INFO("Restoring handles. ");

  uint32_t count = 1;
  APP_ERROR_CHECK(dsm_subnet_get_all(&prov.app_state->netkey_handle, &count));
  NRF_MESH_ASSERT(count == 1);

  APP_ERROR_CHECK(dsm_appkey_get_all(prov.app_state->netkey_handle,
                                     &prov.app_state->appkey_handle, &count));
  NRF_MESH_ASSERT(count == 1);

  dsm_local_unicast_address_t local_addr;
  dsm_local_unicast_addresses_get(&local_addr);
  APP_ERROR_CHECK(dsm_devkey_handle_get(local_addr.address_start,
                                        &prov.app_state->devkey_handle));

  nrf_mesh_address_t addr = {
      .type = NRF_MESH_ADDRESS_TYPE_GROUP,
      .value = APP_BEACON_PUBLISH_ADDRESS,
  };
  APP_ERROR_CHECK(
      dsm_address_handle_get(&addr, &prov.app_state->beacon_addr_handle));

  LOG_INFO("Restoring finished. ");
  LOG_INFO(
      "prov.app_state->netkey_handle = %d, prov.app_state->appkey_handle = %d, "
      "prov.app_state->devkey_handle = %d",
      prov.app_state->netkey_handle, prov.app_state->appkey_handle,
      prov.app_state->devkey_handle);
}

void prov_conf_success_cb(uint16_t node_addr) {
  LOG_INFO("Provisioner is ready for the next device. ");
  prov.state = PROV_STATE_WAIT;

  if (node_addr == APP_GATEWAY_ADDR) {
    NRF_MESH_ASSERT(!prov.init_completed);

    prov.init_completed = true;
    prov.init_complete_cb();

    LOG_INFO("Finished configuring the gateway node. ");
  }
}

void prov_conf_failure_cb(uint16_t node_addr) {
  LOG_INFO("Provisioner has failed to config the current device. ");
  prov.state = PROV_STATE_WAIT;

  if (node_addr == APP_GATEWAY_ADDR) {
    LOG_INFO("Failed to configure the gateway node. ");
    APP_ERROR_CHECK(NRF_ERROR_NOT_FOUND);
  }
}

void prov_init(app_state_t *app_state, prov_init_complete_cb_t complete_cb) {
  nrf_mesh_prov_oob_caps_t caps =
      NRF_MESH_PROV_OOB_CAPS_DEFAULT(ACCESS_ELEMENT_COUNT);

  prov.state = PROV_STATE_IDLE;
  prov.app_state = app_state;
  prov.init_completed = false;
  prov.init_complete_cb = complete_cb;

  APP_ERROR_CHECK(
      nrf_mesh_prov_generate_keys(prov.public_key, prov.private_key));
  APP_ERROR_CHECK(nrf_mesh_prov_init(
      &prov.ctx, prov.public_key, prov.private_key, &caps, prov_evt_handler));
  APP_ERROR_CHECK(nrf_mesh_prov_bearer_add(
      &prov.ctx, nrf_mesh_prov_bearer_adv_interface_get(&prov.bearer)));

  prov.app_state->netkey[0] = 0x30;
  prov.app_state->netkey[1] = 0x6A;
  prov.app_state->netkey[2] = 0xAA;
  prov.app_state->netkey[3] = 0xCB;
  prov.app_state->netkey[4] = 0x4A;
  prov.app_state->netkey[5] = 0x66;
  prov.app_state->netkey[6] = 0xC4;
  prov.app_state->netkey[7] = 0x7A;
  prov.app_state->netkey[8] = 0xAC;
  prov.app_state->netkey[9] = 0x4A;
  prov.app_state->netkey[10] = 0xEC;
  prov.app_state->netkey[11] = 0xEE;
  prov.app_state->netkey[12] = 0xBD;
  prov.app_state->netkey[13] = 0x86;
  prov.app_state->netkey[14] = 0x70;
  prov.app_state->netkey[15] = 0x5F;

  prov.app_state->appkey[0] = 0xA5;
  prov.app_state->appkey[1] = 0x73;
  prov.app_state->appkey[2] = 0x0D;
  prov.app_state->appkey[3] = 0x76;
  prov.app_state->appkey[4] = 0x9B;
  prov.app_state->appkey[5] = 0x28;
  prov.app_state->appkey[6] = 0x1F;
  prov.app_state->appkey[7] = 0x7C;
  prov.app_state->appkey[8] = 0x9C;
  prov.app_state->appkey[9] = 0xDE;
  prov.app_state->appkey[10] = 0x91;
  prov.app_state->appkey[11] = 0x2F;
  prov.app_state->appkey[12] = 0xAC;
  prov.app_state->appkey[13] = 0x79;
  prov.app_state->appkey[14] = 0x6E;
  prov.app_state->appkey[15] = 0xDC;

  bool provisioned = mesh_stack_is_device_provisioned();
  if (provisioned) {
    prov_restore();
  } else {
    prov_self_provision();
  }

  conf_init(prov.app_state, prov_conf_success_cb, prov_conf_failure_cb);

  if (!provisioned) {
    conf_start(APP_GATEWAY_ADDR, prov.app_state->appkey, APP_APPKEY_IDX);
  } else {
    prov.init_completed = true;
    prov.init_complete_cb();
  }

  LOG_INFO("Provisioning initialized.");
}

void prov_start_scan() {
  prov.state = PROV_STATE_WAIT;

  APP_ERROR_CHECK(nrf_mesh_prov_scan_start(prov_evt_handler));
  LOG_INFO("Provisioning started scanning.");
}
