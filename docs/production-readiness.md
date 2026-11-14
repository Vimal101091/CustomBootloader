# Production Readiness Guide — NUCLEO-F446RE Secure Bootloader

**Audience:** engineering, security, QA, and product management preparing this bootloader for commercial B2B sale.
**Companion to:** `README.md` (build/flash), `bootloader-design.md` (architecture).
**Status:** living document — update as features land.

---

## 1. Executive summary

The current codebase is a **lab-quality, single-slot, signed-image loader**. It verifies an Ed25519-signed, SHA-512-digested image before jumping, which is a solid foundation, but it is not yet sellable to B2B customers in regulated or unattended deployments. The gap is not in the cryptography; it is in **root-of-trust provisioning, update resilience (A/B + rollback protection), recovery, key management, and evidence (tests, threat model, compliance)**.

This guide defines the work required to reach a **commercial v1** and, separately, the bar for **safety/regulatory-certified markets** (medical, automotive, functional safety), which is an order of magnitude higher.

---

## 2. Current state snapshot

| Area | Status |
|---|---|
| Signed image verification (Ed25519 + SHA-512) | Implemented and independently verified |
| Bounds-checked manifest parsing | Implemented |
| Single slot, execute-in-place | Implemented |
| Cortex-M4 handoff (VTOR, NVIC, SysTick) | Implemented |
| Fail-closed recovery with reason code | Implemented |
| Host test suite | 7 cases |
| Root of trust / RDP / write protection | **Not implemented (documented only)** |
| A/B slots / trial boots / confirmation | **Not implemented** |
| Rollback / security version | **Not implemented** |
| Authenticated recovery transport | **Not implemented** |
| Watchdog | **Not implemented** |
| Key rotation / multiple keys | **Not implemented** |
| Attestation / measured boot | **Not implemented** |
| Fuzzing / fault injection / soak | **Not implemented** |
| SBOM / threat model / compliance | **Not implemented** |

---

## 3. Definition of "production ready" (exit criteria)

A release may be sold as production-grade only when **all** of the following hold:

1. **Security**: root of trust provisioned (RDP ≥ 1, bootloader sectors write-protected); signed images verified before execution; rollback floor enforced; threat model written and reviewed.
2. **Resilience**: A/B slots with trial boots and confirmation; a failed update never bricks the device; authenticated recovery path exists; power-loss at every state-write boundary tested.
3. **Quality**: ≥ 90% line/branch coverage on core; fuzzing campaign with no open findings; soak test of ≥ 10,000 reboots across temperature range; clean static analysis (MISRA C:2012 mandatory + advisory where feasible).
4. **Evidence**: requirements-to-tests traceability matrix; SBOM; signed release artifacts; changelog; migration guide.
5. **Compliance**: target-market certifications obtained or scoped (see §8).
6. **Support**: published SLA, security disclosure policy (`SECURITY.md`), and deprecation policy.

---

## 4. Phased roadmap

### Phase 0 — Close documented gaps (1–2 weeks)

- [ ] Provision RDP and write-protect bootloader sectors; document the programming sequence in `README.md`.
- [ ] Add `security_version` field to manifest (format v2) and enforce a floor.
- [ ] Tighten `start_image`: minimum stack headroom, entry bounds, add `BOOT_HANDOFF_RETURNED`.
- [ ] Resolve design-doc drift: mark unimplemented features as roadmap, or implement them.
- [ ] Add `THIRD_PARTY_NOTICES.md` (Monocypher 4.0.3 is CC0).
- [ ] Ensure `lab-key.pem` and `build/` are never shipped; verify `.gitignore` coverage.
- [ ] De-duplicate format constants between `src/image.c` and `build.py`; add `_Static_assert`s.

### Phase 1 — Core features B2B customers expect (4–8 weeks)

| Feature | Rationale | Effort |
|---|---|---|
| A/B slots with trial boots + confirmation | Eliminates bricked devices on failed updates | Medium |
| Authenticated recovery (signed image via UART/SWD) | Field recovery without JTAG | Medium |
| Watchdog + reset-cause reporting | Unattended long-life devices | Low |
| Boot-state durability (redundant records, torn-write detection, power-loss tests) | Prevents unrecoverable state corruption | Medium |
| Rollback floor enforcement (software + hardware counter) | Defends against downgrade after a CVE | Low–Medium |
| Optional UART/USB update transport | Production has no ST-LINK | Medium |
| Version/ABI negotiation API | Payload queries bootloader capabilities | Low |

