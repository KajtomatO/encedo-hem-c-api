# encedo-hem-api-doc coverage record

Produced by STEP-M9-010 (1.0 conformance sweep, 2026-08-06). Reconciles
every page of https://github.com/KajtomatO/encedo-hem-api-doc (local
checkout, fw v1.2.2 era) against the SDK surface, field by field.

**Dispositions:** `BOUND` (SDK binding exists), `UNBOUND-DELIBERATE`
(never planned, with reason), `DEFERRED-M10` (post-1.0 milestone),
`ABSENT-IN-FW` (documented but not present in firmware v1.2.2).
Precedence for conflicts is the standing rule (REQUIREMENTS-MANAGEMENT
§8): real device > Encedo Manager > API doc; live-proven divergences are
recorded in the owning REQ and/or KNOWN-ISSUES, and this file only
points at them.

## keymgmt/ — 8 pages, all bound

| Page | Endpoint | Disposition | SDK symbol(s) |
|---|---|---|---|
| list.md | `GET /api/keymgmt/list[/{offset}[/{limit}]]` | BOUND | `ehem_key_list`, `ehem_key_list_all` |
| search.md | `POST /api/keymgmt/search` | BOUND | `ehem_key_search`, `ehem_key_search_all` |
| get.md | `GET /api/keymgmt/get/{kid}` | BOUND | `ehem_key_get` |
| create.md | `POST /api/keymgmt/create` | BOUND | `ehem_key_create` |
| delete.md | `DELETE /api/keymgmt/delete/{kid}` | BOUND | `ehem_key_delete` |
| update.md | `POST /api/keymgmt/update` | BOUND | `ehem_key_update` |
| import.md | `POST /api/keymgmt/import` | BOUND | `ehem_key_import` |
| derive.md | `POST /api/keymgmt/derive` | BOUND | `ehem_key_derive` |

Field-level notes (doc vs SDK/device, with where each is recorded):

- **update omitted-`descr` semantics — doc wrong, live-proven:** the
  firmware rewrites the whole metadata record, so omitting `descr`
  CLEARS it; the doc's "omit to leave unchanged" contradicts the device
  (REQ-KEY-007 rev 2; keymgmt.h `ehem_key_update` doc).
- **get scopes — doc wrong for fw v1.2.2:** the documented prefix
  scopes `keymgmt:get`/`keymgmt:gen` are rejected; the SDK uses only
  exact `keymgmt:use:<kid>` (REQ-KEY-003; ARCHITECTURE §12 risk 3).
- **search no-match — undocumented 404:** the device returns 404 when
  nothing matches (doc lists no 404); the SDK maps it to EHEM_OK with
  an empty page (REQ-KEY-002).
- **search auth surface deliberately unused:** the doc's
  unauthenticated `^`-prefix bypass (`allow_keysearch`) and the
  alternate scopes `keymgmt:list`/`auth:ext:pair` are never used — the
  SDK always authenticates with `keymgmt:search`. Recorded here as a
  deliberate non-use, not a gap: no consumer requirement asks for
  pre-login descriptor discovery.
- **import — dead 70-byte cap, dedup 406:** the doc's 70-byte pubkey
  cap is dead firmware code (broken length check) and PQC-sized keys
  import fine; the doc's "406 = malformed pubkey" is really "406 =
  duplicate key" live (REQ-KEY-008; KNOWN-ISSUES).
- **derive — not externally reproducible; undocumented dedup:** the
  doc's ECDH+HKDF pipeline does NOT reproduce the stored key (extra
  undisclosed repo transform), and exact repeat derivation is
  dedup-406'd (REQ-KEY-009; KNOWN-ISSUES). The HKDF length quirk makes
  derived-longer-than-secret potentially nondeterministic (REQ-KEY-009).
- **derive peer-pubkey cap — doc arithmetic error:** doc says "max 66
  bytes (fits P-521)", but a compressed SEC1 P-521 point is 1+66 = 67
  bytes; the SDK enforces 67 (`KEYMGMT_PEER_PUBKEY_MAX`, aligned with
  the REQ-OPS-004 peer rule the device accepted live). NEW record —
  upstream doc-fix candidate.
- **list/search `type` shape:** real devices return comma-separated
  flag sets (`ATT,PKEY,…`), not the doc examples' bare algorithm
  strings; `ehem_key_type_parse` decomposes them (REQ-KEY-006).
- Client-side caps the doc leaves numberless: label 1..32 printable
  (live-probed), descr ≤64 raw bytes (device read-side truncates at 64;
  create-side check broken) — REQ-KEY-005 rev 2.
