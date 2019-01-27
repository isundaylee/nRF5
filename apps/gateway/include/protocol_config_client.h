#pragma once

#include <stdint.h>
#include <stdbool.h>

uint32_t protocol_config_client_init();

bool protocol_config_client_handle_request(char const* op, char const* params);
bool protocol_config_client_handle_conf_success(uint16_t addr);
bool protocol_config_client_handle_conf_failure(uint16_t addr, uint32_t err);
