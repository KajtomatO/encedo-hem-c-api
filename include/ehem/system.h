/*
 * system.h — Encedo HEM C SDK, `system` protocol bindings.
 *
 * implements: REQ-SYS-001, REQ-SYS-002, REQ-API-005
 *
 * Unauthenticated device introspection: GET /api/system/status and
 * GET /api/system/version. Both return a typed struct the library allocates
 * and the caller releases with the matching ehem_*_free() (REQ-API-005).
 *
 * Field sets follow encedo-hem-api-doc system/status.md and system/version.md
 * (fetched 2026-07-15). Optional fields are marked present/absent: optional
 * strings are NULL when absent; optional numbers/booleans carry a `has_*`
 * flag (0/false being valid values). Unknown fields in the device response are
 * ignored (tolerant parsing); a missing REQUIRED field is EHEM_ERR_PROTOCOL.
 */
#ifndef EHEM_SYSTEM_H
#define EHEM_SYSTEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ehem/ehem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Key-repository memory statistics (status.repo_stats; present only with a
 * valid token, so absent on the M1 unauthenticated call). */
typedef struct ehem_repo_stats {
    int64_t deleted;
    int64_t fragmentation;
    int64_t freespace;
    int64_t invalid;
    int64_t total;
} ehem_repo_stats;

/*
 * GET /api/system/status result (filled by ehem_system_status()). Required
 * fields (always present per the doc): ctx, fls_state, uptime, temp. Everything
 * else is optional. The struct is named `_info` so it does not collide with the
 * ehem_system_status() function (C has one namespace for both).
 */
typedef struct ehem_status_info {
    /* Required. */
    int64_t ctx;         /* context number */
    int64_t fls_state;   /* fail-state value (0 = no errors) */
    int64_t uptime;      /* seconds since boot (primary presence indicator) */
    double  temp;        /* chip temperature, Celsius */

    /* storage: per-disk status strings (doc: array; may be empty). */
    char  **storage;
    size_t  storage_count;

    /* Optional strings (NULL when absent). */
    char *ts;            /* current time, ISO 8601 — only if RTC is set */
    char *hostname;      /* device hostname */
    char *format;        /* disk formatting state during personalization */

    /* Optional number (Unix timestamp — only if RTC is set). */
    bool    has_time;
    int64_t time;

    /* Optional booleans. */
    bool has_fw_upgrade; bool fw_upgrade; /* successful reboot post-upgrade */
    bool has_inited;     bool inited;     /* false if device unpersonalized */
    bool has_https;      bool https;      /* HTTPS availability */
    bool has_tts;        bool tts;        /* false if TrustedTime disabled */

    /* Optional nested repo stats (requires auth). */
    bool            has_repo_stats;
    ehem_repo_stats repo_stats;
} ehem_status_info;

/*
 * GET /api/system/version result (filled by ehem_system_version()). Required
 * fields: hwv, fwv, blv. The signing blobs and the rest are optional strings
 * (NULL when absent). Named `_info` to avoid colliding with the function.
 */
typedef struct ehem_version_info {
    /* Required. */
    char *hwv;   /* hardware version, e.g. "PPA rev 2.2" */
    char *fwv;   /* firmware version */
    char *blv;   /* bootloader version */

    /* Optional signing key/signature blobs (base64). */
    char *fwk;   /* firmware signing public key */
    char *fws;   /* firmware signature */
    char *blk;   /* bootloader signing public key */
    char *bls;   /* bootloader signature */

    /* Optional. */
    char *uis;     /* Encedo Manager version hash */
    char *sd_csd;  /* microSD CSD (requires token) */
    char *sd_cid;  /* microSD CID (requires token) */
} ehem_version_info;

/*
 * Fetch device status. On success writes *out (caller frees with
 * ehem_system_status_free) and returns EHEM_OK. On failure returns an ehem_rc
 * and leaves *out NULL; detail is retrievable via ehem_last_error(ctx).
 */
EHEM_API ehem_rc ehem_system_status(ehem_ctx *ctx, ehem_status_info **out);

/* Release a status struct from ehem_system_status(). NULL is a no-op. */
EHEM_API void ehem_system_status_free(ehem_status_info *status);

