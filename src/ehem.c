/*
 * ehem.c — top-level library entry points.
 *
 * implements: REQ-API-006 (ehem_version, the first exported symbol),
 *             REQ-API-011 (ehem_build_info_get)
 */
#include "ehem/ehem.h"

#include "crypto_shim.h"
#include "transport.h"

#ifndef EHEM_VERSION_STRING
/* Normally injected by the build from PROJECT_VERSION; keep a sane fallback
 * so the TU also compiles standalone. */
#define EHEM_VERSION_STRING "0.0.0-dev"
#endif

const char *ehem_version(void)
{
    return EHEM_VERSION_STRING;
}

/* Each backend describes itself; nothing is cached, so there is no mutable
 * state to share (REQ-API-002) and the answer is the same before init, after
 * it and after a refused one. */
void ehem_build_info_get(ehem_build_info *out)
{
    static const ehem_build_info empty;   /* all NULL, reserved slots included */

    if (out == NULL) {
        return;
    }
    *out = empty;
    ehem_crypto_backend_describe(out);
    ehem_transport_backend_describe(out);
}
