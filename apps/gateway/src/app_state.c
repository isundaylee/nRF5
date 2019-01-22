#include "app_state.h"

#include <app_error.h>
#include <flash_manager.h>

#include "custom_log.h"

#define APP_STATE_FLASH_ENTRY_HANDLE 0x0001

app_state_t app_state;
app_persistent_state_t app_state_last_persisted;

flash_manager_t app_state_flash_manager;

void app_state_write_complete_cb(flash_manager_t const *flash_manager,
                                 fm_entry_t const *entry, fm_result_t result) {
  NRF_MESH_ASSERT(result == FM_RESULT_SUCCESS);
}

void app_state_remove_complete_cb() {}

void app_state_invalidate_complete_cb(flash_manager_t const *flash_manager,
                                      uint16_t x, fm_result_t result) {
  NRF_MESH_ASSERT(false);
}

void app_state_init(void) {
  flash_manager_config_t manager_config;

  manager_config.p_area =
      (const flash_manager_page_t *)(((const uint8_t *)dsm_flash_area_get()) -
                                     (ACCESS_FLASH_PAGE_COUNT * PAGE_SIZE * 2));
  manager_config.page_count = 1;
  manager_config.min_available_space = WORD_SIZE;

  manager_config.write_complete_cb = app_state_write_complete_cb;
  manager_config.remove_complete_cb = app_state_remove_complete_cb;
  manager_config.invalidate_complete_cb = app_state_invalidate_complete_cb;

  APP_ERROR_CHECK(flash_manager_add(&app_state_flash_manager, &manager_config));
  LOG_INFO("App State: App flash manager added. ");
}

bool app_state_load(void) {
  flash_manager_wait();

  uint32_t read_size = sizeof(app_state.persistent);
  uint32_t err = flash_manager_entry_read(&app_state_flash_manager,
                                          APP_STATE_FLASH_ENTRY_HANDLE,
                                          &app_state.persistent, &read_size);

  if (err == NRF_ERROR_NOT_FOUND) {
    LOG_INFO("App State: App flash did not find previous state. ");
    memset(&app_state.persistent, 0, sizeof(app_persistent_state_t));
    memset(&app_state_last_persisted, 0, sizeof(app_persistent_state_t));
    return false;
  } else {
    APP_ERROR_CHECK(err);
    LOG_INFO("App State: App flash finished reading previous data. ");
    return true;
  }
}

typedef void (*app_state_flash_op_t)(void);
void app_state_flash_mem_cb(void *args) { ((app_state_flash_op_t)args)(); }

void app_state_save(void) {
  static fm_mem_listener_t mem_listener = {.callback = app_state_flash_mem_cb,
                                           .p_args = app_state_save};

  if (memcmp(&app_state.persistent, &app_state_last_persisted,
             sizeof(app_persistent_state_t)) == 0) {
    return;
  }

  fm_entry_t *entry = flash_manager_entry_alloc(&app_state_flash_manager,
                                                APP_STATE_FLASH_ENTRY_HANDLE,
                                                sizeof(app_persistent_state_t));
  if (entry == NULL) {
    LOG_ERROR(
        "App State: Cannot store app persistent state. Trying again later. ");
    flash_manager_mem_listener_register(&mem_listener);
  } else {
    memcpy(entry->data, &app_state.persistent, sizeof(app_persistent_state_t));
    memcpy(&app_state_last_persisted, &app_state.persistent,
           sizeof(app_persistent_state_t));
    flash_manager_entry_commit(entry);

    LOG_INFO("App State: App persistent state stored. ");
  }
}

void app_state_clear(void) {
  memset(&app_state.persistent, 0, sizeof(app_persistent_state_t));

  app_state_save();

  LOG_INFO("App State: App persistent state cleared. ");
}
