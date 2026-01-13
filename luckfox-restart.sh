#!/bin/bash
# luckfox-restart.sh - Luckfox Docker 컨테이너 재시작 스크립트

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SERVICE=$1

if [ -z "$SERVICE" ]; then
    echo "🔄 모든 컨테이너 재시작..."
    docker compose -f docker-compose-luckfox.yaml restart
    echo "✅ 재시작 완료!"
else
    echo "🔄 '$SERVICE' 컨테이너 재시작..."
    docker compose -f docker-compose-luckfox.yaml restart $SERVICE
    echo "✅ '$SERVICE' 재시작 완료!"
fi

echo ""
echo "📊 컨테이너 상태:"
docker compose -f docker-compose-luckfox.yaml ps
