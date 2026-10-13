/*
 * crypto_shim_common.h — what the two crypto-shim backends share
 * (crypto_shim_wolfssl.c, crypto_shim_openssl.c; EHEM_CRYPTO_BACKEND picks
 * one). INTERNAL: included by the shim sources only; crypto_shim.h stays the
 * contract every other caller sees.
 *
 * supports: REQ-BUILD-007
 */
#ifndef EHEM_CRYPTO_SHIM_COMMON_H
#define EHEM_CRYPTO_SHIM_COMMON_H

#include <stddef.h>
#include <time.h>

/* Format a UTC calendar time as "YYYY-MM-DDTHH:MM:SSZ" into `out` (capacity
 * `cap`, EHEM_CERT_DATE_CAP for the certificate fields); "" on a NULL time. */
void ehem_crypto_iso_date(const struct tm *t, char *out, size_t cap);

/* The mode set through ehem_crypto_test_force_mismatch() (crypto_shim.h) —
 * EHEM_CRYPTO_FORCE_NONE unless a unit test forces a refusal. */
int ehem_crypto_forced_mismatch(void);

#endif /* EHEM_CRYPTO_SHIM_COMMON_H */
