---
id: REQ-TOOL-016
title: hem-tool ext family — pair (terminal QR), list, login
status: approved
priority: should
revision: 1
source: user decision 2026-07-22 (M8 decomposition; "Add QR code generation to hem-tool" — user decision same day); hem-api-tester test_5.php QR payload; REQ-TOOL-012 family-in-one-REQ precedent
depends_on: ["REQ-AUTH-006", "REQ-AUTH-008", "REQ-AUTH-009", "REQ-AUTH-010", "REQ-KEY-001", "REQ-TOOL-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
---

# hem-tool ext family — pair (terminal QR), list, login

hem-tool SHALL provide an `ext` subcommand family — `ext pair`,
`ext list`, `ext login` — making the ExtAuth surface drivable by hand,
including rendering the pairing QR code directly in the terminal.

- **`ext pair [--label-note <s>]`:** orchestrates registration
  end-to-end: login (passphrase; pairing needs `sub=U`) → device eid via
  config → broker `session` → `ehem_ext_init` → broker `register/init`
  → compose the QR payload `{link, hash, user, email, hostname}` (the
  test_5.php:90 shape; `hash` = `"not_implemented_yet"` until upstream
  defines it) → **render it as a QR code in the terminal** (UTF-8
  half-block cells; `--no-qr` prints the JSON payload only) → poll
  `register/check` (bounded, ~60 s, tester cadence) → `ehem_ext_validate`
  → broker `register/finalise` → print the new `kid` + label. Distinct
  nonzero exit codes: scan timeout, slots-full/dedup 406, broker error,
  auth error (the REQ-TOOL-003 style).
- **QR encoder:** a vendored single-file MIT QR encoder (Project Nayuki
  `qrcodegen`, C port) under the tool tree — vendored like cJSON
  (REQ-BUILD-003 pattern: verbatim upstream + provenance note), compiled
  ONLY into hem-tool/hem-tool-core, never into libencedo-hem, never
  reachable from public headers (the existing header/export gates prove
  it). NOT rendered via any web service — the tester's quickchart.io
  approach ships `user`/`email`/`hostname` to a third party and is
  explicitly rejected.
- **`ext list`:** `ehem_key_list` walk filtered to keys whose descriptor
  carries the `EXTAID` prefix; prints kid, label, and the pid (base64 of
  the 32-byte descriptor suffix). Real phones label themselves (e.g.
  "… (iPhone)") — such labels hit the REQ-TOOL-005 protected classifier,
  so `keys rm --all` already refuses them; `ext list` marks
  `[PROTECTED]` the same way `keys list` does. Unpairing stays
  `keys rm` (protected ritual included) — no `ext rm`.
- **`ext login [--scope <s>] [--timeout <sec>] [--note <s>]`:**
  demonstrates the blocking mobile flow: `ehem_login_mobile` + one
  authenticated call (default scope `system:config`, default timeout
  60 s) → prints the outcome. Exit codes: 0 approved (+ config summary),
  distinct codes for rejected, timeout, no-paired-authenticator, and
  unreachable — the rejected/timeout distinction visible to scripts is
  the point (HEM-SDK-8 made observable).
- All subcommands are public-API-only (the standing hem-tool rule);
  broker access goes through the REQ-AUTH-008 client.

**Rationale:** the tool is the living documentation of the flow and the
vehicle for STEP-M8-080's attended run: `ext pair` + a phone is how the
user actually onboards, and `ext login`'s exit codes let the attended
transcript prove USER_REJECTED vs CONFIRM_TIMEOUT end-to-end. Terminal
QR (user decision 2026-07-22) removes the last external tool from the
loop.

**Acceptance criteria:**
- [ ] Unit (hem-tool-core, fake transport): pair happy path hits the
      legs in order and passes `{kid, code}` verbatim to finalise; scan
      timeout / 406 / broker-error exit codes; list EXTAID filter +
      protected marking; login exit codes for approved, rejected,
      timeout (scripted broker).
- [ ] Unit: QR module renders a known payload to the expected module
      matrix (fixture from the qrcodegen reference); output fits
      standard 80-col terminals for the observed link lengths (else
      falls back to payload print + a notice).
- [ ] Export/header gates stay green with qrcodegen vendored (tool-only
      linkage proven).
- [ ] Live (attended, STEP-M8-080): `ext pair` scanned from the terminal
      QR by the real Encedo app completes registration; `ext list` shows
      the phone; `ext login` demonstrates approve, reject, and timeout
      exit codes; results recorded here.
