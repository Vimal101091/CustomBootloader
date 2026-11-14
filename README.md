# NUCLEO-F446RE bootloader

This first build boots one authenticated application from internal flash. The shared core calls the platform operations to find, verify, and start an image. On failure the STM32 port lights LD2 (PA5) steadily, records the reason in the debugger-visible `boot_failure` variable, and waits for reset or reflashing. The example application blinks LD2 after a successful handoff.

## Build on Windows

Requires Python 3, Arm GNU Toolchain for `arm-none-eabi`, and OpenSSL with Ed25519 support. Put the tools on PATH, set `OPENSSL`, or pass its full path. The optional host tests also require GCC.

~~~powershell
python build.py --lab-key --test --openssl 'C:\Program Files\Git\usr\bin\openssl.exe'
~~~

Omit `--test` if host GCC is unavailable. The lab key is generated locally in `build/lab-key.pem` and reused across builds. You can pass `--key path\to\key.pem` to use an existing Ed25519 key. Keep the private key off the device and out of source control. The loader embeds only the public key.

## Flash and test

Connect the ST-LINK USB port and select **ST-LINK** in STM32CubeProgrammer. Program the two raw binaries at these addresses with verification enabled. Use sector erase when replacing the application package so the bootloader sectors are preserved.

| File | Flash address | Purpose |
| --- | --- | --- |
| `build/bootloader.bin` | `0x08000000` | Loader in sectors 0–3, with a 64 KiB link limit |
| `build/blinky-signed.bin` | `0x08010000` | Signed package in sectors 4–7. Its 512-byte prefix puts the application vector table at `0x08010200`. |
| `build/blinky-corrupt.bin` | `0x08010000` | Negative test package with one payload byte changed after signing |

1. Program the loader and signed package, then press reset. LD2 should blink.
2. Replace the package with the corrupted binary, then reset. LD2 should stay on and `boot_failure` should read `BOOT_BAD_DIGEST`.
3. Restore the signed package and reset. LD2 should blink again.

The build also writes ELF files for debugging and map files for checking addresses and size. The flash layout follows the [STM32F446 reference manual](https://www.st.com/resource/en/reference_manual/dm00135183.pdf).

## Code and image format

- `src/boot.c` owns boot sequencing and reports failures to recovery.
- `include/boot_platform.h` defines the contract that a new target port implements.
- `src/image.c` bounds-checks the header, enforces the target ID, and verifies the signature and digest.
- `ports/stm32f446re/` supplies reset startup, flash and LED access, linker regions, and the Cortex-M handoff.
- `examples/blinky.c` is the test payload. `tests/test_boot.c` exercises the core and real verifier on the host.
- `build.py` builds both images, injects the public key at compile time, and packages test images. The vendored verifier is [Monocypher 4.0.3](https://monocypher.org/download/).

The package begins with `CBL1`, followed by three little-endian 32-bit fields for format/boot ABI version (1), target ID (0x446), and payload length. The next 64 bytes hold the SHA-512 payload digest. Ed25519 signs these first 80 bytes. The 64-byte signature follows, then padding through byte 511 and the firmware payload from byte 512 onward.

The lab flow uses ST-LINK for recovery and installation. UART transfer, transport CRC, OTA, A/B slots, and rollback protection are later features. The lab loader does not provision device write protection or establish a hardware root of trust. Production deployment must protect the loader and its embedded public key.

## Documentation

- [Implementation guide](docs/implementation.md)
- [Bootloader design](bootloader-design.md)
- [Production readiness](docs/production-readiness.md)