### Phase 2 — Security hardening (2–4 weeks)

- [ ] **Key management**: multiple keys with revocation, key IDs in manifest, documented rotation procedure, factory provisioning script.
- [ ] **Anti-tamper**: detect attached debug probes at boot (`DHCSR`), optionally disable debug in production builds.
- [ ] **Side-channel review**: constant-time checks on `image_verify` and `find_image`; fuzz harness.
- [ ] **Authenticated boot state**: HMAC/signature on state records so a compromised app cannot pin an old version.
- [ ] **Attestation**: measured-boot digest (bootloader + app hash) for device identity proofs to cloud backends.
- [ ] **Threat model document** (see §6).

### Phase 3 — Quality & compliance (ongoing, 20–30% of dev time)

- [ ] Fuzzing (`image_verify`, `boot_run`) with sanitizers.
- [ ] Fault injection at every state-write boundary.
- [ ] Soak/HIL: ≥ 10,000 reboot cycles across temperature range; ≥ 2 board revisions.
- [ ] Static analysis: MISRA C:2012 (mandatory + advisory where feasible), `cppcheck`, `clang-tidy`.
- [ ] Requirements-to-tests traceability matrix.
- [ ] SBOM (CycloneDX/SPDX) listing Monocypher version; CVE response SLA.
- [ ] Certifications per target market (§8).

### Phase 4 — Commercial readiness

- [ ] Licensing decision (MIT/Apache-2.0 for adoption vs. commercial license for revenue); Monocypher CC0 confirmed compatible.
- [ ] Documentation package: integration/porting guide, provisioning manual, threat model, security whitepaper, changelog, migration guide.
- [ ] Support model: SLA, issue tracker, `SECURITY.md`, deprecation policy.
- [ ] Reference ports: ≥ 3 (current STM32F4 + STM32H7/U5 + one non-ARM such as ESP32 or RP2040).
- [ ] Host-side updater tool (sign + flash) — currently manual via `build.py` + ST-LINK.
- [ ] Pricing/packaging decision (per-seat, per-device royalty, or OSS + paid support).

---

## 5. Security architecture requirements

### 5.1 Root of trust

- Bootloader must be immutable in production: RDP Level 1 minimum, bootloader flash sectors write-protected.
- Public key embedded at build time; private key never on device or in source control.
- Document and script the provisioning sequence (key injection, RDP, WRP, option bytes).
- Verify provisioning in manufacturing with a read-back test.

### 5.2 Image format (v2 proposal)

The v1 manifest contains: magic, format version, target ID, payload length, SHA-512 digest, signature. v2 should add, inside the signed region:

- `security_version` (uint32) — monotonic, enforced floor
- `key_id` (uint8) — selects verifying key
- `abi_version` (uint8) — boot ABI compatibility
- `payload_type` (uint8) — flat firmware vs. other
- Domain separation tag in the signed message (e.g. `"CBL1-SIG-V2"`) to prevent cross-protocol reuse

### 5.3 Update lifecycle

1. Updater writes inactive slot, flushes, verifies.
2. Publishes pending image ID + attempt budget (durable, redundant record).
3. Bootloader trials pending image (decrement attempts on each boot).
4. Payload confirms after health checks; confirmation raises the security floor.
5. On failure or exhausted attempts, fall back to confirmed slot.
6. Hardware counter (if available) raised only after durable software record.

### 5.4 Key management

- Support at least 2 keys (primary + recovery) with revocation.
- Documented rotation: sign new image with new key, ship update that trusts both, then revoke old key in next update.
- Factory provisioning must bind a unique device identity if attestation is required.

---

## 6. Threat model (outline — to be expanded)

**Assets:** bootloader code, embedded public key, device identity, application firmware, boot state, user data.

**Attacker capabilities considered:**
- Remote network attacker (if device has connectivity)
- Local attacker with physical access (SWD/JTAG, flash read/write)
- Supply-chain attacker (compromised firmware update)
- Malicious application (post-compromise persistence)

**Mitigations mapped:**
| Threat | Mitigation |
|---|---|
| Unsigned/malicious image execution | Ed25519 verification before jump |
| Image tampering in transit | SHA-512 digest in signed header |
| Downgrade to vulnerable version | Security version floor (to be implemented) |
| Bootloader replacement | RDP + write protection (to be provisioned) |
| State corruption / pinning old version | Authenticated, redundant boot-state records (to be implemented) |
| Debug interface abuse | RDP, runtime debug-probe detection |
| Side-channel key recovery | Constant-time crypto (Monocypher); review own code |

