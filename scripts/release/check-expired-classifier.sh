#!/usr/bin/env bash
#
# check-expired-classifier.sh — prove that a hem-tool binary classifies an
# EXPIRED peer certificate (REQ-NET-005) with whatever TLS backend its libcurl
# carries (REQ-BUILD-005: wolfSSL in the release build) — and does NOT so
# classify a certificate that is NOT YET valid (REQ-NET-005 rev 2: wolfSSL
# -150 is ASN_BEFORE_DATE_E, OpenSSL 9 X509_V_ERR_CERT_NOT_YET_VALID).
#
#   scripts/release/check-expired-classifier.sh <hem-tool-binary> [port]
#
# Mints a valid CA + an EXPIRED and a NOT-YET-VALID leaf it signed, serves each
# with `openssl s_server` (on <port> and <port>+1), and runs `hem-tool
# recovery` against both with the CA as the trust anchor (an expired cert
# cannot be its own anchor: wolfSSL refuses to load one). The command prints
# "diagnosis: the device certificate has EXPIRED" ONLY when the transport's
# expired-cert verdict fired (ehem_error.tls_expired); it then fails later (the
# fake server speaks no HEM API) — the exit code is not the point, the
# diagnosis line is. For the not-yet-valid leaf the line must be absent and the
# https probe must have failed on the certificate date.
set -euo pipefail

BIN="${1:?usage: check-expired-classifier.sh <hem-tool> [port]}"
PORT="${2:-8443}"
PORT2=$((PORT + 1))
DIR="$(mktemp -d)"
trap 'kill "${SRV:-}" "${SRV2:-}" 2>/dev/null || true; rm -rf "$DIR"' EXIT

python3 "$(dirname "${BASH_SOURCE[0]}")/make-expired-cert.py" "$DIR"
openssl s_server -quiet -www -accept "$PORT" \
  -cert "$DIR/expired-cert.pem" -key "$DIR/expired-key.pem" > "$DIR/server.log" 2>&1 &
SRV=$!
openssl s_server -quiet -www -accept "$PORT2" \
  -cert "$DIR/future-cert.pem" -key "$DIR/future-key.pem" > "$DIR/server2.log" 2>&1 &
SRV2=$!
sleep 1

# The relaxed check-in that follows the diagnosis must not reach the real
# cloud with the fake device's output: point the relay leg at the fake too
# (it fails there, which is fine — the diagnosis line has already printed).
export EHEM_CHECKIN_URL="https://localhost:$PORT/checkin"
set +e
OUT="$(EHEM_PASSPHRASE=x "$BIN" --url "https://localhost:$PORT" --cacert "$DIR/ca.pem" \
        recovery 2>&1)"
RC=$?
set -e
# shellcheck disable=SC2001  # indenting a multi-line value
echo "$OUT" | sed 's/^/  /'
if echo "$OUT" | grep -q "device certificate has EXPIRED"; then
  echo "classifier OK: expired certificate recognised (recovery exit $RC)"
else
  echo "classifier FAILED: the expired certificate was NOT recognised"
  exit 1
fi

# Not yet valid: refused by TLS on its date, but never "expired" — no check-in.
set +e
OUT2="$(EHEM_PASSPHRASE=x "$BIN" --url "https://localhost:$PORT2" --cacert "$DIR/ca.pem" \
         recovery 2>&1)"
RC2=$?
set -e
# shellcheck disable=SC2001  # indenting a multi-line value
echo "$OUT2" | sed 's/^/  /'
if echo "$OUT2" | grep -q "device certificate has EXPIRED"; then
  echo "classifier FAILED: a NOT-YET-VALID certificate was classified as expired"
  exit 1
fi
# The date wording per stack: OpenSSL "certificate is not yet valid";
# libcurl 8.10.1 + wolfSSL 5.7.2 "certificate not valid yet." (verify result 0,
# measured 2026-10-13); wolfSSL's own text "ASN date error, current date before".
if ! echo "$OUT2" | grep -E -q "https probe failed \(EHEM_ERR_NETWORK.*(not yet valid|not valid yet|current date before|-150)"; then
  echo "classifier check FAILED: the not-yet-valid certificate was not refused on its date"
  exit 1
fi
echo "classifier OK: not-yet-valid certificate refused by TLS, not classified as expired (recovery exit $RC2)"