/*
 * Fetch device version info. On success writes *out (caller frees with
 * ehem_system_version_free) and returns EHEM_OK; otherwise returns an ehem_rc,
 * leaves *out NULL, and records detail on the context.
 */
EHEM_API ehem_rc ehem_system_version(ehem_ctx *ctx, ehem_version_info **out);

/* Release a version struct from ehem_system_version(). NULL is a no-op. */
EHEM_API void ehem_system_version_free(ehem_version_info *version);

/*
 * Result of the check-in handshake (filled by ehem_system_checkin()). The
 * device reports what the cloud response delivered; all fields are optional
 * strings (NULL when absent — tolerant parsing, firmware variants differ).
 */
typedef struct ehem_checkin_info {
    char *status;   /* general check-in status */
    char *newcrt;   /* TLS certificate update status ("OK"/"ERROR") */
    char *newfws;   /* available firmware update info */
    char *newuis;   /* available Manager UI update info */
    bool  cert_updated;  /* derived: newcrt present and non-empty */

    /*
     * REQ-SYS-006 — the certificate the cloud DELIVERED, harvested from the
     * leg-2 `checked` JWT's `newcrt` claim (base64 DER certificate chain),
     * NULL when the cloud sent none. Distinct from `newcrt` above, which is the
     * device's leg-3 status string. Firmware v1.2.2 acknowledges the claim but
     * never installs it (REQ-SYS-003 root cause), so this is what a caller
     * feeds to ehem_system_config_install_cert() to rotate the cert manually
     * (hem-tool cert-install / REQ-TOOL-003).
     */
    char *newcrt_chain;

    /*
     * The serial number (uppercase hex, no separators) of the certificate the
     * device currently serves, taken from the leg-1 `check` JWT's `csn` claim
     * — the same value the cloud uses to detect a stale cert. NULL when the
     * device reported none (no cert loaded, or hostname outside the default
     * TLD). Compare against the harvested leaf's serial (ehem_cert_inspect on
     * newcrt_chain) to decide whether a delivered cert is already installed.
     */
    char *current_serial;
} ehem_checkin_info;

/*
 * Run the three-step check-in handshake — how the device verifies firmware,
 * sets its RTC, and refreshes its TLS certificate:
 *   1. GET /api/system/checkin on the device (challenge),
 *   2. POST the challenge to the Encedo cloud (ehem_options.checkin_url,
 *      default EHEM_DEFAULT_CHECKIN_URL; always TLS-verified),
 *   3. POST the cloud's verified response back to the device.
 * No authentication required. Run it at session start, after long offline
 * periods, or when the device clock/certificate is stale; the SDK also runs
 * it automatically on an expired-certificate failure unless
 * ehem_options.no_auto_checkin is set.
 *
 * On success writes *out (caller frees with ehem_checkin_result_free) and
 * returns EHEM_OK; otherwise returns an ehem_rc, leaves *out NULL, and
 * records detail on the context.
 */
EHEM_API ehem_rc ehem_system_checkin(ehem_ctx *ctx, ehem_checkin_info **out);

/* Release a check-in result from ehem_system_checkin(). NULL is a no-op. */
EHEM_API void ehem_checkin_result_free(ehem_checkin_info *result);

/*
 * Identity of an X.509 certificate, as read by ehem_cert_inspect(). All strings
 * are owned by the struct and released by ehem_cert_info_free(); an optional
 * field the certificate omits is an empty string, never NULL.
 */
typedef struct ehem_cert_info {
    char *serial;       /* serial number, uppercase hex (no separators) */
    char *subject_cn;   /* subject Common Name */
    char *not_before;   /* validity start, UTC ISO-8601 (e.g. 2026-07-08T00:00:00Z) */
    char *not_after;    /* validity end,   UTC ISO-8601 */
} ehem_cert_info;

/*
 * Inspect a base64 DER certificate chain (such as ehem_checkin_info.newcrt_chain
 * or the argument to ehem_system_config_install_cert()): decode it and read the
 * identity of the LEAF certificate — the first one in the chain. On success
 * writes *out (caller frees with ehem_cert_info_free) and returns EHEM_OK;
 * returns EHEM_ERR_ARG on a NULL argument, EHEM_ERR_PROTOCOL if `crt_b64` is not
 * valid base64 or the bytes do not parse as a certificate (detail on the
 * context). Reads structure only — it does not verify the chain, signatures, or
 * whether the certificate is currently within its validity window.
 */
