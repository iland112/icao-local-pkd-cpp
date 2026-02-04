#!/bin/bash

# ==============================================================================
# View PKD Management Development Service Logs
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DOCKER_DIR="$PROJECT_ROOT/docker"

cd "$DOCKER_DIR"

echo "=== PKD Management Development Logs ==="
echo "Press Ctrl+C to exit"
echo ""

docker-compose -f docker-compose.dev.yaml logs -f pkd-management-dev