- Tolerant-parsing note: the wire `listed` field is recomputed from the
  parsed array rather than read (header comment updated at this sweep).

## crypto/ — 13 pages (incl. pqc/), all bound

| Page | Endpoint | Disposition | SDK symbol(s) |
|---|---|---|---|
| exdsa-sign.md | `POST /api/crypto/exdsa/sign` | BOUND | `ehem_sign` |
| exdsa-verify.md | `POST /api/crypto/exdsa/verify` | BOUND | `ehem_verify` |
| ecdh.md | `POST /api/crypto/ecdh` | BOUND | `ehem_ecdh` |
| hmac-hash.md | `POST /api/crypto/hmac/hash` | BOUND | `ehem_hmac` |
| hmac-verify.md | `POST /api/crypto/hmac/verify` | BOUND | `ehem_hmac_verify` |
| cipher-encrypt.md | `POST /api/crypto/cipher/encrypt` | BOUND | `ehem_encrypt` |
| cipher-decrypt.md | `POST /api/crypto/cipher/decrypt` | BOUND | `ehem_decrypt` |
| cipher-wrap.md | `POST /api/crypto/cipher/wrap` | BOUND | `ehem_wrap` |
| cipher-unwrap.md | `POST /api/crypto/cipher/unwrap` | BOUND | `ehem_unwrap` |
| pqc/mlkem-encaps.md | `POST /api/crypto/pqc/mlkem/encaps` | BOUND | `ehem_mlkem_encaps` |
| pqc/mlkem-decaps.md | `POST /api/crypto/pqc/mlkem/decaps` | BOUND | `ehem_mlkem_decaps` |
| pqc/mldsa-sign.md | `POST /api/crypto/pqc/mldsa/sign` | BOUND | `ehem_mldsa_sign` |
| pqc/mldsa-verify.md | `POST /api/crypto/pqc/mldsa/verify` | BOUND | `ehem_mldsa_verify` |

Sweep verdict: on all 13 endpoints, no documented request field is
unsent, no undocumented field is sent, and every documented response
field is parsed and exposed. The five live-proven doc-vs-device
conflicts from M6/M7 are all recorded where they belong and need no new
records:

- HKDF info strings: encrypt/decrypt use `"encedo-aes"`‖ctx,
  wrap/unwrap `"encedo-kek"`‖ctx — the doc's `"encedo"` default is
  wrong (REQ-OPS-006 / REQ-OPS-009; KNOWN-ISSUES).
- hmac ECDH-derived flow uses the RAW ECDH secret — the doc's
  "ECDH + HKDF" is wrong (REQ-OPS-005); direct flow ignores a supplied
  `alg` (REQ-OPS-005).
- ecdh raw mode truncates to a fixed 32 bytes — the doc's "full curve
  length" is wrong (REQ-OPS-004).
- mlkem/decaps response `alg` is an uninitialized-buffer echo — doc
  documents it as a reliable enum (REQ-OPS-007; KNOWN-ISSUES).
- mldsa/verify failure emits a raw out-of-range HTTP status, not the
  documented 406 (REQ-OPS-008; KNOWN-ISSUES).

Additional notes (recorded SDK-side; doc gaps, not SDK gaps):

- **Peer pubkey cap 66 vs 67 — same doc arithmetic error as
  keymgmt/derive:** every crypto page says ≤66 bytes; a compressed
  SEC1 P-521 point is 67, which the SDK enforces
  (`EHEM_ECDH_PUBKEY_MAX`) and the device accepts. Upstream doc-fix
  candidate (one fix covers ecdh/cipher/hmac pages + keymgmt/derive).
- wrap `msg` alignment (multiple of 8, ≥16) is device-enforced but
  absent from the doc page (recorded in crypto.h / REQ-OPS-009).
- exdsa sign rejects an empty message device-side; doc states only
  maxima (recorded in crypto.h).
- 406 conflates kid-not-found with crypto failure on sign/verify —
  deliberately mapped to EHEM_ERR_DEVICE, never NOT_FOUND
  (REQ-OPS-001/003); no crypto page documents a 404.

## auth/ — 7 pages: 6 bound, 1 deliberately unbound

