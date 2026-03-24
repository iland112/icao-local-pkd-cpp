#!/bin/bash
# =============================================================================
# ICAO Local PKD - Podman Complete Clean and Initialization Script
# =============================================================================
# Description: Completely removes all data and containers, then initializes
#              the system with proper LDAP structure.
#              Adapted from Docker version: LDAP init handled by script
#              (no service_completed_successfully condition needed)
# Version: 1.0.0
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

COMPOSE_FILE="docker/docker-compose.podman.yaml"
COMPOSE="podman-compose -f $COMPOSE_FILE"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=============================================${NC}"
echo -e "${BLUE}ICAO Local PKD - Podman Complete Cleanup & Init${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""

# =============================================================================
# Read DB_TYPE from .env
# =============================================================================
DB_TYPE=$(grep -E '^DB_TYPE=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'')
DB_TYPE="${DB_TYPE:-oracle}"

if [ "$DB_TYPE" = "oracle" ]; then
    PROFILE_FLAG="--profile oracle"
    echo -e "${BLUE}Database mode: Oracle${NC}"
else
    PROFILE_FLAG="--profile postgres"
    echo -e "${BLUE}Database mode: PostgreSQL${NC}"
fi
echo ""

# Load LDAP credentials
LDAP_ADMIN_PW="$(grep -E '^LDAP_ADMIN_PASSWORD=' .env 2>/dev/null | cut -d= -f2)"
LDAP_ADMIN_PW="${LDAP_ADMIN_PW:-ldap_test_password_123}"
LDAP_CONFIG_PW="$(grep -E '^LDAP_CONFIG_PASSWORD=' .env 2>/dev/null | cut -d= -f2)"
LDAP_CONFIG_PW="${LDAP_CONFIG_PW:-config_test_123}"

# =============================================================================
# Step 1: Stop and remove all containers
# =============================================================================
echo -e "${YELLOW}[Step 1/8] Stopping and removing all containers...${NC}"

# Remove any existing containers
echo "  Removing any existing icao-local-pkd-* containers..."
podman ps -a --filter "name=icao-local-pkd-" --format "{{.Names}}" | xargs -r podman rm -f 2>/dev/null || true

# Stop compose-managed containers
$COMPOSE down -v 2>/dev/null || true

echo -e "${GREEN}  All containers stopped and removed${NC}"
echo ""

# =============================================================================
# Step 2: Remove all data
# =============================================================================
echo -e "${YELLOW}[Step 2/8] Removing all data directories...${NC}"
if [ -d ".docker-data" ]; then
    # Preserve SSL certificates
    if [ -d ".docker-data/ssl" ] && [ -f ".docker-data/ssl/server.crt" ]; then
        echo "  Backing up SSL certificates..."
        cp -a .docker-data/ssl /tmp/icao-ssl-backup
    fi
    echo "  Removing .docker-data/*"
    # Use podman unshare for files created by rootless containers (different UID mapping)
    podman unshare rm -rf .docker-data/* 2>/dev/null || rm -rf .docker-data/* 2>/dev/null || true
    # Restore SSL certificates
    if [ -d "/tmp/icao-ssl-backup" ]; then
        cp -a /tmp/icao-ssl-backup .docker-data/ssl
        rm -rf /tmp/icao-ssl-backup
        echo "  SSL certificates restored"
    fi
    echo -e "${GREEN}  Data directories removed${NC}"
else
    echo "  .docker-data does not exist, skipping"
fi
echo ""

# =============================================================================
# Step 3: Recreate data directories with proper permissions
# =============================================================================
echo -e "${YELLOW}[Step 3/8] Creating data directories with permissions...${NC}"
mkdir -p .docker-data/postgres
mkdir -p .docker-data/openldap1/data .docker-data/openldap1/config
mkdir -p .docker-data/openldap2/data .docker-data/openldap2/config
mkdir -p .docker-data/pkd-uploads .docker-data/pkd-logs
mkdir -p .docker-data/pa-logs .docker-data/sync-logs
mkdir -p .docker-data/monitoring-logs .docker-data/gateway-logs
mkdir -p .docker-data/ai-analysis-logs .docker-data/nginx .docker-data/ssl

# Oracle uses named volume — remove for clean init
if [ "$DB_TYPE" = "oracle" ]; then
    echo "  Removing Oracle named volume..."
    podman volume rm icao-local-pkd-oracle-data 2>/dev/null || true
fi

# Generate ICAO PKD LDAP TLS certificates using Private CA
ICAO_TLS_DIR=".docker-data/icao-pkd-tls"
SSL_DIR=".docker-data/ssl"
mkdir -p "$ICAO_TLS_DIR"
echo "  Generating ICAO PKD LDAP TLS certificates..."

if [ -f "$SSL_DIR/ca.key" ] && [ -f "$SSL_DIR/ca.crt" ]; then
    echo "  Using Private CA ($SSL_DIR/ca.crt)"
    cp "$SSL_DIR/ca.crt" "$ICAO_TLS_DIR/ca.pem"
    CA_KEY="$SSL_DIR/ca.key"
    CA_CERT="$SSL_DIR/ca.crt"
else
    echo "  Private CA not found, generating standalone CA"
    openssl req -x509 -nodes -newkey rsa:2048 -days 3650 \
        -keyout "$ICAO_TLS_DIR/ca.key" -out "$ICAO_TLS_DIR/ca.pem" \
        -subj "/CN=ICAO PKD Simulation CA/O=ICAO/C=CA" 2>/dev/null
    CA_KEY="$ICAO_TLS_DIR/ca.key"
    CA_CERT="$ICAO_TLS_DIR/ca.pem"
fi

# Server cert for icao-sim LDAP
openssl req -nodes -newkey rsa:2048 \
    -keyout "$ICAO_TLS_DIR/ldap-server.key" -out "$ICAO_TLS_DIR/ldap-server.csr" \
    -subj "/CN=icao-pkd-ldap/O=ICAO PKD Simulation/C=CA" 2>/dev/null
openssl x509 -req -in "$ICAO_TLS_DIR/ldap-server.csr" \
    -CA "$CA_CERT" -CAkey "$CA_KEY" -CAcreateserial \
    -out "$ICAO_TLS_DIR/ldap-server.crt" -days 3650 \
    -extfile <(echo "subjectAltName=DNS:icao-pkd-ldap,DNS:localhost") 2>/dev/null

# Client cert for relay
openssl req -nodes -newkey rsa:2048 \
    -keyout "$ICAO_TLS_DIR/client-key.pem" -out "$ICAO_TLS_DIR/client.csr" \
    -subj "/CN=pkd-relay-client/O=SmartCore PKD/C=KR" 2>/dev/null
openssl x509 -req -in "$ICAO_TLS_DIR/client.csr" \
    -CA "$CA_CERT" -CAkey "$CA_KEY" -CAcreateserial \
    -out "$ICAO_TLS_DIR/client.pem" -days 3650 2>/dev/null

rm -f "$ICAO_TLS_DIR"/*.csr "$ICAO_TLS_DIR"/*.srl "$SSL_DIR"/*.srl
echo -e "${GREEN}✓ ICAO TLS certificates generated (Private CA)${NC}"

chmod -R 777 .docker-data/

# SELinux context for bind mounts (RHEL 9 Enforcing mode)
# Two-step labeling:
#   1) chcon -Rt container_file_t  → set SELinux type
#   2) chcon -R -l s0              → remove MCS categories (c123,c456)
# Rootless Podman containers run with unique MCS labels, so files must
# have no MCS restriction (just "s0") to be accessible by any container.
echo "  Setting SELinux context for bind mounts..."
if command -v chcon > /dev/null 2>&1 && [ "$(getenforce 2>/dev/null)" = "Enforcing" ]; then
    SELINUX_DIRS=".docker-data/ data/cert nginx/ docs/openapi docker/db-oracle/init docker/init-scripts"
    for d in $SELINUX_DIRS; do
        if [ -e "$d" ]; then
            chcon -Rt container_file_t "$d" 2>/dev/null || true
            chcon -R -l s0 "$d" 2>/dev/null || true
        fi
    done
    echo "  SELinux context set (container_file_t:s0, no MCS)"
fi
echo -e "${GREEN}  Data directories created and permissions set${NC}"
echo ""

# =============================================================================
# Step 4: Generate nginx config (Podman DNS)
# =============================================================================
echo -e "${YELLOW}[Step 4/8] Generating nginx config (Podman DNS)...${NC}"

# Podman 네트워크 생성
NETWORK_NAME="docker_pkd-network"
podman network create "$NETWORK_NAME" 2>/dev/null || true

# Load shared library for generate_podman_nginx_conf
RUNTIME="podman"
source "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh"

# 공통 함수로 nginx 설정 생성 (Podman DNS resolver 자동 감지 + 교체)
generate_podman_nginx_conf
echo -e "${GREEN}  nginx config generated${NC}"
echo ""

# =============================================================================
# Step 5: Start infrastructure services (Database + OpenLDAP)
# =============================================================================
echo -e "${YELLOW}[Step 5/8] Starting infrastructure services...${NC}"

# Start OpenLDAP
$COMPOSE up -d --no-build openldap1 openldap2

# Wait for database based on DB_TYPE
if [ "$DB_TYPE" = "oracle" ]; then
    $COMPOSE $PROFILE_FLAG up -d --no-build oracle

    echo -n "  Waiting for Oracle (this may take 3-5 minutes on first run)"
    ORACLE_PWD=$(grep -E '^ORACLE_PASSWORD=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'')
    ORACLE_PWD="${ORACLE_PWD:-pkd_password}"

    for i in {1..180}; do
        ORACLE_HEALTH=$(podman inspect icao-local-pkd-oracle --format='{{.State.Health.Status}}' 2>/dev/null || echo "not-found")
        if [ "$ORACLE_HEALTH" = "healthy" ]; then
            echo ""
            echo -e "${GREEN}  Oracle container is healthy${NC}"

            # Wait for init scripts
            echo -n "  Waiting for Oracle init scripts (pkd_user)"
            for j in {1..30}; do
                INIT_CHECK=$(podman exec icao-local-pkd-oracle bash -c "echo 'SELECT 1 FROM DUAL;' | sqlplus -s pkd_user/${ORACLE_PWD}@//localhost:1521/XEPDB1 2>/dev/null | grep -c '1'" 2>/dev/null || echo "0")
                if [ "$INIT_CHECK" -ge 1 ] 2>/dev/null; then
                    echo ""
                    echo -e "${GREEN}  Oracle pkd_user is ready${NC}"
                    # Wait for all init scripts to complete (check last table in sequence)
                    echo -n "  Waiting for Oracle init scripts to complete"
                    for k in {1..60}; do
                        TABLE_COUNT=$(podman exec icao-local-pkd-oracle bash -c "echo \"SELECT COUNT(*) FROM user_tables WHERE table_name IN ('PENDING_DSC_REGISTRATION','CVC_CERTIFICATE');\" | sqlplus -s pkd_user/${ORACLE_PWD}@//localhost:1521/XEPDB1 2>/dev/null | grep -E '^\s*[0-9]+' | tr -d ' '" 2>/dev/null || echo "0")
                        if [ "$TABLE_COUNT" -ge 2 ] 2>/dev/null; then
                            echo ""
                            echo -e "${GREEN}  Oracle init scripts completed (all tables created)${NC}"
                            break
                        fi
                        echo -n "."
                        sleep 2
                        if [ $k -eq 60 ]; then
                            echo ""
                            echo -e "${YELLOW}⚠ Some Oracle init scripts may not have completed, running manually...${NC}"
                            podman exec icao-local-pkd-oracle bash -c "sqlplus -s pkd_user/${ORACLE_PWD}@//localhost:1521/XEPDB1 @/opt/oracle/scripts/startup/03-core-schema.sql" 2>/dev/null || true
                            podman exec icao-local-pkd-oracle bash -c "sqlplus -s pkd_user/${ORACLE_PWD}@//localhost:1521/XEPDB1 @/opt/oracle/scripts/startup/20-eac-schema.sql" 2>/dev/null || true
                            echo -e "${GREEN}  Oracle migration scripts executed${NC}"
                        fi
                    done
                    break 2
                fi
                echo -n "."
                sleep 3
                if [ $j -eq 30 ]; then
                    echo ""
                    echo -e "${RED}  Oracle init scripts failed (pkd_user not accessible)${NC}"
                    podman logs icao-local-pkd-oracle 2>&1 | grep -E "(ERROR|error|01-create)" | tail -10
                    exit 1
                fi
            done
            break
        fi
        if [ $((i % 10)) -eq 0 ]; then
            echo -n " ${i}s"
        else
            echo -n "."
        fi
        sleep 2
        if [ $i -eq 180 ]; then
            echo ""
            echo -e "${RED}  Oracle failed to start (timeout after 360s)${NC}"
            podman logs icao-local-pkd-oracle 2>&1 | tail -20
            exit 1
        fi
    done
else
    $COMPOSE $PROFILE_FLAG up -d --no-build postgres

    echo -n "  Waiting for PostgreSQL"
    for i in {1..30}; do
        if podman exec icao-local-pkd-postgres pg_isready -U pkd -d localpkd > /dev/null 2>&1; then
            echo ""
            echo -e "${GREEN}  PostgreSQL is ready${NC}"
            break
        fi
        echo -n "."
        sleep 1
        if [ $i -eq 30 ]; then
            echo ""
            echo -e "${RED}  PostgreSQL failed to start${NC}"
            exit 1
        fi
    done
fi

# Wait for OpenLDAP
echo -n "  Waiting for OpenLDAP1"
for i in {1..60}; do
    if podman exec icao-local-pkd-openldap1 ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
        echo ""
        echo -e "${GREEN}  OpenLDAP1 is ready${NC}"
        break
    fi
    echo -n "."
    sleep 1
    if [ $i -eq 60 ]; then
        echo ""
        echo -e "${RED}  OpenLDAP1 failed to start${NC}"
        exit 1
    fi
done

echo -n "  Waiting for OpenLDAP2"
for i in {1..60}; do
    if podman exec icao-local-pkd-openldap2 ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
        echo ""
        echo -e "${GREEN}  OpenLDAP2 is ready${NC}"
        break
    fi
    echo -n "."
    sleep 1
    if [ $i -eq 60 ]; then
        echo ""
        echo -e "${RED}  OpenLDAP2 failed to start${NC}"
        exit 1
    fi
done
echo ""

# =============================================================================
# Step 6: Initialize LDAP (MMR + DIT) — inline, no init containers
# =============================================================================
echo -e "${YELLOW}[Step 6/8] Initializing LDAP DIT structure...${NC}"

# --- MMR Setup on OpenLDAP1 ---
echo "  Setting up MMR on openldap1 (Server ID: 1)..."
podman exec icao-local-pkd-openldap1 bash -c '
LDAP_URI="ldap://localhost:389"
CONFIG_PW="'"$LDAP_CONFIG_PW"'"
ADMIN_PW="'"$LDAP_ADMIN_PW"'"

# Load syncprov module
ldapmodify -x -H $LDAP_URI -D "cn=admin,cn=config" -w "$CONFIG_PW" 2>/dev/null <<LDIF || true
dn: cn=module{0},cn=config
changetype: modify
add: olcModuleLoad
olcModuleLoad: syncprov
LDIF

# Create syncprov overlay
ldapadd -x -H $LDAP_URI -D "cn=admin,cn=config" -w "$CONFIG_PW" 2>/dev/null <<LDIF || true
dn: olcOverlay=syncprov,olcDatabase={1}mdb,cn=config
objectClass: olcOverlayConfig
objectClass: olcSyncProvConfig
olcOverlay: syncprov
olcSpSessionLog: 100
LDIF

# Set server ID
ldapmodify -x -H $LDAP_URI -D "cn=admin,cn=config" -w "$CONFIG_PW" 2>/dev/null <<LDIF || true
dn: cn=config
changetype: modify
add: olcServerID
olcServerID: 1
LDIF

# Configure MMR replication
ldapmodify -x -H $LDAP_URI -D "cn=admin,cn=config" -w "$CONFIG_PW" 2>/dev/null <<LDIF || true
dn: olcDatabase={1}mdb,cn=config
changetype: modify
add: olcSyncRepl
olcSyncRepl: {0}rid=001 provider=ldap://openldap1:389 binddn="cn=admin,dc=ldap,dc=smartcoreinc,dc=com" bindmethod=simple credentials=$ADMIN_PW searchbase="dc=ldap,dc=smartcoreinc,dc=com" scope=sub schemachecking=on type=refreshAndPersist retry="30 5 300 3" timeout=1
olcSyncRepl: {1}rid=002 provider=ldap://openldap2:389 binddn="cn=admin,dc=ldap,dc=smartcoreinc,dc=com" bindmethod=simple credentials=$ADMIN_PW searchbase="dc=ldap,dc=smartcoreinc,dc=com" scope=sub schemachecking=on type=refreshAndPersist retry="30 5 300 3" timeout=1
-
add: olcMirrorMode
olcMirrorMode: TRUE
LDIF

echo "MMR setup complete for openldap1"
'
echo -e "${GREEN}  openldap1 MMR configured${NC}"

# --- MMR Setup on OpenLDAP2 ---
echo "  Setting up MMR on openldap2 (Server ID: 2)..."
podman exec icao-local-pkd-openldap2 bash -c '
LDAP_URI="ldap://localhost:389"
CONFIG_PW="'"$LDAP_CONFIG_PW"'"
ADMIN_PW="'"$LDAP_ADMIN_PW"'"

# Load syncprov module
ldapmodify -x -H $LDAP_URI -D "cn=admin,cn=config" -w "$CONFIG_PW" 2>/dev/null <<LDIF || true
dn: cn=module{0},cn=config
changetype: modify
add: olcModuleLoad
olcModuleLoad: syncprov
LDIF

# Create syncprov overlay
ldapadd -x -H $LDAP_URI -D "cn=admin,cn=config" -w "$CONFIG_PW" 2>/dev/null <<LDIF || true
dn: olcOverlay=syncprov,olcDatabase={1}mdb,cn=config
objectClass: olcOverlayConfig
objectClass: olcSyncProvConfig
olcOverlay: syncprov
olcSpSessionLog: 100
LDIF

# Set server ID
ldapmodify -x -H $LDAP_URI -D "cn=admin,cn=config" -w "$CONFIG_PW" 2>/dev/null <<LDIF || true
dn: cn=config
changetype: modify
add: olcServerID
olcServerID: 2
LDIF

# Configure MMR replication
ldapmodify -x -H $LDAP_URI -D "cn=admin,cn=config" -w "$CONFIG_PW" 2>/dev/null <<LDIF || true
dn: olcDatabase={1}mdb,cn=config
changetype: modify
add: olcSyncRepl
olcSyncRepl: {0}rid=001 provider=ldap://openldap1:389 binddn="cn=admin,dc=ldap,dc=smartcoreinc,dc=com" bindmethod=simple credentials=$ADMIN_PW searchbase="dc=ldap,dc=smartcoreinc,dc=com" scope=sub schemachecking=on type=refreshAndPersist retry="30 5 300 3" timeout=1
olcSyncRepl: {1}rid=002 provider=ldap://openldap2:389 binddn="cn=admin,dc=ldap,dc=smartcoreinc,dc=com" bindmethod=simple credentials=$ADMIN_PW searchbase="dc=ldap,dc=smartcoreinc,dc=com" scope=sub schemachecking=on type=refreshAndPersist retry="30 5 300 3" timeout=1
-
add: olcMirrorMode
olcMirrorMode: TRUE
LDIF

echo "MMR setup complete for openldap2"
'
echo -e "${GREEN}  openldap2 MMR configured${NC}"

# --- PKD DIT Initialization ---
echo "  Initializing PKD DIT structure..."
podman exec icao-local-pkd-openldap1 bash -c '
LDAP_URI="ldap://localhost:389"
ADMIN_DN="cn=admin,dc=ldap,dc=smartcoreinc,dc=com"
ADMIN_PW="'"$LDAP_ADMIN_PW"'"

# Step 1: Create dc=pkd container
echo "  Creating dc=pkd..."
ldapadd -x -H $LDAP_URI -D "$ADMIN_DN" -w "$ADMIN_PW" <<LDIF || true
dn: dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: top
objectClass: dcObject
objectClass: organization
dc: pkd
o: ICAO PKD
LDIF

# Step 2: Create dc=download container
echo "  Creating dc=download..."
ldapadd -x -H $LDAP_URI -D "$ADMIN_DN" -w "$ADMIN_PW" <<LDIF || true
dn: dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: top
objectClass: dcObject
objectClass: organization
dc: download
o: PKD Download
LDIF

# Step 3: Create dc=data container
echo "  Creating dc=data..."
ldapadd -x -H $LDAP_URI -D "$ADMIN_DN" -w "$ADMIN_PW" <<LDIF || true
dn: dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: top
objectClass: dcObject
objectClass: organization
dc: data
o: PKD Data
LDIF

# Step 4: Create dc=nc-data container
echo "  Creating dc=nc-data..."
ldapadd -x -H $LDAP_URI -D "$ADMIN_DN" -w "$ADMIN_PW" <<LDIF || true
dn: dc=nc-data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: top
objectClass: dcObject
objectClass: organization
dc: nc-data
o: PKD Non-Compliant Data
LDIF

echo "  PKD DIT structure initialized"
'

# Verify
sleep 2
LDAP_VERIFY=$(podman exec icao-local-pkd-openldap1 ldapsearch -x -H ldap://localhost \
    -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" -w "$LDAP_ADMIN_PW" \
    -b "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" -s sub "(objectClass=*)" dn 2>/dev/null | grep "^dn:" | wc -l || echo "0")

if [ "$LDAP_VERIFY" -ge 3 ] 2>/dev/null; then
    echo -e "${GREEN}  LDAP DIT verified ($LDAP_VERIFY entries)${NC}"
else
    echo -e "${RED}  LDAP DIT verification failed (entries: $LDAP_VERIFY)${NC}"
    exit 1
fi
echo ""

# =============================================================================
# Step 7: Start application services
# =============================================================================
echo -e "${YELLOW}[Step 7/8] Starting application services...${NC}"
# Start only app services (infra already running from Step 5)
$COMPOSE $PROFILE_FLAG up -d --no-build \
    pkd-management pa-service pkd-relay monitoring-service \
    ai-analysis frontend api-gateway swagger-ui

# Wait for services to be healthy
echo -n "  Waiting for services to be healthy"
for i in {1..90}; do
    HEALTHY_COUNT=$(podman ps --filter "name=icao-local-pkd" --filter "health=healthy" --format "{{.Names}}" 2>/dev/null | wc -l || echo "0")

    if [ "$HEALTHY_COUNT" -ge 5 ]; then
        echo ""
        echo -e "${GREEN}  Services are healthy ($HEALTHY_COUNT containers)${NC}"
        break
    fi
    echo -n "."
    sleep 2
    if [ $i -eq 90 ]; then
        echo ""
        echo -e "${YELLOW}  Some services may not be healthy yet ($HEALTHY_COUNT healthy)${NC}"
        break
    fi
done
echo ""

# =============================================================================
# Step 8: Initialize ICAO PKD Simulation LDAP
# =============================================================================
ICAO_LDIF_DIR="data/icao_ldif"
if [ -d "$ICAO_LDIF_DIR" ] && ls "$ICAO_LDIF_DIR"/*.ldif > /dev/null 2>&1; then
    echo -e "${YELLOW}[Step 8/8] Initializing ICAO PKD Simulation LDAP...${NC}"

    echo -n "  Waiting for ICAO sim LDAP"
    for i in {1..30}; do
        if podman exec icao-local-pkd-icao-sim ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
            echo ""
            echo -e "${GREEN}✓ ICAO sim LDAP is ready${NC}"
            break
        fi
        echo -n "."
        sleep 1
        if [ $i -eq 30 ]; then
            echo ""
            echo -e "${YELLOW}⚠ ICAO sim LDAP not ready, skipping data load${NC}"
        fi
    done

    ICAO_ADMIN_PW="${ICAO_LDAP_ADMIN_PASSWORD:-icao_sim_password}"

    echo "  Creating ICAO PKD DIT structure..."
    DIT_FILE="/tmp/icao-dit-$$.ldif"
    cat > "$DIT_FILE" << 'DITEOF'
dn: dc=pkd,dc=icao,dc=int
objectClass: top
objectClass: dcObject
objectClass: organization
dc: pkd
o: ICAO PKD

dn: dc=download,dc=pkd,dc=icao,dc=int
objectClass: top
objectClass: dcObject
objectClass: organization
dc: download
o: PKD Download

dn: dc=data,dc=download,dc=pkd,dc=icao,dc=int
objectClass: top
objectClass: dcObject
objectClass: organization
dc: data
o: PKD Data

dn: dc=nc-data,dc=download,dc=pkd,dc=icao,dc=int
objectClass: top
objectClass: dcObject
objectClass: organization
dc: nc-data
o: PKD Non-Conformant Data
DITEOF
    podman cp "$DIT_FILE" icao-local-pkd-icao-sim:/tmp/icao-dit.ldif
    podman exec icao-local-pkd-icao-sim ldapadd -x -H ldap://localhost \
        -D "cn=admin,dc=icao,dc=int" -w "$ICAO_ADMIN_PW" \
        -f /tmp/icao-dit.ldif > /dev/null 2>&1 || true
    rm -f "$DIT_FILE"
    echo -e "${GREEN}✓ ICAO PKD DIT structure created${NC}"

    TOTAL_FILES=$(ls "$ICAO_LDIF_DIR"/*.ldif 2>/dev/null | wc -l)
    MAX_PASSES=3
    for pass in $(seq 1 $MAX_PASSES); do
        echo "  Loading $TOTAL_FILES LDIF files (pass $pass/$MAX_PASSES)..."
        for f in $(ls "$ICAO_LDIF_DIR"/*.ldif 2>/dev/null | sort); do
            FNAME=$(basename "$f")
            if [ $pass -eq 1 ]; then
                ENTRIES=$(grep -c "^dn:" "$f" 2>/dev/null || echo 0)
                echo -n "    $FNAME ($ENTRIES entries)..."
            else
                echo -n "    $FNAME..."
            fi
            podman cp "$f" icao-local-pkd-icao-sim:/tmp/"$FNAME"
            podman exec icao-local-pkd-icao-sim ldapadd -x -c -H ldap://localhost \
                -D "cn=admin,dc=icao,dc=int" -w "$ICAO_ADMIN_PW" \
                -f /tmp/"$FNAME" > /dev/null 2>&1 || true
            podman exec icao-local-pkd-icao-sim rm -f /tmp/"$FNAME"
            echo " done"
        done

        TOTAL_ENTRIES=$(podman exec icao-local-pkd-icao-sim ldapsearch -x -H ldap://localhost \
            -D "cn=admin,dc=icao,dc=int" -w "$ICAO_ADMIN_PW" \
            -b "dc=download,dc=pkd,dc=icao,dc=int" -s sub \
            "(|(objectClass=pkdDownload)(objectClass=cRLDistributionPoint)(objectClass=pkdMasterList))" dn 2>/dev/null \
            | grep "numEntries" | grep -oE "[0-9]+" || echo "0")
        echo "  → $TOTAL_ENTRIES entries loaded"

        if [ "$TOTAL_ENTRIES" -ge 20000 ] 2>/dev/null; then
            break
        fi
        if [ $pass -lt $MAX_PASSES ]; then
            echo -e "${YELLOW}  Retrying load (some entries may have missing parent DNs)...${NC}"
        fi
    done
    echo -e "${GREEN}✓ ICAO PKD Simulation LDAP initialized ($TOTAL_ENTRIES entries, $MAX_PASSES passes max)${NC}"
else
    echo -e "${YELLOW}[Step 8/8] Skipping ICAO sim data load (no LDIF files in $ICAO_LDIF_DIR)${NC}"
fi
echo ""

# =============================================================================
# Final Status
# =============================================================================
echo -e "${BLUE}=============================================${NC}"
echo -e "${BLUE}System Initialization Complete${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""

echo "Container Status:"
podman ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}" \
    --filter "name=icao-local-pkd" 2>/dev/null || $COMPOSE ps

echo ""
echo -e "${GREEN}  System is ready for use (DB_TYPE=$DB_TYPE)${NC}"
echo ""
echo "Access URLs:"
echo "  - Frontend: http://localhost:13080"
SSL_DOMAIN="${SSL_DOMAIN:-pkd.smartcoreinc.com}"
echo "  - API Gateway: https://$SSL_DOMAIN/api"
echo "  - API Documentation: http://localhost:18090"
if [ "$DB_TYPE" = "oracle" ]; then
    echo "  - Oracle: localhost:11521 (XEPDB1)"
fi

SSL_DOMAIN="${SSL_DOMAIN:-pkd.smartcoreinc.com}"
if [ -f ".docker-data/ssl/server.crt" ]; then
    echo "  - HTTPS: https://$SSL_DOMAIN/api"
fi
echo ""
