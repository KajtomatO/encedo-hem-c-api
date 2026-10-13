/*
 * delayload_win.c — Windows release build of hem-tool only (CMake
 * EHEM_WOLFSSL_DELAYLOAD_LIB): hem-tool imports libwolfssl.dll through a
 * delay-load import library, so the DLL is loaded at the first wolfSSL call —
 * ehem_global_init(), right after argument parsing (REQ-TOOL-024) — and not by
 * the Windows loader before main(), which fails a missing DLL with 0xC0000135
 * and, depending on the console, no text at all. The delay-load helper
 * (mingw-w64, libmingwex) calls this file's failure hook instead: one message
 * naming the DLL and the remedy, exit 12 — the REQ-TOOL-024 class.
 *
 * implements: REQ-TOOL-026
 *
 * `hem-tool --version` reports the loaded wolfSSL version, so it does call
 * into the DLL: main.c switches the hook to "tolerant" for that report only —
 * a missing DLL then reads "not found" as the loaded version instead of
 * ending the run (help and `keys types` never call wolfSSL at all).
 */
#include <windows.h>
#include <delayimp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "startup.h"            /* HEM_TOOL_EXIT_INIT */

#ifndef EHEM_WOLFSSL_COMPANION_VERSION
#define EHEM_WOLFSSL_COMPANION_VERSION "<version>"   /* CMake passes the real one */
#endif

void hem_tool_delayload_tolerate(int on);

static int g_tolerant;

void hem_tool_delayload_tolerate(int on)
{
    g_tolerant = on;
}

/* Stands in for wolfSSL_lib_version() when the DLL is missing in tolerant
 * mode (same calling convention as every import: the returned pointer is
 * what the caller reads as `const char *`). */
static INT_PTR WINAPI missing_lib_version(void)
{
    return (INT_PTR)"not found";
}

static int is_lib_version(const PDelayLoadInfo info)
{
    return info->dlp.fImportByName && info->dlp.szProcName != NULL &&
           strcmp(info->dlp.szProcName, "wolfSSL_lib_version") == 0;
}

static FARPROC WINAPI delay_failure(unsigned notify, PDelayLoadInfo info)
{
    if (g_tolerant && is_lib_version(info)) {
        if (notify == dliFailLoadLib) {
            /* Any module handle: its GetProcAddress fails next, which lands
             * in dliFailGetProc below and gets the stand-in. */
            return (FARPROC)GetModuleHandleA(NULL);
        }
        if (notify == dliFailGetProc) {
            return (FARPROC)missing_lib_version;
        }
    }
    if (notify == dliFailLoadLib) {
        fputs("error: hem-tool cannot start: ", stderr);
        fputs(info->szDll, stderr);
        fputs(" was not found\n"
              "  remedy: put ", stderr);
        fputs(info->szDll, stderr);
        fputs(" from the release asset wolfssl-"
              EHEM_WOLFSSL_COMPANION_VERSION "-windows-x86_64.zip next to "
              "hem-tool.exe\n", stderr);
    } else if (notify == dliFailGetProc) {
        fputs("error: hem-tool cannot use ", stderr);
        fputs(info->szDll, stderr);
        fputs(": it lacks ", stderr);
        fputs(info->dlp.fImportByName && info->dlp.szProcName != NULL
                  ? info->dlp.szProcName : "a function hem-tool needs", stderr);
        fputs(" - it is not the wolfSSL " EHEM_WOLFSSL_COMPANION_VERSION
              " build hem-tool was made for\n"
              "  remedy: put the DLL from the release asset wolfssl-"
              EHEM_WOLFSSL_COMPANION_VERSION "-windows-x86_64.zip next to "
              "hem-tool.exe\n", stderr);
    } else {
        return NULL;
    }
    fflush(stderr);
    exit(HEM_TOOL_EXIT_INIT);
}

/* The helper reads this (declared in <delayimp.h>); defining it here keeps
 * libmingwex's NULL default out of the link. */
PfnDliHook __pfnDliFailureHook2 = delay_failure;
