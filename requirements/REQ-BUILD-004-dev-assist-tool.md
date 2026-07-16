---
id: REQ-BUILD-004
title: Developer assist tool `./dev` — build, test, and environment runner
status: approved
priority: should
revision: 1
source: user decision 2026-07-16 (commissioned with usage spec; name/runtime/extras/compiler-default chosen via Q&A same day; approved 2026-07-16)
depends_on: ["REQ-BUILD-001", "REQ-TEST-002"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#10-directory-layout"]
---

# Developer assist tool `./dev` — build, test, and environment runner

The repository SHALL ship a bash script `./dev` at the repo root wrapping
the routine build/test workflow. It is developer tooling only: not
installed, not part of the SDK's public surface, and never required by CI
or CMake (both keep working without it).

**Command surface:**

| Command | Behavior |
|---|---|
| `./dev build [--gcc\|--clang\|--both]` | configure (if needed) + build; default compiler from `EHEM_DEV_CC` (gcc when unset); per-compiler build dirs (`build/`, `build-clang/`) |
| `./dev clean [--all]` | remove the active compiler's build dir; `--all` removes every `build*/` dir |
| `./dev test` / `./dev test ut` | unit suite only (`ctest -L unit`) |
| `./dev test it [-d\|--disruptive]` | integration suite; `-d` additionally selects the `disruptive` label and sets `EHEM_ALLOW_DISRUPTIVE=1` |
| `./dev test all [-d\|--disruptive]` | unit + integration (+ disruptive with `-d`) |
| `./dev test asan` | ASan/LSan unit run (GCC — Clang's ASan runtime is absent on the dev machine, M1 finding) |
| `./dev check` | static gates only: export-symbols + public-header checks |
| `./dev ci` | mirror the CI matrix locally: gcc + clang unit builds + checks |
| `./dev tool <args…>` | run the built hem-tool with the device environment loaded, e.g. `./dev tool status` |
| `./dev install-dependencies [--yes]` | install build deps; detects apt (Linux) / pacman (MSYS2); prints the package list and asks before installing |
| `./dev completions` | print the bash completion script (`source <(./dev completions)`) |
| `./dev help` / no args | usage |

**Environment fallback:** commands that need device credentials
(`test it`, `test all`, `tool`) use the `EHEM_*` variables when already
set; otherwise they auto-source `./hem.env` (git-ignored, user decision
2026-07-16) and say so on stderr. Missing both → a clear error naming the
variables (integration tests then skip per REQ-TEST-002 if run anyway).

**Completion:** `<tab>` completes subcommands, `test` suites
(`ut`/`it`/`all`/`asan`), and flags.

**Acceptance criteria:**
- [ ] Every command above works from a fresh checkout on the dev machine;
      exit codes propagate (nonzero on any underlying failure) so the tool
      is scriptable.
- [ ] `./dev test` runs unit only; `it`/`all` gate on env with the
      `hem.env` fallback; `-d/--disruptive` adds the label AND
      `EHEM_ALLOW_DISRUPTIVE=1` (forwards cleanly even before the first
      disruptive test exists).
- [ ] `EHEM_DEV_CC` selects the default compiler; `--gcc/--clang/--both`
      override per invocation; the two build dirs never collide.
- [ ] Completion covers subcommands, test suites, and flags (manual
      verification recorded in step evidence).
- [ ] `shellcheck` clean (or findings justified in step evidence); works
      under MSYS2 bash on Windows (evidence may defer to the next Windows
      session; noted if so).
- [ ] CI and plain CMake/CTest remain fully usable without `./dev`
      (no build logic moves exclusively into the script).