| Page | Endpoint | Disposition | SDK symbol(s) |
|---|---|---|---|
| token.md | `GET/POST /api/auth/token` | BOUND | `ehem_login`/`ehem_logout`/`ehem_login_mobile` (lazy; internal `ehem_auth_ensure_token`) |
| init.md | `GET/POST /api/auth/init` | UNBOUND-DELIBERATE | — one-shot device personalisation (factory/provisioning act, like `config/provisioning`; re-init requires a wipeout first) |
| ext-init.md | `POST /api/auth/ext/init` | BOUND | `ehem_ext_init` |
| ext-validate.md | `POST /api/auth/ext/validate` | BOUND | `ehem_ext_validate` |
| ext-mac.md | `POST /api/auth/ext/mac` | BOUND | `ehem_ext_mac` |
| ext-request.md | `POST /api/auth/ext/request` | BOUND | `ehem_ext_request` |
| ext-token.md | `POST /api/auth/ext/token` | BOUND | `ehem_ext_token` |

Also in this family but with **no doc page at all**: the cloud
notification broker (`api.encedo.com/notify/*`) the mobile flow
requires — bound by `proto_notify.c`, specified only by REQ-AUTH-008
(shapes live-pinned at M8). The doc repo's only trace of it is
Manager-usage snippets.

Field-level notes:

- Full field match on all six bound endpoints; every documented
  response field is parsed and exposed except bearer-JWT claims, which
  the SDK deliberately treats as opaque (only `exp` is decoded, for
  cache expiry) — tolerant-parsing policy, and the bearer itself is
  cache-internal by design (no public token getter; `ehem_ext_token`
  is the one deliberate exception).
- **Deliberate non-use:** the optional eJWT `ctx` claim is never sent
  (no consumer requirement); the challenge `lbl` is parsed but kept
  internal (debug identity only); ext-init's alternate `system:config`
  scope is never used (SDK always `auth:ext:pair`).
- **Doc-vs-device conflicts, all already recorded:** challenge `exp`
  is a submit deadline, not a token-lifetime cap — the doc's
  "challenge exp or smaller" guidance is wrong, the SDK requests
  now+3600 uncapped (REQ-AUTH-001, STEP-M2-045); the ext bearer's
  `exp` is copied from the **authreply**, not the authreq as the doc
  says (REQ-AUTH-007; KNOWN-ISSUES — real phones cap at 15 min);
  ext-token's documented anti-bruteforce delay is dead firmware code
  (KNOWN-ISSUES); a request-body `exp` on ext-request is ignored
  (KNOWN-ISSUES); the ~8%-fast RTC drives both drift-recovery paths
  (KNOWN-ISSUES).
- **SDK strictness beyond the doc (device-pinned):** `pid` must decode
  to exactly 32 bytes (other lengths pair a key that can never log
  in); ctx/note range violations are rejected client-side because the
  firmware silently drops them; ext-validate 406 names dedup as a
  second cause beside slots-full.
- **Upstream doc-fix candidate (NEW):** token.md lists "missing or
  invalid `cfg` object" as a 400 cause on `POST /api/auth/token` —
  `cfg` is an `/api/auth/init` concept; apparent copy-paste error.

## system/ — 14 pages: 8 bound, 2 deliberately unbound, 4 deferred to M10

| Page | Endpoint(s) | Disposition | SDK symbol(s) |
|---|---|---|---|
| status.md | `GET /api/system/status` | BOUND | `ehem_system_status` |
| version.md | `GET /api/system/version` | BOUND | `ehem_system_version` |
| checkin.md | `GET+POST /api/system/checkin` | BOUND | `ehem_system_checkin` (3-leg relay) |
| config.md | `GET /api/system/config` | BOUND | `ehem_system_config` |
| config.md | `POST /api/system/config` | BOUND (tls only — see note) | `ehem_system_config_install_cert`, `ehem_tls_recover` |
| config-attestation.md | `GET /api/system/config/attestation` | BOUND | `ehem_system_attestation` |
| reboot.md | `GET /api/system/reboot` | BOUND | `ehem_system_reboot` |
| selftest.md | `GET /api/system/selftest` | BOUND | `ehem_system_selftest` |
| shutdown.md | `GET /api/system/shutdown` | BOUND | `ehem_system_shutdown` |
| config-provisioning.md | `POST /api/system/config/provisioning` | UNBOUND-DELIBERATE | — factory-only ATECC cert write (REQ-SYS-011 decision) |
| diag.md | `GET /api/diag/*` (9 endpoints) | UNBOUND-DELIBERATE | — DIAG-build-only, unauthenticated, destructive (wipe/corrupt/memdump); production builds 404; the SDK must never bind these |
| upgrade-firmware.md | `POST upload_fw`, `GET check_fw`, `GET install_fw` | DEFERRED-M10 | — |
| upgrade-ui.md | `POST upload_ui`, `GET check_ui`, `GET install_ui` | DEFERRED-M10 | — |
| upgrade-bootloader.md | `POST upload_bootldr`, `GET install_bl` (DIAG-only builds) | DEFERRED-M10 | — |
| upgrade-usbmode.md | `GET /api/system/upgrade/usbmode` | DEFERRED-M10 | — |

