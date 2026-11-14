#include "boot_platform.h"

boot_status boot_run(const boot_platform *platform)
{
    boot_image image = {0};
    boot_status status = platform->find_image(&image);
    if (status == BOOT_OK) {
        status = platform->verify_image(&image);
    }
    if (status == BOOT_OK) {
        /* The real handoff never returns on success. */
        status = platform->start_image(&image);
    }
    if (status != BOOT_OK) {
        platform->recover(status);
    }
    return status;
}
