#pragma once

#include "access.h"
#include <stdint.h>

#define ECARE_COMPANY_ID (0xBAFE)

typedef struct __attribute((packed)) {
  bool fallen;
  int x;
  int y;
  int z;
} ecare_state_t;

typedef enum {
  ECARE_OPCODE_SET = 0xC1,
  ECARE_OPCODE_SET_UNRELIABLE = 0xC2,
  ECARE_OPCODE_STATUS = 0xC3
} ecare_opcode_t;

typedef struct __attribute((packed)) {
  ecare_state_t state;
  uint8_t tid;
} ecare_msg_set_t;

typedef struct __attribute((packed)) {
  ecare_state_t state;
  uint8_t tid;
} ecare_msg_set_unreliable_t;

typedef struct __attribute((packed)) {
  ecare_state_t state;
} ecare_msg_status_t;
