# Releasing the SDK packages

How to cut a release of the SDK library packages and the wolfSSL companion
asset. The rules behind it are REQ-BUILD-010 (`requirements/`); the
machinery is `.github/workflows/release.yml` and `scripts/release/`. What a
release contains and how users install it is in the README, section
"Download". hem-tool, the command-line client, is released from
[encedo-hem-tool](https://github.com/KajtomatO/encedo-hem-tool) since the
SDK's 1.2.1 (its `c-v<version>` tags; it builds on the plain package of an
SDK release) — until 1.2.0 the releases here carried it too.

## Variants

Each release job (Linux, Windows) builds two dependency prefixes (see
"Changing the pinned dependencies") and from them two variants, both through
`scripts/release/package.sh`:

| Variant | Command | Prefix | Assets |
|---|---|---|---|
| dynamic | `package.sh <deps> <os> dist` | `deps` (wolfSSL shared, libcurl static) | `encedo-hem-<label>-<os>-x86_64-shared` (the shared SDK; `package-lib.sh shared`), `wolfssl-<wv>-<os>-x86_64` (+ the wolfSSL source on Linux; `package-wolfssl.sh`) |
| static | `package.sh <deps-openssl> <os> dist static` | `deps-openssl` (OpenSSL + libcurl, both static) | `encedo-hem-<label>-<os>-x86_64` (the static SDK with its archives; `package-lib.sh static`) |

Both write into the same `dist/`, and each run rewrites `SHA256SUMS` over
every archive there: 4 Linux archives, 3 Windows archives and the checksum
file — 8 assets. The **plain name** means OpenSSL backend and every
third-party library inside (the default, self-contained package);
**`-shared`** means wolfSSL linked dynamically, the companion asset required
(names since 1.2.0, user decision 2026-10-19). No plain-named asset contains
wolfSSL, and `package-lib.sh` checks that by symbol; the static package's
OpenSSL reads no configuration on Windows and `/etc/ssl/openssl.cnf` on
Linux (REQ-BUILD-009 rev 4, carried by REQ-BUILD-010).

For each variant `package.sh` configures a Release build WITH the tests
against the prefix, runs the unit suite on it (the consumer gates included),
on Linux proves the REQ-NET-005 expired-certificate classifier against local
expired and not-yet-valid endpoints with `tests/release/tls-probe`
(`check-expired-classifier.sh`, both TLS backends), then packages — and
`package-lib.sh` builds and runs the sample consumer against the unpacked
package exactly as a user gets it (find_package + pkg-config).

## What triggers what

| Event | Result |
|---|---|
| push to `main` | Linux + Windows builds; the archives are **workflow artifacts** on the run's page, labelled `<version>+g<sha>`. No release. |
| Actions → Release → **Run workflow** on a branch | the same, for that branch. No release. |
| push of a tag `v<version>-rc<N>` | builds + a GitHub **pre-release** |
| push of a tag `v<version>` | builds + a GitHub **release** |

Choose a *branch* in "Run workflow": started on a tag, the run behaves like
a tag push and publishes.

A tag run cannot restore the dependency caches made on `development`
(GitHub scopes a cache to the branch that wrote it and to the default
branch), so it builds both prefixes from source. With cold prefixes the
jobs take about 3–4 minutes on Linux and 8–15 on Windows (`v1.2.1`: 3:23
and 13:24 plus 0:33 to publish, of which the prefixes were 2:17 and 10:04;
`v1.2.1-rc2`: 3:30 and 8:22; the main push for 1.2.1: 3:15 and 11:50;
`v1.2.0`: 2:44 and 10:26 with hem-tool still in the build); with warm
caches 1–2 and 2–3.

## Rules the workflow enforces

- **The tag must match the project version** — `project(encedo-hem
  VERSION x.y.z)` in `CMakeLists.txt`. `scripts/release/package.sh`
  accepts only `v<version>` or `v<version>-rc<N>` (N ≥ 1). Anything else
  fails both build jobs before the project is built (the dependency step
  runs first).
