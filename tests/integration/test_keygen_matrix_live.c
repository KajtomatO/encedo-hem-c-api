/*
 * test_keygen_matrix_live.c — the M5 per-family generation matrix.
 *
 * verifies: REQ-TEST-004 (for every fw v1.2.2 create type: create an EHEMTEST
 *           key → find it in a full list walk and capture its live flag-set
 *           type string → get + REQ-KEY-006 classification cross-check → for
 *           the six ExDSA-capable families sign and verify the signature
 *           locally with the crypto shim → delete → confirm it is gone)
 *
 * Runs one family at a time with at most ONE matrix key alive (create → …→
 * delete before the next), so it never leans on the device's small repo
 * capacity. A mid-family failure still deletes the live key (teardown cleanup
 * over the tracked registry). Links the STATIC lib + internal headers (via
 * ehem_test_support) for the crypto shim's local verify, the same exception
 * test_sign_live takes. Skipped (exit 77) unless EHEM_TEST_URL +
 * EHEM_TEST_PASSPHRASE. Keys are EHEMTEST-labeled and cleaned pass-or-fail
 * (REQ-TEST-003).
 */
#define _POSIX_C_SOURCE 199309L   /* nanosleep, struct timespec under -std=c99 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/crypto.h"
#include "ehem/keymgmt.h"
#include "crypto_shim.h"      /* internal: local verify */
#include "integration_env.h"
#include "ehem_test_keys.h"

/* How the sign leg verifies a family's signature (if it can sign at all). */
typedef enum {
    SIG_NONE = 0,       /* not an ExDSA-capable family */
    SIG_ECDSA,          /* DER ECDSA over a hash — verify via ehem_ecdsa_verify */
    SIG_ED25519,        /* raw 64-byte — ehem_ed25519_verify */
    SIG_ED448           /* raw 114-byte — ehem_ed448_verify */
} sig_kind;

typedef struct {
    const char       *type;         /* device create `type` literal */
    const char       *mode;         /* create mode, or NULL */
    ehem_key_family   family;       /* expected classifier family */
    int               asymmetric;   /* 1 → get returns a fixed-size pubkey */
    sig_kind          sig;          /* how (if) this family signs */
    ehem_ecdsa_curve  curve;        /* for SIG_ECDSA */
    const char       *alg;          /* sign selector, for signable families */
} matrix_row;

/* The full fw v1.2.2 create vocabulary (api_keymgmt.c:866-963). NIST-P/K are
 * created ECDH,ExDSA so the sign leg is exercised. */
