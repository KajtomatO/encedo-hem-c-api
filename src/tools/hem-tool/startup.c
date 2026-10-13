/*
 * startup.c — explicit SDK initialisation and the --version report (see
 * startup.h).
 *
 * implements: REQ-TOOL-024, REQ-TOOL-025
 */
#include "startup.h"

#include <string.h>

#include "ehem/ehem.h"

/* The <os> part of the release's companion asset name
 * (wolfssl-<version>-<os>-x86_64, REQ-BUILD-005). */
#ifdef _WIN32
#define HEM_RELEASE_OS "windows"
#else
#define HEM_RELEASE_OS "linux"
#endif

int hem_tool_global_init(FILE *err)
{
    ehem_build_info bi;
    ehem_rc rc = ehem_global_init();

    if (rc == EHEM_OK) {
        return 0;
    }
    if (err == NULL) {
        err = stderr;
    }
    if (rc != EHEM_ERR_BACKEND_MISMATCH) {
        fprintf(err, "error: the SDK could not be initialised: %s\n",
                ehem_rc_str(rc));
        return HEM_TOOL_EXIT_INIT;
    }

    /* The refusal carries no context detail (there is no context yet); the
     * versions come from the build information, which works after a refused
     * init (REQ-API-011). Equal versions = the struct self-test refused a
     * same-version library built with other options. */
    ehem_build_info_get(&bi);
    fprintf(err, "error: hem-tool cannot use the %s library it loaded (%s)\n",
            bi.crypto_backend, ehem_rc_str(rc));
    fprintf(err, "  %s: built against %s, loaded %s%s\n",
            bi.crypto_backend, bi.crypto_version_compiled,
            bi.crypto_version_loaded,
            strcmp(bi.crypto_version_compiled, bi.crypto_version_loaded) == 0
                ? " - same version, incompatible build" : "");
    if (strcmp(bi.crypto_backend, "wolfSSL") == 0) {
        fprintf(err, "  remedy: put the wolfSSL library from the release asset "
                     "wolfssl-%s-%s-x86_64\n"
                     "          next to hem-tool, or use the -static hem-tool "
                     "(1.2.0 and later),\n"
                     "          which needs no separate crypto library\n",
                bi.crypto_version_compiled, HEM_RELEASE_OS);
    } else {
        fprintf(err, "  remedy: install the %s %s library hem-tool was built "
                     "against, or use the\n"
                     "          -static hem-tool (1.2.0 and later), which needs "
                     "no separate crypto library\n",
                bi.crypto_backend, bi.crypto_version_compiled);
    }
    return HEM_TOOL_EXIT_INIT;
}

void hem_tool_print_version(FILE *out)
{
    ehem_build_info bi;

    if (out == NULL) {
        out = stdout;
    }
    ehem_build_info_get(&bi);
    fprintf(out, "hem-tool %s\n", ehem_version());
    fprintf(out, "crypto:    %s %s (compiled) / %s (loaded), %s\n",
            bi.crypto_backend, bi.crypto_version_compiled,
            bi.crypto_version_loaded, bi.crypto_link);
    fprintf(out, "transport: %s %s (compiled) / %s (loaded), %s\n",
            bi.transport_backend, bi.transport_version_compiled,
            bi.transport_version_loaded, bi.transport_link);
}
