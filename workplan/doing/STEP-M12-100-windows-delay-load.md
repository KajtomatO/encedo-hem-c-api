---
id: STEP-M12-100
title: "Windows: hem-tool delay-loads libwolfssl.dll in the release build — failure hook prints the missing-DLL message and exits 12; negative drop-in smoke in the release job"
milestone: M12
implements: ["REQ-TOOL-026"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-020"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] `build-deps.sh` emits `libwolfssl.def` + the delay-load import lib
      in the Windows deps prefix; CMake variable wires the delay lib +
      `delayimp` and the hook source into hem-tool only when set.
- [ ] `delayload_win.c` hook: on `dliFailLoadLib` prints the one-line
      message (DLL name, companion asset, placement) and exits 12.
- [ ] `package.sh` asserts: `libwolfssl.dll` only as a delay import; the
      static import table lists system DLLs only (as REQ-BUILD-005).
- [ ] Release Windows job (`workflow_dispatch` on the branch, run id
      recorded): negative smoke → message + exit 12; positive smoke
      unchanged; `help`/`--version`/`keys types` run without the DLL.
- [ ] Archive README.txt updated (0xC0000135 paragraph replaced; exit 12
      documented).
- [ ] REQ-TOOL-026 OPEN criterion (`-static`/UCRT interplay, wrong-version
      DLL on PATH) resolved and recorded (rev bump); the attended criterion
      stays open for the gate; `implements: REQ-TOOL-026` tags in
      delayload_win.c / package.sh.
