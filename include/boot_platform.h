#ifndef BOOT_PLATFORM_H
#define BOOT_PLATFORM_H
#include <stddef.h>
#include <stdint.h>

typedef enum {
    BOOT_OK, BOOT_NO_IMAGE, BOOT_BAD_HEADER, BOOT_WRONG_TARGET,
    BOOT_BAD_SIGNATURE, BOOT_BAD_DIGEST, BOOT_BAD_ENTRY
} boot_status;

typedef struct {
    const uint8_t *bytes;
    size_t capacity;
    size_t payload_size;
} boot_image;

typedef struct {
    boot_status (*find_image)(boot_image *image);
    boot_status (*verify_image)(boot_image *image);
    boot_status (*start_image)(const boot_image *image);
    void (*recover)(boot_status reason);
} boot_platform;

boot_status boot_run(const boot_platform *platform);
#endif
