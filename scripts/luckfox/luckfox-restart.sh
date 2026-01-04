#!/bin/bash
#
# Luckfox ICAO Local PKD - Restart Services
# Usage: ./luckfox-restart.sh [jvm|cpp] [service...]
#

set -e

# Source common configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/luckfox-common.sh"

# Check for help
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    print_usage "luckfox-restart.sh" "[service...]"
    exit 0
fi

# Parse version and get remaining args
REMAINING_ARGS=$(parse_version "$@")

COMPOSE_FILE=$(get_compose_file)
PROJECT_DIR=$(get_project_dir)

echo "=== ICAO Local PKD - Restarting Services ==="
print_version_info
echo ""

cd "$PROJECT_DIR"

if [ -z "$REMAINING_ARGS" ]; then
    echo "Restarting all services..."
    docker compose -f "$COMPOSE_FILE" restart
else
    echo "Restarting services: $REMAINING_ARGS"
    docker compose -f "$COMPOSE_FILE" restart $REMAINING_ARGS
fi

echo ""
echo "=== Service Status ==="
docker compose -f "$COMPOSE_FILE" ps

print_access_urls
