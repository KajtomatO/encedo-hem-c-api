---
id: STEP-M1-030
title: Vendor cJSON and add internal JSON helpers
milestone: M1
implements: ["REQ-BUILD-003"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#10-directory-layout"]
depends_on: ["STEP-M1-010"]
evidence:
  commits: []   # to be recorded at commit time (user runs commits)
  tests: ["verifies: REQ-BUILD-003 (tests/unit/test_json.c — round-trip + tolerant getters)"]
  notes: >
    Vendored cJSON 1.7.18 (git tag v1.7.18) verbatim at src/vendor/cjson/
    (cJSON.c, cJSON.h, LICENSE) with provenance + SHA-256 in
    src/vendor/cjson/VENDORED.md. Compiled as an OBJECT library (ehem-cjson)
    so the vendored code is exempt from our -Wall -Wextra -Werror policy while
    still inheriting hidden visibility + PIC; CJSON_HIDE_SYMBOLS is defined for
    it AND for the library targets (json.c includes cJSON.h) to strip cJSON's
    default Windows __declspec(dllexport) so it cannot enter the DLL export
    table. Internal helper layer src/json.h + src/json.c wraps cJSON behind an
    opaque `ehem_json` typedef so bindings never include cJSON; getters honor
    the tolerant present/absent/wrong-type/null contract (ARCHITECTURE.md §6).
    Verified on Linux (2026-07-15): GCC build + `ctest -L unit` green
    (test_json 3/3, export_symbols still green); `nm -D` on libencedo-hem.so
    exports only `ehem_version` (cJSON_* present only as local `t` symbols,
    not exported); unit suite clean under GCC -fsanitize=address,undefined.
    (Clang ASan link needs compiler-rt runtime not installed on this box — an
    env gap, not a code issue; GCC ASan covers the LSan/ASan DoD intent.)
reopened: []
cancelled: null
---

**Goal:** Upstream cJSON vendored at `src/vendor/cjson/` with version and
license recorded, compiled into the library with hidden symbols, plus a
thin internal helper layer (parse/get-string/get-int/get-bool with
present/absent handling) that protocol bindings will use.

**Notes:** Record the upstream version and MIT license in a VENDORED.md or
file header. The helper layer is where tolerant-parsing behavior (ignore
unknown fields, report missing required fields) gets a single
implementation.

**Definition of done**
- [x] `src/vendor/cjson/` contains pinned upstream cJSON.c/cJSON.h + provenance note — *v1.7.18 + LICENSE + VENDORED.md (SHA-256 recorded)*
- [x] Build uses only the vendored copy (no system lookup) — *compiled from `src/vendor/cjson/cJSON.c`; no `find_package`/pkg-config for cJSON anywhere*
- [x] cJSON symbols not exported from the shared library; no cJSON type in public headers — *`nm -D` shows only `ehem_version`; bindings see only the opaque `ehem_json` (json.h), never cJSON*
- [x] Unit test round-trips a JSON document through the helper layer (label `unit`) — *tests/unit/test_json.c, `ctest -L unit` green*
