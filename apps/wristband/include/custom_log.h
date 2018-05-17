#pragma once

#include <string.h>

#include "log.h"
#include "nrf_log.h"

#include <SEGGER_RTT.h>

#define LOG_INFO(...) __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_ERROR(...) __LOG(LOG_SRC_APP, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_INIT() __LOG_INIT(LOG_SRC_APP, LOG_LEVEL_INFO, log_callback_custom)

#define LOG_READING_FLOAT(name, value)                                         \
  LOG_INFO("READING " name " " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(value))

/* For some reason, this function, while not static, is not included in the RTT
 * header files. */
int SEGGER_RTT_vprintf(unsigned, const char *, va_list *);

void log_callback_custom(uint32_t dbg_level, const char *p_filename,
                         uint16_t line, uint32_t timestamp, const char *format,
                         va_list arguments);
