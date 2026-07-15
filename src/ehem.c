/*
 * ehem.c — top-level library entry points.
 *
 * implements: REQ-API-006 (ehem_version, the first exported symbol)
 */
#include "ehem/ehem.h"

#ifndef EHEM_VERSION_STRING
/* Normally injected by the build from PROJECT_VERSION; keep a sane fallback
 * so the TU also compiles standalone. */
#define EHEM_VERSION_STRING "0.0.0-dev"
#endif

const char *ehem_version(void)
{
    return EHEM_VERSION_STRING;
}
