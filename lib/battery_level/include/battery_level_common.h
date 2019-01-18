#pragma once

#include <stdint.h>

#define BATTERY_LEVEL_COMPANY_ID 0xBEEE

typedef struct {
  uint16_t level;
} battery_level_state_t;

typedef struct {
  uint16_t level;
} battery_level_status_params_t;
