#!/bin/bash
# docker-stop.sh - Docker 컨테이너 중지 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

echo "🛑 ICAO PKD Docker 컨테이너 중지..."

docker compose -f docker/docker-compose.yaml down

echo "✅ 컨테이너 중지 완료!"
