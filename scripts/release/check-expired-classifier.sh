#!/usr/bin/env bash
#
# check-expired-classifier.sh — prove that a hem-tool binary classifies an
# EXPIRED peer certificate (REQ-NET-005) with whatever TLS backend its libcurl
# carries (REQ-BUILD-005: wolfSSL in the release build).
#
#   scripts/release/check-expired-classifier.sh <hem-tool-binary> [port]
#
# Mints a valid CA + an EXPIRED leaf it signed, serves the leaf with
# `openssl s_server`, and runs `hem-tool recovery` against it with the CA as
# the trust anchor (an expired cert cannot be its own anchor: wolfSSL refuses
# to load one). The command prints "diagnosis: the device certificate has
# EXPIRED" ONLY when the transport's expired-cert verdict fired
# (ehem_error.tls_expired); it then fails later (the fake server speaks no HEM
# API) — the exit code is not the point, the diagnosis line is.
set -euo pipefail

BIN="${1:?usage: check-expired-classifier.sh <hem-tool> [port]}"
PORT="${2:-8443}"
DIR="$(mktemp -d)"
trap 'kill "${SRV:-}" 2>/dev/null || true; rm -rf "$DIR"' EXIT

python3 "$(dirname "${BASH_SOURCE[0]}")/make-expired-cert.py" "$DIR"
openssl s_server -quiet -www -accept "$PORT" \
  -cert "$DIR/expired-cert.pem" -key "$DIR/expired-key.pem" > "$DIR/server.log" 2>&1 &
SRV=$!
sleep 1

# The relaxed check-in that follows the diagnosis must not reach the real
# cloud with the fake device's output: point the relay leg at the fake too
# (it fails there, which is fine — the diagnosis line has already printed).
export EHEM_CHECKIN_URL="https://localhost:$PORT/checkin"
set +e
OUT="$("$BIN" --url "https://localhost:$PORT" --cacert "$DIR/ca.pem" \
        --passphrase x recovery 2>&1)"
RC=$?
set -e
echo "$OUT" | sed 's/^/  /'
if echo "$OUT" | grep -q "device certificate has EXPIRED"; then
  echo "classifier OK: expired certificate recognised (recovery exit $RC)"
else
  echo "classifier FAILED: the expired certificate was NOT recognised"
  exit 1
fi
