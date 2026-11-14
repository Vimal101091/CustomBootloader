#include "image.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
         | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

boot_status image_verify(boot_image *image, uint32_t target,
                         const uint8_t public_key[32])
{
    image->payload_size = 0;
    if (image->capacity < IMAGE_PREFIX_SIZE) {
        return BOOT_BAD_HEADER;
    }
    const uint8_t *header = image->bytes;
    if (read_le32(header) != 0x314c4243u || read_le32(header + 4) != 1u) {
        return BOOT_BAD_HEADER;
    }
    if (read_le32(header + 8) != target) {
        return BOOT_WRONG_TARGET;
    }
    size_t size = read_le32(header + 12);
    if (size < 8 || size > image->capacity - IMAGE_PREFIX_SIZE) {
        return BOOT_BAD_HEADER;
    }
    /* The magic and version bind the signature to this format. */
    if (crypto_ed25519_check(header + 80, public_key, header, 80) != 0) {
        return BOOT_BAD_SIGNATURE;
    }
    uint8_t digest[64];
    crypto_sha512(digest, header + IMAGE_PREFIX_SIZE, size);
    if (crypto_verify64(digest, header + 16) != 0) {
        return BOOT_BAD_DIGEST;
    }
    image->payload_size = size;
    return BOOT_OK;
}
