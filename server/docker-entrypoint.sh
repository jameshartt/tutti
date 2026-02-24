#!/bin/bash
set -euo pipefail

DOMAIN="${DOMAIN:-localhost}"
WT_PORT="${WT_PORT:-4433}"

# Look for Let's Encrypt certs provisioned by Caddy
LE_DIR="/certs/caddy/certificates/acme-v02.api.letsencrypt.org-directory/${DOMAIN}"
CERT_FILE="${LE_DIR}/${DOMAIN}.crt"
KEY_FILE="${LE_DIR}/${DOMAIN}.key"

if [ -f "$CERT_FILE" ] && [ -f "$KEY_FILE" ]; then
    echo "[entrypoint] Using Let's Encrypt certs for ${DOMAIN}"
else
    echo "[entrypoint] Let's Encrypt certs not found, generating self-signed cert"
    mkdir -p /app/certs
    openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
        -keyout /app/certs/key.pem -out /app/certs/cert.pem \
        -days 1 -nodes -subj "/CN=${DOMAIN}" 2>/dev/null

    # Generate cert hash for WebTransport with self-signed certs
    openssl x509 -in /app/certs/cert.pem -outform der \
        | openssl dgst -sha256 -binary \
        | openssl enc -base64 > /app/certs/hash.txt

    CERT_FILE="/app/certs/cert.pem"
    KEY_FILE="/app/certs/key.pem"
fi

exec ./tutti-server \
    --hostname "$DOMAIN" \
    --wt-port "$WT_PORT" \
    --cert "$CERT_FILE" \
    --key "$KEY_FILE" \
    "$@"
