#!/bin/bash
# luckfox-stop.sh - Luckfox Docker 컨테이너 중지 스크립트

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

echo "=== ICAO PKD Docker Stop (Luckfox) ==="
docker compose -f docker-compose-luckfox.yaml stop

echo ""
echo "Containers stopped. Data is preserved."
echo "Restart: ./luckfox-start.sh"
echo ""