static const matrix_row MATRIX[] = {
    { "SECP256R1", "ECDH,ExDSA", EHEM_KEY_FAMILY_SECP256R1, 1, SIG_ECDSA,
      EHEM_ECDSA_SECP256R1, EHEM_SIGN_ALG_SHA256_ECDSA },
    { "SECP384R1", "ECDH,ExDSA", EHEM_KEY_FAMILY_SECP384R1, 1, SIG_ECDSA,
      EHEM_ECDSA_SECP384R1, EHEM_SIGN_ALG_SHA384_ECDSA },
    { "SECP521R1", "ECDH,ExDSA", EHEM_KEY_FAMILY_SECP521R1, 1, SIG_ECDSA,
      EHEM_ECDSA_SECP521R1, EHEM_SIGN_ALG_SHA512_ECDSA },
    { "SECP256K1", "ECDH,ExDSA", EHEM_KEY_FAMILY_SECP256K1, 1, SIG_ECDSA,
      EHEM_ECDSA_SECP256K1, EHEM_SIGN_ALG_SHA256_ECDSA },
    { "ED25519", NULL, EHEM_KEY_FAMILY_ED25519, 1, SIG_ED25519,
      EHEM_ECDSA_SECP256R1, EHEM_SIGN_ALG_ED25519 },
    { "ED448", NULL, EHEM_KEY_FAMILY_ED448, 1, SIG_ED448,
      EHEM_ECDSA_SECP256R1, EHEM_SIGN_ALG_ED448 },
    { "CURVE25519", NULL, EHEM_KEY_FAMILY_CURVE25519, 1, SIG_NONE, 0, NULL },
    { "CURVE448", NULL, EHEM_KEY_FAMILY_CURVE448, 1, SIG_NONE, 0, NULL },
    { "SHA2-256", NULL, EHEM_KEY_FAMILY_HMAC_SHA2_256, 0, SIG_NONE, 0, NULL },
    { "SHA2-384", NULL, EHEM_KEY_FAMILY_HMAC_SHA2_384, 0, SIG_NONE, 0, NULL },
    { "SHA2-512", NULL, EHEM_KEY_FAMILY_HMAC_SHA2_512, 0, SIG_NONE, 0, NULL },
    { "SHA3-256", NULL, EHEM_KEY_FAMILY_HMAC_SHA3_256, 0, SIG_NONE, 0, NULL },
    { "SHA3-384", NULL, EHEM_KEY_FAMILY_HMAC_SHA3_384, 0, SIG_NONE, 0, NULL },
    { "SHA3-512", NULL, EHEM_KEY_FAMILY_HMAC_SHA3_512, 0, SIG_NONE, 0, NULL },
    { "AES128", NULL, EHEM_KEY_FAMILY_AES128, 0, SIG_NONE, 0, NULL },
    { "AES192", NULL, EHEM_KEY_FAMILY_AES192, 0, SIG_NONE, 0, NULL },
    { "AES256", NULL, EHEM_KEY_FAMILY_AES256, 0, SIG_NONE, 0, NULL },
    { "MLKEM512", NULL, EHEM_KEY_FAMILY_MLKEM512, 1, SIG_NONE, 0, NULL },
    { "MLKEM768", NULL, EHEM_KEY_FAMILY_MLKEM768, 1, SIG_NONE, 0, NULL },
    { "MLKEM1024", NULL, EHEM_KEY_FAMILY_MLKEM1024, 1, SIG_NONE, 0, NULL },
    { "MLDSA44", NULL, EHEM_KEY_FAMILY_MLDSA44, 1, SIG_NONE, 0, NULL },
    { "MLDSA65", NULL, EHEM_KEY_FAMILY_MLDSA65, 1, SIG_NONE, 0, NULL },
    { "MLDSA87", NULL, EHEM_KEY_FAMILY_MLDSA87, 1, SIG_NONE, 0, NULL },
};
#define MATRIX_LEN (sizeof MATRIX / sizeof MATRIX[0])

typedef struct {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} matrix_state;

/*
 * The dev device closes TCP after every response and its reachability is
 * intermittent (see project notes); a ~100-request sequential matrix reliably
 * trips at least one transient connect/read timeout. The SDK does no network
 * retries by design (ARCHITECTURE §7 — the caller's job), so the test tolerates
 * the flake: transient network errors are retried a few times with a short
 * settle; device/protocol errors are never retried (a real failure fails fast).
 */
#define MATRIX_RETRIES 6
static int is_transient(ehem_rc rc)
{
    return rc == EHEM_ERR_UNREACHABLE || rc == EHEM_ERR_NETWORK;
}
static void settle(void)
{
    struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
    nanosleep(&ts, NULL);
}

static int setup(void **state)
{
    matrix_state *s = calloc(1, sizeof *s);
    if (s == NULL) {
        return -1;
    }
    /* ML-DSA/ML-KEM keygen on the MCU is slow — give requests plenty of room. */
    if (ehem_test_ctx_timeout(&s->ctx, 120000) != EHEM_OK) {
        free(s);
        return -1;
    }
    ehem_rc rc = EHEM_ERR_PROTOCOL;
    for (int t = 0; t < MATRIX_RETRIES; t++) {
        rc = ehem_login(s->ctx, ehem_test_passphrase());
        if (!is_transient(rc)) {
            break;
        }
        settle();
    }
    if (rc != EHEM_OK) {
        ehem_ctx_destroy(s->ctx);
        free(s);
        return -1;
    }
    ehem_test_sweep(s->ctx);
    *state = s;
    return 0;
}

