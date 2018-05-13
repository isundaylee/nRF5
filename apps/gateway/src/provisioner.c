#include "provisioner.h"

#include "mesh_stack.h"

#include "nrf_mesh_prov.h"
#include "nrf_mesh_prov_bearer_adv.h"

#include "device_state_manager.h"

#include "rand.h"

#include "configurator.h"
#include "custom_log.h"

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

  uint8_t netkey[NRF_MESH_KEY_SIZE];
  uint8_t appkey[NRF_MESH_KEY_SIZE];
  uint8_t devkey[NRF_MESH_KEY_SIZE];

  dsm_handle_t netkey_handle;
  dsm_handle_t devkey_handle;
  dsm_handle_t appkey_handle;
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

void prov_evt_handler(nrf_mesh_prov_evt_t const *evt) {
  switch (evt->type) {
  case NRF_MESH_PROV_EVT_UNPROVISIONED_RECEIVED: // 0
  {
    LOG_INFO("Detected unprovisioned device.");

    if (prov.state == PROV_STATE_WAIT) {
      uint16_t device_addr = prov_find_usable_addr();

      LOG_INFO("Starting to provision a device. Handing out address %d.",
               device_addr);

      nrf_mesh_prov_provisioning_data_t prov_data = {.netkey_index = 0,
                                                     .iv_index = 0,
                                                     .address = device_addr,
                                                     .flags.iv_update = false,
                                                     .flags.key_refresh =
                                                         false};
      memcpy(prov_data.netkey, prov.netkey, NRF_MESH_KEY_SIZE);

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

      conf_start(evt->params.link_closed.p_context->data.address, prov.appkey,
                 0);
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
    APP_ERROR_CHECK(dsm_devkey_add(
        evt->params.complete.p_prov_data->address, prov.netkey_handle,
        evt->params.complete.p_devkey, &devkey_handle));

    break;
  }

  default:
    LOG_INFO("Received unhandled provisioning event: %d", evt->type);

    break;
  }
}

void prov_self_provision() {
  LOG_INFO("Doing self-provisioning. ");

  dsm_local_unicast_address_t local_address = {0x0001, ACCESS_ELEMENT_COUNT};
  APP_ERROR_CHECK(dsm_local_unicast_addresses_set(&local_address));

  // rand_hw_rng_get(prov.netkey, NRF_MESH_KEY_SIZE);
  // rand_hw_rng_get(prov.appkey, NRF_MESH_KEY_SIZE);
  rand_hw_rng_get(prov.devkey, NRF_MESH_KEY_SIZE);

  LOG_INFO("App key is: ");
  LOG_INFO("%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%"
           "02x:%02x:%02x",
           prov.appkey[0], prov.appkey[1], prov.appkey[2], prov.appkey[3],
           prov.appkey[4], prov.appkey[5], prov.appkey[6], prov.appkey[7],
           prov.appkey[8], prov.appkey[9], prov.appkey[10], prov.appkey[11],
           prov.appkey[12], prov.appkey[13], prov.appkey[14], prov.appkey[15]);

  APP_ERROR_CHECK(dsm_subnet_add(0, prov.netkey, &prov.netkey_handle));
  APP_ERROR_CHECK(
      dsm_appkey_add(0, prov.netkey_handle, prov.appkey, &prov.appkey_handle));
  APP_ERROR_CHECK(dsm_devkey_add(0x0001, prov.netkey_handle, prov.devkey,
                                 &prov.devkey_handle));

  LOG_INFO("Self-provisioning finished. ");
  LOG_INFO("prov.netkey_handle = %d, prov.appkey_handle = %d, "
           "prov.devkey_handle = %d",
           prov.netkey_handle, prov.appkey_handle, prov.devkey_handle);
}

void prov_restore() {
  LOG_INFO("Restoring handles. ");

  uint32_t count = 1;
  APP_ERROR_CHECK(dsm_subnet_get_all(&prov.netkey_handle, &count));
  NRF_MESH_ASSERT(count == 1);

  APP_ERROR_CHECK(
      dsm_appkey_get_all(prov.netkey_handle, &prov.appkey_handle, &count));
  NRF_MESH_ASSERT(count == 1);

  dsm_local_unicast_address_t local_addr;
  dsm_local_unicast_addresses_get(&local_addr);
  APP_ERROR_CHECK(
      dsm_devkey_handle_get(local_addr.address_start, &prov.devkey_handle));

  LOG_INFO("Restoring finished. ");
  LOG_INFO("prov.netkey_handle = %d, prov.appkey_handle = %d, "
           "prov.devkey_handle = %d",
           prov.netkey_handle, prov.appkey_handle, prov.devkey_handle);
}

void prov_conf_success_cb() {
  LOG_INFO("Provisioner is ready for the next device. ");
  prov.state = PROV_STATE_WAIT;
}

void prov_conf_failure_cb() {
  LOG_INFO("Provisioner has failed to config the current device. ");
  prov.state = PROV_STATE_WAIT;
}

void prov_init() {
  nrf_mesh_prov_oob_caps_t caps =
      NRF_MESH_PROV_OOB_CAPS_DEFAULT(ACCESS_ELEMENT_COUNT);

  prov.state = PROV_STATE_IDLE;

  APP_ERROR_CHECK(
      nrf_mesh_prov_generate_keys(prov.public_key, prov.private_key));
  APP_ERROR_CHECK(nrf_mesh_prov_init(
      &prov.ctx, prov.public_key, prov.private_key, &caps, prov_evt_handler));
  APP_ERROR_CHECK(nrf_mesh_prov_bearer_add(
      &prov.ctx, nrf_mesh_prov_bearer_adv_interface_get(&prov.bearer)));

  prov.netkey[0] = 0x30;
  prov.netkey[1] = 0x6A;
  prov.netkey[2] = 0xAA;
  prov.netkey[3] = 0xCB;
  prov.netkey[4] = 0x4A;
  prov.netkey[5] = 0x66;
  prov.netkey[6] = 0xC4;
  prov.netkey[7] = 0x7A;
  prov.netkey[8] = 0xAC;
  prov.netkey[9] = 0x4A;
  prov.netkey[10] = 0xEC;
  prov.netkey[11] = 0xEE;
  prov.netkey[12] = 0xBD;
  prov.netkey[13] = 0x86;
  prov.netkey[14] = 0x70;
  prov.netkey[15] = 0x5F;

  prov.appkey[0] = 0xA5;
  prov.appkey[1] = 0x73;
  prov.appkey[2] = 0x0D;
  prov.appkey[3] = 0x76;
  prov.appkey[4] = 0x9B;
  prov.appkey[5] = 0x28;
  prov.appkey[6] = 0x1F;
  prov.appkey[7] = 0x7C;
  prov.appkey[8] = 0x9C;
  prov.appkey[9] = 0xDE;
  prov.appkey[10] = 0x91;
  prov.appkey[11] = 0x2F;
  prov.appkey[12] = 0xAC;
  prov.appkey[13] = 0x79;
  prov.appkey[14] = 0x6E;
  prov.appkey[15] = 0xDC;

  if (mesh_stack_is_device_provisioned()) {
    prov_restore();
  } else {
    prov_self_provision();
  }

  conf_init(prov.appkey, prov_conf_success_cb, prov_conf_failure_cb);

  LOG_INFO("Provisioning initialized.");
}

void prov_start_scan() {
  prov.state = PROV_STATE_WAIT;

  APP_ERROR_CHECK(nrf_mesh_prov_scan_start(prov_evt_handler));
  LOG_INFO("Provisioning started scanning.");
}
