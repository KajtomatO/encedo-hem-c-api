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

### 5. Friendly message when `libwolfssl.dll` is missing (Windows)

**Moved to M12** (user decision 2026-10-12, ARCHITECTURE.md §11 M12
part A); kept here, with its number, for provenance.

Added by user decision 2026-10-10, from the first Windows test of the
release binary. hem-tool links wolfSSL dynamically (REQ-BUILD-005), and
today Windows refuses to start `hem-tool.exe` without `libwolfssl.dll`
before any of its code runs — depending on the console a system dialog or
no message at all, exit code -1073741515 (`0xC0000135`,
STATUS_DLL_NOT_FOUND); the release README.txt documents it. A friendly
message would need the DLL to be loaded on demand instead of at startup
(e.g. a delay-load import), so hem-tool itself could report what is
missing and where to put it.

### 6. `init-wizard` explains the fields it asks for

User note 2026-10-19 (from the 1.2.0-rc1 test; not analysed): the wizard
should explain some of the fields — what `origin` or `dnsd` is, why it is
needed and how it affects the HEM.

### 7. `init-wizard` readability

User note 2026-10-19 (not analysed): the wizard should be more readable —
colours, maybe a simple GUI.

### 8. Backup words: scrollback and a file

User note 2026-10-19 (not analysed): the backup words stay visible in the
terminal history (scrolling up); there should be an option to save them to
a file.

### 9. A safer way to pass the passphrase

User note 2026-10-19 (not analysed): the passphrase lands in the console
history; consider sessions — a `hem-tool login` that takes the passphrase
safely from the user — though how to store it is open. Related:
`--passphrase` is deprecated since 1.2.0 (REQ-TOOL-020 rev 2) and will be
removed; `EHEM_PASSPHRASE` stays until this item lands.

### 10. [Important] Split hem-tool from the SDK

User note 2026-10-19 (not analysed): hem-tool and the SDK should become
separate deliverables.

### 11. `hem-tool random` is slow

User note 2026-10-19 (not analysed): `hem-tool random` takes a long time —
investigate; update the help text or the algorithm. Does it create a key
only for itself? Perhaps keep that key instead of removing it every time:
by default create it only if absent, with options to clean up and to use
another existing key.

## Candidates (no decision taken)

- General config/administration writes other than `wipeout` — `userkey*`
  rotation and `gen_csr` — deliberately unbound at 1.0 (REQ-SYS-004;
  `docs/COVERAGE.md`): no HEM-SDK-1..9 consumer needs them. Listed here
  only so the "candidate if ever wanted" notes have a target. (`wipeout`
  itself became M10 scope by user decision 2026-10-07.)
- macOS builds (arm64 / x86_64) — kept out of M12 (user decision
  2026-10-12): no consumer needs them yet.
- An MSVC-ABI Windows library package — kept out of M12 (user decision
  2026-10-12); ARCHITECTURE.md §1 adds MSVC when a consumer needs
  MSVC-ABI artifacts.

## Provenance

Items 1–2 were M10 scope (user decisions 2026-08-06/07), items 3–4 were M11
scope (user decision 2026-08-07). Moved here by user decision 2026-10-07;
M11 retired the same day (its number is not reused). Firmware-pending
surface — documented or shipped dormant but missing from the running
firmware — is NOT backlog: it stays in milestone MFW (ARCHITECTURE.md §11).
