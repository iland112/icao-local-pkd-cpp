#!/bin/bash
# podman-logs.sh - 로그 확인 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

SERVICE=${1:-}
LINES=${2:-100}

if [ -z "$SERVICE" ]; then
    echo "  전체 컨테이너 로그 (최근 ${LINES}줄):"
    echo "   (Ctrl+C로 종료)"
    echo ""
    podman-compose -f docker/docker-compose.podman.yaml logs -f --tail=$LINES
else
    echo "  $SERVICE 컨테이너 로그 (최근 ${LINES}줄):"
    echo "   (Ctrl+C로 종료)"
    echo ""
    podman-compose -f docker/docker-compose.podman.yaml logs -f --tail=$LINES $SERVICE
fi
