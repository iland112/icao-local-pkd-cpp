#!/bin/bash
# =============================================================================
# Private CA + Server Certificate Generation
# =============================================================================
# Usage: scripts/ssl/init-cert.sh [--domain DOMAIN] [--days DAYS] [--ca-days DAYS]
#
# Creates a Private CA and issues a server certificate signed by it.
# The CA certificate (ca.crt) must be distributed to clients for trust.
#
# Output:
#   .docker-data/ssl/ca.key        - CA private key (keep secret)
#   .docker-data/ssl/ca.crt        - CA certificate (distribute to clients)
#   .docker-data/ssl/server.key    - Server private key
#   .docker-data/ssl/server.crt    - Server certificate (signed by CA)
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

# Defaults
DOMAIN="${SSL_DOMAIN:-pkd.smartcoreinc.com}"
CA_DAYS=3650      # CA validity: 10 years
SERVER_DAYS=365   # Server cert validity: 1 year
CA_KEY_SIZE=4096
SERVER_KEY_SIZE=2048
FORCE=""
EXTRA_IPS=""

# Parse arguments
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
        --ca-days)
            CA_DAYS="$2"
            shift 2
            ;;
        --ip)
            EXTRA_IPS="$EXTRA_IPS $2"
            shift 2
            ;;
        --force)
            FORCE="true"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--domain DOMAIN] [--ip IP] [--days DAYS] [--ca-days DAYS] [--force]"
            exit 1
            ;;
    esac
done

SSL_DIR=".docker-data/ssl"

echo "============================================"
echo "Private CA + Server Certificate Generation"
echo "============================================"
echo "Domain:          $DOMAIN"
echo "CA validity:     $CA_DAYS days ($(( CA_DAYS / 365 )) years)"
echo "Server validity: $SERVER_DAYS days"
echo "Output:          $SSL_DIR/"
echo ""

# Check if CA already exists
if [ -f "$SSL_DIR/ca.crt" ] && [ -z "$FORCE" ]; then
    echo "CA certificate already exists: $SSL_DIR/ca.crt"
    echo ""
    echo "Options:"
    echo "  - Renew server cert only: scripts/ssl/renew-cert.sh"
    echo "  - Regenerate everything:  scripts/ssl/init-cert.sh --force"
    exit 0
fi

# Create directory
mkdir -p "$SSL_DIR"

# Step 1: Generate CA private key
echo "[1/4] Generating CA private key (RSA $CA_KEY_SIZE)..."
openssl genrsa -out "$SSL_DIR/ca.key" "$CA_KEY_SIZE" 2>/dev/null

# Step 2: Generate CA self-signed certificate
echo "[2/4] Generating CA certificate (valid $CA_DAYS days)..."
openssl req -x509 -new -nodes \
    -key "$SSL_DIR/ca.key" \
    -sha256 \
    -days "$CA_DAYS" \
    -out "$SSL_DIR/ca.crt" \
    -subj "/C=KR/O=SmartCore Inc./OU=PKD Operations/CN=ICAO Local PKD Private CA"

# Step 3: Generate server key + CSR
echo "[3/4] Generating server key and CSR..."
openssl genrsa -out "$SSL_DIR/server.key" "$SERVER_KEY_SIZE" 2>/dev/null

# Create SAN config for server certificate
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

# Add extra IPs to SAN
IP_IDX=2
for IP in $EXTRA_IPS; do
    echo "IP.$IP_IDX = $IP" >> "$SSL_DIR/server.cnf"
    IP_IDX=$((IP_IDX + 1))
done

openssl req -new \
    -key "$SSL_DIR/server.key" \
    -out "$SSL_DIR/server.csr" \
    -config "$SSL_DIR/server.cnf"

# Step 4: Sign server certificate with CA
echo "[4/4] Signing server certificate with CA..."
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

# Cleanup CSR (no longer needed)
rm -f "$SSL_DIR/server.csr" "$SSL_DIR/server.cnf"

# Set permissions
chmod 600 "$SSL_DIR/ca.key" "$SSL_DIR/server.key"
chmod 644 "$SSL_DIR/ca.crt" "$SSL_DIR/server.crt"

# Verify
echo ""
echo "============================================"
echo "Certificate generation complete!"
echo "============================================"
echo ""
echo "CA Certificate:"
openssl x509 -in "$SSL_DIR/ca.crt" -noout -subject -dates | sed 's/^/  /'
echo ""
echo "Server Certificate:"
openssl x509 -in "$SSL_DIR/server.crt" -noout -subject -dates | sed 's/^/  /'
echo ""
echo "SAN (Subject Alternative Names):"
openssl x509 -in "$SSL_DIR/server.crt" -noout -text | grep -A1 "Subject Alternative Name" | tail -1 | sed 's/^/  /'
echo ""

echo "Files:"
echo "  $SSL_DIR/ca.key        - CA private key (DO NOT distribute)"
echo "  $SSL_DIR/ca.crt        - CA certificate (distribute to clients)"
echo "  $SSL_DIR/server.key    - Server private key"
echo "  $SSL_DIR/server.crt    - Server certificate"
echo ""
echo "Next steps:"
echo "  1. Start the system: ./docker-start.sh"
echo "     (HTTPS mode will be automatically detected)"
echo "  2. Distribute CA cert to mobile app clients:"
echo "     cp $SSL_DIR/ca.crt /path/to/mobile-app/assets/"
echo "  3. Access: https://$DOMAIN"
echo "     (or http://$DOMAIN for HTTP)"
echo ""
echo "Renew server cert: scripts/ssl/renew-cert.sh"
