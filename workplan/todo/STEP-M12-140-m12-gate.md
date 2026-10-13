---
id: STEP-M12-140
title: "M12 gate — every variant built and asserted in CI, both backends green, attended per-variant status, foreign-library refusal demo, attended wizard, docs checklist, TRACE regen, the 1.2.0 release"
milestone: M12
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-130"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** 1.2.0 ships: every published variant built in CI with its link
mode asserted and the unit suite green on both backends; attended
`hem-tool status` under system trust for each of the four published
hem-tool binaries (dynamic + `-static`, Linux + Windows); the
foreign-library refusal demonstrated — the release dynamic Linux hem-tool
without its companion library, on the dev host whose `libwolfssl.so.42`
is 5.6.6, prints the REQ-TOOL-024 message and exits 12 instead of
crashing; the Windows missing-DLL message shown live (DLL removed, then
restored); an attended `init-wizard` run end to end on the second unit
(Linux and Windows consoles, echo-off included) followed by `status` +
passphrase login; every attended criterion of REQ-API-010, REQ-TOOL-024,
REQ-TOOL-026, REQ-TOOL-028 and REQ-BUILD-009 recorded in its REQ; the
docs checklist re-verified; TRACE regenerated with all M12 REQs
transitioned; ARCHITECTURE §11 M12 marked gate-passed; the release per
docs/RELEASING.md "Final release" (merge, `v1.2.0` tag — the user tags).

**Notes:** Milestone gate (chore, `implements: []`). Attended parts in
one sitting with the user: (1) the four binaries from the rc
pre-release, each `status` against the dev device under system trust
(my.ence.do, public-CA cert), the static ones from a directory with NO
companion library; (2) the refusal demo on this host (`env -i
./hem-tool status` with the companion library absent → exit 12, the
message naming wolfSSL 5.7.2 compiled / 5.6.6 loaded — the exact
KNOWN-ISSUES scenario; the dev host is the only place with a foreign
same-SONAME library at hand); (3) Windows: the published zip, DLL removed
→ the message; DLL restored → `status` works; (4) the wizard on the second
unit — it must be uninitialised: a `wipe-device` first destroys that
unit's keys and any pairing, the user decides at the sitting (REQ-TEST-007
attended-only; the M10 gate did the same cycle); then `status` + login
with the chosen passphrase; (5) the attended tty echo-restoration check
on both consoles (REQ-TOOL-028). CI evidence: the `ci.yml` run on
`development` (all legs: default, switch legs, openssl Linux + Windows)
and the release run of the rc tag (every variant, every assertion) —
read from the Actions API, run ids recorded. Fresh `./dev ci` gcc + clang
+ ASan + gates on both backends locally; full `./dev test it` on the
wolfSSL build (the OpenSSL live sweep was STEP-M12-090's — repeat only if
the shim changed since) — the KNOWN sustained-load stall may need a
power-cycle; device left clean (0 EHEMTEST, protected set intact). Docs
checklist (REQUIREMENTS-MANAGEMENT.md §5.3 / RELEASING "Final release"):
README Status block (version, date, scope, next milestone), new commands
and APIs, Documentation list, Layout; docs/COVERAGE.md; KNOWN-ISSUES.md —
re-read against the shipped behaviour, since the M10 gate's miss is why
this box exists. TRACE (§4.3): REQ-API-010/011, REQ-BUILD-006..009,
REQ-TOOL-024..028 → implemented/verified per evidence (BUILD-006/008/009
may stay `implemented` like BUILD-002/005 — proven by workflow runs — if
no `verifies:` test exists; say so); REQ-NET-005 back to verified;
upstream-filing pile and BACKLOG.md reviewed (macOS, MSVC candidates
stay). Tagging/committing/pushing is the user's (feedback: never commit
without asking).

**Definition of done**
- [ ] CI: `ci.yml` green on `development` with every leg (default, switch
      legs, openssl Linux + Windows); the rc release run green with every
      variant's link-mode / no-wolfSSL / smoke assertions (run ids
      recorded).
- [ ] Fresh local `./dev ci` gcc + clang + ASan + gates green on both
      backends; `./dev test it` green on the wolfSSL build; device clean.
- [ ] Attended: `status` under system trust for all four published
      hem-tool binaries (static ones without a companion library) —
      recorded in REQ-BUILD-009.
- [ ] Attended: foreign-library refusal demo on this host (release dynamic
      Linux hem-tool, no companion, system 5.6.6 → message, exit 12, no
      crash) — recorded in REQ-API-010 and REQ-TOOL-024.
- [ ] Attended: Windows missing-DLL message (zip without DLL → message +
      exit 12; DLL restored → works) — recorded in REQ-TOOL-026.
- [ ] Attended: `init-wizard` end to end on the second unit on Linux and
      Windows (echo-off and restoration checked), then `status` + login —
      recorded in REQ-TOOL-028.
- [ ] Docs checklist re-verified against the shipped behaviour: README
      Status block / commands / APIs / Documentation / Layout,
      docs/COVERAGE.md, KNOWN-ISSUES.md, docs/RELEASING.md.
- [ ] TRACE.md regenerated (§4.3): all M12 REQs transitioned, REQ-NET-005
      verified again, no orphans/broken anchors, zero deliberately open
      criteria or each one named with its reason.
- [ ] ARCHITECTURE §11 M12 marked gate-passed; KNOWN-ISSUES and BACKLOG
      reviewed; `v1.2.0` release per RELEASING "Final release" suggested
      to the user (merge `development` → `main`, annotated tag — user tags).
