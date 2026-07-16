---
id: REQ-AUTH-002
title: Scope-keyed token cache with silent refresh; logout and credential retention
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §5 (token cache, HEM-AUTH-6/7 upstream); encedo-hem-python-api auth.py (cache semantics)
depends_on: ["REQ-AUTH-001", "REQ-API-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session"]
---

# Scope-keyed token cache with silent refresh; logout and credential retention

The context SHALL cache bearer tokens keyed by scope string. A cached token
is reused while valid; expiry uses the token's own `exp` claim when its
payload is readable (the device may shorten lifetimes for some scopes), else
the requested lifetime (3600 s), minus a 60 s safety skew. On a cache miss
or expiry the SDK silently re-acquires a token via the REQ-AUTH-001 flow —
possible only while credential material is retained.

Since STEP-M2-045 the requested lifetime is no longer capped at the ~60 s
challenge deadline, so the device issues bearers with the full lifetime
(~3600 s live-proven) and this cache genuinely holds across many calls
(TTL 3600 s ≫ 60 s skew) instead of re-logging-in almost every request.

**Credential retention:** `ehem_login` retains the passphrase inside the
context (zeroized copy) by default so silent refresh works for the
context's lifetime. An `ehem_options` opt-out (`no_credential_retention`)
makes the SDK keep only the current tokens: refresh after expiry then fails
with `EHEM_ERR_AUTH_EXPIRED` and the caller must call `ehem_login` again.

`ehem_logout(ctx)` SHALL zeroize retained credential material and drop the
entire token cache. `ehem_ctx_destroy` implies logout (REQ-API-001
zeroization).

**Acceptance criteria:**
- [ ] Two requests needing the same scope perform one token acquisition
      (fake-transport unit test asserting request counts).
- [ ] Requests needing different scopes acquire and cache independently
      (unit test).
- [ ] A token whose `exp` is within the skew window is re-acquired before
      use; the cache honors a shorter device-issued `exp` over the
      requested lifetime (unit tests with crafted token payloads).
- [ ] With retention disabled, post-expiry requests fail
      `EHEM_ERR_AUTH_EXPIRED` without a network re-login attempt (unit test).
- [ ] `ehem_logout` drops the cache and zeroizes credentials; subsequent
      authenticated calls fail `EHEM_ERR_AUTH_FAILED` (or `AUTH_EXPIRED`)
      without sending Authorization headers (unit test); ASan/LSan clean.
