#!/bin/bash
# =============================================================================
# ICAO Local PKD - Complete Clean and Initialization Script
# =============================================================================
# Description: Completely removes all data and containers, then initializes
#              the system with proper LDAP structure
# Version: 2.0.0
# Created: 2026-01-25
# =============================================================================

set -e  # Exit on any error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=============================================${NC}"
echo -e "${BLUE}ICAO Local PKD - Complete Cleanup & Init${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""

# =============================================================================
# Read DB_TYPE from .env (postgres or oracle)
# =============================================================================
DB_TYPE=$(grep -E '^DB_TYPE=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'')
DB_TYPE="${DB_TYPE:-postgres}"

# Set docker compose profile based on DB_TYPE
if [ "$DB_TYPE" = "oracle" ]; then
    PROFILE_FLAG="--profile oracle"
    echo -e "${BLUE}Database mode: Oracle${NC}"
else
    PROFILE_FLAG="--profile postgres"
    echo -e "${BLUE}Database mode: PostgreSQL${NC}"
fi
echo ""

# =============================================================================
# Step 1: Stop and remove all containers
# =============================================================================
echo -e "${YELLOW}[Step 1/6] Stopping and removing all containers...${NC}"

# First, remove any manually created containers that might conflict
echo "  Removing any existing icao-local-pkd-* containers..."
docker ps -a --filter "name=icao-local-pkd-" --format "{{.Names}}" | xargs -r docker rm -f 2>/dev/null || true

# Then stop and remove compose-managed containers
docker compose -f docker/docker-compose.yaml $PROFILE_FLAG down -v 2>/dev/null || true

echo -e "${GREEN}✓ All containers stopped and removed${NC}"
echo ""

# =============================================================================
# Step 2: Remove all data (preserve SSL certificates)
# =============================================================================
echo -e "${YELLOW}[Step 2/6] Removing all data directories...${NC}"
if [ -d ".docker-data" ]; then
    # Preserve SSL certificates if they exist
    if [ -d ".docker-data/ssl" ] && [ -f ".docker-data/ssl/server.crt" ]; then
        echo "  Preserving SSL certificates (.docker-data/ssl/)"
        sudo cp -a .docker-data/ssl /tmp/icao-ssl-backup
    fi

    echo "  Removing .docker-data/*"
    sudo rm -rf .docker-data/*

    # Restore SSL certificates
    if [ -d "/tmp/icao-ssl-backup" ]; then
        sudo mv /tmp/icao-ssl-backup .docker-data/ssl
        echo -e "${GREEN}✓ SSL certificates restored${NC}"
    fi

    echo -e "${GREEN}✓ Data directories removed${NC}"
else
    echo "  .docker-data does not exist, skipping"
fi
echo ""

# =============================================================================
# Step 3: Recreate data directories with proper permissions
# =============================================================================
echo -e "${YELLOW}[Step 3/6] Creating data directories with permissions...${NC}"
# Create directories matching docker-compose.yaml volume paths
mkdir -p .docker-data/postgres
mkdir -p .docker-data/openldap1/data
mkdir -p .docker-data/openldap1/config
mkdir -p .docker-data/openldap2/data
mkdir -p .docker-data/openldap2/config
mkdir -p .docker-data/pkd-uploads
mkdir -p .docker-data/pkd-logs
mkdir -p .docker-data/pa-logs
mkdir -p .docker-data/sync-logs
mkdir -p .docker-data/monitoring-logs
mkdir -p .docker-data/gateway-logs
mkdir -p .docker-data/ai-analysis-logs
mkdir -p .docker-data/nginx

# Oracle uses named volume (oracle-data), but remove it for clean init
if [ "$DB_TYPE" = "oracle" ]; then
    echo "  Removing Oracle named volume..."
    docker volume rm icao-local-pkd-oracle-data 2>/dev/null || true
fi

# Generate ICAO PKD LDAP TLS certificates using Private CA
# This ensures CSR-signed certificates are directly compatible with the simulation server
ICAO_TLS_DIR=".docker-data/icao-pkd-tls"
SSL_DIR=".docker-data/ssl"
mkdir -p "$ICAO_TLS_DIR"
echo "  Generating ICAO PKD LDAP TLS certificates..."

if [ -f "$SSL_DIR/ca.key" ] && [ -f "$SSL_DIR/ca.crt" ]; then
    # Use existing Private CA — consistent with CSR signing
    echo "  Using Private CA ($SSL_DIR/ca.crt)"
    cp "$SSL_DIR/ca.crt" "$ICAO_TLS_DIR/ca.pem"
    CA_KEY="$SSL_DIR/ca.key"
    CA_CERT="$SSL_DIR/ca.crt"
else
    # Fallback: generate standalone CA (no SSL cert available)
    echo "  Private CA not found, generating standalone CA"
    openssl req -x509 -nodes -newkey rsa:2048 -days 3650 \
        -keyout "$ICAO_TLS_DIR/ca.key" -out "$ICAO_TLS_DIR/ca.pem" \
        -subj "/CN=ICAO PKD Simulation CA/O=ICAO/C=CA" 2>/dev/null
    CA_KEY="$ICAO_TLS_DIR/ca.key"
    CA_CERT="$ICAO_TLS_DIR/ca.pem"
fi

# Server cert for icao-sim LDAP (signed by Private CA)
openssl req -nodes -newkey rsa:2048 \
    -keyout "$ICAO_TLS_DIR/ldap-server.key" -out "$ICAO_TLS_DIR/ldap-server.csr" \
    -subj "/CN=icao-pkd-ldap/O=ICAO PKD Simulation/C=CA" 2>/dev/null
openssl x509 -req -in "$ICAO_TLS_DIR/ldap-server.csr" \
    -CA "$CA_CERT" -CAkey "$CA_KEY" -CAcreateserial \
    -out "$ICAO_TLS_DIR/ldap-server.crt" -days 3650 \
    -extfile <(echo "subjectAltName=DNS:icao-pkd-ldap,DNS:localhost") 2>/dev/null

# Client cert for relay (signed by Private CA)
openssl req -nodes -newkey rsa:2048 \
    -keyout "$ICAO_TLS_DIR/client-key.pem" -out "$ICAO_TLS_DIR/client.csr" \
    -subj "/CN=pkd-relay-client/O=SmartCore PKD/C=KR" 2>/dev/null
openssl x509 -req -in "$ICAO_TLS_DIR/client.csr" \
    -CA "$CA_CERT" -CAkey "$CA_KEY" -CAcreateserial \
    -out "$ICAO_TLS_DIR/client.pem" -days 3650 2>/dev/null

rm -f "$ICAO_TLS_DIR"/*.csr "$ICAO_TLS_DIR"/*.srl "$SSL_DIR"/*.srl
echo -e "${GREEN}✓ ICAO TLS certificates generated (Private CA)${NC}"

# Set proper permissions (777 for all to avoid permission issues)
sudo chmod -R 777 .docker-data/
echo -e "${GREEN}✓ Data directories created and permissions set${NC}"
echo ""

# =============================================================================
# Step 4: Start infrastructure services (Database, OpenLDAP)
# =============================================================================
echo -e "${YELLOW}[Step 4/6] Starting infrastructure services...${NC}"

# Start DB + LDAP (profile selects the correct database)
docker compose -f docker/docker-compose.yaml $PROFILE_FLAG up -d openldap1 openldap2

# Wait for database based on DB_TYPE
if [ "$DB_TYPE" = "oracle" ]; then
    # Start Oracle explicitly (profiled service)
    docker compose -f docker/docker-compose.yaml $PROFILE_FLAG up -d oracle

    echo -n "  Waiting for Oracle (this may take 3-5 minutes on first run)"
    for i in {1..180}; do
        ORACLE_HEALTH=$(docker inspect icao-local-pkd-oracle --format='{{.State.Health.Status}}' 2>/dev/null || echo "not-found")
        if [ "$ORACLE_HEALTH" = "healthy" ]; then
            echo ""
            echo -e "${GREEN}✓ Oracle container is healthy${NC}"
            # Wait for init scripts to complete (pkd_user must be accessible)
            echo -n "  Waiting for Oracle init scripts (pkd_user)"
            ORACLE_PWD=$(grep -E '^ORACLE_PASSWORD=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'')
            ORACLE_PWD="${ORACLE_PWD:-pkd_password}"
            for j in {1..30}; do
                INIT_CHECK=$(docker exec icao-local-pkd-oracle bash -c "echo 'SELECT 1 FROM DUAL;' | sqlplus -s pkd_user/${ORACLE_PWD}@//localhost:1521/XEPDB1 2>/dev/null | grep -c '1'" 2>/dev/null || echo "0")
                if [ "$INIT_CHECK" -ge 1 ] 2>/dev/null; then
                    echo ""
                    echo -e "${GREEN}✓ Oracle pkd_user is ready${NC}"
                    # Wait for all init scripts to complete (check last table in sequence)
                    echo -n "  Waiting for Oracle init scripts to complete"
                    for k in {1..60}; do
                        TABLE_COUNT=$(docker exec icao-local-pkd-oracle bash -c "echo \"SELECT COUNT(*) FROM user_tables WHERE table_name IN ('PENDING_DSC_REGISTRATION','CVC_CERTIFICATE');\" | sqlplus -s pkd_user/${ORACLE_PWD}@//localhost:1521/XEPDB1 2>/dev/null | grep -E '^\s*[0-9]+' | tr -d ' '" 2>/dev/null || echo "0")
                        if [ "$TABLE_COUNT" -ge 2 ] 2>/dev/null; then
                            echo ""
                            echo -e "${GREEN}✓ Oracle init scripts completed (all tables created)${NC}"
                            break
                        fi
                        echo -n "."
                        sleep 2
                        if [ $k -eq 60 ]; then
                            echo ""
                            echo -e "${YELLOW}⚠ Some Oracle init scripts may not have completed, running manually...${NC}"
                            docker exec icao-local-pkd-oracle bash -c "sqlplus -s pkd_user/${ORACLE_PWD}@//localhost:1521/XEPDB1 @/opt/oracle/scripts/startup/03-core-schema.sql" 2>/dev/null || true
                            docker exec icao-local-pkd-oracle bash -c "sqlplus -s pkd_user/${ORACLE_PWD}@//localhost:1521/XEPDB1 @/opt/oracle/scripts/startup/20-eac-schema.sql" 2>/dev/null || true
                            echo -e "${GREEN}✓ Oracle migration scripts executed${NC}"
                        fi
                    done
                    break 2  # break both loops
                fi
                echo -n "."
                sleep 3
                if [ $j -eq 30 ]; then
                    echo ""
                    echo -e "${RED}✗ Oracle init scripts failed (pkd_user not accessible)${NC}"
                    docker logs icao-local-pkd-oracle 2>&1 | grep -E "(ERROR|error|01-create)" | tail -10
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
            echo -e "${RED}✗ Oracle failed to start (timeout after 360s)${NC}"
            echo "  Oracle container status: $ORACLE_HEALTH"
            docker logs icao-local-pkd-oracle 2>&1 | tail -20
            exit 1
        fi
    done
else
    # Start PostgreSQL explicitly (profiled service)
    docker compose -f docker/docker-compose.yaml $PROFILE_FLAG up -d postgres

    echo -n "  Waiting for PostgreSQL"
    for i in {1..30}; do
        if docker exec icao-local-pkd-postgres pg_isready -U pkd -d localpkd > /dev/null 2>&1; then
            echo ""
            echo -e "${GREEN}✓ PostgreSQL is ready${NC}"
            break
        fi
        echo -n "."
        sleep 1
        if [ $i -eq 30 ]; then
            echo ""
            echo -e "${RED}✗ PostgreSQL failed to start${NC}"
            exit 1
        fi
    done
fi

# Wait for OpenLDAP
echo -n "  Waiting for OpenLDAP1"
for i in {1..30}; do
    if docker exec icao-local-pkd-openldap1 ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
        echo ""
        echo -e "${GREEN}✓ OpenLDAP1 is ready${NC}"
        break
    fi
    echo -n "."
    sleep 1
    if [ $i -eq 30 ]; then
        echo ""
        echo -e "${RED}✗ OpenLDAP1 failed to start${NC}"
        exit 1
    fi
done

echo -n "  Waiting for OpenLDAP2"
for i in {1..30}; do
    if docker exec icao-local-pkd-openldap2 ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
        echo ""
        echo -e "${GREEN}✓ OpenLDAP2 is ready${NC}"
        break
    fi
    echo -n "."
    sleep 1
    if [ $i -eq 30 ]; then
        echo ""
        echo -e "${RED}✗ OpenLDAP2 failed to start${NC}"
        exit 1
    fi
done
echo ""

# =============================================================================
# Step 5: Initialize LDAP DIT structure
# =============================================================================
echo -e "${YELLOW}[Step 5/6] Initializing LDAP DIT structure...${NC}"

# Start MMR setup containers
docker compose -f docker/docker-compose.yaml up -d ldap-mmr-setup1 ldap-mmr-setup2

# Wait for MMR setup to complete
echo -n "  Waiting for MMR setup"
for i in {1..60}; do
    STATUS1=$(docker inspect icao-local-pkd-ldap-mmr-setup1 --format='{{.State.Status}}' 2>/dev/null || echo "not-found")
    STATUS2=$(docker inspect icao-local-pkd-ldap-mmr-setup2 --format='{{.State.Status}}' 2>/dev/null || echo "not-found")

    if [ "$STATUS1" = "exited" ] && [ "$STATUS2" = "exited" ]; then
        echo ""
        echo -e "${GREEN}✓ MMR setup completed${NC}"
        break
    fi
    echo -n "."
    sleep 1
    if [ $i -eq 60 ]; then
        echo ""
        echo -e "${RED}✗ MMR setup timed out${NC}"
        echo "MMR Setup 1 status: $STATUS1"
        echo "MMR Setup 2 status: $STATUS2"
        docker logs icao-local-pkd-ldap-mmr-setup1 2>&1 | tail -20
        docker logs icao-local-pkd-ldap-mmr-setup2 2>&1 | tail -20
        exit 1
    fi
done

# Start ldap-init container
docker compose -f docker/docker-compose.yaml up -d ldap-init

# Wait for ldap-init to complete
echo -n "  Waiting for LDAP DIT initialization"
for i in {1..60}; do
    STATUS=$(docker inspect icao-local-pkd-ldap-init --format='{{.State.Status}}' 2>/dev/null || echo "not-found")

    if [ "$STATUS" = "exited" ]; then
        EXIT_CODE=$(docker inspect icao-local-pkd-ldap-init --format='{{.State.ExitCode}}' 2>/dev/null || echo "255")
        if [ "$EXIT_CODE" = "0" ]; then
            echo ""
            echo -e "${GREEN}✓ LDAP DIT initialized successfully${NC}"
            break
        else
            echo ""
            echo -e "${RED}✗ LDAP DIT initialization failed (exit code: $EXIT_CODE)${NC}"
            docker logs icao-local-pkd-ldap-init 2>&1 | tail -30
            exit 1
        fi
    fi
    echo -n "."
    sleep 1
    if [ $i -eq 60 ]; then
        echo ""
        echo -e "${RED}✗ LDAP DIT initialization timed out${NC}"
        docker logs icao-local-pkd-ldap-init 2>&1 | tail -30
        exit 1
    fi
done

# Verify LDAP structure
echo "  Verifying LDAP structure..."

# Check ldap-init logs for verification
LDAP_INIT_SUCCESS=$(docker logs icao-local-pkd-ldap-init 2>&1 | grep -c "PKD DIT structure initialized successfully" || echo "0")

if [ "$LDAP_INIT_SUCCESS" -ge 1 ]; then
    echo -e "${GREEN}✓ LDAP structure verified (initialization successful)${NC}"

    # Show the initialized entries
    echo "  LDAP DIT entries:"
    docker logs icao-local-pkd-ldap-init 2>&1 | grep "^dn: dc=" | sed 's/^/    /'
else
    echo -e "${RED}✗ LDAP DIT initialization failed${NC}"
    echo "  ldap-init logs:"
    docker logs icao-local-pkd-ldap-init 2>&1 | tail -20
    exit 1
fi
echo ""

# =============================================================================
# Step 6: Start application services
# =============================================================================
echo -e "${YELLOW}[Step 6/6] Starting application services...${NC}"
docker compose -f docker/docker-compose.yaml $PROFILE_FLAG up -d

# Wait for services to be healthy
echo -n "  Waiting for services to be healthy"
for i in {1..60}; do
    HEALTHY_COUNT=$(docker compose -f docker/docker-compose.yaml ps --format json 2>/dev/null | \
        jq -r 'select(.Health == "healthy") | .Service' 2>/dev/null | wc -l || echo "0")

    if [ "$HEALTHY_COUNT" -ge 6 ]; then
        echo ""
        echo -e "${GREEN}✓ All services are healthy${NC}"
        break
    fi
    echo -n "."
    sleep 2
    if [ $i -eq 60 ]; then
        echo ""
        echo -e "${YELLOW}⚠ Some services may not be healthy yet${NC}"
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
docker compose -f docker/docker-compose.yaml $PROFILE_FLAG ps

echo ""
echo -e "${GREEN}✓ System is ready for use (DB_TYPE=$DB_TYPE)${NC}"
echo ""

# Detect SSL mode from .env
NGINX_CONF=$(grep -E '^NGINX_CONF=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'')
echo "Access URLs:"
if [[ "$NGINX_CONF" == *"ssl"* ]] && [ -f ".docker-data/ssl/server.crt" ]; then
    echo "  - Frontend: https://dev.pkd.smartcoreinc.com"
    echo "  - API Gateway: https://dev.pkd.smartcoreinc.com/api"
else
    echo "  - Frontend: http://localhost:13080"
    echo "  - API Gateway: http://localhost:18080/api"
fi
echo "  - API Documentation: http://localhost:18090"
if [ "$DB_TYPE" = "oracle" ]; then
    echo "  - Oracle: localhost:11521 (SYS/SYSTEM)"
fi
echo ""
