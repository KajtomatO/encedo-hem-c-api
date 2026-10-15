# Releasing hem-tool

How to cut a release of `hem-tool` and its wolfSSL companion asset. The
rules behind it are REQ-BUILD-005 (`requirements/`); the machinery is
`.github/workflows/release.yml` and `scripts/release/`. What a release
contains and how users install it is in the README, section "Download
hem-tool".

## What triggers what

| Event | Result |
|---|---|
| push to `main` | Linux + Windows builds; the archives are **workflow artifacts** on the run's page, labelled `<version>+g<sha>`. No release. |
| Actions → Release → **Run workflow** on a branch | the same, for that branch. No release. |
| push of a tag `v<version>-rc<N>` | builds + a GitHub **pre-release** |
| push of a tag `v<version>` | builds + a GitHub **release** |

Choose a *branch* in "Run workflow": started on a tag, the run behaves like
a tag push and publishes.

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

Semantic versioning per REQ-API-008: a bug fix is a patch, new API or a
new hem-tool feature is a minor, and an ABI break is a major (with a
SONAME bump). Bump `VERSION` in `CMakeLists.txt` on `development` before
the first rc of the new version. The SONAME follows the major only (1.x
stays `libencedo-hem.so.1`).

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
git tag -a v1.1.0-rc5 -m "1.1.0-rc5 - <what this candidate is for>"
git push origin v1.1.0-rc5
```

Then on GitHub (Actions → Release): wait for the run to finish green, and
check the pre-release under Releases. It should have six assets: two
hem-tool archives, two wolfSSL archives, the wolfSSL source tarball and
`SHA256SUMS`.

Test the candidate on both platforms, as a user would:

1. Download the hem-tool archive and the matching wolfSSL asset, plus
   `SHA256SUMS`.
2. Verify them: `sha256sum -c SHA256SUMS --ignore-missing` (Linux), or
   `CertUtil -hashfile <file> SHA256` compared by hand (Windows).
3. Unpack both and **put the wolfSSL library next to the binary**
   (`libwolfssl.so.42` / `libwolfssl.dll`). On Linux, hem-tool otherwise
   falls back to the system's `libwolfssl.so.42`, which crashes it unless
   that library was built from the same version and configuration.
4. Run `hem-tool status` against the device with system trust (no
   `--cacert` / `--insecure`), then whatever the milestone needs.

A broken candidate is fixed on `development` and replaced with the next rc
number. Delete the bad pre-release in the GitHub UI (Releases → the
release → Delete) and its tag if you want:

```sh
git push --delete origin v1.1.0-rc5
git tag -d v1.1.0-rc5
```

### 2. Final release

Before tagging:

- the milestone gate has passed (ARCHITECTURE.md §11), and an rc of the
  same code was tested;
- `CMakeLists.txt` carries the version you are about to tag;
- **the user-facing docs describe this version** — they are what the
  repository page shows once `main` moves, and the tag freezes them:
  - `README.md`: the Status block (version, date, scope, the next
    milestone), new commands and APIs in their sections (hem-tool
    examples, test labels), the Documentation list and the Layout;
  - `docs/COVERAGE.md`: every endpoint the release binds marked BOUND
    with its symbols (`docs/API-GUIDE.md` is kept complete by a CTest
    gate, the other two are not);
  - `KNOWN-ISSUES.md`: anything users of the binaries will hit;
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
   git tag -a v1.1.0 --cleanup=whitespace -F release-notes-1.1.0.md   # or: -m "..."
   git push origin v1.1.0
   ```

   The file is Markdown: GitHub renders it, and the workflow appends the
   checksums. Keep `--cleanup=whitespace`. Without it, git treats every
   line that starts with `#` as a comment and drops it, so Markdown
   headings silently disappear from the release text. The first line is
   the tag's subject and opens the release text. `git cat-file -p v1.1.0`
   shows exactly what was stored.
3. Check the run and the release as in step 1, and smoke-test the
   published assets once more.

Never move or reuse a published `v<version>` tag. If a release is wrong,
fix it and publish the next patch version.

## Changing the pinned dependencies

wolfSSL, OpenSSL and libcurl are built from pinned source tarballs. Their
versions and SHA-256 hashes are at the top of `scripts/release/build-deps.sh`,
which builds two prefixes in each release job:

| Prefix | Command | Contents | Used by |
|---|---|---|---|
| `deps` | `build-deps.sh deps` | wolfSSL (shared) + libcurl on wolfSSL (static) | hem-tool, the wolfSSL companion asset, the SDK library package (REQ-BUILD-005, REQ-BUILD-009) |
| `deps-openssl` | `build-deps.sh deps-openssl openssl` | OpenSSL 3.5 LTS + libcurl on OpenSSL, both static | the `-static` variants (REQ-BUILD-009) |

To bump one:

1. Update the version and its sha256. Compute the hash from the official
   download and check it against the upstream announcement. OpenSSL's
   release text carries no hash: verify the tarball's `.asc` signature
   against the keys at openssl-library.org/source (`pubkeys.asc`), in a
   throwaway keyring.
2. Read the license again: wolfSSL's `COPYING` / `LICENSING` (the release
   states "GPLv2 or later" from the 5.7.2 files, REQ-BUILD-005); OpenSSL's
   `LICENSE.txt` (Apache-2.0 for 3.5.9 — the prefix keeps it as
   `LICENSE.openssl` for the notices); curl's `COPYING`. OpenSSL stays on
   the 3.5 LTS line (REQ-BUILD-009): 3.0 LTS ended on 2026-09-07.
3. Push. Each prefix's cache is keyed on the script's hash, so the next run
   rebuilds both from scratch. The companion asset's name follows the
   wolfSSL version (`wolfssl-<version>-...`). If the SONAME changes, the
   hem-tool README text follows it automatically.

No published package may contain wolfSSL statically linked (ARCHITECTURE.md
§1, user decision 2026-10-09); `package.sh` asserts that in every build. No
static library ships with wolfSSL either: the SDK library package carries
the shared library only (user decision 2026-10-14) — static linking is the
OpenSSL `-static` package.
