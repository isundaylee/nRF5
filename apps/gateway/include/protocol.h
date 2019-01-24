#pragma once

#include <app_uart.h>
#include <nrf_uart.h>

typedef void (*protocol_request_handler_t)(char const *op, char const *params);

uint32_t protocol_init(uint32_t tx_pin, uint32_t rx_pin,
                       protocol_request_handler_t request_handler);
uint32_t protocol_start();

void protocol_send(char const *fmt, ...);
void protocol_reply(uint32_t err, char const *fmt, ...);

void protocol_process();
