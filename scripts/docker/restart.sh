#!/bin/bash
# docker-restart.sh - Docker 컨테이너 재시작 스크립트

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

# Load shared library
RUNTIME="docker"
source "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh"

# Read DB_TYPE from .env
parse_db_type "postgres"

SERVICE=${1:-}

echo "🔄 ICAO PKD Docker 컨테이너 재시작... (DB_TYPE=$DB_TYPE)"

if [ -z "$SERVICE" ]; then
    # 전체 재시작: stop + start (의존성 순서 보장)
    echo "  전체 재시작: stop -> start..."
    bash "$SCRIPT_DIR/scripts/docker/stop.sh"
    echo ""
    bash "$SCRIPT_DIR/scripts/docker/start.sh"
else
    echo "   서비스: $SERVICE"
    docker compose -f docker/docker-compose.yaml $PROFILE_FLAG restart $SERVICE

    echo ""
    echo "📊 컨테이너 상태:"
    docker compose -f docker/docker-compose.yaml ps

    echo ""
    echo "✅ 컨테이너 재시작 완료!"
fi
