#!/bin/bash

# =============================================================================
# Start PA Service Development Container
# =============================================================================
# Purpose: Start pa-service-dev container for Repository Pattern refactoring
# Branch: feature/pa-service-repository-pattern
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DOCKER_DIR="$PROJECT_ROOT/docker"

cd "$DOCKER_DIR"

echo "=========================================="
echo "Starting PA Service Development Container"
echo "=========================================="
echo ""
echo "Branch: feature/pa-service-repository-pattern"
echo "Port: 8092 (dev) vs 8082 (production)"
echo "DB/LDAP: Shared with production"
echo ""

# Check if production services are running
if ! docker ps | grep -q "icao-local-pkd-postgres"; then
    echo "⚠️  WARNING: Production postgres not running!"
    echo "   Please start production services first:"
    echo "   cd docker && docker-compose up -d"
    echo ""
    read -p "Continue anyway? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Build and start pa-service-dev
echo "Building pa-service-dev..."
docker-compose -f docker-compose.dev.yml build pa-service-dev

echo ""
echo "Starting pa-service-dev..."
docker-compose -f docker-compose.dev.yml up -d pa-service-dev

echo ""
echo "Waiting for service to be healthy..."
sleep 5

# Health check
if curl -s http://localhost:8092/api/health > /dev/null 2>&1; then
    echo "✅ PA Service Dev is running!"
    echo ""
    echo "Access points:"
    echo "  - Health Check: http://localhost:8092/api/health"
    echo "  - PA Verify:    http://localhost:8092/api/pa/verify"
    echo "  - API Docs:     http://localhost:8092/api/docs"
    echo ""
    echo "Logs:"
    echo "  docker-compose -f docker-compose.dev.yml logs -f pa-service-dev"
    echo ""
    echo "Rebuild:"
    echo "  ./scripts/dev/rebuild-pa-dev.sh"
else
    echo "⚠️  Service started but health check failed"
    echo "   Check logs: docker-compose -f docker-compose.dev.yml logs pa-service-dev"
fi

echo ""
echo "Done!"
