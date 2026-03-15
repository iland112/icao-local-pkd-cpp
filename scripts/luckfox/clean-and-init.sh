#!/bin/bash
# luckfox-clean-and-init.sh - Luckfox 완전 초기화 + LDAP DIT 재구성
# WARNING: Deletes ALL containers, DB data, and LDAP data!
# Note: Luckfox uses host slapd (systemd), not Docker OpenLDAP containers.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$SCRIPT_DIR"
elif [ -f "$SCRIPT_DIR/../../docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
else
    echo "Error: docker-compose-luckfox.yaml not found"; exit 1
fi
cd "$PROJECT_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=============================================${NC}"
echo -e "${BLUE}ICAO Local PKD - Luckfox Clean & Init${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""

# LDAP admin credentials from .env
LDAP_BIND_DN="cn=admin,dc=ldap,dc=smartcoreinc,dc=com"
LDAP_BASE_DN_ROOT="dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"
LDAP_DOWNLOAD_DN="dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"
LDAP_BIND_PASSWORD=$(grep -E '^LDAP_BIND_PASSWORD=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'' || echo "core")

# --force flag skips confirmation
FORCE=false
for arg in "$@"; do
    if [ "$arg" = "--force" ] || [ "$arg" = "-f" ]; then
        FORCE=true
    fi
done

if [ "$FORCE" = false ]; then
    echo -e "${RED}WARNING: This will delete ALL data!${NC}"
    echo ""
    echo "  Data to be deleted:"
    echo "    - PostgreSQL database (.docker-data/postgres)"
    echo "    - LDAP data (host slapd, dc=pkd tree)"
    echo "    - Upload files (.docker-data/pkd-uploads)"
    echo "    - Service logs"
    echo ""
    read -p "Continue? (yes/no): " CONFIRM
    if [ "$CONFIRM" != "yes" ]; then
        echo "Cancelled."
        exit 0
    fi
fi

echo ""

# =============================================================================
# Step 1: Stop and remove containers
# =============================================================================
echo -e "${YELLOW}[Step 1/5] Stopping containers...${NC}"
docker compose -f docker-compose-luckfox.yaml down 2>/dev/null || true
echo -e "${GREEN}✓ Containers stopped${NC}"
echo ""

# =============================================================================
# Step 2: Clear PostgreSQL data (using docker alpine to bypass permission issues)
# =============================================================================
echo -e "${YELLOW}[Step 2/5] Clearing PostgreSQL data...${NC}"
docker run --rm \
    -v "${PROJECT_DIR}/.docker-data/postgres:/data" \
    alpine sh -c "rm -rf /data/* /data/.[!.]*" 2>/dev/null || true

# Clear other data directories
rm -rf .docker-data/pkd-uploads/* 2>/dev/null || true
rm -rf .docker-data/pkd-logs/* .docker-data/pa-logs/* \
       .docker-data/sync-logs/* .docker-data/ai-logs/* \
       .docker-data/monitoring-logs/* 2>/dev/null || true

echo -e "${GREEN}✓ Data directories cleared${NC}"
echo ""

# =============================================================================
# Step 3: Reset LDAP (host slapd)
# =============================================================================
echo -e "${YELLOW}[Step 3/5] Resetting LDAP DIT (host slapd)...${NC}"

# Check slapd is running
if ! ldapsearch -x -H ldap://localhost -b "" -s base "(objectClass=*)" > /dev/null 2>&1; then
    echo -e "${RED}✗ slapd is not running. Please start it first: sudo systemctl start slapd${NC}"
    exit 1
fi

# Delete existing dc=pkd tree (ignore error if not exists)
echo "  Deleting dc=pkd tree..."
ldapdelete -x -H ldap://localhost \
    -D "$LDAP_BIND_DN" -w "$LDAP_BIND_PASSWORD" \
    -r "$LDAP_BASE_DN_ROOT" 2>/dev/null || true

# Initialize DIT structure
echo "  Initializing DIT structure..."
ldapadd -x -H ldap://localhost \
    -D "$LDAP_BIND_DN" -w "$LDAP_BIND_PASSWORD" << 'LDIF'
dn: dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: top
objectClass: dcObject
objectClass: organization
dc: pkd
o: ICAO PKD

dn: dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: top
objectClass: dcObject
objectClass: organization
dc: download
o: PKD Download

dn: dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: top
objectClass: dcObject
objectClass: organization
dc: data
o: PKD Data

dn: dc=nc-data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: top
objectClass: dcObject
objectClass: organization
dc: nc-data
o: PKD Non-Compliant Data
LDIF

if [ $? -ne 0 ]; then
    echo -e "${RED}✗ LDAP DIT initialization failed${NC}"
    exit 1
fi

# Verify
ENTRY_COUNT=$(ldapsearch -x -H ldap://localhost \
    -b "$LDAP_DOWNLOAD_DN" "(objectClass=*)" dn 2>/dev/null | grep -c "^dn:" || echo "0")
echo -e "${GREEN}✓ LDAP DIT initialized ($ENTRY_COUNT entries under dc=download)${NC}"
echo ""

# =============================================================================
# Step 4: Start services
# =============================================================================
echo -e "${YELLOW}[Step 4/5] Starting services...${NC}"
bash "$SCRIPT_DIR/start.sh"
echo ""

# =============================================================================
# Step 5: Verify
# =============================================================================
echo -e "${YELLOW}[Step 5/5] Verifying system...${NC}"
sleep 5

# API health check
API_STATUS=$(curl -s http://localhost:8080/api/health 2>/dev/null | grep -c '"status":"UP"' || echo "0")
if [ "$API_STATUS" -ge 1 ]; then
    echo -e "${GREEN}✓ API Gateway is UP${NC}"
else
    echo -e "${RED}✗ API Gateway not responding${NC}"
fi

# DB check
DB_COUNT=$(docker exec icao-pkd-postgres \
    psql -U pkd -d localpkd -t -c "SELECT COUNT(*) FROM certificate;" 2>/dev/null | tr -d ' ' || echo "?")
echo -e "${GREEN}✓ DB certificates: ${DB_COUNT}${NC}"

# LDAP check
LDAP_COUNT=$(ldapsearch -x -H ldap://localhost \
    -b "$LDAP_DOWNLOAD_DN" "(objectClass=*)" dn 2>/dev/null | grep -c "^dn:" || echo "0")
echo -e "${GREEN}✓ LDAP entries: ${LDAP_COUNT}${NC}"

echo ""
echo -e "${BLUE}=============================================${NC}"
echo -e "${GREEN}Luckfox clean & init complete!${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""
echo "  Frontend:    http://192.168.100.11/"
echo "  API Gateway: http://192.168.100.11:8080/api"
echo ""
