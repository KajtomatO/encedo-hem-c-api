---
id: STEP-M9-055
title: "Broker drift-recovery threshold: close the 1..15 s dead window"
milestone: M9
implements: ["REQ-AUTH-009"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M9-020"]
evidence:
  commits: ["c0d6586"]
  tests: ["tests/unit/test_confirm.c test_begin_drift_recovery (verifies: REQ-AUTH-009 — new dead-window case: iat=now+8 fires check-in + re-fire; far-future and no-evidence cases unchanged)"]
  notes: >
    Diagnosis from live gate-prep failures (logs get --mobile 401 at
    broker event/new): device drift measured +14 s then +22 s
    (~4.8 s/min = the known 8% RTC), i.e. INSIDE the rev-2 15 s
    evidence gate while the broker already rejects — and the user's
    later random --mobile succeeded exactly when drift crossed 15 s
    and the recovery engaged (its check-in also explains the fresh
    resync trace). Fix: EXT_BROKER_IAT_FUTURE_SKEW=2 /
    EXT_BROKER_IAT_PAST_SKEW=15 (asymmetric; user decision "threshold
    only" — wait-out variant declined; accepted residual: a re-fired
    authreq can still land +1..2 s ahead of the broker right after a
    resync). ./dev ci 41/41 GCC+Clang + ASan green.
reopened: []
cancelled: null
---

**Goal:** Mobile login no longer has an unhealable window. Live gate-prep
testing (user, 2026-08-06) found `--mobile` 401-ing at the broker's
`event/new` with the device only **+14 s** ahead: the broker rejects ANY
future authreq `iat` (its "~zero tolerance" was already in KNOWN-ISSUES),
but the M8-080 recovery gate demanded |drift| > 15 s of evidence before
firing the check-in — so in the (0, 15] s window, re-entered ~3 minutes
after every clock sync at the ~8%-fast RTC, the 401 stayed terminal. The
future-direction evidence gate drops to > 2 s (jitter margin); the past
direction stays 15 s (a past iat is acceptable to the broker — a strongly
negative reading means a wrong local clock).

**Notes:** Mid-milestone insertion during gate prep; user decision
2026-08-06: "Threshold only" (the wait-out-residual variant was offered
and declined). Known residual risk, accepted: immediately after a resync
the re-fired authreq can still land +1..2 s ahead of the broker and fail
its zero tolerance — in practice today's live runs show post-check-in
re-fires passing. Diagnosis evidence: drift measured +14 s at the failing
runs and growing ~4.8 s/min; the user's later `random 10 --mobile`
succeeded exactly because drift had crossed 15 s and the recovery finally
engaged.

**Definition of done**
- [x] proto_ext.c: future-drift evidence gate > 2 s (past stays 15 s),
      constants + comment record today's live evidence.
- [x] test_confirm.c: regression for the previously-dead window (authreq
      iat = now+8 → check-in + re-fire happen); existing far-future and
      no-evidence cases stay green.
- [x] REQ-AUTH-009 rev 3 records the narrowed tolerance + asymmetric
      gate; KNOWN-ISSUES mitigation text updated.
- [x] `./dev ci` + ASan green; live (ATTENDED, M9-060 gate,
      2026-08-06): `keys list --mobile` APPROVED at measured +4 s drift
      — inside the formerly-dead (0,15] window — notice + listing,
      exit 0; the old gate would have hard-failed here.
