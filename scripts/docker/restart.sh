#!/bin/bash
# docker-restart.sh - Docker 컨테이너 재시작 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

SERVICE=${1:-}

echo "🔄 ICAO PKD Docker 컨테이너 재시작..."

if [ -z "$SERVICE" ]; then
    docker compose -f docker/docker-compose.yaml restart
else
    echo "   서비스: $SERVICE"
    docker compose -f docker/docker-compose.yaml restart $SERVICE
fi

echo ""
echo "📊 컨테이너 상태:"
docker compose -f docker/docker-compose.yaml ps

echo ""
echo "✅ 컨테이너 재시작 완료!"
