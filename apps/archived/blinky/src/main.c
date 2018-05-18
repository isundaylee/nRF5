#include <stdbool.h>
#include <stdint.h>
#include "nrf_delay.h"
#include "boards.h"

int main(void)
{
    bsp_board_init(BSP_INIT_LEDS);

    while (true)
    {
        bsp_board_led_invert(0);
        nrf_delay_ms(500);
    }
}
