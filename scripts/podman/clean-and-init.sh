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
echo -e "${YELLOW}[Step 1/7] Stopping and removing all containers...${NC}"

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
echo -e "${YELLOW}[Step 2/7] Removing all data directories...${NC}"
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
echo -e "${YELLOW}[Step 3/7] Creating data directories with permissions...${NC}"
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
echo -e "${YELLOW}[Step 4/7] Generating nginx config (Podman DNS)...${NC}"

# SSL 감지
SSL_DOMAIN="${SSL_DOMAIN:-pkd.smartcoreinc.com}"
if [ -f ".docker-data/ssl/server.crt" ] && [ -f ".docker-data/ssl/server.key" ]; then
    SSL_SOURCE="nginx/api-gateway-ssl.conf"
    echo "  SSL mode: HTTPS + HTTP"
else
    SSL_SOURCE="nginx/api-gateway.conf"
    echo "  SSL mode: HTTP only"
fi

# 임시 DNS — 네트워크 생성 후 업데이트
PODMAN_DNS="10.89.0.1"

# Podman 네트워크 생성 (podman-compose 프로젝트명: docker → docker_pkd-network)
NETWORK_NAME="docker_pkd-network"
podman network create "$NETWORK_NAME" 2>/dev/null || true

# 실제 DNS IP 감지
DETECTED_DNS=$(podman network inspect "$NETWORK_NAME" 2>/dev/null | \
    python3 -c "import sys,json; nets=json.load(sys.stdin); print(nets[0]['subnets'][0]['gateway'])" 2>/dev/null || echo "")
if [ -n "$DETECTED_DNS" ]; then
    PODMAN_DNS="$DETECTED_DNS"
fi
echo "  Podman DNS resolver: $PODMAN_DNS"

sed "s|resolver 127.0.0.11|resolver $PODMAN_DNS|g" \
    "$SSL_SOURCE" > ".docker-data/nginx/api-gateway.conf"

export NGINX_CONF="../.docker-data/nginx/api-gateway.conf"
echo -e "${GREEN}  nginx config generated${NC}"
echo ""

# =============================================================================
# Step 5: Start infrastructure services (Database + OpenLDAP)
# =============================================================================
echo -e "${YELLOW}[Step 5/7] Starting infrastructure services...${NC}"

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
echo -e "${YELLOW}[Step 6/7] Initializing LDAP DIT structure...${NC}"

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
echo -e "${YELLOW}[Step 7/7] Starting application services...${NC}"
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
echo "  - API Gateway: http://localhost:18080/api"
echo "  - API Documentation: http://localhost:18090"
if [ "$DB_TYPE" = "oracle" ]; then
    echo "  - Oracle: localhost:11521 (XEPDB1)"
fi

SSL_DOMAIN="${SSL_DOMAIN:-pkd.smartcoreinc.com}"
if [ -f ".docker-data/ssl/server.crt" ]; then
    echo "  - HTTPS: https://$SSL_DOMAIN/api"
fi
echo ""
