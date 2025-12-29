#!/bin/bash
# =============================================================================
# ICAO Local PKD - Docker Start Script
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DOCKER_DIR="$PROJECT_DIR/docker"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== ICAO Local PKD Docker Start ===${NC}"
echo ""

# Parse arguments
BUILD=false
SKIP_APP=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --build)
            BUILD=true
            shift
            ;;
        --skip-app)
            SKIP_APP=true
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

cd "$DOCKER_DIR"

# Build if requested
if [ "$BUILD" = true ]; then
    echo -e "${YELLOW}Building Docker images...${NC}"
    docker compose build
fi

# Start services
if [ "$SKIP_APP" = true ]; then
    echo -e "${GREEN}Starting infrastructure services (without app)...${NC}"
    docker compose up -d postgres pgadmin haproxy openldap1 openldap2 phpldapadmin
else
    echo -e "${GREEN}Starting all services...${NC}"
    docker compose up -d
fi

echo ""
echo -e "${GREEN}Services started!${NC}"
echo ""
echo "Access points:"
echo "  - Application:    http://localhost:8081"
echo "  - pgAdmin:        http://localhost:5050"
echo "  - phpLDAPadmin:   http://localhost:8080"
echo "  - HAProxy Stats:  http://localhost:8404/stats"
echo ""
echo "LDAP connections:"
echo "  - HAProxy (Read): ldap://localhost:389"
echo "  - OpenLDAP 1:     ldap://localhost:3891"
echo "  - OpenLDAP 2:     ldap://localhost:3892"
echo ""
echo "Database:"
echo "  - PostgreSQL:     localhost:5432"
echo "    User: pkd, Password: pkd, Database: localpkd"
