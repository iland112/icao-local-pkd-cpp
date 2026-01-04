#!/bin/bash
#
# Luckfox ICAO Local PKD - Start Services
# Usage: ./luckfox-start.sh [service...]
#

set -e

COMPOSE_FILE="/home/luckfox/icao-local-pkd-cpp-v2/docker-compose-luckfox.yaml"
cd /home/luckfox/icao-local-pkd-cpp-v2

echo "=== ICAO Local PKD - Starting Services ==="

if [ $# -eq 0 ]; then
    echo "Starting all services..."
    docker compose -f $COMPOSE_FILE up -d
else
    echo "Starting services: $@"
    docker compose -f $COMPOSE_FILE up -d "$@"
fi

echo ""
echo "=== Service Status ==="
docker compose -f $COMPOSE_FILE ps

echo ""
echo "=== Access URLs ==="
echo "Frontend:    http://192.168.100.11:3000"
echo "PKD API:     http://192.168.100.11:8081/api"
echo "PA API:      http://192.168.100.11:8082/api"
echo "Sync API:    http://192.168.100.11:8083/api"
