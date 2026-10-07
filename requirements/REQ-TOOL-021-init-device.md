---
id: REQ-TOOL-021
title: hem-tool init-device — personalise an uninitialised device
status: draft
priority: should
revision: 1
source: user decision 2026-10-07 (M10: "add both init-device and wipe-device to the tool"; attended-only); REQ-AUTH-011 (the binding); Encedo Manager assets/build.js initFinal (the authoritative client flow); encedo-hem-api-doc auth/init.md
depends_on: ["REQ-AUTH-011", "REQ-SYS-003", "REQ-TEST-007"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool init-device — personalise an uninitialised device

`hem-tool init-device` SHALL initialise an uninitialised device through
`ehem_device_init` (REQ-AUTH-011): it runs a check-in first (the init
endpoints demand a set RTC — 403 otherwise), fetches the init challenge,
derives the user key from the given passphrase, signs the init JWT with
the master key, posts it, and prints what the device returned.

- **Inputs:** the user passphrase via the standard `--passphrase` /
  `EHEM_PASSPHRASE` sources (it becomes the device's user password);
  the master secret via `--master-secret-hex HEX` (32 bytes) — or, when
  absent, `--master-generate`, which creates 32 random bytes from the
  OS RNG and prints them **once** as hex with a keep-this warning (the
  device offers no way to rotate `masterkey` later; a Manager-style
  BIP39 mnemonic is NOT produced — see the open criterion); the `cfg`
  fields as flags: `--user`, `--email`, `--hostname`, `--ip A.B.C.D/N`
  (required); `--storage-mode N`, `--disk0-size N`, `--dnsd`,
  `--no-trusted-ts`, `--no-trusted-backend`, `--no-allow-keysearch`,
  `--origin`, `--gen-csr`, `--ctx N` with the Manager's defaults
  (`dnsd` false, `origin` "*", `ctx` 0, the three trust booleans true,
  `gen_csr` false).
- **URL:** a wiped device has no HTTPS; the command is used as
  `hem-tool --url http://<host> init-device …`, exactly like
  `tls-recover`.
- **Output:** `instanceid`, `reboot_required`, the PEM `csr` when
  `--gen-csr` was given (also written to `--csr-out FILE` if asked), and
  the next steps: reboot when required (`--reboot` does it and waits),
  then `recovery` / `tls-recover` to restore HTTPS. The returned bearer
  (`sub="U"`, scope `system:config`) is not printed.
- **Exit codes:** 0 = initialised; 1 = device/login failure; 2 =
  usage/environment (missing passphrase/master secret/required cfg);
  3 = device already initialised (406); 4 = RTC still unset after the
  check-in (403); 5 = `cfg` rejected (400 — a field failed validation).
- **Auth class:** none — the init endpoints take no bearer; the
  passphrase is input data, not a credential, and the help says so.

**Rationale:** the SDK should be able to initialise a wiped device (user
decision 2026-08-07) and the tool is the living usage documentation;
without this command the only init path is the Manager's web UI.

**Acceptance criteria:**
- [ ] OPEN, user decision: master-secret format. The Manager generates a
      24-word BIP39 mnemonic and derives the master key from its seed
      (`build.js:691-702`); matching that needs a vendored English
      wordlist + PBKDF2-HMAC-SHA512 seed derivation (tool-only). The
      draft above takes/produces raw 32-byte hex instead, which the
      Manager's master-password UI cannot consume. Decide: raw hex (as
      drafted) or BIP39-compatible.
- [ ] Unit (hem-tool-core, fake transport): the full sequence (check-in
      legs → GET init → POST init) with the Manager-default `cfg`;
      required-flag omissions → exit 2 with zero traffic; 406 → exit 3;
      403 after check-in → exit 4; 400 → exit 5; the master secret never
      appears in output except the one `--master-generate` print.
- [ ] **Attended-only** (REQ-TEST-007): no live CTest. Evidence to record
      here: date, device, the init result (instanceid, reboot_required),
      and that a subsequent `hem-tool status` + passphrase login worked.
- [ ] `--help` documents every cfg flag with its default and the
      irreversibility of the master secret.
