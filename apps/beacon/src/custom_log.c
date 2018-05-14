#include "custom_log.h"

void log_callback_custom(uint32_t dbg_level, const char *p_filename,
                         uint16_t line, uint32_t timestamp, const char *format,
                         va_list arguments) {
  char const *level_str;

  if (dbg_level == LOG_LEVEL_INFO) {
    level_str = "   INFO    ";
  } else if (dbg_level == LOG_LEVEL_ERROR) {
    level_str = "!! ERROR !!";
  } else {
    level_str = "***********";
  }

  SEGGER_RTT_printf(0, "[%7u.%03u] [%s] ", timestamp / 32768,
                    (timestamp % 32768) * 1000 / 32768, level_str);
  SEGGER_RTT_vprintf(0, format, &arguments);

  if (format[strlen(format) - 1] != '\n') {
    SEGGER_RTT_printf(0, "\n");
  }
}
