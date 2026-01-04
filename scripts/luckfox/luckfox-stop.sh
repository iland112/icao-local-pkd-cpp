#!/bin/bash
#
# Luckfox ICAO Local PKD - Stop Services
# Usage: ./luckfox-stop.sh [service...]
#

set -e

COMPOSE_FILE="/home/luckfox/icao-local-pkd-cpp-v2/docker-compose-luckfox.yaml"
cd /home/luckfox/icao-local-pkd-cpp-v2

echo "=== ICAO Local PKD - Stopping Services ==="

if [ $# -eq 0 ]; then
    echo "Stopping all services..."
    docker compose -f $COMPOSE_FILE down
else
    echo "Stopping services: $@"
    docker compose -f $COMPOSE_FILE stop "$@"
fi

echo ""
echo "=== Service Status ==="
docker compose -f $COMPOSE_FILE ps
