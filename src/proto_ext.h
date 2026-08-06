/*
 * proto_ext.h — internal seams for the ExtAuth confirm engine.
 *
 * implements: REQ-AUTH-009 (test seam only — the public engine API lives in
 *             <ehem/auth.h>)
 *
 * INTERNAL header — not shipped, not exported. Linked only by the static
 * library / unit tests, the ehem_auth_test_* precedent.
 */
#ifndef EHEM_PROTO_EXT_H
#define EHEM_PROTO_EXT_H

/*
 * Override the ehem_ext_confirm_wait poll interval (default 5000 ms) so unit
 * tests never sleep for real. ms <= 0 restores the default. Process-global,
 * tests only.
 */
void ehem_ext_test_set_poll_interval(long ms);

#endif /* EHEM_PROTO_EXT_H */
