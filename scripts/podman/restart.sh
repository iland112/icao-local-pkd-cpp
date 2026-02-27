#!/bin/bash
# podman-restart.sh - Podman 컨테이너 재시작 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

# Read DB_TYPE from .env
DB_TYPE=$(grep -E '^DB_TYPE=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'')
DB_TYPE="${DB_TYPE:-oracle}"

if [ "$DB_TYPE" = "oracle" ]; then
    PROFILE_FLAG="--profile oracle"
else
    PROFILE_FLAG="--profile postgres"
fi

COMPOSE="podman-compose -f docker/docker-compose.podman.yaml $PROFILE_FLAG"
SERVICE=${1:-}

echo "  ICAO PKD Podman 컨테이너 재시작... (DB_TYPE=$DB_TYPE)"

if [ -z "$SERVICE" ]; then
    # 전체 재시작: compose restart는 의존성 순서 문제 발생 → stop + start
    echo "  전체 재시작: stop → start..."
    bash "$SCRIPT_DIR/scripts/podman/stop.sh"
    echo ""
    bash "$SCRIPT_DIR/scripts/podman/start.sh"
else
    echo "   서비스: $SERVICE"
    $COMPOSE restart $SERVICE

    echo ""
    echo "  컨테이너 상태:"
    podman ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}" \
        --filter "name=icao-local-pkd" 2>/dev/null || $COMPOSE ps

    echo ""
    echo "  컨테이너 재시작 완료!"
fi
