#pragma once

#include <stdint.h>

void conf_init();
void conf_start(uint16_t node_addr, uint8_t const *appkey, uint16_t appkey_idx);
