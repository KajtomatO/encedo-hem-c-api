---
id: STEP-M9-060
title: "M9 gate — the 1.0 release"
milestone: M9
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M9-050"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Fresh `./dev ci` green (GCC+Clang) + ASan; export/header/docs
      gates green.
- [ ] Fresh `./dev test it` full integration sweep green against the
      dev device; device left clean (0 EHEMTEST, protected set
      intact).
- [ ] Attended: `--mobile` approve + reject demonstrated
      (REQ-TOOL-018 criterion closed).
- [ ] REQ-KEY-008 + REQ-SYS-008 open criteria dispositioned (resolved
      or re-carried by recorded user decision).
- [ ] CI green on push confirmed by the user — linux + windows-mingw,
      covering the backlog since STEP-M8-070.
- [ ] TRACE.md regenerated (§4.3); all M9 REQs at their evidence-backed
      status; no orphans/broken anchors; coverage report clean.
- [ ] ARCHITECTURE §11 M9 marked gate-passed; KNOWN-ISSUES and the
      upstream-filing pile reviewed for release notes.
- [ ] `v1.0.0` tag suggested to the user (user tags/pushes).
