#include "custom_log.h"

#include "app_config.h"

#if APP_CUSTOM_LOG_USE_PROTOCOL
#include "protocol.h"
#endif

void log_callback_custom(uint32_t dbg_level, const char *p_filename,
                         uint16_t line, uint32_t timestamp, const char *format,
                         va_list arguments) {
  char const *level_str;
  char buf[512];

  if (dbg_level == LOG_LEVEL_INFO) {
    level_str = "   INFO    ";
  } else if (dbg_level == LOG_LEVEL_ERROR) {
    level_str = "!! ERROR !!";
  } else {
    level_str = "***********";
  }

  char *cursor = buf;
  bool send_on_protocol = true;

  if (strcmp(format, "noproto") == 0) {
    send_on_protocol = false;
    format += strlen("noproto") + 1;
  }

  // Print header
  int ret = snprintf(cursor, buf + sizeof(buf) - cursor, "[%7u.%03u] [%s] ",
                     timestamp / 32768, (timestamp % 32768) * 1000 / 32768,
                     level_str);
  if (ret < 0 || ret >= (buf + sizeof(buf) - cursor)) {
    return;
  }
  cursor += ret;

  // Print log message
  ret = vsnprintf(cursor, buf + sizeof(buf) - cursor, format, arguments);
  if (ret < 0 || ret >= (buf + sizeof(buf) - cursor)) {
    return;
  }
  cursor += ret;

  if (format[strlen(format) - 1] != '\n') {
    SEGGER_RTT_printf(0, "%s\n", buf);
#if APP_CUSTOM_LOG_USE_PROTOCOL
    if (send_on_protocol) {
      protocol_send("log %s", buf);
    }
#endif
  } else {
    SEGGER_RTT_printf(0, "%s", buf);
#if APP_CUSTOM_LOG_USE_PROTOCOL
    if (send_on_protocol) {
      *(--cursor) = '\0';
      protocol_send("log %s", buf);
    }
#endif
  }
}
