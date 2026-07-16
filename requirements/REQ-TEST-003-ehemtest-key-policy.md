---
id: REQ-TEST-003
title: Device-key hygiene in tests — EHEMTEST prefix, own-key-only mutation
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §9 (reserved EHEMTEST prefix); user decision 2026-07-15 (disposable device EXCEPT the protected set); REQ-TEST-002 rationale ("those rules bind from M3"); approved 2026-07-16
depends_on: ["REQ-TEST-002", "REQ-KEY-005", "REQ-KEY-004"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy"]
---

# Device-key hygiene in tests — EHEMTEST prefix, own-key-only mutation

Integration and disruptive tests SHALL confine device-key mutations to
keys carrying the reserved label prefix `EHEMTEST`: every key a test
creates is labeled `EHEMTEST…`, every key a test deletes matches that
prefix (its own creations, plus optionally leftover `EHEMTEST` keys from
interrupted earlier runs — the reason the prefix is reserved), and no
test modifies or deletes any other key — the protected set (REQ-TOOL-005
labels) is thereby doubly out of bounds. Each test cleans up its
creations before finishing, pass or fail, so the disposable dev device
does not accumulate garbage.

**Rationale:** the disposable-device decision (2026-07-15) allows tests
to create/delete freely *except* the device's TLS material and paired
authenticators; a reserved prefix makes test keys recognizable, safely
sweepable, and distinguishable from anything a human created. This rule
was announced in REQ-TEST-002's rationale as binding from M3 — this REQ
makes it normative now that M3 tests actually create keys.

**Acceptance criteria:**
- [x] A shared test-support helper produces `EHEMTEST`-prefixed labels
      (unique per run, e.g. suffixed with a timestamp) and a cleanup
      routine deleting the keys it registered; tests use it instead of
      ad-hoc labels (grep: no integration test creates a key with a
      literal non-EHEMTEST label). — tests/support/ehem_test_keys.h
      (ehem_test_label = "EHEMTEST-<time>-<counter>", ehem_test_track /
      ehem_test_cleanup); the only key-creating test uses it.
- [x] Cleanup runs even when the test body fails (teardown path), and a
      leftover-sweep utility (or test-suite preamble) can remove stray
      `EHEMTEST` keys by prefix. — cmocka setup/teardown: cleanup in
      teardown (runs after a failed body), ehem_test_sweep in setup
      (verified: a leftover from an earlier failed run was swept).
- [x] No integration/disruptive test issues a delete/update for a kid it
      did not create or sweep by `EHEMTEST` prefix (review criterion,
      checked at M3 gate). — the sole mutating test deletes only its own
      tracked kids and EHEMTEST-prefixed sweep hits; to be re-confirmed at
      the M3 gate as more mutating tests land.
