# Platform-Agnostic Bootloader Design

**Status:** Proposed v1 architecture

## Scope

Reuse one boot policy across microcontrollers, firmware-hosted systems, and development simulators. Each target builds its own bootloader binary and supplies a startup shim, storage adapter, and payload handoff implementation. Payload binaries remain specific to their instruction set and boot protocol. A release may package multiple target builds, with each device receiving the package matching its provisioned target profile.

The first release supports signed images, two independently bootable slots, bounded trial boots, and authenticated recovery. Each port defines its RAM budget, storage geometry, startup time budget, and available hardware trust features.

## Architecture

~~~mermaid
flowchart TD
    Entry["Reset vector, firmware entry, or simulator"]
    Shim["Target startup shim and profile"]
    Select["Portable core: select trial or confirmed slot"]
    Verify["Portable core: authenticate and validate image"]
    Prepare["Target handoff: preflight ABI and memory"]
    Trial["Portable core: commit attempt if trial boot"]
    Adapters["Target adapters: slot I/O, state, trust, memory"]
    Handoff["Target handoff: transfer control"]
    Payload["Verified payload"]
    Recovery["Authenticated recovery"]

    Entry --> Shim --> Select --> Verify
    Select -.-> Adapters
    Verify -.-> Adapters
    Trial -.-> Adapters
    Verify -->|Verified candidate| Prepare --> Trial --> Handoff --> Payload
    Verify -->|Candidate rejected| Select
    Select -->|No usable slot| Recovery
    Prepare -.->|Preflight failure| Select
~~~

~~~text
Reset / firmware entry
        |
  Target startup shim
        |
  Portable boot core ------> Platform adapters
  - Image validation        - Slot I/O and durable metadata
  - Slot selection          - Trust store and version floor
  - Trial/confirm policy     - RAM reservation and watchdog
  - Recovery policy          - Diagnostics and payload handoff
        |
  Verified payload or authenticated recovery
~~~

Use a freestanding C11 core with fixed-width types, bounded caller-provided scratch space, and no required heap, scheduler, or filesystem. Decode storage fields explicitly with checked arithmetic. Keep linker scripts, startup assembly, CPU registers, and vendor headers in ports. A target profile binds the adapters and declares accepted image types, target IDs, memory reservations, and the maximum trial count.

| Interface | Contract |
|---|---|
| Startup | Establish stack and runtime, initialize required hardware or bind firmware services, and report reset reason when available. |
| Slot I/O | Expose bounded reads by slot and offset. A read fills the entire requested span or returns an error. Provide a stable view of image bytes through handoff. |
| Boot state | Read the last committed record and commit a complete replacement. Report success only once it survives reset. Serialize writes by the updater, bootloader, and confirmation service. |
| Trust | Resolve key IDs from a provisioned trust store. Verify signatures with a vetted library or hardware provider. Expose a monotonic version floor where supported. |
| Load and handoff | Reserve payload memory, validate the entry point and boot ABI, prepare machine state, and transfer control. Report preflight errors before irreversible teardown. |
| Recovery and diagnostics | Record bounded status codes and expose a port-specific path for installing an authenticated image. Service or configure the watchdog as needed. |

## Image and trust contract

A package contains a fixed-format v1 manifest, a signature, and the payload. The manifest uses canonical little-endian encoding with explicit lengths, a format version, target and ABI identifiers, payload type and length, security version, SHA-256 payload digest, and key ID. The signature covers the complete canonical manifest under a fixed boot-image domain tag. The profile pins the signature suite and accepted keys. Load addresses or entry parameters supplied by the image must be covered by the signature and checked against the target's allowed ranges.

Check bounds and target compatibility before processing large inputs. Verify the manifest signature and security version, then hash the exact bytes the loader will execute. For RAM loading, hash while copying to reserved, non-executable memory and enable execution only after verification. For execute-in-place, protect the verified slot from modification before hashing and through execution. Validate segment sizes, alignment, overlap, and executable entry ranges before transfer. Initial payload types can be limited to flat firmware and a firmware-native executable handled by its own port.

The bootloader and its trust store must be authenticated by an earlier stage or protected from modification by the platform. Authenticated boot depends on that root. A hardware-backed monotonic counter is required for rollback resistance against an attacker who can restore an older storage snapshot. A profile without that capability can still enforce the version recorded in its current boot state.

## Boot sequence

1. Initialize the port and recover the newest valid boot-state record. If no valid record exists, enter recovery or use a separately provisioned factory boot policy.
2. If a pending image has attempts left, validate and prepare it. Commit one fewer attempt and the image identity before handing off. A reset after this commit consumes the attempt even if transfer never occurs.
3. If the pending image is invalid or has exhausted its attempts, clear the trial state durably and try the confirmed slot. Verify the confirmed image on every boot, including its recorded identity and applicable security floor.
4. If the confirmed image cannot boot, enter authenticated recovery. If recovery cannot proceed, report a stable failure code and wait for a supported recovery trigger or reset.
5. Pass the selected slot, image identity, boot attempt identifier, reset reason, and profile-specific boot data to the payload using the target ABI. A return after irreversible handoff triggers a port-defined reset or halt.

## Updates, confirmation, and power loss

The updater writes only the inactive slot, flushes it, and verifies it before publishing the pending image identity and attempt budget. The previous confirmed slot remains available throughout the trial. An authorized confirmation service checks that the request identifies the pending image and the recorded attempt. The payload requests confirmation after its required health checks pass.

