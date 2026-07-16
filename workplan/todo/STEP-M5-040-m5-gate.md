---
id: STEP-M5-040
title: "M5 gate — full family matrix live; KEY-005/KEY-006 criteria closed; trace regen"
milestone: M5
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M5-010", "STEP-M5-020", "STEP-M5-030"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** The M5 milestone gate demonstrated live against the dev device:
the full per-family generation matrix green (generate → list → sign where
ExDSA-capable → delete, all 21 fw v1.2.2 types — §11 gate wording per the
2026-07-16 decomposition), plus the `keys gen` tool demo noted.
Bookkeeping: REQ-KEY-005's boundary criterion and REQ-KEY-006's flag-set
vocabulary criterion recorded and checked; REQ-TEST-004's open live
criteria recorded; TRACE.md regenerated per §4.3.

**Notes:** Chore step (implements: []) — gate + trace regen, mirrors
STEP-M2-070/M3-070/M4-060. Gate criterion per ARCHITECTURE §11 M5
(amended at decomposition): the matrix cycle per family on the real
device. Integration suite fully green (`./dev test it`), unit suites
green gcc/clang + ASan, Windows CI green. Record any device/doc
divergences found on the way in the affected REQs (device > doc).
Hardware random is out of M5 scope (no endpoint in fw v1.2.2) — its plan
is REQ-OPS-002 (draft, M6 encrypt-IV harvest); nothing to gate here.

**Definition of done**
- [ ] Fresh full integration run green including the matrix test; unit
      gcc+clang + ASan green; Windows CI green.
- [ ] REQ-KEY-006 flag-set vocabulary criterion checked off with the
      recorded live strings; REQ-KEY-005 boundary criterion checked off
      (done in M5-010, verified still recorded); REQ-TEST-004 live
      criteria recorded.
- [ ] `keys gen` live demo evidence present (M5-030) and referenced.
- [ ] TRACE.md regenerated (§4.3); coverage report clean of new
      violations; summary reported in chat.