Field-level notes:

- **`POST /api/system/config` is bound for the `tls` sub-object only.**
  The SDK never sends the general config-write fields (`user`, `email`,
  `origin`, the option booleans, `storage_*`, `userkey*`, `gen_csr`) —
  and never sends **`wipeout`** (device factory reset). Disposition:
  UNBOUND-DELIBERATE — no consumer requirement (HEM-SDK-1..9) needs
  device administration; the TLS path exists only because fw v1.2.2
  cannot apply check-in certs itself (REQ-SYS-003/004/013). Recorded at
  this sweep; a general config/administration binding is an M10
  candidate if ever needed. POST-response `csr`/`genuine` unparsed
  (tolerant policy).
- **status `tts` — doc type is wrong, SDK right (fw-source-verified at
  this sweep):** firmware emits `tts` as a JSON boolean
  (`cJSON_AddBoolToObject`, api_system.c:92); the doc types it Number.
  Upstream doc-fix candidate.
- **status `repo_stats` — SDK DEFECT found by this sweep:** the status
  parser carries a dead `repo_stats` block (keys
  `fragmentation`/`freespace`) — firmware emits `repo_stats` ONLY from
  the selftest handler, and with keys `fragmented`/`freeslots`
  (api_system.c:328-330, DEV-365; status.md:56 agrees). The status-side
  public surface (`ehem_repo_stats`, `has_repo_stats`) can never
  populate on any real firmware; nothing consumes it (hem-tool prints
  repo stats from the selftest binding, which is correct). Fix proposed
  (user decision): remove the dead status-side surface pre-ABI-freeze.
- **version `blv` — SDK DEFECT found by this sweep:** firmware emits
  `blv`/`blk`/`bls` only when the bootloader footer's publisher matches
  (`if (bldr != NULL)`, api_system.c:215); the doc marks them
  conditional; the SDK hard-requires `blv` → a legal response fails
  with EHEM_ERR_PROTOCOL. Fix proposed (user decision, touches a
  REQ-SYS-002 criterion): demote `blv` to optional.
- checkin: request/response relayed verbatim across the 3 legs; the
  extra exposed fields (`newcrt_chain`, `current_serial`,
  `cert_updated`) derive from documented JWT claims (REQ-SYS-006); the
  fw-never-installs-`newcrt` divergence is recorded (REQ-SYS-003).
- Scope narrowing throughout (documented-or-stricter, deliberate):
  `system:config` for config/reboot/selftest/attestation (attestation
  and selftest docs say any-token; recorded rationale), `system:shutdown`
  for shutdown; config GET's alternate scopes (`logger:*`,
  `auth:ext:init`) unused.
- config GET deliberately unparsed: `eid_sign`, `spk`, `nonce`,
  `http_option_dosprot_mode` (recorded in system.h; tolerant policy).
- version `fwk`/`fws`: doc-Always but SDK-optional — tolerant
  direction, harmless.

## logger/ + storage/ — 6 pages: 5 bound, 1 absent-in-fw

| Page | Endpoint | Disposition | SDK symbol(s) |
|---|---|---|---|
| logger/key.md | `GET /api/logger/key` | BOUND | `ehem_logger_key` |
| logger/list.md | `GET /api/logger/list[/{offset}]` | BOUND | `ehem_logger_list` |
| logger/get.md | `GET /api/logger/{id}` | BOUND | `ehem_logger_get` |
| logger/delete.md | `DELETE /api/logger/{id}` | ABSENT-IN-FW | — handler exists but its dispatch is commented out (`/* not available in CC mode */`); every request 404s. Recorded in REQ-SYS-009 + ARCHITECTURE §11; source comment added at this sweep |
| storage/unlock.md | `GET /api/storage/unlock` | BOUND | `ehem_storage_unlock` |
| storage/lock.md | `GET /api/storage/lock` | BOUND | `ehem_storage_lock` |

Field-level notes:

- logger key/list/get: full field coverage; strict 32/32/64-byte
  decodes on key (deliberate); list always sends the explicit
  `/list/{offset}` form; get returns the body verbatim.
- **logger/get body format — doc wrong, recorded:** real fw v1.2.2 logs
  are a `# Encedo nGINE FW` header + pipe-delimited CRLF records, not
  the doc's "JSON-like record per line" (REQ-SYS-009; KNOWN-ISSUES).
