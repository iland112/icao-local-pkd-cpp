#!/bin/bash
# podman-restart.sh - Podman 컨테이너 재시작 스크립트

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

# Load shared library
RUNTIME="podman"
source "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh"

COMPOSE_FILE="docker/docker-compose.podman.yaml"

# Read DB_TYPE from .env
parse_db_type "oracle"

COMPOSE="podman-compose -f $COMPOSE_FILE $PROFILE_FLAG"
SERVICE=${1:-}

echo "  ICAO PKD Podman 컨테이너 재시작... (DB_TYPE=$DB_TYPE)"

if [ -z "$SERVICE" ]; then
    # 전체 재시작: compose restart는 의존성 순서 문제 발생 → stop + start
    echo "  전체 재시작: stop → start..."
    bash "$SCRIPT_DIR/scripts/podman/stop.sh"
    echo ""
    bash "$SCRIPT_DIR/scripts/podman/start.sh"
else
    # 여러 서비스를 인자로 받을 수 있음
    SERVICES="$@"
    echo "   서비스: $SERVICES"

    # api-gateway 재시작 시 Podman DNS resolver 설정 자동 적용
    for svc in $SERVICES; do
        if [ "$svc" = "api-gateway" ]; then
            echo "  nginx 설정 생성 (Podman DNS resolver)..."
            generate_podman_nginx_conf
            break
        fi
    done

    # up -d --force-recreate: 새 이미지 반영을 위해 컨테이너 재생성
    # (compose restart는 기존 컨테이너만 재시작하여 이미지 변경이 반영되지 않음)
    for svc in $SERVICES; do
        echo "  $svc 중지 및 제거..."
        podman stop "icao-local-pkd-${svc}" 2>/dev/null || true
        podman rm "icao-local-pkd-${svc}" 2>/dev/null || true
    done
    $COMPOSE up -d $SERVICES

    echo ""
    echo "  컨테이너 상태:"
    podman ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}" \
        --filter "name=icao-local-pkd" 2>/dev/null || $COMPOSE ps

    echo ""
    echo "  컨테이너 재시작 완료!"
fi