EHEM_API ehem_rc ehem_cert_inspect(ehem_ctx *ctx, const char *crt_b64,
                                   ehem_cert_info **out);

/* Release a cert-info struct from ehem_cert_inspect(). NULL is a no-op. */
EHEM_API void ehem_cert_info_free(ehem_cert_info *info);

/* ==========================================================================
 * Device configuration + reboot (authenticated; scope "system:config").
 * implements: REQ-SYS-004, REQ-SYS-005
 * ========================================================================== */

/*
 * GET /api/system/config result (filled by ehem_system_config()). Required core
 * fields: devid, hostname, user. Everything else is optional with the same
 * has_* / NULL discipline as ehem_status_info. The device also returns the
 * session-crypto values `spk` and `nonce` and a few niche fields (eid_sign,
 * http_option_dosprot_mode); these have no SDK use case yet and are deliberately
 * NOT surfaced (unknown fields are ignored — tolerant parsing, ARCHITECTURE §6).
 */
typedef struct ehem_config_info {
    /* Required. */
    char *devid;       /* device id (hex) */
    char *hostname;    /* configured hostname, e.g. "my.ence.do" */
    char *user;        /* user identity label */

    /* Optional strings (NULL when absent; may be present-but-empty, e.g. email). */
    char *email;       /* user email */
    char *eid;         /* device EID (base64) */
    char *instanceid;  /* provisioning instance UUID */
    char *origin;      /* configured CORS origin */
    char *ip;          /* device IP / CIDR */
    char *genuine_id;  /* attestation / genuine id */

    /* Optional numbers (has_* distinguishes 0 from absent). */
    bool has_iat;               int64_t iat;               /* config issued-at (unix) */
    bool has_uts;               int64_t uts;               /* config update timestamp */
    bool has_ctx;               int64_t ctx;               /* context number */
    bool has_storage_mode;      int64_t storage_mode;      /* storage default modes */
    bool has_storage_disk0size; int64_t storage_disk0size; /* plaintext disk size */
    bool has_storage_capacity;  int64_t storage_capacity;  /* total storage sectors */

    /* Optional booleans (has_* distinguishes false from absent). */
    bool has_dnsd;            bool dnsd;             /* mDNS/DNS responder enabled */
    bool has_trusted_ts;      bool trusted_ts;       /* trusted-time source */
    bool has_trusted_backend; bool trusted_backend;  /* trusted remote backend */
    bool has_allow_keysearch; bool allow_keysearch;  /* key-search security mode */
    bool has_http_hsts;       bool http_hsts;        /* HSTS enabled (http_option_hsts) */
} ehem_config_info;

/*
 * Read device configuration. Authenticated with scope "system:config". On
 * success writes *out (caller frees with ehem_system_config_free) and returns
 * EHEM_OK; otherwise returns an ehem_rc, leaves *out NULL, and records detail on
 * the context.
 */
EHEM_API ehem_rc ehem_system_config(ehem_ctx *ctx, ehem_config_info **out);

/* Release a config struct from ehem_system_config(). NULL is a no-op. */
EHEM_API void ehem_system_config_free(ehem_config_info *config);

/* Result of ehem_system_config_install_cert(). */
typedef struct ehem_cert_install_info {
    bool updated;          /* the device stored the new certificate */
    bool reboot_required;  /* a reboot is needed for it to take effect */
} ehem_cert_install_info;

/*
 * Install a TLS certificate: POST /api/system/config with body
 * {"tls":{"crt":"<crt_b64>"}} — the cert-only variant that replaces the stored
 * certificate and keeps the stored private key. `crt_b64` is the base64 DER
 * certificate chain. Authenticated with scope "system:config". On success
 * writes *out (caller frees with ehem_cert_install_free; may be NULL if the
 * caller does not need the flags) and returns EHEM_OK; a validator failure is
 * HTTP 400 (device payload in ehem_last_error), an install already in progress
 * is HTTP 409 → EHEM_ERR_DEVICE. Firmware v1.2.2 loads the TLS certificate only
 * at boot, so a successful install typically sets reboot_required — follow with
 * ehem_system_reboot().
 */
EHEM_API ehem_rc ehem_system_config_install_cert(ehem_ctx *ctx,
                                                 const char *crt_b64,
                                                 ehem_cert_install_info **out);

