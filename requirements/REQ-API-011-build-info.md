---
id: REQ-API-011
title: Build information API — crypto and transport backend names and versions (compiled against vs loaded)
status: verified
priority: should
revision: 3   # 2026-10-14 (STEP-M12-090 done): the both-backends criterion ticked (CI run 38482938701). No meaning change. Rev 2 = 2026-10-13 (STEP-M12-020): shape settled (link-mode fields + a fixed-size reserved tail for 1.x growth); unit criteria ticked
source: user decision 2026-10-12 (M12 planning: `hem-tool --version` shows the SDK version, the wolfSSL version compiled against and the one loaded, and the libcurl version); ARCHITECTURE.md §11 M12 part A; §3 (hem-tool uses the public API only — it cannot include wolfSSL or libcurl headers itself); approved 2026-10-12 (M12 decomposition, user: "approve")
depends_on: ["REQ-API-006", "REQ-API-008"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#11-milestones"]
---

# Build information API

The SDK SHALL expose, through a public function that cannot fail and
needs no initialised backend, the identity and versions of the libraries
it runs on: the crypto backend's name (`"wolfSSL"` / `"OpenSSL"`), the
version it was compiled against and the version loaded at run time, and
the same pair for the transport backend (`"libcurl"`).

Proposed shape (settled at the step; names are the user's call):

```c
typedef struct ehem_build_info {
    const char *crypto_backend;           /* "wolfSSL" | "OpenSSL" */
    const char *crypto_version_compiled;  /* e.g. "5.7.2" */
    const char *crypto_version_loaded;    /* the run-time library's own report */
    const char *transport_backend;        /* "libcurl" */
    const char *transport_version_compiled;
    const char *transport_version_loaded;
} ehem_build_info;

EHEM_API void ehem_build_info_get(ehem_build_info *out);
```

- The strings are static, library-owned (never freed by the caller);
  the struct is the caller's, so the function keeps REQ-API-002 (no
  file-scope mutable state). A NULL `out` is a no-op.
- "Loaded" comes from the library itself (`wolfSSL_lib_version()`,
  `OpenSSL_version(OPENSSL_VERSION)`, `curl_version_info()`); "compiled"
  from its headers (`LIBWOLFSSL_VERSION_STRING`, `OPENSSL_VERSION_STR`,
  `LIBCURL_VERSION`). Both are available before `ehem_global_init()` and
  after a refused one (REQ-API-010) — that is the point: the refusal
  message needs them.
- `ehem_version()` (the SDK's own version) is unchanged.

**Shape settled at STEP-M12-020 (2026-10-13):** the proposed names are
kept and two fields are added — `crypto_link` and `transport_link`
(`"dynamic"` / `"static"`, the build's resolved link mode of each
dependency, which REQ-TOOL-025 prints and the tool cannot learn any other
way) — followed by `const char *reserved[8]` (NULL today). The struct is
the one caller-allocated OUTPUT struct of the API, so the `abi_size`
discipline of `ehem_options` does not fit it (nothing would stamp the
caller's size before the call): instead its size is FIXED for the whole
1.x line and a future fact takes a reserved slot — a binary built against
any 1.x header can call any 1.x library. Recorded in the ABI promise at
`ehem_version()` in ehem.h. The link modes are a configure-time fact
(top-level CMakeLists.txt `ehem_link_mode`: an archive `.a`/`.lib` that is
not a MinGW `.dll.a` import library = static), passed to the SDK as
`EHEM_CRYPTO_LINK_STATIC` / `EHEM_TRANSPORT_LINK_STATIC`; STEP-M12-060's
link switches refine how the dependencies resolve, not this mechanism.

**Rationale:** hem-tool must print what it was built against and what it
loaded (user decision 2026-10-12) and must explain a REQ-API-010 refusal;
it uses the public API only (ARCHITECTURE §3, §8), so the SDK has to hand
the facts over. A consumer such as encedo-pkcs11 gets the same
diagnostics for its own logs.

**Acceptance criteria:**
- [x] Declared in `include/ehem/ehem.h`, exported (an addition to the
      1.0 baseline — minor bump, REQ-API-008), indexed in
      docs/API-GUIDE.md (docs gate), `ehem_version()` untouched.
      *(2026-10-13, STEP-M12-020: `src/ehem.c` asks each backend to
      describe itself — `ehem_crypto_backend_describe` in the shim,
      `ehem_transport_backend_describe` in `transport_curl.c`; the export
      gate passes with `ehem_build_info_get` as the only new symbol, the
      describe functions stay hidden; API-GUIDE `<ehem/ehem.h>` row.)*
- [x] Unit: every string is non-NULL; `crypto_backend` names the backend
      the build was configured with; `crypto_version_compiled` equals the
      header constant of that backend; the transport pair reads
      `"libcurl"` + the libcurl header/run-time versions; calling before
      `ehem_global_init()`, after it, and after a forced REQ-API-010
      refusal all return the same values.
      *(`tests/unit/test_build_info.c`. The expected compiled versions and
      link modes come from the build's own discovery — pkg-config's
      `wolfssl` version, FindCURL's version, the configure-time link
      detection — an oracle independent of the headers, so no test includes
      a backend header; the dev host: wolfSSL 5.6.6 / libcurl 8.5.0, both
      dynamic. Also: loaded == compiled for the crypto library whenever init
      passes, reserved slots NULL, NULL `out` a no-op. `./dev ci` 51/51 gcc
      + clang, ASan 51/51.)*
- [x] Both backends (REQ-BUILD-007 CI legs) fill the struct.
      *(test_build_info checks the struct against CMake's own record of
      the backend — name, compiled version, link mode: green in the
      wolfSSL and the OpenSSL legs of CI run 38482938701 (Linux gcc + clang,
      Windows MinGW), 2026-10-13.)*
