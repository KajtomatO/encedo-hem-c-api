---
id: STEP-M12-120
title: "Release matrix: -static hem-tool and -static SDK library package on OpenSSL (both platforms), no-wolfSSL assertions, classifier under OpenSSL-libcurl, release.yml matrix, README download table, RELEASING.md"
milestone: M12
implements: ["REQ-BUILD-009"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M12-090", "STEP-M12-100", "STEP-M12-110"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** the release workflow builds, checks and publishes
`hem-tool-<label>-<os>-x86_64-static` (one file, OpenSSL backend, every
third-party library static) and `encedo-hem-<label>-<os>-x86_64-static`
(static SDK + `libcurl.a`, `libssl.a`, `libcrypto.a`, config + `.pc`,
OpenSSL notice) for Linux and Windows beside the unchanged M10 assets;
the job asserts the link mode (Linux: `ldd` = glibc family only, no
RUNPATH; Windows: system DLLs only), **no wolfSSL symbol or string in any
`-static` asset**, runs the unit suite on the Release build, the
REQ-NET-005 classifier under the OpenSSL-backed libcurl
(`check-expired-classifier.sh` on the static binary), `hem-tool --version`
showing `OpenSSL <pinned>`, and the consumer smoke for the static library
package; README "Download" gets the asset table and "which one do I
want"; `docs/RELEASING.md` the new variants and the attended per-variant
smoke.

**Notes:** `package.sh` gains a variant argument (`dynamic` = today /
`static`): configure `EHEM_CRYPTO_BACKEND=openssl EHEM_DEPS_LINK=static
EHEM_TOOL_LINK=static` against `deps-openssl` (STEP-M12-110). Linux link:
third-party archives + `-static-libgcc`, glibc dynamic — no `-static`
(a static glibc breaks `getaddrinfo`/NSS; settled at the decomposition);
Windows: `-static` as today (`crypt32`, `ws2_32`, `bcrypt` … are system
DLLs). Trust store: Linux — OpenSSL's `OPENSSLDIR` default plus libcurl's
distro CA-bundle fallbacks as REQ-BUILD-005; Windows —
`CURLSSLOPT_NATIVE_CA` (the transport sets it on `_WIN32`; libcurl ≥ 7.71
honours it with OpenSSL). Assertions in `package.sh`: `ldd`/`readelf`,
`objdump -p`, plus `nm`/`strings` for `wolfSSL_` / `wc_` → must be absent
(the §1 rule as a test). `check-expired-classifier.sh` run on the static
binary closes REQ-BUILD-007's last criterion (OpenSSL verdict 10 /
"certificate has expired" — the original path). `package-lib.sh static`:
`libencedo-hem.a` + the three OpenSSL/curl archives + config/`.pc`
pointing at them + notices incl. OpenSSL `LICENSE.txt`; consumer smoke
with `pkg-config --libs --static` must link with no unresolved symbol
(REQ-BUILD-008 criterion 4). `release.yml`: the two jobs each build both
prefixes and all variants, or a 2×2 matrix — pick by runner time; the
publishing job keeps its shape (assemble, verify `SHA256SUMS`, duplicate
names, publish), asset count grows from 6 to 10 + SHA256SUMS (no OpenSSL
source tarball — Apache-2.0 needs none). `hem-tool --version` in the job
log must say `OpenSSL 3.5.x (compiled) / 3.5.x (loaded), static`. README
"Download": table of the five asset kinds + guidance (dynamic + companion
= default; `-static` = no companion, OpenSSL; library packages for
consumers); `docs/RELEASING.md`: variants, prefixes, attended smoke list
per variant. Prove via `workflow_dispatch` on the branch; the
`v1.2.0-rc<N>` tag path is STEP-M12-130's. Gotcha: never read link
results off the dev host (system wolfSSL masks); the MinGW printf rule
for anything new in the tool.

**Definition of done**
- [ ] `package.sh static` + `package-lib.sh static` produce the two
      `-static` assets on both platforms from `deps-openssl`; notices
      include OpenSSL's Apache-2.0 text.
- [ ] Job assertions green: Linux `ldd` glibc only + no RUNPATH; Windows
      import table system DLLs only; `nm`/`strings` show no `wolfSSL_`/`wc_`
      in any `-static` asset; unit suite on the Release OpenSSL build both
      platforms; `hem-tool --version` reports the pinned OpenSSL compiled
      and loaded, `static` (run id recorded in REQ-BUILD-009).
- [ ] `check-expired-classifier.sh` passes on the static Linux binary
      (REQ-BUILD-007 last criterion checked).
- [ ] Consumer smoke for the static library package (find_package +
      pkg-config `--static`) links and runs in the job (REQ-BUILD-008
      criterion 4 checked).
- [ ] `release.yml` builds every asset of the REQ-BUILD-009 table on a
      `workflow_dispatch` run; `SHA256SUMS` covers all; publishing job
      unchanged in shape and within the runner budget (times recorded).
- [ ] README "Download" asset table + guidance; `docs/RELEASING.md`
      variants, prefixes and attended per-variant smoke; `implements:
      REQ-BUILD-009` tags; `./dev ci` green.
