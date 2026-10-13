---
id: STEP-M12-100
title: "Windows: hem-tool delay-loads libwolfssl.dll in the release build — failure hook prints the missing-DLL message and exits 12; negative drop-in smoke in the release job"
milestone: M12
implements: ["REQ-TOOL-026"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-020"]
evidence:
  commits: ["57dce33", "48494dc"]   # start; implementation — the release-job run and the done commit follow
  tests:
    - "Local Windows CROSS build (Debian x86_64-w64-mingw32 GCC 13, msvcrt; scratch toolchain = the repo's + the deps prefix as a find root): pinned wolfSSL 5.7.2 DLL with --output-def (2378 exports) → dlltool --output-delaylib libwolfssl.delay.a; pinned libcurl 8.10.1 static (its CMake package records libwolfssl.dll.a by path); the project Release + -static + EHEM_WOLFSSL_DELAYLOAD_LIB, tests off"
    - "hem-tool.exe: ordinary imports bcrypt, KERNEL32, msvcrt, WS2_32 — NO libwolfssl.dll; __DELAY_IMPORT_DESCRIPTOR_…libwolfssl_delay_a, __delayLoadHelper2 and hem-tool's __pfnDliFailureHook2 (D, not libmingwex's B default) linked; link line = …libcurl.a, libwolfssl.delay.a, -ldelayimp, no libwolfssl.dll.a. libencedo-hem.dll keeps its ORDINARY libwolfssl.dll import (the switch is per consuming target)"
    - "package.sh's Windows delay-import assertion (objdump DLL Name list + nm descriptor + strings DLL name) run against the cross-built binaries: PASS on hem-tool.exe, FAIL on libencedo-hem.dll (ordinary import) — both directions"
    - "./dev ci 54/54 gcc + clang (native Linux unaffected); shellcheck build-deps.sh / package.sh clean; MinGW -Wall -Wextra -Werror compile of delayload_win.c and main.c (HEM_TOOL_DELAYLOAD) inside the cross build"
  notes: |
    PENDING for done: the Windows release job (workflow_dispatch of
    release.yml on development — REQ-BUILD-005 allows any ref into
    artifacts): build-deps with the new recipe (stamp +r2, cache key changes →
    full deps rebuild), package.sh's delay-import assertion on the UCRT64
    binary, and the negative smoke — hem-tool.exe WITHOUT the DLL: `status` →
    "libwolfssl.dll was not found" + exit 12, `help` / `keys types` exit 0,
    `--version` reads "not found (loaded)"; then the positive smoke with the
    DLL beside it reports 5.7.2 loaded. Run id → here and REQ-TOOL-026.
    Design: build-deps.sh (Windows) adds -Wl,--output-def to the wolfSSL DLL
    link (gendef fallback) and makes $PREFIX/lib/libwolfssl.delay.a with
    dlltool --output-delaylib. CMake EHEM_WOLFSSL_DELAYLOAD_LIB (WIN32 only,
    needs EHEM_TOOL_LINK=static and a dynamic wolfSSL): every wolfSSL link
    item — the SDK targets' and libcurl's own interface entry — becomes a
    generator expression on the consuming target's EHEM_WOLFSSL_DELAYLOAD
    property, set only on hem-tool: hem-tool gets the delay library +
    delayimp, every other target (the SDK DLL, the tests) the ordinary import
    library. hem-tool also compiles delayload_win.c: the failure hook prints
    "error: hem-tool cannot start: libwolfssl.dll was not found / remedy: put
    libwolfssl.dll from the release asset wolfssl-<v>-windows-x86_64.zip next
    to hem-tool.exe" (dliFailLoadLib) or names the missing function
    (dliFailGetProc — a different wolfSSL build), exit 12 (HEM_TOOL_EXIT_INIT).
    Conflict found and settled (REQ-TOOL-026 rev 2): `--version` DOES call
    wolfSSL (the loaded version, REQ-TOOL-025), so main.c sets the hook
    tolerant for that report only — wolfSSL_lib_version() then resolves to a
    stand-in returning "not found" (the hook returns the exe's own module for
    dliFailLoadLib, GetProcAddress fails, dliFailGetProc returns the
    stand-in; libcurl's version query shares the same import slot).
    GNU ld leaves the PE delay-import directory entry EMPTY (objdump shows no
    delay table even for a correct binary) — the assertion therefore uses the
    descriptor symbol (release binaries are not stripped) and the DLL-name
    string, on top of the ordinary import table not naming the DLL.
    The archive README.txt's 0xC0000135 paragraph is replaced (exit 12, what
    works without the DLL); release.yml's header describes the new smoke.
reopened: []
cancelled: null
---

**Goal:** the released Windows `hem-tool.exe` (dynamic-wolfSSL variant)
imports `libwolfssl.dll` through a delay-load import library, so a
missing DLL is reported by hem-tool itself — one stderr message naming
the DLL and where it belongs (the `wolfssl-<v>-windows-x86_64.zip`
companion), exit **12** — instead of Windows failing with `0xC0000135`
before `main`. `help`, `--version`, `keys types` run without the DLL.
`package.sh` asserts the delay import; the release job's drop-in smoke
gains the negative case.

**Notes:** Mechanism proven to link at the decomposition (2026-10-12,
Debian `x86_64-w64-mingw32` GCC 13; PoC in the session scratchpad):
`dlltool --output-delaylib libwolfssl_delay.a -d libwolfssl.def -D
libwolfssl.dll` + `-ldelayimp` (mingw-w64's `__delayLoadHelper2`); the
PoC exe's static import table listed KERNEL32/msvcrt only; the hook
`static FARPROC WINAPI fail_hook(unsigned dliNotify, PDelayLoadInfo
pdli)` with `PfnDliHook __pfnDliFailureHook2 = fail_hook;` (from
`<delayimp.h>`) receives `dliFailLoadLib` with `pdli->szDll`. No wine on
the dev host — run-time proof is the Windows release job. Work:
`scripts/release/build-deps.sh` Windows wolfSSL build emits the `.def`
(`LDFLAGS=-Wl,--output-def,libwolfssl.def`, or `gendef libwolfssl.dll`
from mingw-w64-tools as fallback) and the delay lib; CMake gains a cache
variable (e.g. `EHEM_WOLFSSL_DELAYLOAD_LIB=<path>`) that, when set on
`_WIN32`, replaces the wolfSSL import library on hem-tool's link line
with the delay lib + `delayimp` and compiles
`src/tools/hem-tool/delayload_win.c` (the hook: message to stderr via
`fputs`, `%s` only, ASCII only; exit with STEP-M12-020's shared exit-12
constant); development builds against MSYS2 keep ordinary imports.
Because STEP-M12-020 makes `ehem_global_init()` the first SDK call, the
failure surfaces at startup. `package.sh` Windows leg: pass the variable;
assert with `objdump -p hem-tool.exe` that `libwolfssl.dll` appears in
the "Delay Import" section and NOT in the regular import table. Release
job (`release.yml`, Windows): negative drop-in smoke — hem-tool zip alone,
`hem-tool status` → the message on stderr and exit 12 (not
-1073741515); positive case (DLL beside the exe) unchanged; `help`,
`--version`, `keys types` succeed without the DLL. The archive README.txt
0xC0000135 paragraph is replaced. Run it via `workflow_dispatch` on the
branch (REQ-BUILD-005 allows any ref into artifacts) — no push to `main`
needed. OPEN criterion to resolve and record (rev bump): the helper with
`-static` (libgcc/winpthread) + UCRT (the PoC used msvcrt) — expected to
work; a wrong-version DLL on `PATH` → the delay load succeeds and
REQ-API-010 refuses (expected; prove it if a second DLL build is at hand,
else record as reasoned). Attended Windows check (published zip, DLL
removed/restored) is the gate's.

**Definition of done**
- [x] `build-deps.sh` emits `libwolfssl.def` + the delay-load import lib
      in the Windows deps prefix; CMake variable wires the delay lib +
      `delayimp` and the hook source into hem-tool only when set.
- [x] `delayload_win.c` hook: on `dliFailLoadLib` prints the one-line
      message (DLL name, companion asset, placement) and exits 12.
- [x] `package.sh` asserts: `libwolfssl.dll` only as a delay import; the
      static import table lists system DLLs only (as REQ-BUILD-005).
- [ ] Release Windows job (`workflow_dispatch` on the branch, run id
      recorded): negative smoke → message + exit 12; positive smoke
      unchanged; `help`/`--version`/`keys types` run without the DLL.
- [x] Archive README.txt updated (0xC0000135 paragraph replaced; exit 12
      documented).
- [ ] REQ-TOOL-026 OPEN criterion (`-static`/UCRT interplay, wrong-version
      DLL on PATH) resolved and recorded (rev bump); the attended criterion
      stays open for the gate; `implements: REQ-TOOL-026` tags in
      delayload_win.c / package.sh.
