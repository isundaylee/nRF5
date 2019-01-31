#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"

void address_book_init(app_state_t *app_state);

bool address_book_remove(uint8_t const *uuid);
void address_book_add(uint8_t const *uuid, uint16_t addr,
                      uint8_t const *devkey);

size_t address_book_free_slot_count();
