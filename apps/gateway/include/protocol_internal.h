#pragma once

#include "protocol.h"

extern protocol_request_handler_t protocol_request_handler;

void protocol_post_rx_char(char c);
bool protocol_send_raw(char const *buf, size_t len, bool guaranteed);
