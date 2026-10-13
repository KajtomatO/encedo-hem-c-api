---
id: STEP-M12-110
title: "Release: SDK library packages, wolfSSL flavour (encedo-hem-<label>-<os>-x86_64) on both platforms + the deps-openssl prefix (OpenSSL 3.5 LTS pin) in build-deps.sh"
milestone: M12
implements: ["REQ-BUILD-009"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M12-070"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** the release workflow produces `encedo-hem-<label>-linux-x86_64.tar.gz`
and `encedo-hem-<label>-windows-x86_64.zip` — headers, `libencedo-hem`
shared (+ import lib on Windows) and static, the pinned static
`libcurl.a` the static library needs, the CMake package config,
`encedo-hem.pc`, notices, README — running and linking against the
unchanged wolfSSL companion asset; the job builds the REQ-BUILD-008
sample consumer against the unpacked package (find_package + pkg-config)
and runs it; `SHA256SUMS` covers the new assets. In the same step
`build-deps.sh` gains the second prefix `deps-openssl` (pinned OpenSSL
3.5.x + libcurl with OpenSSL as TLS backend) so STEP-M12-120 can start
from a cached, proven prefix.

**Notes:** New `scripts/release/package-lib.sh` next to `package.sh` /
`package-wolfssl.sh`: `cmake --install` of the Release build `package.sh`
already makes (default options against the deps prefix: shared SDK links
`libwolfssl.so.<N>` dynamically — RUNPATH `$ORIGIN` so the companion
library beside it is found — and `libcurl.a` statically) into a staging
tree; add `libcurl.a` (+ its `.pc` as shipped by the pinned build) so the
*static* SDK's consumers can link; notices: MIT, curl, cJSON, BIP39
wordlist — no wolfSSL text (not distributed in it). Settle at the step
how the package's CMake config / `.pc` locate wolfSSL for a consumer of
the static SDK: the companion asset carries only the library + COPYING +
BUILD-CONFIG + README (no headers, no `.pc`) — options: ship a stub
`wolfssl.pc` in the package pointing at the companion's directory (a
text file, no wolfSSL code), or a `find_library` fallback in the config
when pkg-config has no `wolfssl`; the shared-SDK consumer needs neither
(the `.so`'s own NEEDED + RUNPATH). OPEN criterion of the REQ: whether
the Windows companion asset must gain `libwolfssl.dll.a` for a consumer
of the static SDK, or Windows consumers are told to use the shared SDK
(record the decision, rev bump) — the dynamic-link rule of §1 is not
touched either way. Consumer smoke in both jobs: unpack the package +
the companion asset side by side, build `tests/consumer` via
`find_package` and `pkg_check_modules`, run it (`ldd`/`objdump` of the
result: the companion's wolfSSL and nothing else non-system for the
static-SDK variant). `deps-openssl` prefix: OpenSSL 3.5.x (exact version
+ sha256 from the official download, checked against the announcement;
`LICENSE.txt` Apache-2.0 captured for the notices; `no-shared no-tests`
minimal config, `--prefix`/`--openssldir` for the Linux trust store) and
libcurl 8.10.1 built again `--with-openssl`, HTTP/HTTPS only as today;
CI dependency caches keyed per prefix; `docs/RELEASING.md` "Changing the
pinned dependencies" covers both prefixes. Keep the free-runner budget
in view (OpenSSL build ~5 min cold, cached afterwards). Run through
`workflow_dispatch` on the branch; nothing goes to `main` from here.
Gotcha (memory): never conclude link behaviour from the dev host — its
system `libwolfssl.so.42` masks clean-runner failures; the job log is
the proof.

**Definition of done**
- [ ] `package-lib.sh` produces the wolfSSL-flavour library package on
      Linux and Windows with the REQ-BUILD-009 contents; notices without
      wolfSSL text; README states the companion-asset requirement.
- [ ] Consumer smoke in both jobs: find_package + pkg-config, shared and
      static, against the unpacked package + companion asset; the
      static-SDK consumer links the companion wolfSSL and nothing else
      non-system (job log, run id recorded).
- [ ] `SHA256SUMS` lists the new assets; the publishing job's
      duplicate-name check and re-verification cover them (dispatch run).
- [ ] `build-deps.sh` builds `deps-openssl` (OpenSSL 3.5.x pin + sha256 +
      libcurl with OpenSSL) on both platforms; LICENSE.txt read and the
      pin recorded in REQ-BUILD-009 (its OPEN pin criterion); CI cache
      keyed per prefix.
- [ ] REQ-BUILD-009 OPEN criterion (Windows `libwolfssl.dll.a`) decided
      and recorded (rev bump); `docs/RELEASING.md` pins section updated;
      `implements: REQ-BUILD-009` tags in package-lib.sh / build-deps.sh /
      release.yml.
- [ ] REQ-BUILD-005 outputs byte-for-byte the same kind (same scripts,
      same assertions — the dynamic hem-tool archive, companion assets,
      source tarball unchanged; checked in the dispatch run).
