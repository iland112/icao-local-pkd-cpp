#!/bin/bash
#
# Luckfox ICAO Local PKD - Update Service from Image File
# Usage: ./luckfox-update.sh [jvm|cpp] <image_file.tar.gz> <service_name>
#
# Example:
#   ./luckfox-update.sh cpp icao-frontend-arm64.tar.gz frontend
#   ./luckfox-update.sh jvm icao-backend-arm64.tar.gz backend
#

set -e

# Source common configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/luckfox-common.sh"

# Check for help
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    print_usage "luckfox-update.sh" "<image_file.tar.gz> <service_name>"
    echo ""
    echo "CPP Services:"
    echo "  frontend       - React frontend"
    echo "  pkd-management - PKD Management service"
    echo "  pa-service     - PA Verification service"
    echo "  sync-service   - DB-LDAP Sync service"
    echo ""
    echo "JVM Services:"
    echo "  frontend       - React frontend"
    echo "  backend        - Spring Boot backend"
    echo ""
    echo "Example:"
    echo "  ./luckfox-update.sh cpp /home/luckfox/icao-frontend-arm64.tar.gz frontend"
    echo "  ./luckfox-update.sh jvm /home/luckfox/icao-backend-arm64.tar.gz backend"
    exit 0
fi

# Parse version and get remaining args
REMAINING_ARGS=$(parse_version "$@")
set -- $REMAINING_ARGS

if [ $# -lt 2 ]; then
    echo "=== ICAO Local PKD - Update Service ==="
    print_version_info
    echo ""
    echo "Usage: ./luckfox-update.sh [jvm|cpp] <image_file.tar.gz> <service_name>"
    echo ""
    echo "Use -h for help"
    exit 1
fi

IMAGE_FILE="$1"
SERVICE_NAME="$2"
COMPOSE_FILE=$(get_compose_file)
PROJECT_DIR=$(get_project_dir)

if [ ! -f "$IMAGE_FILE" ]; then
    echo "Error: Image file not found: $IMAGE_FILE"
    exit 1
fi

echo "=== ICAO Local PKD - Update Service ==="
print_version_info
echo "Image file: $IMAGE_FILE"
echo "Service: $SERVICE_NAME"
echo ""

# Load new image
echo "=== Loading Docker Image ==="
docker load < "$IMAGE_FILE"

# Restart service
echo ""
echo "=== Restarting Service: $SERVICE_NAME ==="
cd "$PROJECT_DIR"
docker compose -f "$COMPOSE_FILE" stop "$SERVICE_NAME"
docker compose -f "$COMPOSE_FILE" up -d "$SERVICE_NAME"

# Show status
echo ""
echo "=== Service Status ==="
docker compose -f "$COMPOSE_FILE" ps "$SERVICE_NAME"

echo ""
echo "=== Update Complete ==="