static int teardown(void **state)
{
    matrix_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

static const uint8_t MSG[] = "signed by test_keygen_matrix_live";
#define MSG_LEN (sizeof MSG - 1)

/* Find `kid` in a full repository walk; return its live `type` string copied
 * into `type_out` (flag-set form as the device lists it). Fails the test if
 * the key is not present. */
/* list_all with transient-retry. */
static ehem_rc list_all_retry(matrix_state *s, ehem_key_page **page)
{
    ehem_rc rc = EHEM_ERR_PROTOCOL;
    for (int t = 0; t < MATRIX_RETRIES; t++) {
        rc = ehem_key_list_all(s->ctx, page);
        if (!is_transient(rc)) {
            break;
        }
        settle();
    }
    return rc;
}

static void list_find(matrix_state *s, const char *kid,
                      char *type_out, size_t cap)
{
    ehem_key_page *page = NULL;
    ehem_rc rc = list_all_retry(s, &page);
    if (rc != EHEM_OK) {
        fail_msg("list_all failed: %s (%s)", ehem_rc_str(rc),
                 ehem_last_error(s->ctx)->message);
    }
    assert_non_null(page);

    int found = 0;
    for (size_t i = 0; i < page->listed; i++) {
        if (strcmp(page->entries[i].kid, kid) == 0) {
            snprintf(type_out, cap, "%s", page->entries[i].type);
            found = 1;
            break;
        }
    }
    ehem_key_page_free(page);
    if (!found) {
        fail_msg("created kid %s not found in list walk", kid);
    }
}

/* Confirm `kid` is absent from a full walk (post-delete). */
static void list_absent(matrix_state *s, const char *kid)
{
    ehem_key_page *page = NULL;
    ehem_rc rc = list_all_retry(s, &page);
    if (rc != EHEM_OK) {
        fail_msg("list_all (absence) failed: %s", ehem_rc_str(rc));
    }
    assert_non_null(page);
    for (size_t i = 0; i < page->listed; i++) {
        if (strcmp(page->entries[i].kid, kid) == 0) {
            ehem_key_page_free(page);
            fail_msg("deleted kid %s still present", kid);
        }
    }
    ehem_key_page_free(page);
}

static void run_row(matrix_state *s, const matrix_row *r)
{
    char label[32];
    char list_type[96] = {0};
    ehem_key_create_params p;
    char kid[EHEM_KID_HEX_SIZE] = {0};
    ehem_key_details *d = NULL;
    ehem_key_type_info info;
    ehem_rc rc;

    /* 1. create (retry transient network flakes) */
    ehem_test_label(label, sizeof label);
    memset(&p, 0, sizeof p);
    p.type = r->type;
    p.label = label;
    p.mode = r->mode;
    for (int t = 0; t < MATRIX_RETRIES; t++) {
        rc = ehem_key_create(s->ctx, &p, kid);
        if (!is_transient(rc)) {
            break;
        }
        settle();
    }
    if (rc != EHEM_OK) {
        fail_msg("create %s failed: %s (%s)", r->type, ehem_rc_str(rc),
                 ehem_last_error(s->ctx)->message);
    }
    ehem_test_track(&s->reg, kid);

    /* 2. list walk — membership + live flag-set capture */
    list_find(s, kid, list_type, sizeof list_type);

    /* 3. get + REQ-KEY-006 classification cross-check */
    for (int t = 0; t < MATRIX_RETRIES; t++) {
        rc = ehem_key_get(s->ctx, kid, &d);
        if (!is_transient(rc)) {
            break;
        }
        settle();
    }
    if (rc != EHEM_OK) {
        fail_msg("get %s failed: %s (%s)", r->type, ehem_rc_str(rc),
                 ehem_last_error(s->ctx)->message);
    }
    assert_non_null(d);
    assert_int_equal(ehem_key_type_parse(d->type, &info), EHEM_OK);
    assert_int_equal(info.family, r->family);
    if (r->asymmetric && info.pubkey_len > 0) {
        /* the classifier's fixed pubkey size matches the wire material */
        assert_int_equal((int)d->pubkey_len, (int)info.pubkey_len);
    }
    printf("[matrix] %-10s family=%-11s list_type=\"%s\" get_type=\"%s\" "
           "pubkey_len=%d\n",
           r->type, ehem_key_family_str(r->family), list_type, d->type,
           (int)d->pubkey_len);

    /* 4. sign + local verify (ExDSA-capable families only) */
    if (r->sig != SIG_NONE) {
        ehem_signature *sig = NULL;
        int valid = 0;
        for (int t = 0; t < MATRIX_RETRIES; t++) {
            rc = ehem_sign(s->ctx, kid, r->alg, MSG, MSG_LEN, NULL, 0, &sig);
            if (!is_transient(rc)) {
                break;
            }
            settle();
        }
        if (rc != EHEM_OK) {
            ehem_key_details_free(d);
            fail_msg("sign %s failed: %s (%s)", r->type, ehem_rc_str(rc),
                     ehem_last_error(s->ctx)->message);
        }
        assert_non_null(sig);

        ehem_rc vrc = EHEM_ERR_PROTOCOL;
        switch (r->sig) {
        case SIG_ECDSA:
            assert_int_equal(sig->sig[0], 0x30);            /* DER SEQUENCE */
            assert_true(sig->sig_len <= info.sig_max_len);
            vrc = ehem_ecdsa_verify(r->curve, d->pubkey, d->pubkey_len,
                                    MSG, MSG_LEN, sig->sig, sig->sig_len,
                                    &valid);
            break;
        case SIG_ED25519:
            assert_int_equal((int)sig->sig_len, EHEM_ED25519_SIG_SIZE);
            vrc = ehem_ed25519_verify(d->pubkey, MSG, MSG_LEN,
                                      sig->sig, sig->sig_len, &valid);
            break;
        case SIG_ED448:
            assert_int_equal((int)sig->sig_len, EHEM_ED448_SIG_SIZE);
            vrc = ehem_ed448_verify(d->pubkey, MSG, MSG_LEN,
                                    sig->sig, sig->sig_len, &valid);
            break;
        default:
            break;
        }

        if (vrc == EHEM_ERR_UNSUPPORTED) {
            /* wolfSSL build without the needed primitive (e.g. compressed EC
             * points, Ed448) — record and continue; not a device failure. */
            printf("[matrix] %-10s local verify UNSUPPORTED in this build "
                   "(recorded, not failed)\n", r->type);
        } else {
            assert_int_equal(vrc, EHEM_OK);
            assert_int_equal(valid, 1);
            printf("[matrix] %-10s signature (%d bytes) verified locally\n",
                   r->type, (int)sig->sig_len);
        }
        ehem_signature_free(sig);
    }
    ehem_key_details_free(d);

    /* 5. delete + absence check (keeps ≤ 1 matrix key alive) */
    for (int t = 0; t < MATRIX_RETRIES; t++) {
        rc = ehem_key_delete(s->ctx, kid);
        if (!is_transient(rc)) {
            break;
        }
        settle();
    }
    if (rc != EHEM_OK) {
        fail_msg("delete %s failed: %s (%s)", r->type, ehem_rc_str(rc),
                 ehem_last_error(s->ctx)->message);
    }
    ehem_test_untrack(&s->reg, kid);
    list_absent(s, kid);
}

static void test_family_matrix(void **state)
{
    matrix_state *s = *state;
    for (size_t i = 0; i < MATRIX_LEN; i++) {
        run_row(s, &MATRIX[i]);
    }
    printf("[matrix] all %zu families: create → list → get/classify → "
           "sign(where ExDSA) → delete OK\n", MATRIX_LEN);
}

int main(void)
{
    ehem_require_test_url();   /* skip (exit 77) without a device */
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_family_matrix, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