- **The tag must be annotated** (`git tag -a`). Its message becomes the
  release text, followed by the SHA256SUMS block
  (`scripts/release/release-notes.sh`). A lightweight tag fails the
  publishing job; the builds still run, but nothing is published. A signed
  tag's signature is stripped from the text.
- **Every file the workflow calls must be committed** in the tagged commit.
  v1.1.0-rc1 failed because `release-notes.sh` was still an untracked file.
- **Checksums are verified** after the artifacts are downloaded
  (`sha256sum -c`), and an asset name may not appear twice.

## Version number

Semantic versioning per REQ-API-008: a bug fix — or a change that touches
nothing under `include/`, such as 1.2.1's removal of hem-tool — is a patch,
new API is a minor, and an ABI break is a major (with a SONAME bump). Bump
`VERSION` in `CMakeLists.txt` on `development` before the first rc of the
new version. The SONAME follows the major only (1.x stays
`libencedo-hem.so.1`). hem-tool's version is encedo-hem-tool's own and
need not match.

## Where to tag

- **rc tags on `development`**, before the merge, to try a release
  candidate without touching `main`.
- **The final `v<version>` on `main`**, after the merge. The release is
  then exactly what `main` holds, the main-push build and the release build
  come from the same commit, and nothing can change in the pull request
  after tagging.

The workflow builds the tagged commit no matter which branch it is on, so
technically either works.

## Procedure

### 1. Release candidate

```sh
git switch development && git pull
git status                      # clean: nothing the workflow needs is untracked
git tag -a v1.2.1-rc1 -m "1.2.1-rc1 - <what this candidate is for>"
git push origin v1.2.1-rc1
```

Then on GitHub (Actions → Release): wait for the run to finish green, and
check the pre-release under Releases. From 1.2.1 it has eight assets (1.2.0:
twelve, with hem-tool; 1.1: six): per platform the static SDK package and
its `-shared` one and the wolfSSL archive; the wolfSSL source tarball;
`SHA256SUMS` (GitHub lists its own two source archives besides).

The packages are proven by the job itself — the unit suite on each Release
build, the link asserts, the sample consumer built and run against each
unpacked package, the classifier under both backends — so there is nothing
to attend beyond a look at the published set:

1. Download the archives, plus `SHA256SUMS`, and verify them:
   `sha256sum -c SHA256SUMS --ignore-missing` (Linux), or
   `CertUtil -hashfile <file> SHA256` compared by hand (Windows).
2. List each archive: libraries, headers, CMake config, `.pc`, `README.txt`,
   `LICENSES/` — and no program (REQ-BUILD-010: no `hem-tool` file in any
   asset).
3. Optionally build `examples/status.c` against an unpacked package
   (`-DCMAKE_PREFIX_PATH=<package>`, `find_package(encedo-hem)`) and run it
   against a device.

A broken candidate is fixed on `development` and replaced with the next rc
number. Delete the bad pre-release in the GitHub UI (Releases → the
release → Delete) and its tag if you want:

```sh
git push --delete origin v1.2.1-rc1
git tag -d v1.2.1-rc1
```

### 2. Final release

Before tagging:

- the milestone gate has passed (ARCHITECTURE.md §11), and an rc of the
  same code was tested;
- `CMakeLists.txt` carries the version you are about to tag;
- **the user-facing docs describe this version** — they are what the
  repository page shows once `main` moves, and the tag freezes them:
  - `README.md`: the Status block (version, date, scope, the next
    milestone), new APIs in their sections, the Documentation list and the
    Layout;
  - `docs/COVERAGE.md`: every endpoint the release binds marked BOUND
    with its symbols (`docs/API-GUIDE.md` is kept complete by a CTest
    gate, the other two are not);
  - `KNOWN-ISSUES.md`: anything users of the packages will hit;
- CI is green on `development`.

