#include "address_book.h"

#include <string.h>

#include "app_error.h"
#include "device_state_manager.h"
#include "nrf_mesh.h"

#include "app_config.h"
#include "custom_log.h"
#include "protocol.h"

typedef struct {
  app_state_t *app_state;
} address_book_t;

address_book_t address_book;

////////////////////////////////////////////////////////////////////////////////

static void report_capacity() {
  protocol_send("address_book_capacity %d %d", address_book_free_slot_count(),
                APP_MAX_PROVISIONEES);
}

////////////////////////////////////////////////////////////////////////////////

void address_book_init(app_state_t *app_state) {
  address_book.app_state = app_state;
  report_capacity();
}

bool address_book_remove(uint8_t const *uuid) {
  // Remove the device from DSM if we have previously provisioned it to free up
  // space.
  for (int i = 0; i < APP_MAX_PROVISIONEES; i++) {
    if (memcmp(uuid,
               address_book.app_state->persistent.network.provisioned_uuids[i],
               NRF_MESH_UUID_SIZE) == 0) {
      uint16_t addr =
          address_book.app_state->persistent.network.provisioned_addrs[i];

      // We need to remove the previous devkey and publish address from DSM.
      dsm_handle_t address_handle;
      nrf_mesh_address_t address = {.type = NRF_MESH_ADDRESS_TYPE_UNICAST,
                                    .value = addr};
      APP_ERROR_CHECK(dsm_address_handle_get(&address, &address_handle));
      APP_ERROR_CHECK(dsm_address_publish_remove(address_handle));

      dsm_handle_t devkey_handle;
      APP_ERROR_CHECK(dsm_devkey_handle_get(addr, &devkey_handle));
      APP_ERROR_CHECK(dsm_devkey_delete(devkey_handle));

      address_book.app_state->persistent.network.provisioned_addrs[i] = 0;
      memset(address_book.app_state->persistent.network.provisioned_uuids[i], 0,
             NRF_MESH_UUID_SIZE);
      app_state_save();

      LOG_INFO("Address Book: Removed previous node with address %d. ", addr);

      report_capacity();

      return true;
    }
  }

  report_capacity();

  return false;
}

void address_book_add(uint8_t const *uuid, uint16_t addr,
                      uint8_t const *devkey) {
  for (int i = 0; i < APP_MAX_PROVISIONEES; i++) {
    if (address_book.app_state->persistent.network.provisioned_addrs[i] != 0) {
      continue;
    }

    address_book.app_state->persistent.network.provisioned_addrs[i] = addr;
    memcpy(address_book.app_state->persistent.network.provisioned_uuids[i],
           uuid, NRF_MESH_UUID_SIZE);
    app_state_save();

    dsm_handle_t addr_handle;
    dsm_handle_t devkey_handle;
    APP_ERROR_CHECK(dsm_address_publish_add(addr, &addr_handle));
    APP_ERROR_CHECK(dsm_devkey_add(
        addr, address_book.app_state->ephemeral.network.netkey_handle, devkey,
        &devkey_handle));

    LOG_INFO("Address Book: Added node with address %d. ", addr);

    report_capacity();

    return;
  }

  report_capacity();

  // If we're here, we have more than APP_MAX_PROVISIONEES provisionees.
  NRF_MESH_ASSERT(false);
}

size_t address_book_free_slot_count() {
  size_t count = 0;

  for (int i = 0; i < APP_MAX_PROVISIONEES; i++) {
    if (address_book.app_state->persistent.network.provisioned_addrs[i] == 0) {
      count++;
    }
  }

  return count;
}
