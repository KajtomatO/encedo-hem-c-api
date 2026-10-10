/*
 * bip39.h — Manager-compatible master-secret derivation (REQ-AUTH-012):
 * 24-word BIP39 English mnemonics, the standard seed step, and Encedo
 * Manager's nibble-shifted 32-byte slice of that seed.
 *
 * INTERNAL header. The public entry points (ehem_mnemonic_generate,
 * ehem_master_secret_from_mnemonic) live in include/ehem/auth.h and wrap these.
 */
#ifndef EHEM_BIP39_H
#define EHEM_BIP39_H

#include <stddef.h>
#include <stdint.h>

#include "ehem/ehem.h"

#define EHEM_BIP39_WORDS        24
#define EHEM_BIP39_ENTROPY_SIZE 32   /* 256 bits → 24 words */
#define EHEM_BIP39_SEED_SIZE    64   /* PBKDF2-HMAC-SHA512 output */
#define EHEM_BIP39_SECRET_SIZE  32   /* the Manager's master secret */
/* 24 words of at most 8 letters, 23 single spaces, NUL. */
#define EHEM_BIP39_MNEMONIC_CAP (EHEM_BIP39_WORDS * 9)

/*
 * Encode 32 bytes of entropy as 24 words (checksum = the first 8 bits of
 * SHA-256(entropy), BIP39), joined by single spaces into `out`
 * (cap >= EHEM_BIP39_MNEMONIC_CAP). EHEM_ERR_ARG on NULL / short buffer.
 */
ehem_rc ehem_bip39_from_entropy(const uint8_t entropy[EHEM_BIP39_ENTROPY_SIZE],
                                char *out, size_t cap);

/* 24 words from fresh backend entropy (ehem_random_bytes — the caller has run
 * ehem_global_init()). Same output contract as ehem_bip39_from_entropy. */
ehem_rc ehem_bip39_generate(char *out, size_t cap);

/*
 * Validate `words` — any whitespace between words, exactly 24 lowercase words
 * from the English list, BIP39 checksum correct (what jsbip39's check() does)
 * — and derive the Manager master secret exactly as build.js:692-697 does:
 *   seed   = PBKDF2-HMAC-SHA512(canonical mnemonic, "mnemonic", 2048, 64 B)
 *   secret = the 32 bytes encoded by hex(seed)[1..65) — `seedM.substr(1, 64)`,
 *            i.e. secret[i] = (seed[i] << 4) | (seed[i+1] >> 4), i = 0..31
 * The canonical mnemonic is the words joined by single spaces (jsbip39's
 * toSeed re-joins them the same way). On an invalid mnemonic returns
 * EHEM_ERR_ARG and, when `reason` is non-NULL, writes a short explanation
 * (`reason_cap` bytes); no derivation happens. Intermediates are zeroized.
 */
ehem_rc ehem_bip39_to_master_secret(const char *words,
                                    uint8_t secret[EHEM_BIP39_SECRET_SIZE],
                                    char *reason, size_t reason_cap);

#endif /* EHEM_BIP39_H */
