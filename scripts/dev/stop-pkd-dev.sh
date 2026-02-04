#!/bin/bash

# ==============================================================================
# Stop PKD Management Development Service
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DOCKER_DIR="$PROJECT_ROOT/docker"

cd "$DOCKER_DIR"

echo "=== Stopping PKD Management Development Service ==="
docker-compose -f docker-compose.dev.yaml down

echo ""
echo "✅ Development service stopped"
echo ""