Boot state includes the confirmed and pending slot identities, attempts remaining, active attempt ID, committed security floor, and a sequence number. Use redundant records in separate failure units or a storage-native transaction. On raw flash, write and verify the replacement record before committing it with a marker suited to the medium's atomic program unit. Keep the previous record until the replacement is durable. A checksum detects torn writes. Protect state writes through the platform trust boundary, and use authenticated storage where an untrusted writer can access the medium.

Confirmation commits the new confirmed slot and raises the software floor in one durable record. If a hardware floor is present, raise it only after that record is durable. On reboot, enforce the greater of the committed software floor and the hardware floor, and retry any pending hardware advancement before handing off a normal payload. Keep recovery capable of accepting an authorized image at or above the hardware floor. Before confirmation, a failed trial can fall back to the previously confirmed version. After confirmation raises the floor, recovery may require a newly signed image.

## Example ports

| Target | Startup and storage | Handoff |
|---|---|---|
| Hosted simulator | In-memory or file-backed slots with injected reset and I/O failures | Capture the handoff descriptor for assertions. |
| Cortex-M board | Reset handler, linker-defined memory map, and flash partitions with independent erase regions | Validate vector-table alignment, stack range, and reset-handler address. Set the vector base where supported and apply the board's interrupt and watchdog rules. |
| x86-64 UEFI | EFI application using firmware storage and memory allocation services | For an EFI payload, use the EFI image launch protocol. For direct OS takeover, implement the OS boot ABI and acquire the final memory map for ExitBootServices, following the firmware retry rules. [UEFI Specification 2.11](https://uefi.org/sites/default/files/resources/UEFI_Spec_Final_2.11.pdf) |

## Proposed source tree

~~~text
bootloader/
  CMakeLists.txt                   Core library, port selection, and test targets
  include/boot/
    boot.h                         Public boot entry point, configuration, status codes
    platform.h                     Adapter contracts and capability declarations
  src/core/
    boot.c                         Boot sequence, slot selection, and recovery decisions
    image.c                        Manifest decoding, signature and digest checks
    boot_state.c                   Record validation, trial accounting, confirmation policy
    update.c                       Inactive-slot verification and pending-state publication
  ports/
    hosted/
      main.c                       Simulator entry and reset loop
      platform.c                   Simulated slot I/O, fault injection, captured handoff
    cortex_m/
      startup.S                    Reset entry and vector table
      platform.c                   Flash, watchdog, trust, and diagnostics adapters
      handoff.S                    Stack setup and final branch to firmware
      board.ld                     Bootloader memory regions and reserved RAM
      profile.c                    Board layout, image IDs, and memory constraints
    uefi/
      entry.c                      EFI application entry and platform binding
      platform.c                   Firmware-backed storage, trust, and diagnostics adapters
      handoff.c                    EFI image launch or OS-specific handoff
      profile.c                    Slot locations, image IDs, and supported boot ABI
  tests/
    test_boot.c                    Selection, trial, confirmation, and recovery
    test_image.c                   Manifest parsing and verification failures
    test_power_loss.c              Reset injection at persistent write boundaries
~~~

Build one port at a time and link it with the same core library. The port's entry point constructs the platform adapter table and profile, supplies scratch memory, and calls `boot_run()`. The updater and confirmation service can reuse the state transitions in `update.c` and `boot_state.c` through the same serialized storage interface.

## Platform adaptation

1. **Bind the entry point.** Use a normal process entry for the simulator, a reset vector for Cortex-M, or an EFI application entry for UEFI. Establish enough runtime for the C core and pass it the target profile.
2. **Describe the target.** Define accepted target and boot ABI IDs, slot locations and capacities, memory reservations, scratch budget, and the trial-attempt limit. Sign the payload for those identifiers.
3. **Map durable storage and trust.** Implement bounded slot reads, stable access to verified bytes, atomic boot-state commits, key lookup, and any hardware version floor. Confirm that storage flushes survive reset on the target.
4. **Implement the handoff.** Capture the request in the simulator, set up the vector table and stack on Cortex-M, or launch an EFI image through firmware on UEFI. A direct OS handoff needs the selected OS ABI and the UEFI memory-map and boot-services transition.
5. **Qualify the port.** Run the shared policy tests, then check the actual entry state, reset and watchdog behavior, and power-loss durability on the target.

A new bare-metal or firmware-hosted target adds its startup code, profile, and adapters under `ports/`. A new payload ABI belongs in the port's handoff code. A new package format or a change to trial policy belongs in the shared core.

## Implementation and acceptance

1. Build the core and hosted port. Exercise the boot policy with deterministic slot contents and storage failures.
2. Add one microcontroller port and one UEFI port. Use the same manifest parser and state machine in both builds.
3. Run the same policy tests across ports. Cover truncated and oversized images, integer overflow, wrong target or ABI, bad signatures, revoked keys, rollback attempts, invalid load ranges, and recovery with no valid slot.
4. Inject resets at each update and metadata write boundary, including trial consumption, confirmation, and hardware floor advancement. Verify bounded attempts, preservation of the confirmed slot during trials, and recovery if both normal slots are unusable.
5. On each target, verify the actual payload entry contract and record boot latency, scratch RAM, and flash use against the profile's budgets.

The trial and fallback lifecycle is comparable to the documented test, confirm, and revert behavior in MCUboot. This design uses independently bootable slots and keeps flash layout and handoff in the target port. [MCUboot design documentation](https://docs.mcuboot.com/design.html)
