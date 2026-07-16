---
id: STEP-M2-070
title: "M2 gate: live authenticated round-trip; record KDF/sub facts; close §12 risk 2"
milestone: M2
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M2-060"]
evidence:
  commits:
    - "b9686b4 — M2 gate: regenerate TRACE, close §12 risk 2, verify live"
  tests:
    - "tests/integration/test_auth_live.c — live login → scoped token → cache reuse; sub=U, TTL=3598s"
    - "tests/integration/test_config_live.c — live authenticated config GET (devid 3dfd39eb56787905, hostname my.ence.do)"
    - "tests/integration/test_checkin_live.c — live check-in + REQ-SYS-006 harvest (current_serial=C173D2A9148ECD6225A3DFE5299B82CE)"
    - "requirements/TRACE.md — regenerated (§4.3): 29 REQs, 25 verified / 4 implemented"
  notes: >
    M2 milestone gate (chore, implements: []). Live against my.ence.do:
    login → scoped bearer (sub=U = UserKey/PBKDF2, scope echoed) → same-scope
    cache reuse (token==token2) → authenticated config GET returns device data;
    TTL 3598s. `ctest -L integration` 4/4 green with EHEM_TEST_URL +
    EHEM_TEST_PASSPHRASE; hem-tool cert-install exercised live (exits 0 "already
    current"). Recorded PBKDF2-600k + sub=U in REQ-AUTH-001; resolved
    REQ-SYS-004 live config shape (M2-050) — both criteria already RESOLVED in
    the REQs, verified here. ARCHITECTURE §12 risk 2 marked RESOLVED (dev device
    is UserKey/PBKDF2; Argon2 deferred). TRACE.md regenerated per §4.3 (status
    transitions applied to 26 REQs; added missing implements:/verifies: tags for
    SYS-004/005/006, AUTH-003, TOOL-003 so tracing is accurate). Coverage report
    flags TOOL-001/002 + BUILD-002/004 as code-without-unit-test (live/manual
    only) and M1 + M2-040/045/050 steps with empty evidence.commits (non-blocking
    hygiene). Also fixed a portability bug the Windows-CI check surfaced:
    cert_install.c nanosleep → Sleep() on _WIN32; test_cert_install.c
    open_memstream → portable tmpfile() (both would have broken the MinGW unit
    build). Linux gcc/clang unit 15/15 + ASan clean; export/header gates green.
    Uncommitted per [[feedback_never_commit.md]]; evidence.commits: [].
reopened: []
cancelled: null
---

**Goal:** Milestone gate (chore step — verification, no new REQ): against
the real dev-machine HEM, demonstrate login → scoped call → silent
refresh behavior end to end; `ctest -L integration` fully green with
EHEM_TEST_URL + EHEM_TEST_PASSPHRASE; `hem-tool` authenticated paths
work. Record into REQ-AUTH-001's open criterion: the accepted KDF
(PBKDF2-600k confirmed) and the issued token's `sub` claim (U or M).
Update ARCHITECTURE §12 risk 2 to resolved. Resolve REQ-SYS-004's open
criterion (live config shape).

**Notes:** Chore/gate step, `implements: []` per §5.1 — it verifies
REQ-AUTH-001/002/003 + REQ-SYS-004..006 rather than adding scope. The
disruptive cert-install live run is NOT required for the gate (may stay
deferred per REQ-TOOL-003). After the gate: regenerate TRACE.md (§4.3)
once the user has committed the M2 work.

**Definition of done**
- [x] Live: login succeeds, an authenticated binding returns device data,
      cache reuse observed (test_auth_live asserts token==token2 on the second
      same-scope ensure; test_config_live returns typed config).
- [x] REQ-AUTH-001 + REQ-SYS-004 open criteria resolved with live facts
      (sub=U / PBKDF2-600k; live config shape); ARCHITECTURE §12 risk 2 marked
      RESOLVED.
- [x] All unit + integration suites green on GCC + Clang; ASan/LSan clean.
      Linux CI mirror (`./dev ci`) green; Windows MinGW CI runs on push — the
      two POSIX-only constructs it would have hit were made portable here.