- storage: scope-carries-arguments (`storage:disk<N>[:rw]`) matches the
  doc exactly; the `/ro`/`/rw` URL narrowing variants are deliberately
  not exposed (Manager-side override; recorded in proto_storage.c); the
  fw uninitialized-`sub` caution is recorded (REQ-SYS-010;
  KNOWN-ISSUES).
- Minor exposure note: `ehem_logger_page` has no `has_total` flag — an
  absent `total` reads as 0. Tolerable; recorded here, no change.

## concepts/device-options.md — field pass vs `ehem_config_info`

All nine documented options are exposed with present/absent flags
except `http_option_dosprot_mode` (deliberately not surfaced, recorded
in system.h — no SDK use case). The SDK exposes identity/config fields
beyond the page's list (`devid`, `eid`, `instanceid`, `genuine_id`,
`iat`, `uts`, `ctx`, `storage_capacity`) — the page's own "identity
fields like…" wording is non-exhaustive, so this is doc under-coverage,
not a conflict. `storage_mode` is exposed as the raw bitfield (masks
not decomposed). No documented option is writable through the SDK (see
the config POST note above).

## Cross-check: README.md + FIRMWARE_NOTES.md endpoint inventories

Every endpoint mentioned in the doc repo's README (52 distinct) and
FIRMWARE_NOTES maps to a page dispositioned above — no orphan
endpoints. FIRMWARE_NOTES additionally records Manager's
`POST /api/keymgmt/list` as aspirational (firmware implements GET
only — matches the SDK's GET binding). **`stream/*`: zero mentions
anywhere in the doc repo** — nothing to bind or defer at 1.0; the M10
"re-check against the then-current firmware" note stands unchanged.

## discrepancies/ — all 28 registry entries reconciled

The three registries (DISCREPANCIES-OFFICIAL-DOCS 11 entries,
DISCREPANCIES-ENCEDO-MANAGER 9, DISCREPANCIES-HEM-TEST 8) were each
mapped to where this project records the divergence:

- **22 already recorded or not applicable.** Recorded: storage
  suffix-vs-scope (REQ-SYS-010), eJWT construction (REQ-AUTH-001),
  ecdh-vs-derive distinction (REQ-OPS-004/KEY-009), cipher shapes
  reconstructed from the tester (REQ-OPS-006/009), empty-success bodies
  (REQ-KEY-004/007, REQ-SYS-004/010), logger-DELETE dead dispatch
  (REQ-SYS-009), exact `keymgmt:use:<kid>` scope truth (REQ-KEY-003 +
  the OPS REQs), KDF split PBKDF2-vs-Argon2 (REQ-AUTH-001,
  ARCHITECTURE §12 risk 2), type-string vocabulary (REQ-KEY-005),
  log-chain verification material (REQ-SYS-009). Not applicable:
  Manager-internal registry noise (scope-string drift, duplicate JS
  keys, missing UI for PQC/ecdh/shutdown/upload_bootldr — the SDK *is*
  the third-party consumer those entries anticipate), docs-site link
  rot, and the diag/upgrade surfaces dispositioned above.
- **3 gaps recorded at this sweep:**
  1. unauthenticated config-GET identity carve-out → REQ-SYS-004 rev 2
     (recorded as unverified, with the SDK's non-reliance explained);
  2. Manager's aspirational `POST api/keymgmt/list` (firmware is
     GET-only) → REQ-KEY-001 rev 2 (sweep hook);
  3. firmware validates only the JWT header's `ecdh` field (`alg`/`typ`
     are not gates) → REQ-AUTH-001 rev 2 + src/ejwt.h comment.

## Sweep conclusions (STEP-M9-010)

- **No must-bind gaps for 1.0.** Every documented endpoint is either
  bound with full field coverage, deliberately unbound with a recorded
  reason (auth/init, config-provisioning, diag/*, general config
  writes incl. wipeout, logger DELETE, storage /ro//rw variants,
  search's unauthenticated bypass), or deferred to M10 (the upgrade
  family). `stream/*` does not exist anywhere in the doc repo.
- **Two SDK defects found** (fix decisions pending, see the step
  record): the version parser hard-requires the firmware-conditional
  `blv`; the status parser carries a dead, wrong-keyed `repo_stats`
  surface.
- **Upstream doc-fix candidates collected here:** pubkey cap 66→67
  (all crypto pages + keymgmt/derive), status `tts` Number→bool,
  token.md's stray `cfg` 400 row, update.md's wrong omitted-`descr`
  semantics, plus the long-standing recorded divergences above.
