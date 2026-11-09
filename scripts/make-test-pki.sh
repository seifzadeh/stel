#!/usr/bin/env bash
set -euo pipefail
STEL_PROJECT_DIR="$(cd -- "$(dirname -- "$0")/.." && pwd)"
STEL_PKI_DIR="${1:-$STEL_PROJECT_DIR/.secrets/pki}"
mkdir -p "$STEL_PKI_DIR"
chmod 700 "$STEL_PKI_DIR"
if [[ -e "$STEL_PKI_DIR/ca.key" ]]; then
  echo "Existing CA preserved: $STEL_PKI_DIR"; exit 0
fi
umask 077
cd "$STEL_PKI_DIR"
openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 30 -subj '/CN=Stel Local Test CA' \
  -keyout ca.key -out ca.pem 2>openssl.log
for STEL_ID in server 1001 1002; do
  if [[ "$STEL_ID" == server ]]; then
    STEL_SUBJECT=pbx.stel.test
    STEL_EXT='subjectAltName=DNS:pbx.stel.test,DNS:localhost,IP:192.168.17.12,IP:127.0.0.1
extendedKeyUsage=serverAuth'
  else
    STEL_SUBJECT="sip:$STEL_ID@pbx.stel.test"
    STEL_EXT="subjectAltName=DNS:sip:$STEL_ID@pbx.stel.test,URI:sip:$STEL_ID@pbx.stel.test
extendedKeyUsage=clientAuth"
  fi
  openssl req -new -newkey rsa:2048 -sha256 -nodes -subj "/CN=$STEL_SUBJECT" \
    -keyout "$STEL_ID.key" -out "$STEL_ID.csr" 2>>openssl.log
  printf '%s\n' "$STEL_EXT" >"$STEL_ID.ext"
  openssl x509 -req -sha256 -in "$STEL_ID.csr" -CA ca.pem -CAkey ca.key -CAcreateserial \
    -days 30 -extfile "$STEL_ID.ext" -out "$STEL_ID.pem" 2>>openssl.log
done
openssl verify -CAfile ca.pem -purpose sslserver server.pem
openssl verify -CAfile ca.pem -purpose sslclient 1001.pem 1002.pem
echo "Local test certificates created: $STEL_PKI_DIR (30-day lifetime). Private keys stay here."
