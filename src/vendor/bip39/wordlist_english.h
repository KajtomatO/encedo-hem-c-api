/*
 * wordlist_english.h — the BIP39 English wordlist (see wordlist_english.c).
 */
#ifndef EHEM_BIP39_WORDLIST_ENGLISH_H
#define EHEM_BIP39_WORDLIST_ENGLISH_H

#define EHEM_BIP39_WORDLIST_SIZE 2048

/* Sorted ascending (strcmp order), lowercase ASCII, 3..8 letters each. */
extern const char *const ehem_bip39_wordlist_en[EHEM_BIP39_WORDLIST_SIZE];

#endif /* EHEM_BIP39_WORDLIST_ENGLISH_H */
