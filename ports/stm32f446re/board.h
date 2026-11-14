#ifndef BOARD_H
#define BOARD_H
#include <stdint.h>

#define APP_PACKAGE_BASE 0x08010000u
#define FLASH_END 0x08080000u
#define SRAM_BASE 0x20000000u
#define SRAM_END 0x20020000u
#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

static inline void led_init(void)
{
    REG32(0x40023830u) |= 1u;  /* RCC AHB1ENR, GPIOA */
    (void)REG32(0x40023830u);
    REG32(0x40020000u) = (REG32(0x40020000u) & ~(3u << 10)) | (1u << 10);
}

static inline void led_set(unsigned on)
{
    REG32(0x40020018u) = 1u << (on ? 5u : 21u);
}
#endif
