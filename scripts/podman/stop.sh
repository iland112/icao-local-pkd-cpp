#!/bin/bash
# podman-stop.sh - Podman 컨테이너 중지 스크립트

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

echo "  ICAO PKD Podman 컨테이너 중지... (DB_TYPE=$DB_TYPE)"

podman-compose -f docker/docker-compose.podman.yaml $PROFILE_FLAG down 2>/dev/null || {
    echo "  compose down 실패 — 개별 컨테이너 강제 제거..."
    podman ps -a --filter "name=icao-local-pkd" --format "{{.Names}}" | \
        xargs -r podman rm -f 2>/dev/null || true
}

echo "  컨테이너 중지 완료!"
