/*
 * test_update_import_live.c — live keymgmt update + import against the real
 * dev-machine HEM.
 *
 * verifies: REQ-KEY-007 (live: create → update label+descr → get/list/search
 *           reflect it; a label-only update CLEARS the stored descr —
 *           whole-record rewrite, device > doc, pinned; PROBE: unknown-kid
 *           status [406 confirmed]; PROBE: descr-only body → 400, the
 *           label-required firmware rule the SDK mirrors),
 *           REQ-KEY-008 (live: import a shim-generated X25519 public key →
 *           ehem_ecdh via ext_kid matches the shim-computed secret
 *           byte-exact; PROBE: re-import dedup [python: 406]; PROBE: which
 *           type families import — SECP256R1 compressed point, ED25519,
 *           MLKEM512 800-byte pubkey vs the dead 70-byte cap)
 *
 * Links the STATIC lib + internal headers (ehem_test_support) for the
 * crypto-shim X25519 and — in the label-required probe — the internal request
 * path (the public API cannot emit a label-less update body by design).
 * EHEMTEST key hygiene per REQ-TEST-003.
 */
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
#include "crypto_shim.h"      /* internal: local X25519 for the cross-check */
#include "proto_common.h"     /* internal: raw request for the 400 probe */
#include "transport.h"
#include "integration_env.h"
#include "ehem_test_keys.h"

typedef struct ui_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} ui_state;

static int setup(void **state)
{
    ui_state *s = calloc(1, sizeof *s);
    if (s == NULL) {
        return -1;
    }
    if (ehem_test_ctx(&s->ctx) != EHEM_OK ||
        ehem_login(s->ctx, ehem_test_passphrase()) != EHEM_OK) {
        free(s);
        return -1;
    }
    ehem_test_sweep(s->ctx);
    *state = s;
    return 0;
}

