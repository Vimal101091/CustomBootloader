#include "board.h"
#include "image.h"
#include "public_key.h"

extern _Noreturn void jump_to_application(uint32_t stack, uint32_t reset);
volatile boot_status boot_failure;

static boot_status find_image(boot_image *image)
{
    if (REG32(APP_PACKAGE_BASE) == UINT32_MAX) {
        return BOOT_NO_IMAGE;
    }
    image->bytes = (const uint8_t *)(uintptr_t)APP_PACKAGE_BASE;
    image->capacity = FLASH_END - APP_PACKAGE_BASE;
    return BOOT_OK;
}

static boot_status verify_image(boot_image *image)
{
    return image_verify(image, IMAGE_TARGET_F446RE, boot_public_key);
}

static boot_status start_image(const boot_image *image)
{
    uint32_t vectors = APP_PACKAGE_BASE + IMAGE_PREFIX_SIZE;
    uint32_t stack = REG32(vectors);
    uint32_t reset = REG32(vectors + 4u);
    uint32_t entry = reset & ~1u;
    if (stack <= SRAM_BASE || stack > SRAM_END || (stack & 7u) != 0
        || (reset & 1u) == 0 || entry < vectors + 8u
        || entry - vectors > image->payload_size - 2u) {
        return BOOT_BAD_ENTRY;
    }

    /* This loader uses polling and starts no DMA or timers. */
    __asm volatile ("cpsid i" ::: "memory");
    REG32(0xe000e010u) = 0;  /* Stop SysTick. */
    REG32(0xe000ed04u) = (1u << 25) | (1u << 27);
    for (unsigned bank = 0; bank < 3; ++bank) {
        REG32(0xe000e180u + 4u * bank) = UINT32_MAX;
        REG32(0xe000e280u + 4u * bank) = UINT32_MAX;
    }
    REG32(0xe000ed08u) = vectors;
    jump_to_application(stack, reset);
}

static void recover(boot_status reason)
{
    boot_failure = reason;  /* Inspect in the debugger. */
    led_init();
    led_set(1);
    for (;;) {
        __asm volatile ("wfi");
    }
}

void bootloader_main(void)
{
    static const boot_platform platform = {
        find_image, verify_image, start_image, recover
    };
    boot_run(&platform);
    recover(BOOT_BAD_ENTRY);
}
