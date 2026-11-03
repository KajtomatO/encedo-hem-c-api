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

CLI orchestrator over item 1 (user decision 2026-07-17). A hem-tool item:
since the split (M13, 2026-10-27) it belongs to
[encedo-hem-tool](https://github.com/KajtomatO/encedo-hem-tool) and would
follow item 1 there.

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

**Moved to encedo-hem-tool** with hem-tool (M13, user decision 2026-10-27);
kept here, with its number, for provenance. User note 2026-10-19 (from the
1.2.0-rc1 test): the wizard should explain some of the fields — what
`origin` or `dnsd` is, why it is needed and how it affects the HEM.

### 7. `init-wizard` readability

**Moved to encedo-hem-tool** (M13); kept for provenance. User note
2026-10-19: the wizard should be more readable — colours, maybe a simple
GUI.

### 8. Backup words: scrollback and a file

**Moved to encedo-hem-tool** (M13); kept for provenance. User note
2026-10-19: the backup words stay visible in the terminal history
(scrolling up); there should be an option to save them to a file.

### 9. A safer way to pass the passphrase

**Moved to encedo-hem-tool** (M13); kept for provenance. User note
2026-10-19: the passphrase lands in the console history; consider sessions
— a `hem-tool login` that takes the passphrase safely from the user —
though how to store it is open. `--passphrase` is deprecated since 1.2.0
(REQ-TOOL-020 rev 2, now encedo-hem-tool's). The SDK side has nothing to
add: `ehem_login()` takes the passphrase from the caller and never from
the environment.

### 10. [Important] Split hem-tool from the SDK

**Done — M13 (SDK 1.2.1) and encedo-hem-tool T1 (`c-v1.3.0`, released
2026-10-28).** User note 2026-10-19; user decisions 2026-10-27 (ARCHITECTURE.md
§11 M13; the split plan is recorded in encedo-hem-tool's ARCHITECTURE §1
and §7): hem-tool is a separate deliverable with its own repository and
version, built on the SDK's published static package; the SDK releases the
libraries only from 1.2.1 on. Kept here, with its number, for provenance.

### 11. `hem-tool random` is slow

**Moved to encedo-hem-tool** (M13); kept for provenance. User note
2026-10-19: `hem-tool random` takes a long time — investigate; update the
help text or the algorithm; perhaps keep its key instead of removing it
every time. (The SDK's `ehem_random` harvests random bytes through AES
encrypt IVs — REQ-OPS-002 — and is what the command drives.)

### 12. hem-tool on Windows: device strings in the console's OEM code page

**Moved to encedo-hem-tool** (M13); kept for provenance. Found 2026-10-20
while fixing the `ext pair` QR display (STEP-M12-137): hem-tool writes
UTF-8 to stdout, and a Windows console decodes it in its OEM code page — a
non-ASCII key label, user name or hostname from the device shows as
mojibake there, and what the wizard reads from the console comes back in
that code page. The QR fix bypassed the code page for the QR rows only.

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
