#include "board.h"

void blinky_main(void)
{
    led_init();
    for (;;) {
        for (unsigned on = 0; on < 2; ++on) {
            led_set(on);
            for (volatile unsigned delay = 0; delay < 350000u; ++delay) {
                __asm volatile ("nop");
            }
        }
    }
}
