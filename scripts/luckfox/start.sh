#!/bin/bash
# luckfox-start.sh - Luckfox Docker 컨테이너 시작 스크립트
# Updated: 2026-02-12

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Find project root (where docker-compose-luckfox.yaml lives)
if [ -f "$SCRIPT_DIR/docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$SCRIPT_DIR"
elif [ -f "$SCRIPT_DIR/../../docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
else
    echo "Error: docker-compose-luckfox.yaml not found"
    exit 1
fi
cd "$PROJECT_DIR"

echo "=== ICAO PKD Docker Start (Luckfox) ==="
echo ""

# 1. 필요한 디렉토리 생성
echo "[1/3] Creating directories..."
mkdir -p ./.docker-data/postgres
mkdir -p ./.docker-data/pkd-uploads
mkdir -p ./.docker-data/pkd-logs
mkdir -p ./.docker-data/pa-logs
mkdir -p ./.docker-data/sync-logs
chmod 777 ./.docker-data/postgres ./.docker-data/pkd-uploads 2>/dev/null || true

# 2. Docker Compose 시작
echo "[2/3] Starting Docker Compose..."
docker compose -f docker-compose-luckfox.yaml up -d

# 3. 컨테이너 상태 확인
echo ""
echo "[3/3] Waiting for services..."
sleep 5

echo ""
docker compose -f docker-compose-luckfox.yaml ps
echo ""

echo "=== Access Info ==="
echo "  PostgreSQL:      127.0.0.1:5432 (localpkd/pkd/pkd)"
echo "  Frontend:        http://192.168.100.11/"
echo "  API Gateway:     http://192.168.100.11:8080/api"
echo "  PKD Management:  http://127.0.0.1:8081"
echo "  PA Service:      http://127.0.0.1:8082"
echo "  PKD Relay:       http://127.0.0.1:8083"
echo "  Swagger UI:      http://192.168.100.11:8888/api-docs/"
echo ""
