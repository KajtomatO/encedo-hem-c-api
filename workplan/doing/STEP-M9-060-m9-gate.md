---
id: STEP-M9-060
title: "M9 gate — the 1.0 release"
milestone: M9
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M9-050"]
evidence:
  commits: ["db2a1ce", "a6f3166"]
  tests: []
  notes: >
    Gate run 2026-08-06 (attended). Fresh ./dev ci 41/41 GCC+Clang +
    ASan + all gates (docs_coverage + export baseline included).
    Attended --mobile: approve at +4 s measured drift (push notice
    shown; the STEP-M9-055 window) exit 0; reject exit 14 — real phone
    SM-S938B. Full ./dev test it: attempt 1 hard-stalled the device at
    test 15/23 (KNOWN sustained-load stall, matrix-free; KNOWN-ISSUES
    updated) -> physical power-cycle -> attempt 2 23/23 GREEN (337 s).
    KEY-008 probe (scratchpad key008_probe.c vs shared lib): dedup does
    NOT persist across delete, same boot or across reboot, on the
    healthy repo — content-derived kid identical every import; July
    observation = pre-wipe debris (REQ-KEY-008 rev 4). SYS-008 attended
    shutdown as the LAST live act (user decision): EHEM_OK, device DARK
    ~3 s later, physical power-cycle = recovery (REQ-SYS-008 rev 2).
    Device left: shutdown-dark by design; repo clean (probe cleaned its
    key; sweep leaves 0 EHEMTEST; 3 protected keys incl. the phone
    pairing). ZERO open acceptance criteria across all 84 REQs.
    PENDING (the one unchecked box): user pushes ~30 local commits;
    linux + windows-mingw CI must be confirmed green (Windows backlog
    since STEP-M8-070). Tag v1.0.0 suggested — user tags/pushes.
reopened: []
cancelled: null
---

**Goal:** 1.0 ships: fresh full live sweep green, the attended
`--mobile` demonstration done, the two carried-open criteria
dispositioned by user decision, TRACE regenerated with all M9 REQs
transitioned, CI green on push (including the commits since
STEP-M8-070 that have never seen windows-mingw), and the v1.0.0 tag
suggested to the user.

**Notes:** Milestone gate (chore, `implements: []`). Attended parts in
one sitting with the user: (1) REQ-TOOL-018's open criterion — a
representative `--mobile` command approved then rejected on the real
phone (broker pushes never unattended, REQ-TEST-006 policy); (2) the
carried-open criteria — REQ-KEY-008 dedup-across-reboot re-probe
(recommended: run it, cheap and disruptive-gated) and REQ-SYS-008
attended shutdown (physical power-cycle to recover — user's call);
each either resolved or explicitly re-carried, decision recorded in
the REQ. Keygen matrix stays quarantined `disruptive` (REQ-TEST-004
rev 3) — not part of the default sweep. Tagging/committing/pushing is
the user's (feedback: never commit without asking).

**Definition of done**
- [x] Fresh `./dev ci` green (GCC+Clang) + ASan; export/header/docs
      gates green (41/41, 2026-08-06).
- [x] Fresh `./dev test it` full integration sweep green — 23/23
      (337 s) post-power-cycle (attempt 1 = the known stall, recorded);
      device clean (0 EHEMTEST, protected set intact).
- [x] Attended: `--mobile` approve (exit 0, +4 s drift, notice shown)
      + reject (exit 14) demonstrated (REQ-TOOL-018 criterion closed).
- [x] REQ-KEY-008 + REQ-SYS-008 open criteria RESOLVED live (user
      chose to run both probes; KEY-008 rev 4, SYS-008 rev 2).
- [ ] CI green on push confirmed by the user — linux + windows-mingw,
      covering the backlog since STEP-M8-070.
- [x] TRACE.md regenerated (§4.3); all M9 REQs verified; ZERO open
      criteria project-wide; no orphans/broken anchors.
- [x] ARCHITECTURE §11 M9 marked gate-passed; KNOWN-ISSUES updated
      (gate stall occurrence) and the upstream-filing pile reviewed
      (5 open groups + COVERAGE.md doc-fix candidates).
- [x] `v1.0.0` tag suggested to the user (user tags/pushes).
