#pragma once

#include <stdint.h>

#define BATTERY_LEVEL_STATUS_MINLEN 2
#define BATTERY_LEVEL_STATUS_MAXLEN 2

typedef enum {
  BATTERY_LEVEL_OPCODE_STATUS = 0b11000000 + 0x01,
} battery_level_opcode_t;

typedef struct __attribute((packed)) {
  uint16_t level;
} battery_level_status_msg_pkt_t;
