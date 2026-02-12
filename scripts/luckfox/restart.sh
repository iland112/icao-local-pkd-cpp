#!/bin/bash
# luckfox-restart.sh - Luckfox Docker 컨테이너 재시작 스크립트

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$SCRIPT_DIR"
elif [ -f "$SCRIPT_DIR/../../docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
else
    echo "Error: docker-compose-luckfox.yaml not found"; exit 1
fi
cd "$PROJECT_DIR"

SERVICE=$1

if [ -z "$SERVICE" ]; then
    echo "=== Restarting all containers ==="
    docker compose -f docker-compose-luckfox.yaml restart
else
    echo "=== Restarting '$SERVICE' ==="
    docker compose -f docker-compose-luckfox.yaml restart $SERVICE
fi

echo ""
docker compose -f docker-compose-luckfox.yaml ps
