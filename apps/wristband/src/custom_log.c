#include "custom_log.h"

void log_callback_custom(uint32_t dbg_level, const char *p_filename,
                                uint16_t line, uint32_t timestamp,
                                const char *format, va_list arguments) {
  SEGGER_RTT_printf(0, "[%10u] ", timestamp);
  SEGGER_RTT_vprintf(0, format, &arguments);

  if (format[strlen(format) - 1] != '\n') {
    SEGGER_RTT_printf(0, "\n");
  }
}