static int teardown(void **state)
{
    ui_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

/* Find `kid` in a full list walk; copies its label into label_out (cap 64,
 * NUL-terminated; label_out[0] stays '\0' when the kid is absent) and the raw
 * descr into descr/descr_len when non-NULL. */
static void find_key_meta(ehem_ctx *ctx, const char *kid, char *label_out,
                          uint8_t *descr, size_t *descr_len)
{
    ehem_key_page *page = NULL;
    size_t i;

    label_out[0] = '\0';
    assert_int_equal(ehem_key_list_all(ctx, &page), EHEM_OK);
    for (i = 0; i < page->listed; i++) {
        if (strcmp(page->entries[i].kid, kid) == 0) {
            snprintf(label_out, 64, "%s",
                     page->entries[i].label ? page->entries[i].label : "");
            if (descr != NULL && descr_len != NULL) {
                *descr_len = page->entries[i].descr_len;
                if (page->entries[i].descr_len > 0) {
                    memcpy(descr, page->entries[i].descr,
                           page->entries[i].descr_len);
                }
            }
            break;
        }
    }
    ehem_key_page_free(page);
}

/* -------------------------------------------------------------------------- */
/* REQ-KEY-007: update round-trip                                             */
/* -------------------------------------------------------------------------- */

static void test_update_roundtrip(void **state)
{
    ui_state *s = *state;
    ehem_key_create_params cp;
    char kid[EHEM_KID_HEX_SIZE];
    char label1[32], label2[40];
    static const uint8_t descr1[] = "M7-UPD-PROBE";
    uint8_t got_descr[64];
    size_t got_descr_len = 0;
    char got[64];
    ehem_key_page *hits = NULL;

    memset(&cp, 0, sizeof cp);
    ehem_test_label(label1, sizeof label1);
    cp.type = "ED25519";
    cp.label = label1;
    assert_int_equal(ehem_key_create(s->ctx, &cp, kid), EHEM_OK);
    ehem_test_track(&s->reg, kid);

    /* Rename (still EHEMTEST-prefixed per REQ-TEST-003) + set descr. */
    snprintf(label2, sizeof label2, "%s-upd", label1);
    assert_true(strlen(label2) <= 32);
    assert_int_equal(ehem_key_update(s->ctx, kid, label2,
                                     descr1, sizeof descr1 - 1), EHEM_OK);

    /* list reflects the new label AND the new descr. */
    find_key_meta(s->ctx, kid, got, got_descr, &got_descr_len);
    assert_string_equal(got, label2);
    assert_int_equal((int)got_descr_len, (int)(sizeof descr1 - 1));
    assert_memory_equal(got_descr, descr1, sizeof descr1 - 1);

    /* search by the new descr prefix finds the key (REQ-KEY-002 interop). */
    assert_int_equal(ehem_key_search_all(s->ctx, descr1, 6,
                                         EHEM_KEY_SEARCH_PREFIX, &hits),
                     EHEM_OK);
    assert_true(hits->listed >= 1);
    ehem_key_page_free(hits);

    /* FINDING (2026-07-18, device > doc): a label-only update CLEARS the
     * stored descr — the firmware rewrites the whole metadata record, so an
     * omitted descr means EMPTY, not "keep" (the doc's "left untouched" note
     * is wrong; recorded in REQ-KEY-007 rev2). Pin the real semantics. */
    assert_int_equal(ehem_key_update(s->ctx, kid, label1, NULL, 0), EHEM_OK);
    got_descr_len = 99;
    find_key_meta(s->ctx, kid, got, got_descr, &got_descr_len);
    assert_string_equal(got, label1);
    assert_int_equal((int)got_descr_len, 0);   /* descr wiped by the rewrite */
}

/* PROBE (REQ-KEY-007 open criterion): a well-formed kid the device does not
 * hold — doc says 406 → SDK NOT_FOUND. */
static void test_update_unknown_kid_probe(void **state)
{
    ui_state *s = *state;
    ehem_rc rc = ehem_key_update(s->ctx, "00112233445566778899aabbccddeeff",
                                 "EHEMTEST-nope", NULL, 0);
    printf("[probe] update unknown kid: rc=%s http=%ld\n",
           ehem_rc_str(rc), ehem_last_error(s->ctx)->http_status);
    assert_int_equal(rc, EHEM_ERR_NOT_FOUND);
    assert_int_equal(ehem_last_error(s->ctx)->http_status, 406);
}

/* PROBE (REQ-KEY-007 open criterion): a descr-only update body (no label) is
 * a firmware parse 400 — driven through the internal request path because the
 * public API refuses to build such a body. */
static void test_update_label_required_probe(void **state)
{
    ui_state *s = *state;
    ehem_key_create_params cp;
    char kid[EHEM_KID_HEX_SIZE];
    char label[32], body[128];
    char *resp = NULL;
    ehem_rc rc;

    memset(&cp, 0, sizeof cp);
    ehem_test_label(label, sizeof label);
    cp.type = "ED25519";
    cp.label = label;
    assert_int_equal(ehem_key_create(s->ctx, &cp, kid), EHEM_OK);
    ehem_test_track(&s->reg, kid);

    snprintf(body, sizeof body, "{\"kid\":\"%s\",\"descr\":\"AQID\"}", kid);
    rc = ehem_proto_request_raw(s->ctx, EHEM_HTTP_POST, "/api/keymgmt/update",
                                body, "keymgmt:upd", EHEM_TLS_REQ_DEFAULT,
                                &resp);
    printf("[probe] update without label: rc=%s http=%ld\n",
           ehem_rc_str(rc), ehem_last_error(s->ctx)->http_status);
    free(resp);
    assert_int_equal(rc, EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(s->ctx)->http_status, 400);
}

/* -------------------------------------------------------------------------- */
/* REQ-KEY-008: import + ECDH ext_kid cross-check + dedup                     */
/* -------------------------------------------------------------------------- */

/* Per-run unique X25519 seed. FINDING (2026-07-18, full-sweep run): the
 * repo's import dedup can reject material that was imported AND DELETED in
 * an earlier run once the device has rebooted in between (406 on previously
 * used constants) — fixed import fixtures are not rerun-safe across reboots.
 * Deriving the seed from the wall clock keeps every run's imported pubkey
 * unique, so the deliberate same-run duplicate probe stays the only dedup. */
static void import_seed(uint8_t seed[EHEM_X25519_KEYSIZE])
{
    uint64_t t = (uint64_t)time(NULL);
    size_t i;
    for (i = 0; i < EHEM_X25519_KEYSIZE; i++) {
        seed[i] = (uint8_t)((t >> ((i % 8) * 8)) ^ (0xA5u + 31u * (unsigned)i));
    }
}

static void test_import_x25519_ecdh_crosscheck(void **state)
{
    ui_state *s = *state;
    uint8_t seed[EHEM_X25519_KEYSIZE];
    uint8_t local_priv[EHEM_X25519_KEYSIZE];
    uint8_t local_pub[EHEM_X25519_KEYSIZE];
    uint8_t local_secret[EHEM_X25519_KEYSIZE];
    char kid_imp[EHEM_KID_HEX_SIZE], kid_imp2[EHEM_KID_HEX_SIZE];
    char kid_dev[EHEM_KID_HEX_SIZE];
    char label[32];
    ehem_key_import_params ip;
    ehem_key_create_params cp;
    ehem_key_details *d = NULL;
    ehem_ecdh_secret *dev = NULL;
    ehem_rc rc;

    import_seed(seed);
    assert_int_equal(ehem_x25519_keypair_from_seed(seed, local_priv,
                                                   local_pub), EHEM_OK);

    /* Import our public half. */
    memset(&ip, 0, sizeof ip);
    ehem_test_label(label, sizeof label);
    ip.type = "CURVE25519";
    ip.label = label;
    ip.pubkey = local_pub;
    ip.pubkey_len = sizeof local_pub;
    assert_int_equal(ehem_key_import(s->ctx, &ip, kid_imp), EHEM_OK);
    ehem_test_track(&s->reg, kid_imp);

    /* The imported key reads back with OUR public bytes. */
    assert_int_equal(ehem_key_get(s->ctx, kid_imp, &d), EHEM_OK);
    assert_non_null(d->pubkey);
    assert_int_equal((int)d->pubkey_len, EHEM_X25519_KEYSIZE);
    assert_memory_equal(d->pubkey, local_pub, EHEM_X25519_KEYSIZE);
    ehem_key_details_free(d);
    d = NULL;

    /* Device X25519 key ECDHes against the imported peer BY KID; the secret
     * must equal the shim-computed one byte-for-byte. */
    memset(&cp, 0, sizeof cp);
    ehem_test_label(label, sizeof label);
    cp.type = "CURVE25519";
    cp.label = label;
    assert_int_equal(ehem_key_create(s->ctx, &cp, kid_dev), EHEM_OK);
    ehem_test_track(&s->reg, kid_dev);

    assert_int_equal(ehem_key_get(s->ctx, kid_dev, &d), EHEM_OK);
    assert_int_equal(ehem_x25519_shared(local_priv, d->pubkey, local_secret),
                     EHEM_OK);
    ehem_key_details_free(d);

    assert_int_equal(ehem_ecdh(s->ctx, kid_dev, kid_imp, NULL, 0, NULL, &dev),
                     EHEM_OK);
    assert_int_equal((int)dev->secret_len, EHEM_X25519_KEYSIZE);
    assert_memory_equal(dev->secret, local_secret, EHEM_X25519_KEYSIZE);
    ehem_ecdh_secret_free(dev);

    /* PROBE: re-importing the identical pubkey — python-client finding says
     * the repo dedups with a 406. */
    ehem_test_label(label, sizeof label);
    ip.label = label;
    rc = ehem_key_import(s->ctx, &ip, kid_imp2);
    printf("[probe] duplicate import: rc=%s http=%ld payload=%s\n",
           ehem_rc_str(rc), ehem_last_error(s->ctx)->http_status,
           ehem_last_error(s->ctx)->device_payload
               ? ehem_last_error(s->ctx)->device_payload : "(none)");
    if (rc == EHEM_OK) {
        ehem_test_track(&s->reg, kid_imp2);   /* unexpected — clean it up */
    }
    assert_int_equal(rc, EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(s->ctx)->http_status, 406);

    ehem_zeroize(local_priv, sizeof local_priv);
    ehem_zeroize(local_secret, sizeof local_secret);
}

/* -------------------------------------------------------------------------- */
/* PROBE (REQ-KEY-008 open criterion): which type families import            */
/* -------------------------------------------------------------------------- */

/* SEC1 compressed P-256 generator point (02 ‖ Gx) — a valid public key. */
static const uint8_t P256_G_COMPRESSED[33] = {
    0x02,
    0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47,
    0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40, 0xf2,
    0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0,
    0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96,
};

/* RFC 8032 §7.1 TEST 1 Ed25519 public key. */
static const uint8_t ED25519_RFC_PUB[32] = {
    0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7,
    0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
    0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25,
    0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a,
};

static void probe_import(ui_state *s, const char *type, const char *mode,
                         const uint8_t *pub, size_t pub_len)
{
    ehem_key_import_params ip;
    char kid[EHEM_KID_HEX_SIZE];
    char label[32];
    ehem_rc rc;

    memset(&ip, 0, sizeof ip);
    ehem_test_label(label, sizeof label);
    ip.type = type;
    ip.label = label;
    ip.pubkey = pub;
    ip.pubkey_len = pub_len;
    ip.mode = mode;
    rc = ehem_key_import(s->ctx, &ip, kid);
    if (rc == EHEM_OK) {
        ehem_test_track(&s->reg, kid);
        printf("[probe] import %-10s (%3u bytes): ACCEPTED kid=%s\n",
               type, (unsigned)pub_len, kid);
    } else if (rc == EHEM_ERR_DEVICE &&
               ehem_last_error(s->ctx)->http_status == 406) {
        /* Dedup against this constant's earlier imports (which can outlive
         * their DELETION across a reboot — see import_seed's note): the
         * material got past type/shape validation, so the type IS supported. */
        printf("[probe] import %-10s (%3u bytes): 406 dedup — type "
               "supported, material previously imported\n",
               type, (unsigned)pub_len);
    } else {
        printf("[probe] import %-10s (%3u bytes): rc=%s http=%ld\n",
               type, (unsigned)pub_len, ehem_rc_str(rc),
               ehem_last_error(s->ctx)->http_status);
    }
}

static void test_import_type_support_probe(void **state)
{
    ui_state *s = *state;
    static uint8_t mlkem_pub[800];
    size_t i;

    /* Deterministic non-trivial bytes for the ML-KEM shape probe (the point is
     * the 800-byte length vs the dead 70-byte cap, not key validity). */
    for (i = 0; i < sizeof mlkem_pub; i++) {
        mlkem_pub[i] = (uint8_t)(i * 7 + 3);
    }

    probe_import(s, "SECP256R1", "ECDH,ExDSA",
                 P256_G_COMPRESSED, sizeof P256_G_COMPRESSED);
    probe_import(s, "ED25519", NULL, ED25519_RFC_PUB, sizeof ED25519_RFC_PUB);
    probe_import(s, "MLKEM512", NULL, mlkem_pub, sizeof mlkem_pub);
    /* No hard asserts: this is a recorded probe — REQ-KEY-008 captures the
     * accepted/rejected outcome per family. Cleanup runs via the registry. */
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping\n");
        return EHEM_SKIP_EXIT;
    }
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_update_roundtrip, setup, teardown),
        cmocka_unit_test_setup_teardown(test_update_unknown_kid_probe, setup, teardown),
        cmocka_unit_test_setup_teardown(test_update_label_required_probe, setup, teardown),
        cmocka_unit_test_setup_teardown(test_import_x25519_ecdh_crosscheck, setup, teardown),
        cmocka_unit_test_setup_teardown(test_import_type_support_probe, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
