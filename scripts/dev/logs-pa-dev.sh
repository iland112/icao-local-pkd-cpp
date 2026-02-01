#!/bin/bash

# =============================================================================
# View PA Service Development Logs
# =============================================================================
# Purpose: Tail logs for pa-service-dev container
# Usage: ./scripts/dev/logs-pa-dev.sh [lines]
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DOCKER_DIR="$PROJECT_ROOT/docker"

cd "$DOCKER_DIR"

LINES="${1:-100}"

echo "=========================================="
echo "PA Service Development Logs (last $LINES lines)"
echo "=========================================="
echo ""

docker-compose -f docker-compose.dev.yml logs --tail="$LINES" -f pa-service-dev