**Residual risks to disclose:** no hardware root of trust on STM32F4 (no secure element); flash is readable if RDP is bypassed via voltage glitching (document as out of scope for v1 or mitigate with RDP2 + anti-tamper).

---

## 7. Quality assurance plan

### 7.1 Test matrix

| Category | Tests |
|---|---|
| Unit (host) | Manifest parsing, signature/digest, bounds, integer overflow, truncated/oversized, wrong target/ABI/version, bad magic |
| Integration (HIL) | Full boot flow, handoff, LED, watchdog |
| Fault injection | Power loss at each state-write boundary; torn writes |
| Fuzzing | `image_verify`, `read_le32`, boot-state parser |
| Soak | ≥ 10,000 reboots, temperature sweep |
| Security | Timing analysis, debug-probe detection, RDP verification |

### 7.2 Static analysis & coding standard

- MISRA C:2012 (mandatory rules required; advisory where feasible).
- `cppcheck`, `clang-tidy` in CI; zero warnings under `-Wall -Wextra -Werror -Wconversion`.
- `clang-format` with a committed `.clang-format`.

### 7.3 Coverage

- ≥ 90% line and branch coverage on `src/` core.
- Coverage reports archived per release.

---

## 8. Compliance & certification matrix

| Market | Standard | Notes |
|---|---|---|
| General IoT (EU) | CE (RED), RoHS, REACH | Radio if wireless transport added |
| General IoT (US) | FCC Part 15 | |
| Industrial | IEC 62443 (cybersecurity), IEC 61000 (EMC) | IEC 62443 increasingly required by enterprise buyers |
| Functional safety | IEC 61508, ISO 13849 | Only if claiming safety function; requires safety case |
| Medical | IEC 62304, ISO 13485, FDA 510(k)/CE MDR | Major effort; separate product line |
| Automotive | ISO 26262 | **Not feasible for this codebase as-is**; do not market as automotive-grade without full safety case |
| Export control | EAR/ITAR (US), dual-use (EU) | Review if crypto is the controlled item |

**Important:** certification is a product-level activity, not a bootloader-level one. A bootloader can be "designed to support" a safety standard, but the final device holds the certificate.

---

## 9. Documentation deliverables (minimum for sale)

- [ ] `README.md` — build, flash, verify (exists; update as features land)
- [ ] `docs/integration-guide.md` — porting to a new MCU
- [ ] `docs/provisioning-manual.md` — key injection, RDP, WRP, verification
- [ ] `docs/threat-model.md` — full version of §6
- [ ] `docs/security-whitepaper.md` — customer-facing summary
- [ ] `CHANGELOG.md` — semver, signed releases
- [ ] `MIGRATION.md` — upgrading between bootloader versions
- [ ] `SECURITY.md` — disclosure policy, contact, SLA
- [ ] `THIRD_PARTY_NOTICES.md`
- [ ] SBOM per release (CycloneDX or SPDX)

---

## 10. Commercial & support model

- **Licensing**: choose early. MIT/Apache-2.0 maximizes adoption; commercial license enables revenue. Mixed (OSS core + commercial features) is common in this space.
- **Support tiers**: community (GitHub issues) → standard (email, N-business-day SLA) → premium (phone, 24/7, dedicated engineer).
- **Security disclosure**: 90-day coordinated disclosure; CVE assignment; patched release within SLA.
- **Deprecation**: support N-1 major versions for ≥ 12 months; publish EOL dates.
- **Reference ports**: ≥ 3 before claiming "platform-agnostic"; a single port reads as a demo.

---

## 11. 2-week sprint plan (recommended starting point)

1. Implement A/B slots + trial boot + confirmation (highest customer value).
2. Add RDP/WRP provisioning to README + a provisioning script.
3. Add `security_version` field (format v2).
4. Write the full threat model (`docs/threat-model.md`).
5. Set up fuzzing + CI pipeline (`build.py --lab-key --test` on every PR).
6. Add `SECURITY.md` and `THIRD_PARTY_NOTICES.md`.

---

## 12. Honest caveat

If target customers are in **medical, automotive, or functional-safety industrial** markets, the bar is an order of magnitude higher (IEC 62304, ISO 26262, IEC 61508) and requires a qualified safety case, independent assessment, and typically 6–12 months. **Do not market this bootloader as safety-certified** until that work is done. For general industrial IoT and commercial embedded products, the roadmap above is realistic for a small team in ~3 months.

---

*Last updated: generated during initial production-readiness review. Update checkboxes as work completes.*
