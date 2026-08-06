---
id: STEP-M4-040
title: "hem-tool keys pub — public material + typed metadata by KID"
milestone: M4
implements: ["REQ-TOOL-007"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M4-010"]
evidence:
  commits:
    - "2ad49ca — hem-tool keys pub (REQ-TOOL-007)"
  tests:
    - "tests/unit/test_keys_pub.c — asymmetric b64 summary (kid/type/family/updated/pubkey) + read-only 3-request assert; --hex + flag-set classification (ECDH,CURVE25519 → family/modes); --raw = exactly the material bytes, no prose/newline; CERT der; symmetric 'material: (none' exit 0 (+ raw = empty stdout exit 0); missing passphrase / NULL kid / malformed kid → exit 2 with 0 requests; 406 → exit 1 naming the kid"
  notes: >
    hem-tool-core keys.{h,c}: hem_keys_pub_opts {passphrase,kid,format
    B64|HEX|RAW} + hem_keys_pub_run (login → ehem_key_get → summary with
    REQ-KEY-006 classification via ehem_key_type_parse/family_str). RAW
    keeps stdout bytes-only (fwrite; prose → err; cert notice suppressed
    in main.c for raw). Local kid_ok + fprint_b64 in keys.c — hem-tool-core
    is public-API-only and the SDK exposes no encoder (decoded bytes in,
    display encoding is the tool's). main.c: `keys pub KID` third
    positional (other keys subcommands still reject a stray arg), --hex/
    --raw flags (mutually exclusive → 2), usage text. LIVE (my.ence.do,
    2026-07-16): pub of the paired-authenticator CURVE25519 key → b64
    32-byte pubkey + family/updated; CERT key --hex → full DER chain;
    --raw | wc -c → exactly 32. ./dev ci 20/20 gcc+clang; asan clean;
    export/header gates green (no new SDK exports — tool-side only).
reopened: []
cancelled: null
---

**Goal:** `hem-tool keys pub <kid>` fetches via `ehem_key_get` and prints
type string, REQ-KEY-006 classification (family, modes), updated
timestamp, and the material (`pubkey`/`der`) as padded base64; `--hex`
switches encoding; `--raw` writes only the raw material bytes to stdout
(pipeline-friendly for the gate demo). Symmetric key → metadata + "no
public material", exit 0. Exit 0/1/2 per tool conventions.

**Notes:** Read-only thin consumer; formatting logic in `hem-tool-core`
(shared with unit tests, like keys list). Credentials via
`EHEM_URL`/`--url`, `EHEM_PASSPHRASE`/`--passphrase`. `--raw` must write
bytes exactly (fwrite, no trailing newline) and suppress all prose on
stdout (notices → stderr) so pipes stay clean.

**Definition of done**
- [x] Unit (fake transport): asymmetric fixture prints type +
      classification + b64 material; `--hex` and `--raw` switch encoding
      (`--raw` = material bytes only); CERT/DER_PKEY prints der;
      symmetric prints "no public material" exit 0. (test_keys_pub 7 cases.)
- [x] Unit: malformed/missing kid or missing URL/passphrase → exit 2, no
      I/O; 406 → exit 1 naming the kid; request sequence is auth + get
      only (read-only assert). (test_pub_usage_errors_no_io — 0 requests;
      test_pub_not_found; test_pub_asymmetric_b64 asserts 3 requests.)
- [x] Live: `keys pub` of a device key shows its 32-byte pubkey (manual;
      the CURVE25519 authenticator key + the CERT chain, all 3 formats —
      see evidence; EHEMTEST demo repeats at the M4 gate).
- [x] MinGW clean (no POSIX-only I/O — tmpfile capture, %lld via
      long long cast, binary-safe fwrite); ASan/LSan clean. (asan 20/20;
      MinGW leg proven by CI on push — no platform-specific code added.)
