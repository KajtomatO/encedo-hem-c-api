---
id: STEP-M3-070
title: "M3 gate: goal.txt tool milestone live demo; TRACE regen"
milestone: M3
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M3-030", "STEP-M3-040", "STEP-M3-060"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** Milestone gate (chore step — verification, no new REQ): the
goal.txt tool milestone demonstrated live against the dev device —
`hem-tool keys list` shows the repo with the TLS pair marked
`[PROTECTED]`; create EHEMTEST keys, `keys rm --label-prefix EHEMTEST
--yes` removes them; `keys rm --all --dry-run` demonstrably excludes the
protected keys from bulk removal. `ctest -L integration` fully green
with EHEM_TEST_URL + EHEM_TEST_PASSPHRASE (now including the keymgmt
create→list→search→get→delete round-trips). Open criteria resolved or
explicitly carried: REQ-KEY-002 no-match status code, REQ-KEY-003 scope
probe (recorded in §12 risk 3), REQ-KEY-005 label/descr limits (may
carry to M5). Regenerate TRACE.md (§4.3).

**Notes:** Chore/gate step, `implements: []` per §5.1 — it verifies
REQ-KEY-001..005 + REQ-TOOL-004..006 + REQ-TEST-003 rather than adding
scope. No protected key is ever deleted on the dev device — the guard
demo is dry-run only. Verify no EHEMTEST leftovers remain on the device
after the full suite (REQ-TEST-003 hygiene). After the gate: TRACE
regeneration once the user has committed the M3 work.

**Definition of done**
- [ ] Live demo recorded: keys list with protected marks; EHEMTEST
      create + keys rm removal; --all --dry-run excludes protected keys.
- [ ] `ctest -L integration` green (unit suites green on GCC + Clang;
      ASan/LSan clean; CI green on Linux + Windows MinGW).
- [ ] REQ open criteria resolved with live facts or explicitly deferred
      with a note (KEY-002 404 fact, KEY-003 scope probe → §12 risk 3,
      KEY-005 limits); no EHEMTEST leftovers on the device.
- [ ] TRACE.md regenerated (§4.3); coverage report clean for M3 REQs.
