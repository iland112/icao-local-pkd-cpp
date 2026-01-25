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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
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
# Step 1: Stop and remove all containers
# =============================================================================
echo -e "${YELLOW}[Step 1/6] Stopping and removing all containers...${NC}"
docker compose -f docker/docker-compose.yaml down 2>/dev/null || true
echo -e "${GREEN}✓ Containers stopped and removed${NC}"
echo ""

# =============================================================================
# Step 2: Remove all data
# =============================================================================
echo -e "${YELLOW}[Step 2/6] Removing all data directories...${NC}"
if [ -d ".docker-data" ]; then
    echo "  Removing .docker-data/*"
    sudo rm -rf .docker-data/*
    echo -e "${GREEN}✓ Data directories removed${NC}"
else
    echo "  .docker-data does not exist, skipping"
fi
echo ""

# =============================================================================
# Step 3: Recreate data directories with proper permissions
# =============================================================================
echo -e "${YELLOW}[Step 3/6] Creating data directories with permissions...${NC}"
mkdir -p .docker-data/postgres-data
mkdir -p .docker-data/openldap1-data
mkdir -p .docker-data/openldap2-data
mkdir -p .docker-data/pkd-uploads
mkdir -p .docker-data/pkd-logs
mkdir -p .docker-data/pa-logs
mkdir -p .docker-data/sync-logs

sudo chmod -R 777 .docker-data/
echo -e "${GREEN}✓ Data directories created and permissions set${NC}"
echo ""

# =============================================================================
# Step 4: Start infrastructure services (PostgreSQL, OpenLDAP)
# =============================================================================
echo -e "${YELLOW}[Step 4/6] Starting infrastructure services...${NC}"
docker compose -f docker/docker-compose.yaml up -d postgres openldap1 openldap2

# Wait for PostgreSQL
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
docker compose -f docker/docker-compose.yaml up -d

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
docker compose -f docker/docker-compose.yaml ps

echo ""
echo -e "${GREEN}✓ System is ready for use${NC}"
echo ""
echo "Access URLs:"
echo "  - Frontend: http://localhost:3000"
echo "  - API Gateway: http://localhost:8080/api"
echo "  - API Documentation: http://localhost:8090"
echo ""
