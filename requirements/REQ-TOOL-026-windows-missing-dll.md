---
id: REQ-TOOL-026
title: hem-tool on Windows reports a missing libwolfssl.dll itself (delay-load import) instead of failing before main
status: approved
priority: should
revision: 1
source: user decision 2026-10-10 (BACKLOG item 5, from the first Windows test of the release binary — Windows refuses to start hem-tool.exe without libwolfssl.dll, exit -1073741515 / 0xC0000135, often with no message); moved into M12 by user decision 2026-10-12 (ARCHITECTURE.md §11 M12 part A: "the MinGW mechanism is proven at decomposition"); mechanism proven to link 2026-10-12 with the Debian x86_64-w64-mingw32 toolchain (GCC 13): `dlltool --output-delaylib` + mingw-w64 `libdelayimp.a` / `delayimp.h` (`__pfnDliFailureHook2`, `dliFailLoadLib`) — the PoC exe's static import table lists system DLLs only; run-time proof pending the Windows release job (no wine on the dev host); approved 2026-10-12 (M12 decomposition, user: "approve")
depends_on: ["REQ-BUILD-005", "REQ-TOOL-024"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool on Windows reports a missing libwolfssl.dll itself

The released Windows `hem-tool.exe` (the dynamic-wolfSSL variant,
REQ-BUILD-005) SHALL import `libwolfssl.dll` through a **delay-load
import library**, so that a missing or unloadable DLL is reported by
hem-tool itself — one stderr message naming the DLL and that it belongs
next to `hem-tool.exe` (from the release's
`wolfssl-<v>-windows-x86_64.zip`), exit code **12** (REQ-TOOL-024's
class) — instead of the Windows loader failing with `0xC0000135` before
any hem-tool code runs.

- **Mechanism (MinGW):** the import library for `libwolfssl.dll` is
  generated with `dlltool --output-delaylib` from the DLL's `.def` (the
  wolfSSL build of `scripts/release/build-deps.sh` emits it with
  `-Wl,--output-def`, or `gendef` recovers it from the DLL) and linked
  together with mingw-w64's `libdelayimp.a` (`__delayLoadHelper2`). The
  first call into wolfSSL then loads the DLL; hem-tool installs
  `__pfnDliFailureHook2` (declared in `<delayimp.h>`), which on
  `dliFailLoadLib` prints the message and exits 12. Because REQ-TOOL-024
  makes `ehem_global_init()` the first SDK call, the failure surfaces at
  startup, before any command output.
- **Scope:** the release build only (`package.sh` Windows leg, where the
  `.def` of the shipped DLL exists); development builds against the
  MSYS2 package keep ordinary imports. `help`, `--help`, `--version` and
  `keys types` never touch wolfSSL and therefore work without the DLL.
- Linux is not affected: the loader's own "cannot open shared object
  file" message already names the library, and a *wrong* library is
  REQ-API-010's case on both platforms.
- The archive README.txt's 0xC0000135 paragraph is replaced by the new
  behaviour; the exit code is documented as in REQ-TOOL-024.

**Rationale:** a silent exit code is the worst first experience a
Windows user can have with a downloaded tool; the DLL is deliberately a
separate download (§1 licensing), so the tool must explain it. The
MinGW toolchain supports the mechanism out of the box, so no second
executable or launcher is needed.

**Acceptance criteria:**
- [x] Linking proven at decomposition (2026-10-12, Debian cross
      toolchain): PoC exe built against `dlltool --output-delaylib` +
      `-ldelayimp` has only `KERNEL32.dll`/CRT in its static import
      table (the target DLL absent); `__pfnDliFailureHook2` resolves.
      *(Recorded in STEP-M12-100's notes: `x86_64-w64-mingw32-gcc` 13,
      `dlltool --output-delaylib libfoo_delay.a -d foo.def -D foo.dll`,
      link with `libfoo_delay.a -ldelayimp`, `objdump -p` of the exe →
      KERNEL32.dll + msvcrt.dll only. Run-time proof = the Windows release
      job.)*
- [ ] Release Windows job: the drop-in smoke test gains a negative case —
      the hem-tool archive unpacked WITHOUT the companion DLL,
      `hem-tool status` → the message on stderr, exit 12 (not
      0xC0000135); the positive case (DLL beside the exe) unchanged;
      `objdump -p hem-tool.exe` shows `libwolfssl.dll` only as a delay
      import (asserted in `package.sh`).
- [ ] `hem-tool help` / `--version` / `keys types` run without the DLL
      present (same negative smoke).
- [ ] Attended on Windows at the M12 gate: the published zip, DLL
      removed → the message; DLL restored → `status` works (recorded).
- [ ] OPEN (needs verification at the step): interplay of the delay-load
      helper with the `-static` (libgcc/winpthread) release link and the
      UCRT; whether a DLL found on `PATH` but of a wrong version still
      reaches REQ-API-010's refusal (expected: yes — the delay load
      succeeds and the version check fails).
