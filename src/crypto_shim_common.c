/*
 * crypto_shim_common.c — the backend-free part of the crypto shim: the scrub,
 * the serial formatter, the certificate date formatter and the test seam of
 * the backend compatibility check. Compiled with either backend
 * (EHEM_CRYPTO_BACKEND); no crypto library header here.
 *
 * implements: REQ-AUTH-001 (ehem_zeroize), REQ-SYS-006 (ehem_serial_hex),
 *             REQ-BUILD-007 (the split behind the unchanged crypto_shim.h)
 */
#include "crypto_shim.h"
#include "crypto_shim_common.h"

#include <stdio.h>

void ehem_zeroize(void *p, size_t n)
{
    if (p == NULL) {
        return;
    }
    /* volatile write loop: the compiler may not treat these stores as dead,
     * so the scrub survives -O2 (proven in test_crypto.c via a volatile read). */
    volatile unsigned char *vp = (volatile unsigned char *)p;
    while (n-- > 0) {
        *vp++ = 0;
    }
}

void ehem_serial_hex(const uint8_t *serial, size_t len, char *out, size_t cap)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t pos = 0;
    size_t i = 0;

    if (cap == 0) {
        return;
    }
    if (serial == NULL) {
        out[0] = '\0';
        return;
    }
    if (len > 1 && serial[0] == 0x00) {
        i = 1;   /* drop the DER sign byte */
    }
    for (; i < len && pos + 2 < cap; i++) {
        out[pos++] = HEX[(serial[i] >> 4) & 0x0F];
        out[pos++] = HEX[serial[i] & 0x0F];
    }
    out[pos] = '\0';
}

void ehem_crypto_iso_date(const struct tm *t, char *out, size_t cap)
{
    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (t == NULL) {
        return;
    }
    /* Clamp every field to its digit width so the formatted length is provably
     * 20 + NUL = EHEM_CERT_DATE_CAP (GCC -O3 -Wformat-truncation, which the
     * Release build of REQ-BUILD-005 runs with, cannot see that itself). */
    {
        unsigned y  = (unsigned)(t->tm_year + 1900) % 10000u;
        unsigned mo = (unsigned)(t->tm_mon + 1) % 100u;
        unsigned d  = (unsigned)t->tm_mday % 100u;
        unsigned hh = (unsigned)t->tm_hour % 100u;
        unsigned mi = (unsigned)t->tm_min % 100u;
        unsigned s  = (unsigned)t->tm_sec % 100u;
        (void)snprintf(out, cap, "%04u-%02u-%02uT%02u:%02u:%02uZ",
                       y, mo, d, hh, mi, s);
    }
}

/* Test seam (crypto_shim.h). Never exported; unit tests reach it through the
 * static library. */
static int g_force_mismatch = EHEM_CRYPTO_FORCE_NONE;

void ehem_crypto_test_force_mismatch(int mode)
{
    g_force_mismatch = mode;
}

int ehem_crypto_forced_mismatch(void)
{
    return g_force_mismatch;
}