/* Release a cert-install result. NULL is a no-op. */
EHEM_API void ehem_cert_install_free(ehem_cert_install_info *info);

/*
 * Reboot the device: authenticated GET /api/system/reboot (scope
 * "system:config"). The device answers 200 and closes the connection, then
 * reboots after a short delay. A reboot invalidates every token the device
 * issued, so on success the SDK drops this context's token cache (the retained
 * passphrase, if any, allows transparent re-login on the next call). Returns
 * EHEM_OK once the device has accepted the reboot; the device is then briefly
 * unreachable. DISRUPTIVE — interrupts the device for all users.
 */
EHEM_API ehem_rc ehem_system_reboot(ehem_ctx *ctx);

/* ==========================================================================
 * Self-test (authenticated; the firmware checks no scope — the SDK requests
 * "system:config" and shares that token).
 * implements: REQ-SYS-007
 * ========================================================================== */

/*
 * Result of ehem_system_selftest(). `fls_state` is the device's own health
 * verdict (0 = nominal) and the one field the SDK requires in the response;
 * everything else parses tolerantly:
 *   - kat_busy is only emitted while a KAT run is in progress → false when
 *     absent;
 *   - se_state (ATECC608 secure-enclave status) is PPA-build-only → -1 when
 *     absent;
 *   - the repo_* fields mirror the response's repo_stats block — the ONLY
 *     place key-slot usage is visible (status does not carry it) → -1 each
 *     when the block is absent.
 * Timestamps are unix seconds, retro-adjusted by the firmware when the RTC
 * was set after a pre-clock run.
 */
typedef struct ehem_selftest_info {
    int64_t fls_state;            /* current fail-state after THIS run; 0 = OK */
    int64_t selftest_ts;          /* timestamp of this run */
    int64_t last_selftest_ts;     /* previous run */
    int64_t last_fls_state;       /* fail-state at the previous run */
    int64_t last_entropytest_ts;  /* most recent entropy self-test */
    int64_t last_kat_ts;          /* most recent known-answer-test pass */
    bool    kat_busy;             /* a KAT run is in progress (absent→false) */
    int64_t se_state;             /* ATECC608 status; -1 when absent (EPA) */
    int64_t repo_total;           /* keys in the repository; -1 when absent */
    int64_t repo_deleted;         /* logically deleted slots */
    int64_t repo_invalid;         /* slots flagged invalid */
    int64_t repo_fragmented;      /* slots awaiting compaction */
    int64_t repo_freeslots;       /* free slots remaining */
} ehem_selftest_info;

/*
 * Run the device self-test battery and read the result:
 * GET /api/system/selftest. SIDE EFFECT BY DESIGN: every call synchronously
 * re-runs the full battery (KAT, entropy, FLS) on the device — do not poll
 * this in a tight loop. The firmware accepts any valid bearer (no scope
 * check); the SDK requests "system:config".
 *
 * On success writes *out (caller frees with ehem_selftest_free) and returns
 * EHEM_OK. A response without `fls_state` is EHEM_ERR_PROTOCOL; 401 maps per
 * REQ-AUTH-003; 500 → EHEM_ERR_DEVICE.
 */
EHEM_API ehem_rc ehem_system_selftest(ehem_ctx *ctx, ehem_selftest_info **out);

/* Release a selftest result. NULL is a no-op. */
EHEM_API void ehem_selftest_free(ehem_selftest_info *info);

/* ==========================================================================
 * Device attestation material (authenticated; no firmware scope check — the
 * SDK requests "system:config").
 * implements: REQ-SYS-011
 * ========================================================================== */

/*
 * Result of ehem_system_attestation(). Two shapes, by ATECC608 state:
 *   - provisioned device (the normal case): `crt_b64` set (base64 DER device
 *     certificate — inspect with ehem_cert_inspect), csr_pem/key_b64 NULL;
 *   - fresh chip: `csr_pem` (PEM CSR) + `key_b64` (base64 DER attestation
 *     public key) set, crt_b64 NULL.
 * `genuine` is always present: a freshly-timestamped attestation token
 * proving a real ATECC608 holds the private key.
 */
