#!/usr/bin/env bash
# Generate a self-signed TLS certificate for Tutti WebTransport development.
#
# WebTransport requires HTTPS (QUIC). For local development, generate a
# self-signed certificate. Chrome supports connecting to WebTransport servers
# with self-signed certs using the serverCertificateHashes option.
#
# Usage:
#   cd server/certs && ./generate.sh
#
# Generates:
#   cert.pem  - Self-signed certificate (valid 30 days)
#   key.pem   - Private key
#   hash.txt  - SHA-256 certificate hash (for Chrome's serverCertificateHashes)

set -euo pipefail
cd "$(dirname "$0")"

DAYS=14
HOSTNAME="${1:-localhost}"

echo "Generating self-signed certificate for: $HOSTNAME"
echo "Valid for: $DAYS days"

# Generate EC private key and self-signed certificate
openssl req -x509 \
    -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
    -keyout key.pem \
    -out cert.pem \
    -days "$DAYS" \
    -nodes \
    -subj "/CN=$HOSTNAME" \
    -addext "subjectAltName=DNS:$HOSTNAME,DNS:*.local,IP:127.0.0.1,IP:::1"

# Generate SHA-256 hash for Chrome's serverCertificateHashes
# This allows Chrome to connect to WebTransport with self-signed certs
openssl x509 -in cert.pem -outform der | \
    openssl dgst -sha256 -binary | \
    openssl enc -base64 > hash.txt

echo ""
echo "Generated files:"
echo "  cert.pem - Certificate"
echo "  key.pem  - Private key"
echo "  hash.txt - SHA-256 hash for serverCertificateHashes"
echo ""
echo "Certificate hash: $(cat hash.txt)"
echo ""
echo "Use in Chrome WebTransport constructor:"
echo "  new WebTransport(url, {"
echo "    serverCertificateHashes: [{"
echo "      algorithm: 'sha-256',"
echo "      value: Uint8Array.from(atob('$(cat hash.txt)'), c => c.charCodeAt(0))"
echo "    }]"
echo "  })"
