"""Build and test the NUCLEO-F446RE bootloader and signed Blinky image."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
BOARD = "ports/stm32f446re"
VENDOR = "third_party/monocypher-4.0.3/src"
CORE = ["src/boot.c", "src/image.c", f"{VENDOR}/monocypher.c",
        f"{VENDOR}/optional/monocypher-ed25519.c"]
INCLUDES = ["-Iinclude", f"-I{BOARD}", "-Ibuild", f"-I{VENDOR}",
            f"-I{VENDOR}/optional"]


def run(*args):
    subprocess.run(args, check=True, cwd=ROOT)


def tool(name):
    path = shutil.which(name)
    if path:
        return path
    raise SystemExit(f"Missing tool on PATH: {name}")


def build_firmware(name, origin, length, entry, sources):
    elf = BUILD / (name + ".elf")
    run(tool("arm-none-eabi-gcc"), "-mcpu=cortex-m4", "-mthumb",
        "-std=c11", "-Os", "-g", "-ffreestanding", "-ffunction-sections",
        "-fdata-sections", "-Wall", "-Wextra", "-Werror", "--specs=nosys.specs",
        "-nostartfiles", f"-Wl,--gc-sections,--defsym=flash_base={origin:#x},"
        f"--defsym=flash_size={length:#x},-Map={elf.with_suffix('.map')}",
        f"-T{BOARD}/board.ld", *INCLUDES, f"-DFIRMWARE_ENTRY={entry}", f"{BOARD}/startup.S", *sources, "-o", elf)
    run(tool("arm-none-eabi-objcopy"), "-O", "binary", elf, elf.with_suffix(".bin"))
    run(tool("arm-none-eabi-size"), elf)


def sign_image(payload, key, openssl, target=0x446):
    header = struct.pack("<4sIII64s", b"CBL1", 1, target, len(payload),
                         hashlib.sha512(payload).digest())
    header_file = BUILD / "manifest.tmp"
    signature_file = BUILD / "signature.tmp"
    header_file.write_bytes(header)
    run(openssl, "pkeyutl", "-sign", "-rawin", "-inkey", key, "-in",
        header_file, "-out", signature_file)
    signature = signature_file.read_bytes()
    if len(signature) != 64:
        raise SystemExit("Expected an Ed25519 signature")
    return (header + signature).ljust(512, b"\xff") + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    keys = parser.add_mutually_exclusive_group(required=True)
    keys.add_argument("--key", type=Path, help="Existing private Ed25519 PEM key")
    keys.add_argument("--lab-key", action="store_true", help="Use a local development key")
    parser.add_argument("--openssl", default=os.getenv("OPENSSL", "openssl"))
    parser.add_argument("--test", action="store_true", help="Run host tests with GCC")
    args = parser.parse_args()

    BUILD.mkdir(exist_ok=True)
    openssl = tool(args.openssl)
    key = BUILD / "lab-key.pem" if args.lab_key else args.key.resolve()
    if args.lab_key and not key.exists():
        run(openssl, "genpkey", "-algorithm", "ED25519", "-out", key)
    der = BUILD / "public-key.der"
    run(openssl, "pkey", "-in", key, "-pubout", "-outform", "DER", "-out", der)
    public = der.read_bytes()
    prefix = bytes.fromhex("302a300506032b6570032100")
    if not public.startswith(prefix) or len(public) != len(prefix) + 32:
        raise SystemExit("Expected an Ed25519 key")
    key_bytes = ", ".join(f"0x{value:02x}" for value in public[len(prefix):])
    (BUILD / "public_key.h").write_text(
        f"static const uint8_t boot_public_key[32] = {{{key_bytes}}};\n", encoding="ascii"
    )

    build_firmware("bootloader", 0x08000000, 0x10000, "bootloader_main",
                   CORE + [f"{BOARD}/platform.c", f"{BOARD}/handoff.S"])
    build_firmware("blinky", 0x08010200, 0x6FE00, "blinky_main", ["examples/blinky.c"])
    payload = (BUILD / "blinky.bin").read_bytes()
    package = sign_image(payload, key, openssl)
    (BUILD / "blinky-signed.bin").write_bytes(package)
    corrupt = bytearray(package)
    corrupt[-1] ^= 1
    (BUILD / "blinky-corrupt.bin").write_bytes(corrupt)
    if args.test:
        wrong_target = BUILD / "wrong-target.bin"
        wrong_target.write_bytes(sign_image(payload, key, openssl, target=1))
        exe = BUILD / "test_boot.exe"
        run(tool("gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror", *INCLUDES,
            *CORE, "tests/test_boot.c", "-o", exe)
        run(exe)


if __name__ == "__main__":
    main()
