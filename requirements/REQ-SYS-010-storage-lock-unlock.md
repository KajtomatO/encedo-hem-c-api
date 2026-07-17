---
id: REQ-SYS-010
title: Bindings for /api/storage/unlock and /api/storage/lock — USB-MSC partition visibility
status: approved
priority: must
revision: 2
source: ARCHITECTURE.md §11 (M7: storage group); encedo-hem-api-doc storage/unlock.md, storage/lock.md, discrepancies/DISCREPANCIES-HEM-TEST.md §5; encedo_firmware api_storage.c (fw v1.2.2 — including the uninitialized `sub` read in both handlers' scope checks, api_storage.c:44-46/132-134); encedo-hem-python-api storage.py (OQ-24 no-op observation); approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
---

# Bindings for /api/storage/unlock and /api/storage/lock — USB-MSC partition visibility

The SDK SHALL provide `ehem_storage_unlock(ctx, disk, writable)` and
`ehem_storage_lock(ctx, disk)` over `GET /api/storage/unlock` /
`GET /api/storage/lock`, composing the required scope from the
arguments, and treating empty-200 as success.

- **The scope string carries the arguments** — the firmware reads the
  disk index and mode from the JWT scope, not the URL: unlock requests
  scope `storage:disk<N>` (read-only) or `storage:disk<N>:rw`
  (writable); lock requests `storage:disk<N>`. The SDK uses the plain
  URL paths (no `/ro`/`/rw` suffix — the tester-proven form; the
  suffixes exist as a Manager-side narrowing override only).
- `disk` ∈ {0 (plaintext partition), 1 (encrypted partition)}; 2 is
  DEBUG-build-only — the SDK accepts 0..2 and lets the device 406 what
  it doesn't support. Pre-validation: disk outside 0..2 →
  `EHEM_ERR_ARG`.
- "Unlock" only flips USB-MSC visibility flags (`sd_card_map[n]`); it is
  not a crypto operation (doc note) — but it exposes the partition to
  whatever host the device's USB port is plugged into; the binding docs
  say so.
- PPA-only (`USB_MSC_AVAILABLE`); on EPA the route is absent → 404 →
  `EHEM_ERR_NOT_FOUND`, tests skip with a note.
- Errors: 406 (bad disk digit / mode not allowed by scope) →
  `EHEM_ERR_DEVICE` with detail; 403 → `EHEM_ERR_SCOPE_DENIED`; 409
  (fls_state, uninitialised, formatting) → `EHEM_ERR_DEVICE`.
- **Firmware bug (found by inspection, this decomposition):** both
  handlers evaluate `strcmp(sub, "M")` on an **uninitialized pointer**
  (`sub` is only assigned inside the audit-log branch,
  api_storage.c:44-46 and :132-134) — behavior when the scope prefix
  matches is undefined: may pass, 403, or hard-fault (which reboots the
  device via the HardFault handler; the watchdog is disabled). The live
  probe is therefore **attended and disruptive-gated first**; the test
  is downgraded to the plain `integration` label only after the device
  proves stable. Upstream filing candidate.

**Rationale:** M7 milestone "storage group". Completes the last
non-deferred endpoint group; the scope-carries-arguments pattern is
unique on this device and worth pinning in SDK code + tests before a
consumer trips over it.

**Acceptance criteria:**
- [ ] Unit (fake transport): unlock(0,false) requests scope
      `storage:disk0`, unlock(1,true) → `storage:disk1:rw`, lock(1) →
      `storage:disk1` (scope asserted via the recorded challenge/eJWT or
      the auth seam); plain paths; empty-200 → OK; 403/406/409/404
      mapping; disk 3 → `EHEM_ERR_ARG`, no I/O.
- [x] Live (attended, disruptive-gated, user-authorized 2026-07-18, TWO
      clean runs): unlock disk 0 read-only → 200; status green
      MID-UNLOCK; lock → 200. The uninitialized-`sub` read did not
      fault or 403 in practice on this build (stack garbage happened
      benign — still an upstream-filing bug, since UB can shift with any
      firmware change). Python OQ-24's "no-op" impression explained: the
      call only flips USB-MSC visibility, invisible from the API side.
- [x] ~~OPEN~~ **RESOLVED (live 2026-07-18):** the rw scope variant
      (`storage:disk0:rw`) is granted and accepted end-to-end (this auth
      model self-selects scopes, so rw is available to any credential
      holder); PPA routing confirmed (route present — consistent with
      the REQ-SYS-007 se_state PPA determination).
- [x] ~~OPEN~~ **DECIDED (2026-07-18):** the live test STAYS
      `disruptive`-labeled, two reasons recorded: (1) the handler UB
      remains UB — clean runs today do not make it safe against firmware
      changes; (2) a successful unlock exposes the microSD to whatever
      host holds the device's USB — not appropriate as an unattended
      every-suite side effect.
