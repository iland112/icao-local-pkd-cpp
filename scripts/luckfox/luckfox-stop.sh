#!/bin/bash
#
# Luckfox ICAO Local PKD - Stop Services
# Usage: ./luckfox-stop.sh [jvm|cpp] [service...]
#

set -e

# Source common configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/luckfox-common.sh"

# Check for help
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    print_usage "luckfox-stop.sh" "[service...]"
    exit 0
fi

# Parse version and get remaining args
REMAINING_ARGS=$(parse_version "$@")

COMPOSE_FILE=$(get_compose_file)
PROJECT_DIR=$(get_project_dir)

echo "=== ICAO Local PKD - Stopping Services ==="
print_version_info
echo ""

cd "$PROJECT_DIR"

if [ -z "$REMAINING_ARGS" ]; then
    echo "Stopping all services..."
    docker compose -f "$COMPOSE_FILE" down
else
    echo "Stopping services: $REMAINING_ARGS"
    docker compose -f "$COMPOSE_FILE" stop $REMAINING_ARGS
fi

echo ""
echo "=== Service Status ==="
docker compose -f "$COMPOSE_FILE" ps
