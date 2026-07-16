---
id: STEP-M2-005
title: "./dev developer assist tool: build/clean/test/check/ci/tool/install-dependencies + bash completion"
milestone: M2
implements: ["REQ-BUILD-004"]
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#10-directory-layout"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** Executable bash script `./dev` at the repo root implementing the
REQ-BUILD-004 command surface: build (EHEM_DEV_CC default, --gcc/--clang/
--both, per-compiler build dirs), clean [--all], test ut|it|all|asan with
-d/--disruptive, check (export + header gates), ci (local CI mirror),
tool passthrough, install-dependencies (apt/pacman), completions, help.
Credentials fallback: auto-source ./hem.env when EHEM_* vars are unset
(stderr notice).

**Notes:** Numbered 005 so the tool exists before the M2 code steps — it
is the dev loop for all of them. Keep it a thin wrapper: cmake/ctest
invocations only, no logic CI doesn't also have (REQ-BUILD-004 last
criterion). `test all` composes label selections (`-L unit`,
`-L integration`, `+ -L disruptive` with -d) as separate ctest runs so a
missing label never fails the set; `-d` also exports
EHEM_ALLOW_DISRUPTIVE=1 (label arrives with STEP-M2-060). asan =
GCC-only (Clang ASan runtime missing on dev machine, M1 evidence).
Completion: single `_dev_complete` function + `complete -F` emitted by
`./dev completions`. Also: chmod +x tracked in git; add `dev` to
ARCHITECTURE §10 layout listing and a short README "Developer workflow"
blurb; shellcheck if available.

**Definition of done**
- [ ] All REQ-BUILD-004 commands demonstrated on the dev machine (evidence
      lists the command outputs/exit codes, incl. hem.env fallback notice
      and a `--disruptive` dry composition).
- [ ] `source <(./dev completions)` verified: subcommands, test suites,
      and flags complete via <tab> (manual evidence).
- [ ] shellcheck clean or findings justified in evidence.
- [ ] ARCHITECTURE §10 layout + README updated; CI untouched and green;
      plain `cmake -B build && ctest` still documented and working.
