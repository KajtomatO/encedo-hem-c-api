/*
 * cert_fixture.h — frozen check-in / certificate fixtures for the offline
 * harvest and cert-install unit tests.
 *
 * supports: REQ-SYS-006 (harvest the leg-2 `newcrt` chain + leg-1 `csn` serial),
 *           REQ-TOOL-003 (skip-if-current serial comparison, install summary).
 *
 * The certificate is a self-signed EC leaf generated ONCE with a pinned serial
 * and validity window, then frozen here as base64 DER so its parsed fields
 * never drift. Expected values below were produced by wolfCrypt's cert decoder
 * (the same code path the SDK uses); openssl reports the identical serial and
 * dates. The check-in envelope constants embed that cert / a `csn` claim inside
 * a JWT the way the device and cloud legs deliver them — the JWT signature is
 * irrelevant (the harvest reads the payload, it does not verify).
 *
 * Regeneration recipe (scratchpad):
 *   openssl ecparam -name prime256v1 -genkey -noout -out leaf.key
 *   openssl req -new -key leaf.key -subj /CN=my.ence.do -out leaf.csr
 *   openssl x509 -req -in leaf.csr -signkey leaf.key -days 3650 \
 *     -set_serial 0xC173D2A90102030405060708 -outform DER -out leaf.der
 *   # then base64 leaf.der and re-derive the JWT envelopes.
 */
#ifndef EHEM_CERT_FIXTURE_H
#define EHEM_CERT_FIXTURE_H

/* The frozen leaf certificate (base64 standard DER), as delivered in `newcrt`. */
#define EHEM_FX_LEAF_B64 "MIIBHDCBxAINAMFz0qkBAgMEBQYHCDAKBggqhkjOPQQDAjAVMRMwEQYDVQQDDApteS5lbmNlLmRvMB4XDTI2MDcxNjEzMjI0MVoXDTM2MDcxMzEzMjI0MVowFTETMBEGA1UEAwwKbXkuZW5jZS5kbzBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABImFjFGuNVqQyhz/2Af5LJP4i1polpPcVZmUWBKHxWdTODjJCkq5czZvR0wAVg5xb9ntdKqy0vHDmp1XThI4ncwwCgYIKoZIzj0EAwIDRwAwRAIgE8ZA7/AEXXtsiZM9WWcMFM2Uawd/9rYzyS57AGQX/fYCICY4h95cUv9qhqhCqvORpiJIThBtDi6EjKT0kkDBEty4"

/* Parsed identity of EHEM_FX_LEAF_B64 (uppercase hex serial, ISO-8601 dates). */
#define EHEM_FX_LEAF_SERIAL   "C173D2A90102030405060708"
#define EHEM_FX_LEAF_CN       "my.ence.do"
#define EHEM_FX_LEAF_NOT_BEFORE "2026-07-16T13:22:41Z"
#define EHEM_FX_LEAF_NOT_AFTER  "2036-07-13T13:22:41Z"

/* Leg-2 `{"checked":<jwt>}` bodies: with the newcrt chain, without it, and with
 * an undecodable payload segment (harvest must tolerate the last two). */
#define EHEM_FX_CHECKED_WITH_NEWCRT "{\"checked\":\"eyJlY2RoIjoieDI1NTE5In0.eyJuZXdjcnQiOiJNSUlCSERDQnhBSU5BTUZ6MHFrQkFnTUVCUVlIQ0RBS0JnZ3Foa2pPUFFRREFqQVZNUk13RVFZRFZRUUREQXB0ZVM1bGJtTmxMbVJ2TUI0WERUSTJNRGN4TmpFek1qSTBNVm9YRFRNMk1EY3hNekV6TWpJME1Wb3dGVEVUTUJFR0ExVUVBd3dLYlhrdVpXNWpaUzVrYnpCWk1CTUdCeXFHU000OUFnRUdDQ3FHU000OUF3RUhBMElBQkltRmpGR3VOVnFReWh6LzJBZjVMSlA0aTFwb2xwUGNWWm1VV0JLSHhXZFRPRGpKQ2txNWN6WnZSMHdBVmc1eGI5bnRkS3F5MHZIRG1wMVhUaEk0bmN3d0NnWUlLb1pJemowRUF3SURSd0F3UkFJZ0U4WkE3L0FFWFh0c2laTTlXV2NNRk0yVWF3ZC85cll6eVM1N0FHUVgvZllDSUNZNGg5NWNVdjlxaHFoQ3F2T1JwaUpJVGhCdERpNkVqS1Qwa2tEQkV0eTQiLCJuZXdmd3MiOiIiLCJzdGF0dXMiOiJPSyJ9.sig\"}"
#define EHEM_FX_CHECKED_NO_NEWCRT "{\"checked\":\"eyJlY2RoIjoieDI1NTE5In0.eyJzdGF0dXMiOiJPSyJ9.sig\"}"
#define EHEM_FX_CHECKED_BADPAYLOAD "{\"checked\":\"eyJlY2RoIjoieDI1NTE5In0.!!!not-base64url!!!.sig\"}"
/* `newcrt` present and valid base64, but the bytes are not an X.509 cert. */
#define EHEM_FX_CHECKED_BAD_NEWCRT "{\"checked\":\"eyJlY2RoIjoieDI1NTE5In0.eyJuZXdjcnQiOiJhR1ZzYkc4Z2QyOXliR1E9In0.sig\"}"

/* Leg-1 `{"check":<jwt>}` bodies: `csn` matching the leaf serial, a different
 * serial, and no `csn` at all (device with no loaded cert / non-TLD hostname). */
#define EHEM_FX_CHECK_CSN_MATCH "{\"check\":\"eyJlY2RoIjoieDI1NTE5In0.eyJqdGkiOiJ4IiwiY3NuIjoid1hQU3FRRUNBd1FGQmdjSSJ9.sig\"}"
#define EHEM_FX_CHECK_CSN_OTHER "{\"check\":\"eyJlY2RoIjoieDI1NTE5In0.eyJqdGkiOiJ4IiwiY3NuIjoiQUtxN3pOMD0ifQ.sig\"}"
#define EHEM_FX_CHECK_NO_CSN "{\"check\":\"eyJlY2RoIjoieDI1NTE5In0.eyJqdGkiOiJ4In0.sig\"}"

/* Serial the EHEM_FX_CHECK_CSN_OTHER `csn` decodes to (00AABBCCDD normalized). */
#define EHEM_FX_OTHER_SERIAL "AABBCCDD"

#endif /* EHEM_CERT_FIXTURE_H */
