# Vendored: BIP39 English wordlist

- **Upstream:** the standard BIP39 English wordlist (2048 words, sorted), taken
  from Encedo Manager's copy `assets/wordlist_english_v1.js` (encedo-manager
  `b33c236`) so the SDK derives master secrets from exactly the list the
  Manager uses (REQ-AUTH-012). The Manager's BIP39 code is iancoleman's
  `jsbip39` (`assets/jsbip39_v1.js`), a port of python-mnemonic.
- **License:** MIT — Copyright (c) 2013 Pavol Rusnak (the `jsbip39_v1.js`
  header block, copied verbatim into `LICENSE` here).
- **Files vendored:** `wordlist_english.c` (generated: the list as a C array),
  `wordlist_english.h`, `LICENSE`.
- **Retrieved:** 2026-10-08 from the local checkout of encedo-manager.

## Provenance / integrity

SHA-256 of the Manager source the array was generated from:

```
39fe7e02d9d6392817302698653b28e56b0540c55c646f6705adbcd19b34086e  assets/wordlist_english_v1.js
```

The C array holds the 2048 words in the file's order (which is sorted —
`src/bip39.c` binary-searches it). Regenerate with a 10-line script that
extracts every `"word"` string between the `= [` and `]` of that file; the
word count and sort order are asserted.

## Why vendored

The derivation must match Encedo Manager bit for bit (a device initialised by
either client must be administrable from the other), so the list is the
Manager's, compiled in — no system dependency, no download at build time
(REQ-BUILD-003 style).

## Symbol containment (REQ-API-006)

The array is `const char *const ehem_bip39_wordlist_en[]`, compiled with the
library's hidden default visibility; it never appears in a public header. The
public surface is `ehem_mnemonic_generate` / `ehem_master_secret_from_mnemonic`
in `include/ehem/auth.h`.

## Local modifications

None — the words are the upstream list verbatim.
