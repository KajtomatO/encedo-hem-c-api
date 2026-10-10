# Backlog

Unscheduled work. Nothing in this file has a milestone, a requirement or a
step, and the SDK does not promise any of it. An item leaves this file only
by user decision into a numbered milestone of [ARCHITECTURE.md](ARCHITECTURE.md)
§11, after which it is decomposed per
[REQUIREMENTS-MANAGEMENT.md](REQUIREMENTS-MANAGEMENT.md) §5.3.

Created 2026-10-07 from the former M10/M11 scope (user decision 2026-10-07:
M10 keeps device initialisation only; M11 retired). The dated decisions
inside each item are carried over unchanged.

## Items

### 1. `system/upgrade` family

Firmware upload/check/install triad (`POST upload_fw`, `GET check_fw`,
`GET install_fw`); UI triad (`POST upload_ui`, `GET check_ui`,
`GET install_ui`); bootloader upload (`POST upload_bootldr`,
`GET install_bl` — DIAG builds only); `GET /api/system/upgrade/usbmode`;
plus the bare `GET /api/system/upgrade` base route, found live and
auth-gated on fw v1.2.2 at the M9 firmware scan. Deferred since M7; doc
pages `upgrade-firmware.md`, `upgrade-ui.md`, `upgrade-bootloader.md`,
`upgrade-usbmode.md` (dispositions in `docs/COVERAGE.md`). Everything here
is disruptive on the device side (ARCHITECTURE.md §9 test policy applies).

### 2. `hem-tool fw-upgrade`

CLI orchestrator over item 1 (user decision 2026-07-17).

### 3. `POST /api/system/config/provisioning`

Factory ATECC certificate write: one-shot per ATECC608, 403 on any
initialised device. Re-dispositioned from "deliberately unbound" on
2026-08-07 (user decision: every unimplemented firmware feature gets a
milestone — it was last in line in M11). REQ-SYS-011 keeps the
factory-context caveats.

### 4. `diag/*` family

Nine DIAG-build-only endpoints — liveness, TRNG draw, fault injection,
wipe_config, corrupt_repo, memdump; unauthenticated and destructive;
production firmware builds 404. **Reserved design constraint (user note
2026-08-07):** diag support must NOT enter the production SDK. Candidate
shapes: (a) hem-tool-only implementation (the tool talks to `diag/*`
directly, no library surface), or (b) a separate diagnostic SDK/library
beside the production one. The production `libencedo-hem` stays diag-free
either way.

## Candidates (no decision taken)

- General config/administration writes other than `wipeout` — `userkey*`
  rotation and `gen_csr` — deliberately unbound at 1.0 (REQ-SYS-004;
  `docs/COVERAGE.md`): no HEM-SDK-1..9 consumer needs them. Listed here
  only so the "candidate if ever wanted" notes have a target. (`wipeout`
  itself became M10 scope by user decision 2026-10-07.)

## Provenance

Items 1–2 were M10 scope (user decisions 2026-08-06/07), items 3–4 were M11
scope (user decision 2026-08-07). Moved here by user decision 2026-10-07;
M11 retired the same day (its number is not reused). Firmware-pending
surface — documented or shipped dormant but missing from the running
firmware — is NOT backlog: it stays in milestone MFW (ARCHITECTURE.md §11).
