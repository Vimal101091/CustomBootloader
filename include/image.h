#ifndef IMAGE_H
#define IMAGE_H
#include "boot_platform.h"

#define IMAGE_PREFIX_SIZE 512u
#define IMAGE_TARGET_F446RE 0x446u
boot_status image_verify(boot_image *image, uint32_t target,
                         const uint8_t public_key[32]);
#endif
