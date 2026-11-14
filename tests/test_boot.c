#include <assert.h>
#include <stdio.h>
#include "image.h"
#include "public_key.h"

static uint8_t package[0x70000];
static size_t length;
static boot_status find_result;
static boot_status handoff_result;
static boot_status recovery_reason;
static unsigned launches;

static boot_status locate(boot_image *image)
{
    image->bytes = package;
    image->capacity = length;
    return find_result;
}

static boot_status verify(boot_image *image)
{
    return image_verify(image, IMAGE_TARGET_F446RE, boot_public_key);
}

static boot_status start(const boot_image *image)
{
    assert(image->payload_size >= 8);
    ++launches;
    return handoff_result;
}

static void recover(boot_status reason)
{
    recovery_reason = reason;
}

static void load(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    length = fread(package, 1, sizeof(package), file);
    assert(feof(file) && !ferror(file));
    assert(fclose(file) == 0);
}

static void check(boot_status expected, unsigned expected_launches)
{
    const boot_platform platform = {locate, verify, start, recover};
    launches = 0;
    recovery_reason = BOOT_OK;
    assert(boot_run(&platform) == expected);
    assert(launches == expected_launches);
    assert(recovery_reason == expected);
}

int main(void)
{
    load("build/blinky-signed.bin");
    check(BOOT_OK, 1);
    handoff_result = BOOT_BAD_ENTRY;
    check(BOOT_BAD_ENTRY, 1);
    handoff_result = BOOT_OK;
    find_result = BOOT_NO_IMAGE;
    check(BOOT_NO_IMAGE, 0);
    find_result = BOOT_OK;
    --length;
    check(BOOT_BAD_HEADER, 0);
    ++length;
    package[80] ^= 1;
    check(BOOT_BAD_SIGNATURE, 0);

    load("build/blinky-corrupt.bin");
    check(BOOT_BAD_DIGEST, 0);
    load("build/wrong-target.bin");
    check(BOOT_WRONG_TARGET, 0);
    puts("7 boot policy and image verification cases passed");
}
