#!/usr/bin/env bash
#
# check-expired-classifier.sh — prove, against real TLS handshakes, that the SDK
# classifies an EXPIRED peer certificate (REQ-NET-005) with whatever TLS backend
# its libcurl carries (the release builds: wolfSSL in the -shared prefix,
# OpenSSL in the plain one — REQ-BUILD-010) — and does NOT so classify a
# certificate that is NOT YET valid (REQ-NET-005 rev 2: wolfSSL -150 is
# ASN_BEFORE_DATE_E, OpenSSL 9 X509_V_ERR_CERT_NOT_YET_VALID).
#
# implements: REQ-BUILD-010 (the per-build classifier check)
#
#   scripts/release/check-expired-classifier.sh <tls-probe> [port]
#
# <tls-probe> is the build's tests/release/tls-probe (public API only; until
# M13 the driver was `hem-tool recovery`, now encedo-hem-tool's). Mints a valid
# CA + an EXPIRED and a NOT-YET-VALID leaf it signed (make-expired-cert.py),
# serves each with `openssl s_server` (on <port> and <port>+1) and runs the
# probe against both with the CA as the trust anchor (an expired cert cannot be
# its own anchor: wolfSSL refuses to load one). The probe prints one line,
# `rc=... tls_expired=... detail=...`: tls_expired=1 is the transport's
# expired-cert verdict (ehem_error.tls_expired) and must appear for the first
# leaf only; for the second the detail must show the refusal on the date. The
# probe runs with the automatic check-in OFF, so nothing but the two handshakes
# happens — no cloud, no relay.
set -euo pipefail

BIN="${1:?usage: check-expired-classifier.sh <tls-probe> [port]}"
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

# Expired: the one classifiable, check-in-recoverable failure.
OUT="$("$BIN" "https://localhost:$PORT" "$DIR/ca.pem")"
echo "  expired leaf:       $OUT"
case "$OUT" in
  *" tls_expired=1 "*) echo "classifier OK: expired certificate recognised" ;;
  *) echo "classifier FAILED: the expired certificate was NOT recognised"; exit 1 ;;
esac

# Not yet valid: refused by TLS on its date, but never "expired".
OUT2="$("$BIN" "https://localhost:$PORT2" "$DIR/ca.pem")"
echo "  not-yet-valid leaf: $OUT2"
case "$OUT2" in
  *" tls_expired=1 "*)
    echo "classifier FAILED: a NOT-YET-VALID certificate was classified as expired"; exit 1 ;;
esac
# The date wording per stack: OpenSSL "certificate is not yet valid";
# libcurl 8.10.1 + wolfSSL 5.7.2 "certificate not valid yet." (verify result 0,
# measured 2026-10-13); wolfSSL's own text "ASN date error, current date before".
if ! echo "$OUT2" | grep -E -q "^rc=EHEM_ERR_NETWORK tls_expired=0 detail=.*(not yet valid|not valid yet|current date before|-150)"; then
  echo "classifier check FAILED: the not-yet-valid certificate was not refused on its date"
  exit 1
fi
echo "classifier OK: not-yet-valid certificate refused by TLS, not classified as expired"
