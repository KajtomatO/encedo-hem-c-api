---
id: STEP-M2-005
title: "./dev developer assist tool: build/clean/test/check/ci/tool/install-dependencies + bash completion"
milestone: M2
implements: ["REQ-BUILD-004"]
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#10-directory-layout"]
depends_on: []
evidence:
  commits: []          # pending user commit — add the [STEP-M2-005] SHA(s) here
  tests: []            # no verifies: tag — bash tool, verified manually (see notes)
  notes: |
    Verified on the dev machine 2026-07-16 (device https://my.ence.do reachable,
    valid cert — no --insecure needed). `./dev` is tagged `implements: REQ-BUILD-004`
    in its header comment (repo-root file, same convention as CMakeLists.txt /
    ci.yml per TRACE.md §4.2 SETUP note); no verifies: unit test — it is dev
    tooling exercised by the runs below.

    All REQ-BUILD-004 commands demonstrated (exit codes in parens):
      - build (gcc default, reused cc-configured build/ w/o reconfigure) (0)
      - build --clang -> build-clang/ (CMAKE_C_COMPILER=/usr/bin/clang) (0)
      - build --both  -> build/ + build-clang/ (0)
      - clean -> removed build/ only; clean --all -> removed all build*/;
        clean --all again -> "nothing to remove" (0)
      - test / test ut -> 9 unit tests pass (0)
      - test asan -> build-asan/ w/ EHEM_SANITIZE=ON, 9 unit pass, ASan/LSan clean (0)
      - test it -d -> hem.env fallback notice on stderr; 2 integration tests pass
        live; empty `disruptive` label ignored via --no-tests=ignore (0)
      - test all -> unit(9) + integration(2 live) pass (0)
      - check -> only export_symbols + public_headers_curl_free run, both pass (0)
      - ci -> gcc + clang unit legs both green, "all legs green" (0)
      - tool status -> live device status printed (fw v1.2.2-DIAG) (0)
      - install-dependencies --yes -> apt path; all deps already newest incl.
        libwolfssl-dev 5.6.6 (ready for M2-010); verification summary all ok (0)
      - EHEM_DEV_CC=clang selects build-clang/ as the default (0)
      - error paths (unknown command/option/suite, `test ut -d`) exit 1
      - "already set" path: EHEM_URL set -> zero hem.env sourcing notices

    Completions: `source <(./dev completions)` defines `_dev_complete`; driven
    programmatically, completes subcommands (`./dev <tab>`), test suites
    (`test <tab>` -> ut/it/all/asan), test flags (`test it <tab>` -> -d/--disruptive),
    build flags (`build --<tab>`), and prefixes (`c<tab>` -> clean/check/ci/completions).

    shellcheck 0.9.0: clean (one initial SC2015 info finding fixed by rewriting
    `A && shift || true` as an explicit `if`).

    Docs: ARCHITECTURE §10 layout gained a `dev` line; README gained a
    "Developer workflow (./dev)" section. CI workflow untouched (git status
    clean for .github/); plain `cmake -B build && cmake --build build &&
    ctest --test-dir build -L unit` still documented (README) and green.

    Windows/MSYS2: install-dependencies has a pacman branch (mingw-w64 toolset
    matching CI's pinned list) but is unverified this session — deferred to the
    next Windows session per REQ-BUILD-004 acceptance criterion.
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
- [x] All REQ-BUILD-004 commands demonstrated on the dev machine (evidence
      lists the command outputs/exit codes, incl. hem.env fallback notice
      and a `--disruptive` dry composition).
- [x] `source <(./dev completions)` verified: subcommands, test suites,
      and flags complete via <tab> (manual evidence).
- [x] shellcheck clean or findings justified in evidence.
- [x] ARCHITECTURE §10 layout + README updated; CI untouched and green;
      plain `cmake -B build && ctest` still documented and working.
