#pragma once

#include <app_uart.h>
#include <nrf_uart.h>

uint32_t protocol_init(uint32_t tx_pin, uint32_t rx_pin);

void protocol_send(char const *fmt, ...);
