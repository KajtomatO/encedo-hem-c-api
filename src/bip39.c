/*
 * bip39.c — Manager-compatible master-secret derivation (see bip39.h).
 *
 * implements: REQ-AUTH-012 (the public wrappers are in proto_auth.c)
 *
 * The reference is Encedo Manager's own code (encedo-manager b33c236):
 * assets/jsbip39_v1.js (standard BIP39: generate/check/toSeed with
 * PBKDF2-HMAC-SHA512, 2048 rounds, salt "mnemonic", NFKD — identity for the
 * English list) and assets/build.js:697 / :6829, which take the 64-byte seed as
 * hex and slice `substr(1, 64)`: 64 hex characters from offset ONE, a
 * nibble-shifted copy of the seed. That slice is what every Manager-initialised
 * device holds as its master key, so it is reproduced here exactly.
 */
#include "bip39.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "crypto_shim.h"
#include "vendor/bip39/wordlist_english.h"

#define BIP39_SALT   "mnemonic"
#define BIP39_ROUNDS 2048u
#define BIP39_BITS   (EHEM_BIP39_WORDS * 11)        /* 264 = 256 entropy + 8 checksum */
#define BIP39_BYTES  ((BIP39_BITS + 7) / 8)         /* 33 */

/* Index of the `len`-character word `w` in the (sorted) list, or -1. */
static int word_index(const char *w, size_t len)
{
    int lo = 0, hi = EHEM_BIP39_WORDLIST_SIZE - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const char *c = ehem_bip39_wordlist_en[mid];
        int cmp = strncmp(w, c, len);
        if (cmp == 0 && c[len] != '\0') {
            cmp = -1;                /* w is a proper prefix of c → w sorts first */
        }
        if (cmp == 0) {
            return mid;
        }
        if (cmp < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return -1;
}

static void say(char *reason, size_t cap, const char *text)
{
    if (reason != NULL && cap != 0) {
        snprintf(reason, cap, "%s", text);
    }
}

ehem_rc ehem_bip39_from_entropy(const uint8_t entropy[EHEM_BIP39_ENTROPY_SIZE],
                                char *out, size_t cap)
{
    uint8_t bits[BIP39_BYTES];
    uint8_t hash[EHEM_SHA256_SIZE];
    size_t pos = 0;
    ehem_rc rc;

    if (entropy == NULL || out == NULL || cap < EHEM_BIP39_MNEMONIC_CAP) {
        return EHEM_ERR_ARG;
    }
    rc = ehem_sha256(entropy, EHEM_BIP39_ENTROPY_SIZE, hash);
    if (rc != EHEM_OK) {
        return rc;
    }
    memcpy(bits, entropy, EHEM_BIP39_ENTROPY_SIZE);
    bits[EHEM_BIP39_ENTROPY_SIZE] = hash[0];      /* 8 checksum bits */

    for (int i = 0; i < EHEM_BIP39_WORDS; i++) {
        unsigned idx = 0;
        for (int b = 0; b < 11; b++) {
            unsigned bit = (unsigned)(i * 11 + b);
            idx = (idx << 1) | ((unsigned)(bits[bit >> 3] >> (7 - (bit & 7))) & 1u);
        }
        const char *w = ehem_bip39_wordlist_en[idx];
        size_t wl = strlen(w);
        if (i != 0) {
            out[pos++] = ' ';
        }
        memcpy(out + pos, w, wl);
        pos += wl;
    }
    out[pos] = '\0';
    ehem_zeroize(bits, sizeof bits);
    ehem_zeroize(hash, sizeof hash);
    return EHEM_OK;
}

ehem_rc ehem_bip39_generate(char *out, size_t cap)
{
    uint8_t entropy[EHEM_BIP39_ENTROPY_SIZE];
    ehem_rc rc = ehem_random_bytes(entropy, sizeof entropy);
    if (rc == EHEM_OK) {
        rc = ehem_bip39_from_entropy(entropy, out, cap);
    }
    ehem_zeroize(entropy, sizeof entropy);
    return rc;
}

ehem_rc ehem_bip39_to_master_secret(const char *words,
                                    uint8_t secret[EHEM_BIP39_SECRET_SIZE],
                                    char *reason, size_t reason_cap)
{
    char canon[EHEM_BIP39_MNEMONIC_CAP];
    uint8_t bits[BIP39_BYTES];
    uint8_t hash[EHEM_SHA256_SIZE];
    uint8_t seed[EHEM_BIP39_SEED_SIZE];
    size_t n = 0, pos = 0;
    const char *p;
    ehem_rc rc;

    say(reason, reason_cap, "");
    if (words == NULL || secret == NULL) {
        return EHEM_ERR_ARG;
    }
    memset(bits, 0, sizeof bits);

    /* Split on any whitespace (jsbip39 splitWords), look each word up, pack
     * its 11-bit index, and rebuild the canonical single-space mnemonic. */
    p = words;
    for (;;) {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        const char *start = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (n == EHEM_BIP39_WORDS) {
            say(reason, reason_cap, "more than 24 words");
            goto invalid;
        }
        int idx = (len >= 3 && len <= 8) ? word_index(start, len) : -1;
        if (idx < 0) {
            char msg[96];
            snprintf(msg, sizeof msg, "word %u (\"%.*s\") is not in the BIP39 "
                     "English list (lowercase only)",
                     (unsigned)(n + 1), (int)(len > 16 ? 16 : len), start);
            say(reason, reason_cap, msg);
            goto invalid;
        }
        if (n != 0) {
            canon[pos++] = ' ';
        }
        memcpy(canon + pos, start, len);
        pos += len;
        for (int b = 0; b < 11; b++) {
            unsigned bit = (unsigned)(n * 11 + (size_t)b);
            if ((idx >> (10 - b)) & 1) {
                bits[bit >> 3] |= (uint8_t)(0x80u >> (bit & 7));
            }
        }
        n++;
    }
    canon[pos] = '\0';
    if (n != EHEM_BIP39_WORDS) {
        char msg[64];
        snprintf(msg, sizeof msg, "%u words, need exactly 24", (unsigned)n);
        say(reason, reason_cap, msg);
        goto invalid;
    }

    /* Checksum: the last 8 bits must be SHA-256(entropy)[0]. */
    rc = ehem_sha256(bits, EHEM_BIP39_ENTROPY_SIZE, hash);
    if (rc != EHEM_OK) {
        goto out;
    }
    if (hash[0] != bits[EHEM_BIP39_ENTROPY_SIZE]) {
        say(reason, reason_cap, "checksum mismatch — a word is wrong or out of order");
        goto invalid;
    }

    /* The Manager's derivation: seed, then the substr(1, 64) nibble shift. */
    rc = ehem_kdf_pbkdf2_sha512((const uint8_t *)canon, pos,
                                (const uint8_t *)BIP39_SALT, sizeof BIP39_SALT - 1,
                                BIP39_ROUNDS, seed, sizeof seed);
    if (rc != EHEM_OK) {
        goto out;
    }
    for (int i = 0; i < EHEM_BIP39_SECRET_SIZE; i++) {
        secret[i] = (uint8_t)((seed[i] << 4) | (seed[i + 1] >> 4));
    }
    rc = EHEM_OK;
    goto out;

invalid:
    rc = EHEM_ERR_ARG;
out:
    ehem_zeroize(canon, sizeof canon);
    ehem_zeroize(bits, sizeof bits);
    ehem_zeroize(hash, sizeof hash);
    ehem_zeroize(seed, sizeof seed);
    return rc;
}
