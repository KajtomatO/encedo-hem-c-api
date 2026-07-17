---
id: REQ-SYS-009
title: Bindings for /api/logger — audit-log signing key, file list, file download
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §11 (M7: logger group); encedo-hem-api-doc logger/key.md, logger/list.md, logger/get.md, logger/delete.md (DELETE not dispatched), discrepancies/DISCREPANCIES-HEM-TEST.md §"log chain verification"; encedo_firmware api_logger.c (fw v1.2.2 — api_get_logger_key outside the USB_MSC_AVAILABLE guard, list/get inside it); approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#2-context--constraints"]
---

# Bindings for /api/logger — audit-log signing key, file list, file download

The SDK SHALL provide the logger read family: `ehem_logger_key(ctx,
&out)` over `GET /api/logger/key`, `ehem_logger_list(ctx, offset, &out)`
over `GET /api/logger/list[/{offset}]`, and `ehem_logger_get(ctx, id,
&out)` over `GET /api/logger/{id}` returning the raw log file body.

- **key** (PPA **and** EPA): `{key, nonce, nonce_signed}` base64-decoded
  into fixed-size buffers — Ed25519 public key (32 B), nonce (32 B),
  signature (64 B); wrong decoded lengths → `EHEM_ERR_PROTOCOL`. This is
  `Session.EIDkeySign` — the key that signs every audit record; distinct
  from auth's Curve25519 `eid` (doc note).
- **list** (PPA only): `{total, id[]}` — one page of hex log-file IDs
  starting at `offset` (page size is firmware-internal; the caller
  advances by the returned count, python-client convention). Invalid
  offset → 406 → `EHEM_ERR_DEVICE`; listing failure → 404 →
  `EHEM_ERR_NOT_FOUND`.
- **get** (PPA only): raw `text/plain` body (Content-Length known,
  device streams in 1 KiB chunks) returned as caller-owned bytes via the
  raw request path — NOT parsed as JSON. 404 → `EHEM_ERR_NOT_FOUND`;
  406 (FR_LOCKED — file busy) → `EHEM_ERR_DEVICE` with detail. A short
  read vs the declared length (device I/O error mid-stream, doc note)
  maps to `EHEM_ERR_PROTOCOL`.
- Scope `logger:get` (prefix) for all three; `logger:del` /
  `DELETE /api/logger/{id}` is NOT bound — the dispatch is commented out
  in fw v1.2.2 (doc: every request 404s); recorded here, swept at M9 if
  firmware re-enables it.
- On an EPA device list/get are unrouted (404): the binding maps it as
  NOT_FOUND and the integration tests skip with a note.

**Rationale:** M7 milestone "logger group". The signed-nonce triple lets
a consumer verify the device holds the log-signing key and audit the
chain offline (tester lib.php implements full chain verification —
future consumer work, not SDK scope; the SDK delivers the material).

**Acceptance criteria:**
- [ ] Unit (fake transport): key triple base64-decoded with length
      enforcement (32/32/64) and PROTOCOL on deviation; list page
      parsed (total + ids, empty page OK); get returns raw bytes
      verbatim (binary-safe, not JSON-parsed) with length; error
      mapping per endpoint.
- [ ] Live: `ehem_logger_key` → nonce_signed verifies over nonce with
      the crypto shim's `ehem_ed25519_verify` against `key` (test-only
      shim use, static-lib link like test_auth_live).
- [ ] Live (PPA path): list page 0 → total ≥ 1 (the suite's own audit
      events exist), fetch the first id → non-empty text body whose
      first line looks like a log record; on an EPA device both skip
      with the 404 recorded.
- [ ] OPEN (live probe): dev-device PPA/EPA determination (list/get
      routed or 404) + observed page size and id format recorded here.
