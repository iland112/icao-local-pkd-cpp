#!/bin/bash
#
# Luckfox ICAO Local PKD - Update Service from Image File
# Usage: ./luckfox-update.sh <image_file.tar.gz> <service_name>
#
# Example:
#   ./luckfox-update.sh icao-frontend-arm64.tar.gz frontend
#

set -e

if [ $# -lt 2 ]; then
    echo "Usage: ./luckfox-update.sh <image_file.tar.gz> <service_name>"
    echo ""
    echo "Services:"
    echo "  frontend       - React frontend"
    echo "  pkd-management - PKD Management service"
    echo "  pa-service     - PA Verification service"
    echo "  sync-service   - DB-LDAP Sync service"
    echo ""
    echo "Example:"
    echo "  ./luckfox-update.sh /home/luckfox/icao-frontend-arm64.tar.gz frontend"
    exit 1
fi

IMAGE_FILE="$1"
SERVICE_NAME="$2"
COMPOSE_FILE="/home/luckfox/icao-local-pkd-cpp-v2/docker-compose-luckfox.yaml"

if [ ! -f "$IMAGE_FILE" ]; then
    echo "Error: Image file not found: $IMAGE_FILE"
    exit 1
fi

echo "=== ICAO Local PKD - Update Service ==="
echo "Image file: $IMAGE_FILE"
echo "Service: $SERVICE_NAME"
echo ""

# Load new image
echo "=== Loading Docker Image ==="
docker load < "$IMAGE_FILE"

# Restart service
echo ""
echo "=== Restarting Service: $SERVICE_NAME ==="
cd /home/luckfox/icao-local-pkd-cpp-v2
docker compose -f $COMPOSE_FILE stop "$SERVICE_NAME"
docker compose -f $COMPOSE_FILE up -d "$SERVICE_NAME"

# Show status
echo ""
echo "=== Service Status ==="
docker compose -f $COMPOSE_FILE ps "$SERVICE_NAME"

echo ""
echo "=== Update Complete ==="
