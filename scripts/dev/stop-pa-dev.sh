#!/bin/bash

# =============================================================================
# Stop PA Service Development Container
# =============================================================================
# Purpose: Stop pa-service-dev container
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DOCKER_DIR="$PROJECT_ROOT/docker"

cd "$DOCKER_DIR"

echo "=========================================="
echo "Stopping PA Service Development Container"
echo "=========================================="
echo ""

docker-compose -f docker-compose.dev.yml down

echo ""
echo "✅ PA Service Dev stopped!"
echo ""
echo "Production services are still running."
echo "To stop all services: cd docker && docker-compose down"