The release text (the tag message) is written last, from the README
Status block and the milestone's ARCHITECTURE.md §11 entry.

Then:

1. Open the pull request `development` → `main`, let CI pass, merge it.
   The push to `main` also runs the Release workflow and produces the
   artifacts.
2. Tag the merge commit on `main` and push the tag:

   ```sh
   git switch main && git pull
   git tag -a v1.2.1 --cleanup=whitespace -F release-notes-1.2.1.md   # or: -m "..."
   git push origin v1.2.1
   ```

   The file is Markdown: GitHub renders it, and the workflow appends the
   checksums. Keep `--cleanup=whitespace`. Without it, git treats every
   line that starts with `#` as a comment and drops it, so Markdown
   headings silently disappear from the release text. The first line is
   the tag's subject and opens the release text. `git cat-file -p v1.2.1`
   shows exactly what was stored.
3. Check the run and the release as in step 1.

Never move or reuse a published `v<version>` tag. If a release is wrong,
fix it and publish the next patch version.

After an SDK release, encedo-hem-tool bumps its pin (`cmake/sdk-version.cmake`
+ the `c/sdk` submodule) and releases its own `c-v<version>` against the new
plain package — its procedure, in its `c/docs/RELEASING.md`.

## Changing the pinned dependencies

wolfSSL, OpenSSL and libcurl are built from pinned source tarballs. Their
versions and SHA-256 hashes are at the top of `scripts/release/build-deps.sh`,
which builds two prefixes in each release job:

| Prefix | Command | Contents | Used by |
|---|---|---|---|
| `deps` | `build-deps.sh deps` | wolfSSL (shared) + libcurl on wolfSSL (static) | the `-shared` SDK package and the wolfSSL companion asset (REQ-BUILD-010) |
| `deps-openssl` | `build-deps.sh deps-openssl openssl` | OpenSSL 3.5 LTS + libcurl on OpenSSL, both static | the plain-named static SDK package (REQ-BUILD-010) |

To bump one:

1. Update the version and its sha256. Compute the hash from the official
   download and check it against the upstream announcement. OpenSSL's
   release text carries no hash: verify the tarball's `.asc` signature
   against the keys at openssl-library.org/source (`pubkeys.asc`), in a
   throwaway keyring.
2. Read the license again: wolfSSL's `COPYING` / `LICENSING` (the release
   states "GPLv2 or later" from the 5.7.2 files); OpenSSL's `LICENSE.txt`
   (Apache-2.0 for 3.5.9 — the prefix keeps it as `LICENSE.openssl` for
   the notices); curl's `COPYING`. OpenSSL stays on the 3.5 LTS line: 3.0
   LTS ended on 2026-09-07.
3. Keep OpenSSL's build choices across a bump (REQ-BUILD-009 rev 4, carried
   by REQ-BUILD-010): `no-shared no-module no-tests`; on Windows
   `no-autoload-config` and libcurl's
   `CURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG` (no configuration is read); and
   the compiled-in directories passed as make variables. On Linux these
   are `/etc/ssl` plus `/usr/lib/{engines-3,ossl-modules}`; on Windows,
   under the administrator-only `C:/Program Files/Common Files/SSL`. They
   never name the build prefix, which on the runner is a path that a user
   could create on another machine. `build-deps.sh` checks all of them.
4. Push. Each prefix's cache is keyed on the script's hash, so the next run
   rebuilds both from scratch — any edit of the script, a comment included,
   costs one cold run. The companion asset's name follows the wolfSSL
   version (`wolfssl-<version>-...`); if the SONAME changes, the package
   README texts follow it automatically.

No published package may contain wolfSSL statically linked (ARCHITECTURE.md
§1, user decision 2026-10-09): wolfSSL is always the separate download, with
its source. No static library ships with wolfSSL either: the `-shared` SDK
package carries the shared library only (user decision 2026-10-14) — static
linking is the plain-named OpenSSL package.
