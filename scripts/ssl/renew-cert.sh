#!/bin/bash
# =============================================================================
# Server Certificate Renewal (using existing Private CA)
# =============================================================================
# Usage: scripts/ssl/renew-cert.sh [--days DAYS] [--domain DOMAIN]
#
# Renews the server certificate using the existing CA.
# Does NOT regenerate the CA certificate.
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

DOMAIN="${SSL_DOMAIN:-pkd.smartcoreinc.com}"
SERVER_DAYS=365
SERVER_KEY_SIZE=2048
SSL_DIR=".docker-data/ssl"

while [[ $# -gt 0 ]]; do
    case $1 in
        --domain)
            DOMAIN="$2"
            shift 2
            ;;
        --days)
            SERVER_DAYS="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

echo "Renewing server certificate for $DOMAIN..."

# Check CA exists
if [ ! -f "$SSL_DIR/ca.key" ] || [ ! -f "$SSL_DIR/ca.crt" ]; then
    echo "ERROR: CA certificate not found."
    echo "Run scripts/ssl/init-cert.sh first to create the CA."
    exit 1
fi

# Show current certificate expiry
if [ -f "$SSL_DIR/server.crt" ]; then
    echo ""
    echo "Current certificate:"
    openssl x509 -in "$SSL_DIR/server.crt" -noout -dates | sed 's/^/  /'
    echo ""
fi

# Generate new server key + CSR
echo "Generating new server certificate..."
openssl genrsa -out "$SSL_DIR/server.key" "$SERVER_KEY_SIZE" 2>/dev/null

cat > "$SSL_DIR/server.cnf" <<EOF
[req]
default_bits = $SERVER_KEY_SIZE
prompt = no
distinguished_name = dn
req_extensions = v3_req

[dn]
C = KR
O = SmartCore Inc.
OU = PKD Operations
CN = $DOMAIN

[v3_req]
subjectAltName = @alt_names
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth

[alt_names]
DNS.1 = $DOMAIN
DNS.2 = localhost
DNS.3 = api-gateway
IP.1 = 127.0.0.1
EOF

openssl req -new \
    -key "$SSL_DIR/server.key" \
    -out "$SSL_DIR/server.csr" \
    -config "$SSL_DIR/server.cnf"

openssl x509 -req \
    -in "$SSL_DIR/server.csr" \
    -CA "$SSL_DIR/ca.crt" \
    -CAkey "$SSL_DIR/ca.key" \
    -CAcreateserial \
    -out "$SSL_DIR/server.crt" \
    -days "$SERVER_DAYS" \
    -sha256 \
    -extensions v3_req \
    -extfile "$SSL_DIR/server.cnf" 2>/dev/null

rm -f "$SSL_DIR/server.csr" "$SSL_DIR/server.cnf"
chmod 600 "$SSL_DIR/server.key"
chmod 644 "$SSL_DIR/server.crt"

# Reload nginx
echo "Reloading nginx..."
docker exec icao-local-pkd-api-gateway nginx -s reload 2>/dev/null || {
    echo "WARNING: Could not reload nginx. Restart the api-gateway container manually."
}

echo ""
echo "Server certificate renewed successfully!"
echo ""
echo "New certificate:"
openssl x509 -in "$SSL_DIR/server.crt" -noout -subject -dates | sed 's/^/  /'
