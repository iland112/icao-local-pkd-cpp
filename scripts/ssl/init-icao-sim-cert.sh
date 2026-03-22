#!/bin/bash
# =============================================================================
# ICAO PKD Simulation LDAP — TLS Server Certificate
# =============================================================================
# Issues a server certificate for icao-pkd-ldap using the existing Private CA.
# The same CA signs both the server cert and the client cert (from CSR).
#
# Usage: scripts/ssl/init-icao-sim-cert.sh
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SSL_DIR="$PROJECT_ROOT/.docker-data/ssl"
ICAO_TLS_DIR="$PROJECT_ROOT/.docker-data/icao-pkd-tls"

# Check CA exists
if [ ! -f "$SSL_DIR/ca.key" ] || [ ! -f "$SSL_DIR/ca.crt" ]; then
    echo "ERROR: Private CA not found. Run scripts/ssl/init-cert.sh first."
    exit 1
fi

echo "=== Generating ICAO PKD Simulation LDAP Server Certificate ==="
echo "CA: $SSL_DIR/ca.crt"
echo "Output: $ICAO_TLS_DIR/"

mkdir -p "$ICAO_TLS_DIR"

# Generate server key
openssl genrsa -out "$ICAO_TLS_DIR/ldap-server.key" 2048

# Create server CSR config
cat > "$ICAO_TLS_DIR/ldap-server.cnf" << 'EOF'
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
C = CH
O = ICAO
OU = PKD
CN = icao-pkd-ldap

[v3_req]
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
subjectAltName = @alt_names

[alt_names]
DNS.1 = icao-pkd-ldap
DNS.2 = localhost
IP.1 = 127.0.0.1
EOF

# Generate server CSR
openssl req -new \
    -key "$ICAO_TLS_DIR/ldap-server.key" \
    -out "$ICAO_TLS_DIR/ldap-server.csr" \
    -config "$ICAO_TLS_DIR/ldap-server.cnf"

# Sign with Private CA
openssl x509 -req \
    -in "$ICAO_TLS_DIR/ldap-server.csr" \
    -CA "$SSL_DIR/ca.crt" \
    -CAkey "$SSL_DIR/ca.key" \
    -CAcreateserial \
    -out "$ICAO_TLS_DIR/ldap-server.crt" \
    -days 365 \
    -sha256 \
    -extensions v3_req \
    -extfile "$ICAO_TLS_DIR/ldap-server.cnf"

# Copy CA cert (for LDAP server to verify clients)
cp "$SSL_DIR/ca.crt" "$ICAO_TLS_DIR/ca.pem"

# Cleanup CSR
rm -f "$ICAO_TLS_DIR/ldap-server.csr" "$ICAO_TLS_DIR/ldap-server.cnf"

# Set permissions
chmod 644 "$ICAO_TLS_DIR/ldap-server.crt" "$ICAO_TLS_DIR/ca.pem"
chmod 600 "$ICAO_TLS_DIR/ldap-server.key"

echo ""
echo "=== ICAO PKD LDAP Server Certificate Created ==="
echo "  Server cert: $ICAO_TLS_DIR/ldap-server.crt"
echo "  Server key:  $ICAO_TLS_DIR/ldap-server.key"
echo "  CA cert:     $ICAO_TLS_DIR/ca.pem"
echo ""
echo "Verify:"
openssl x509 -in "$ICAO_TLS_DIR/ldap-server.crt" -noout -subject -issuer -dates
