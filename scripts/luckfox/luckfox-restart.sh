#!/bin/bash
#
# Luckfox ICAO Local PKD - Restart Services
# Usage: ./luckfox-restart.sh [service...]
#

set -e

COMPOSE_FILE="/home/luckfox/icao-local-pkd-cpp-v2/docker-compose-luckfox.yaml"
cd /home/luckfox/icao-local-pkd-cpp-v2

echo "=== ICAO Local PKD - Restarting Services ==="

if [ $# -eq 0 ]; then
    echo "Restarting all services..."
    docker compose -f $COMPOSE_FILE restart
else
    echo "Restarting services: $@"
    docker compose -f $COMPOSE_FILE restart "$@"
fi

echo ""
echo "=== Service Status ==="
docker compose -f $COMPOSE_FILE ps
