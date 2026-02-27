#!/bin/bash
# podman-stop.sh - Podman 컨테이너 중지 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

echo "  ICAO PKD Podman 컨테이너 중지..."

podman-compose -f docker/docker-compose.podman.yaml down 2>/dev/null || {
    echo "  compose down 실패 — 개별 컨테이너 강제 제거..."
    podman ps -a --filter "name=icao-local-pkd" --format "{{.Names}}" | \
        xargs -r podman rm -f 2>/dev/null || true
}

echo "  컨테이너 중지 완료!"