typedef struct ehem_attestation_info {
    char *crt_b64;   /* base64 DER device certificate, or NULL */
    char *csr_pem;   /* PEM CSR (fresh chip), or NULL */
    char *key_b64;   /* base64 DER attestation public key (fresh chip), or NULL */
    char *genuine;   /* attestation token (always present) */
} ehem_attestation_info;

/*
 * Fetch ATECC608 attestation material: GET /api/system/config/attestation.
 * PPA-only — on an EPA build the route is absent and the device 404s →
 * EHEM_ERR_NOT_FOUND. A response without `genuine` is EHEM_ERR_PROTOCOL; a
 * secure-element I/O failure is HTTP 500 with an "atecc_N" body →
 * EHEM_ERR_DEVICE (body in ehem_last_error()->device_payload); 409 (install
 * in progress / fls_state) → EHEM_ERR_DEVICE.
 */
EHEM_API ehem_rc ehem_system_attestation(ehem_ctx *ctx,
                                         ehem_attestation_info **out);

/* Release an attestation result. NULL is a no-op. */
EHEM_API void ehem_attestation_free(ehem_attestation_info *info);

/* ==========================================================================
 * TLS recovery (attestation-authenticated cloud ceremony + config install).
 * implements: REQ-SYS-013
 * ========================================================================== */

/* Factory provisioning-cloud endpoint that issues a TLS bundle for the
 * default `my.ence.do` identity. Devices on custom domains pass their own
 * register endpoint to ehem_tls_recover(). */
#define EHEM_DEFAULT_REGISTER_URL "https://api.encedo.com/domain/register/my"

/*
 * Restore the device's TLS material from the Encedo provisioning cloud:
 * a device wipe deletes the stored TLS key + certificate, after which the
 * device serves plain HTTP only (httpsd cannot start) and
 * ehem_system_config_install_cert() cannot help — that path installs a
 * cert against an EXISTING key. This binding closes the gap:
 *
 *   1. fetch the device attestation (ehem_system_attestation) and present
 *      its `genuine` token to `register_url` (NULL →
 *      EHEM_DEFAULT_REGISTER_URL) — a cloud call, always fully
 *      TLS-verified regardless of the context's TLS mode;
 *   2. the cloud replies with a bundle {emp, key, crt, ...} in which the
 *      fresh TLS PRIVATE key is encrypted to the device's secure element
 *      (the firmware performs an ATECC ECDH against `emp` to decrypt it —
 *      the SDK relays the bundle VERBATIM and can never read the key);
 *   3. install it via POST /api/system/config {"tls": <bundle>} (scope
 *      "system:config").
 *
 * On success writes *out (the ehem_cert_install_info flags; free with
 * ehem_cert_install_free; `out` may be NULL) — `reboot_required` is
 * normally true: follow with ehem_system_reboot(), after which the device
 * serves HTTPS again. Use this against the device's http:// URL — that is
 * the state the device is in when recovery is needed.
 *
 * A cloud response that is not a JSON object carrying `crt` is
 * EHEM_ERR_PROTOCOL ("no usable bundle"); cloud/device failures map per
 * the shared path with the payload retrievable. DISRUPTIVE in effect:
 * installs key material (and the caller reboots).
 */
EHEM_API ehem_rc ehem_tls_recover(ehem_ctx *ctx, const char *register_url,
                                  ehem_cert_install_info **out);

/* ==========================================================================
 * Shutdown (authenticated, scope "system:shutdown").
 * implements: REQ-SYS-008
 * ========================================================================== */

/*
 * Stop the device's network and USB services: GET /api/system/shutdown. The
 * device answers 200, closes the connection, then stops its USB stack and
 * web-server task ~2 s later.
 *
 * WARNING: there is NO wake-up endpoint — recovery from shutdown requires a
 * PHYSICAL POWER-CYCLE of the device. This call is shipped API-complete per
 * the architecture's dangerous-operations rule; its live test is attended-
 * manual only and it must never run in unattended automation.
 *
 * As with reboot, success drops this context's entire token cache. The
 * device accepts scope "system:shutdown" or "system:config"; the SDK
 * requests the narrower "system:shutdown". Returns EHEM_OK once the device
 * has accepted the shutdown; 409 (install in progress) → EHEM_ERR_DEVICE.
 */
EHEM_API ehem_rc ehem_system_shutdown(ehem_ctx *ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_SYSTEM_H */
