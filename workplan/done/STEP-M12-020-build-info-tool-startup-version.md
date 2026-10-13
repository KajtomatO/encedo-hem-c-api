---
id: STEP-M12-020
title: "ehem_build_info_get + hem-tool initialises the SDK up front (exit 12 refusal message) + hem-tool --version"
milestone: M12
implements: ["REQ-API-011", "REQ-TOOL-024", "REQ-TOOL-025"]
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-010"]
evidence:
  commits: ["1319bd3", "5be7b1a"]   # start; implementation (the done commit follows)
  tests:
    - "tests/unit/test_build_info.c (verifies: REQ-API-011) — values equal the build's own discovery (pkg-config wolfssl version, FindCURL version, configure-time link modes), loaded == compiled crypto once init passes, identical before init / after init / after a forced version or probe refusal, reserved slots NULL, NULL out a no-op"
    - "tests/unit/test_tool_startup.c (verifies: REQ-TOOL-024, REQ-TOOL-025) — hem-tool-core startup.c: silent init on the matching library; one refusal message (backend, both versions, remedy, companion asset name, ASCII) + exit 12 for both seam modes; init succeeds after a refusal; the three-line --version report rebuilt from ehem_build_info_get, under a forced mismatch"
    - "tool_startup_cli = tests/unit/check_tool_startup.cmake (verifies: REQ-TOOL-024, REQ-TOOL-025) on the REAL main.c (test build hem-tool-forced-mismatch + tool_force_mismatch.c constructor): status/keys list -> exit 12 with no output before the message; help, --help, status --help, help keys rm, --version, version, --version status -> 0; usage errors keep 2; unforced status reaches the transport (exit 1)"
    - "tests/unit/test_registry.c test_top_page_shape — `hem-tool --version` usage line and the `exit 12` note, page 43 lines"
    - "./dev ci 51/51 gcc + clang (gates: export_symbols with ehem_build_info_get as the only new export, public_headers_dep_free, docs_coverage, ascii_strings); ./dev test asan 51/51; MinGW -fsyntax-only clean for ehem.c, crypto_shim.c, transport_curl.c, context.c, startup.c, main.c, registry.c, tool_force_mismatch.c; shellcheck package.sh clean"
  notes: |
    Settled at start: (1) ehem_build_info shape — the REQ's proposed names
    kept + crypto_link / transport_link ("dynamic"/"static", needed by
    REQ-TOOL-025, unknowable to the public-API-only tool) + a fixed-size
    `const char *reserved[8]` tail: the one caller-allocated OUTPUT struct
    cannot use the abi_size discipline (nothing stamps the caller's size
    before the call), so its size is frozen for 1.x and new facts take a
    reserved slot (ABI promise at ehem_version() in ehem.h; ARCHITECTURE §4
    and API-GUIDE ownership notes). REQ-API-011 rev 2. (2) The link word
    is derived NOW at configure time, as the step allowed: CMake
    `ehem_link_mode()` classifies the resolved wolfSSL (WOLFSSL_LINK_LIBRARIES)
    and CURL::libcurl (alias -> STATIC_LIBRARY, or the imported location's
    extension; a MinGW .dll.a is an import library = dynamic) and passes
    EHEM_CRYPTO_LINK_STATIC / EHEM_TRANSPORT_LINK_STATIC; configure prints
    "encedo-hem: crypto wolfSSL 5.6.6 (dynamic), transport libcurl 8.5.0
    (dynamic)" on the dev host; the release prefix's curl config makes
    CURL::libcurl an ALIAS of the STATIC IMPORTED CURL::libcurl_static
    (checked in the 2026-10-09 prefix), so release builds will say
    "static" for libcurl. STEP-M12-060 changes how deps resolve, not this.
    (3) Init placement in main.c: parse -> conflicts -> --version/version
    -> help -> no-command usage -> auth-class check (moved before the URL
    notice; still a usage error, exit 2) -> hem_tool_global_init() ->
    URL resolution -> dispatch. The refusal is therefore the first and only
    output. Unknown commands under a refused library exit 12 (not looked
    up first) - recorded in REQ-TOOL-024 rev 2 with the message text.
    (4) The main.c smoke compiles the UNCHANGED main.c into a test
    executable with a GCC/Clang constructor (tests/unit/tool_force_mismatch.c,
    `supports:` tag) that reads EHEM_TEST_FORCE_MISMATCH=version|probe and
    calls the STEP-M12-010 seam before main(); production hem-tool has no
    hook. Zero transport I/O is shown by the URL http://127.0.0.1:1: the
    unforced run reaches the transport and exits 1.
    Expected values in test_build_info come from CMake discovery
    (EHEM_TEST_* definitions), not from backend headers, so no test file
    includes a wolfSSL/curl header (STEP-M12-090's future grep gate).
    `keys types` under the forced mismatch is STEP-M12-040's (it adds the
    command to the pre-init block and to check_tool_startup.cmake); the
    OpenSSL leg of REQ-API-011/TOOL-025 is STEP-M12-080/090's; the gate
    demo is STEP-M12-140's. Release README.txt documents exit 12 and the
    package.sh job log now prints `hem-tool --version`. KNOWN-ISSUES
    foreign-libwolfssl entry updated (still OPEN until 1.2.0 ships).
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
- [x] `ehem_build_info_get` declared in ehem.h, exported, indexed in
      docs/API-GUIDE.md; `ehem_version()` untouched; export gate green
      (addition only).
- [x] Unit: every string non-NULL; backend name and compiled version equal
      the header constants; libcurl pair filled; identical values before
      init, after init and after a forced REQ-API-010 refusal.
- [x] hem-tool calls `ehem_global_init()` exactly once per run, before any
      output or traffic; under a forced mismatch `status` prints one message
      (backend, compiled version, loaded version, remedy), exits 12, zero
      transport I/O (unit smoke, `verifies:` tagged).
- [x] `help`, `--help`, `--version` succeed under the same forced mismatch;
      without a forced mismatch the existing unit suite is green unchanged.
- [x] `hem-tool --version` and `hem-tool version` print `hem-tool <version>`
      + crypto line + transport line, exit 0, zero I/O; `--version status`
      prints the version; output ASCII; values equal `ehem_build_info_get`
      and `ehem_version()`.
- [x] Top-level help ≤ 45 lines with the usage line amended; exit code 12
      in the help page's exit-code note and in the release README.txt.
- [x] `./dev ci` gcc + clang, ASan, ASCII gate, MinGW syntax check green;
      `implements:` tags for REQ-API-011 (shim/transport/ehem.c) and
      REQ-TOOL-024/025 (main.c / registry).
