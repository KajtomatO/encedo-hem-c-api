---
id: STEP-M12-020
title: "ehem_build_info_get + hem-tool initialises the SDK up front (exit 12 refusal message) + hem-tool --version"
milestone: M12
implements: ["REQ-API-011", "REQ-TOOL-024", "REQ-TOOL-025"]
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-010"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** the SDK exports `ehem_build_info_get()` (backend names, compiled
vs loaded versions for crypto and libcurl — works before, after and
instead of a successful init); hem-tool calls `ehem_global_init()` right
after argument parsing and, on failure, prints one message (backend, both
versions, the remedy) and exits **12**; `hem-tool --version` / `version`
prints the three-line report; `help`, `--help`, `--version` run before the
init and therefore on any library.

**Notes:** SDK side — `include/ehem/ehem.h`: `ehem_build_info` struct +
`EHEM_API void ehem_build_info_get(ehem_build_info *out)` (static,
library-owned strings; NULL = no-op; REQ-API-002 holds — no mutable
globals). Facts come from the shim (`ehem_crypto_backend_name()`,
`LIBWOLFSSL_VERSION_STRING` vs `wolfSSL_lib_version()`) and from
`src/transport_curl.c` (`LIBCURL_VERSION` vs
`curl_version_info(CURLVERSION_NOW)->version`). New exported symbol → an
*addition* to the 1.0 baseline (legal, REQ-API-008); docs/API-GUIDE.md
index row (docs gate enforces it). The "dynamic / static" word for the
crypto library is a compile-time fact the build knows — STEP-M12-060's
switch sets a definition (`EHEM_CRYPTO_LINK_STATIC`); until it lands,
derive it from the resolved library file's extension at configure time
or omit the word (settle at start, keep the output format stable).
Tool side — `src/tools/hem-tool/main.c`: today only `ehem_global_cleanup()`
runs at the end; add the init after parsing and after the pre-init
short-circuit (`help`, `--help`, `--version`; `keys types` joins it when
STEP-M12-040 lands — whichever step lands second wires the other's
command into the short-circuit). Exit code 12 as a named constant in a
hem-tool-core header (STEP-M12-100's delay-load hook reuses it). The
message: ASCII only (`check_ascii_strings` gate), `%s` only (MinGW
printf gotcha), names the remedy (companion asset beside the binary, or
the `-static` hem-tool once REQ-BUILD-009 ships — word it so it is true
before 1.2.0 too). `--version` wins over a following command like
`--help` does; fold `--version` into the existing usage lines so the
top page stays ≤ 45 (test_registry `test_top_page_shape`). Unit tests use
STEP-M12-010's seam to force a mismatch: `status` → message, exit 12,
zero transport I/O; `help`/`--help`/`--version` still exit 0. Release
README.txt (scripts/release/package.sh) documents exit 12.

**Definition of done**
- [ ] `ehem_build_info_get` declared in ehem.h, exported, indexed in
      docs/API-GUIDE.md; `ehem_version()` untouched; export gate green
      (addition only).
- [ ] Unit: every string non-NULL; backend name and compiled version equal
      the header constants; libcurl pair filled; identical values before
      init, after init and after a forced REQ-API-010 refusal.
- [ ] hem-tool calls `ehem_global_init()` exactly once per run, before any
      output or traffic; under a forced mismatch `status` prints one message
      (backend, compiled version, loaded version, remedy), exits 12, zero
      transport I/O (unit smoke, `verifies:` tagged).
- [ ] `help`, `--help`, `--version` succeed under the same forced mismatch;
      without a forced mismatch the existing unit suite is green unchanged.
- [ ] `hem-tool --version` and `hem-tool version` print `hem-tool <version>`
      + crypto line + transport line, exit 0, zero I/O; `--version status`
      prints the version; output ASCII; values equal `ehem_build_info_get`
      and `ehem_version()`.
- [ ] Top-level help ≤ 45 lines with the usage line amended; exit code 12
      in the help page's exit-code note and in the release README.txt.
- [ ] `./dev ci` gcc + clang, ASan, ASCII gate, MinGW syntax check green;
      `implements:` tags for REQ-API-011 (shim/transport/ehem.c) and
      REQ-TOOL-024/025 (main.c / registry).
