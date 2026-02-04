#!/bin/bash

# ==============================================================================
# Start PKD Management Development Service
# ==============================================================================
# Purpose: Start development version of pkd-management service
# Port: 8091 (production: 8081)
# Branch: feature/certificate-file-upload
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DOCKER_DIR="$PROJECT_ROOT/docker"

cd "$DOCKER_DIR"

echo "=== Starting PKD Management Development Service ==="
echo "Port: 8091"
echo "Environment: Development"
echo "Branch: feature/certificate-file-upload"
echo ""

# Check if production network exists
if ! docker network ls | grep -q pkd-network; then
    echo "❌ Production network 'pkd-network' not found"
    echo "Please start production services first: ./docker-start.sh"
    exit 1
fi

# Check if production postgres is running
if ! docker ps | grep -q icao-local-pkd-postgres; then
    echo "❌ Production postgres not running"
    echo "Please start production services first: ./docker-start.sh"
    exit 1
fi

# Check if production LDAP is running
if ! docker ps | grep -q openldap1; then
    echo "❌ Production LDAP not running"
    echo "Please start production services first: ./docker-start.sh"
    exit 1
fi

echo "✅ Production dependencies running"
echo ""

# Load .env file
if [ -f .env ]; then
    export $(cat .env | grep -v '^#' | xargs)
else
    echo "❌ .env file not found"
    exit 1
fi

# Start development service
echo "Starting pkd-management-dev..."
docker-compose -f docker-compose.dev.yaml up -d pkd-management-dev

echo ""
echo "=== Development Service Started ==="
echo "Service: pkd-management-dev"
echo "Port: 8091"
echo "Container: icao-pkd-management-dev"
echo ""
echo "Check logs: ./scripts/dev/logs-pkd-dev.sh"
echo "Check health: curl http://localhost:8091/api/health"
echo "Stop service: ./scripts/dev/stop-pkd-dev.sh"
echo ""
