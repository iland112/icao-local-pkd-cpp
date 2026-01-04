#!/bin/bash
#
# Luckfox ICAO Local PKD - Start Services
# Usage: ./luckfox-start.sh [jvm|cpp] [service...]
#

set -e

# Source common configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/luckfox-common.sh"

# Check for help
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    print_usage "luckfox-start.sh" "[service...]"
    exit 0
fi

# Parse version and get remaining args
REMAINING_ARGS=$(parse_version "$@")

COMPOSE_FILE=$(get_compose_file)
PROJECT_DIR=$(get_project_dir)

echo "=== ICAO Local PKD - Starting Services ==="
print_version_info
echo ""

cd "$PROJECT_DIR"

if [ -z "$REMAINING_ARGS" ]; then
    echo "Starting all services..."
    docker compose -f "$COMPOSE_FILE" up -d
else
    echo "Starting services: $REMAINING_ARGS"
    docker compose -f "$COMPOSE_FILE" up -d $REMAINING_ARGS
fi

echo ""
echo "=== Service Status ==="
docker compose -f "$COMPOSE_FILE" ps

print_access_urls
