# Implementation guide

This guide describes the current NUCLEO-F446RE lab build. See the [README](../README.md) for prerequisites, build commands, and flashing addresses. The [design](../bootloader-design.md) and [production-readiness guide](production-readiness.md) cover planned capabilities.

## Boot sequence

~~~text
Reset → bootloader Reset_Handler → bootloader_main()
  → boot_run() → find_image() → verify_image() → start_image()
  → jump_to_application() → Blinky Reset_Handler → blinky_main()
~~~

The build selects each binary's entry point with `FIRMWARE_ENTRY`. The STM32 port reads the package from internal flash. After authentication, the port checks the application's stack pointer and reset handler, disables and clears interrupt sources, sets the vector table, and calls the assembly handoff. The assembly switches to the application's stack and branches to its reset handler. Any boot failure before handoff calls recovery. LD2 stays on and the failure code is available as `boot_failure` in the debugger.

| Source | Responsibility |
| --- | --- |
| [src/boot.c](../src/boot.c) | Shared find, verify, start, and recovery sequence |
| [include/boot_platform.h](../include/boot_platform.h) | Contract between the boot core and a native port |
| [src/image.c](../src/image.c) | Package bounds, target ID, Ed25519 signature, and SHA-512 payload checks |
| [ports/stm32f446re](../ports/stm32f446re) | Startup, flash mapping, LED, vector checks, and Cortex-M handoff |
| [build.py](../build.py) | Cross-compiles both images, embeds the public key, and signs the package on the PC |
| [tests/test_boot.c](../tests/test_boot.c) | Host tests for the shared boot sequence and real image verifier |

## Signed package layout

Offsets are relative to the beginning of `blinky-signed.bin`, which is flashed at `0x08010000`.

| Offset | Contents |
| --- | --- |
| `0x0000–0x000F` | Package marker `CBL1`, version, target ID, payload length |
| `0x0010–0x004F` | SHA-512 fingerprint of the firmware |
| `0x0050–0x008F` | Ed25519 signature over the complete 80-byte header |
| `0x0090–0x01FF` | Padding |
| `0x0200` onward | Firmware, beginning with the vector table at flash address `0x08010200` |

OpenSSL signs the header with the private key. The loader checks the signature using its embedded public key, then computes SHA-512 over the declared payload and compares it with the signed fingerprint. Changing only the payload can leave the signature valid while causing the fingerprint check to fail. Both checks are required for launch.

## Verification status

The Arm build and seven host tests cover compilation, boot sequencing, and image verification. They exercise valid, missing, truncated, corrupt, wrong-target, and bad-signature images, plus a simulated handoff failure. A board test is still needed to confirm the physical handoff and LD2 behavior. The current build uses ST-LINK for installation and recovery. UART transfer, transport CRC, and A/B OTA are future work.
