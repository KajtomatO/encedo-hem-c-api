# Vendored: cJSON

- **Upstream:** https://github.com/DaveGamble/cJSON
- **Version:** 1.7.18 (git tag `v1.7.18`)
- **License:** MIT (see `LICENSE` in this directory; header block repeated
  at the top of `cJSON.c` / `cJSON.h`)
- **Files vendored:** `cJSON.c`, `cJSON.h`, `LICENSE`
- **Retrieved:** 2026-07-15 from
  `https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.18/`

## Provenance / integrity

SHA-256 of the files as vendored (unmodified upstream):

```
75c51de8fa40ac9d7a99319c6330719bd692eb81c0a869265f3d4c682533f9b9  cJSON.c
0578cc29132912edbc88f83207a8fc76e5db3db0605497e909a9384ef3cc474b  cJSON.h
a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c  LICENSE
```

## Why vendored

Two-file, MIT-licensed, stable — vendoring removes a class of packaging
friction on Windows/MinGW and adds no system dependency (REQ-BUILD-003,
ARCHITECTURE.md §1). The build compiles only this copy; no
`find_package`/pkg-config lookup for a system cJSON exists.

## Symbol containment (REQ-API-006)

The whole library is built with hidden default visibility
(`C_VISIBILITY_PRESET hidden`), so cJSON's `extern` symbols are **not**
exported from `libencedo-hem.so`/`.dll` — the `export_symbols` unit check
fails on any non-`ehem_` export. cJSON types never appear in a public
`include/ehem/` header; the SDK talks to cJSON only through the internal
`src/json.h` helper layer, whose public-facing functions return SDK types.

## Local modifications

None. The upstream files are vendored verbatim so a future version bump is a
clean re-download. If a patch ever becomes necessary, record it here with a
rationale and keep it minimal.

## Updating

1. Re-download the three files from the new tag's raw URL.
2. Update the version, retrieval date, and SHA-256 lines above.
3. Rebuild and run `ctest -L unit` — `test_json` exercises the helper layer
   against this copy.
