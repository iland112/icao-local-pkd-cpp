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
    # 여러 서비스를 인자로 받을 수 있음
    SERVICES="$@"
    echo "   서비스: $SERVICES"

    # up -d --force-recreate: 새 이미지 반영을 위해 컨테이너 재생성
    # (compose restart는 기존 컨테이너만 재시작하여 이미지 변경이 반영되지 않음)
    docker compose -f docker/docker-compose.yaml $PROFILE_FLAG up -d --force-recreate $SERVICES

    echo ""
    echo "📊 컨테이너 상태:"
    docker compose -f docker/docker-compose.yaml ps

    echo ""
    echo "✅ 컨테이너 재시작 완료!"
fi
