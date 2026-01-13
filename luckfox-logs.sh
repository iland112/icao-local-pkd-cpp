#!/bin/bash
# luckfox-logs.sh - Luckfox Docker 컨테이너 로그 확인 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SERVICE=$1
LINES=${2:-50}

if [ -z "$SERVICE" ]; then
    echo "📋 모든 컨테이너 로그 (최근 ${LINES}줄)..."
    echo ""
    docker compose -f docker-compose-luckfox.yaml logs --tail=$LINES
else
    echo "📋 '$SERVICE' 컨테이너 로그 (최근 ${LINES}줄)..."
    echo ""
    docker compose -f docker-compose-luckfox.yaml logs --tail=$LINES $SERVICE
fi

echo ""
echo "💡 실시간 로그: ./luckfox-logs.sh [서비스명] -f"
echo "   사용 가능한 서비스: postgres, pkd-management, pa-service, sync-service, api-gateway, frontend"
