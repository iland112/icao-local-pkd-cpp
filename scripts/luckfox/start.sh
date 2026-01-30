#!/bin/bash
# luckfox-start.sh - Luckfox Docker 컨테이너 시작 스크립트
# Updated: 2026-01-13 - Luckfox 전용 버전

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 옵션 파싱
BUILD_FLAG=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --build)
            BUILD_FLAG="--build"
            shift
            ;;
        *)
            shift
            ;;
    esac
done

echo "🚀 ICAO PKD Docker 컨테이너 시작 (Luckfox)..."
echo ""

# 1. 필요한 디렉토리 생성
echo "📁 디렉토리 생성 중..."
mkdir -p ./.docker-data/postgres
mkdir -p ./.docker-data/pkd-uploads
chmod 777 ./.docker-data/postgres ./.docker-data/pkd-uploads 2>/dev/null || true

# 2. Docker Compose 시작
echo "🐳 Docker Compose 시작..."
docker compose -f docker-compose-luckfox.yaml up -d $BUILD_FLAG

# 3. 컨테이너 상태 확인
echo ""
echo "⏳ 컨테이너 시작 대기 중..."
sleep 5

echo ""
echo "📊 컨테이너 상태:"
docker compose -f docker-compose-luckfox.yaml ps

echo ""
echo "✅ 컨테이너 시작 완료!"

echo ""
echo "📌 접속 정보:"
echo "   - PostgreSQL:      127.0.0.1:5432 (localpkd/pkd/pkd)"
echo "   - Frontend:        http://192.168.100.11:3000"
echo "   - API Gateway:     http://192.168.100.11:8080/api"
echo "   - PKD Management:  http://127.0.0.1:8081"
echo "   - PA Service:      http://127.0.0.1:8082"
echo "   - Sync Service:    http://127.0.0.1:8083"
echo ""
echo "🔍 로그 확인: ./luckfox-logs.sh [서비스명]"
echo "🛑 중지:     ./luckfox-stop.sh"
echo "🔄 재시작:   ./luckfox-restart.sh"
echo "🧹 정리:     ./luckfox-clean.sh"
echo "❤️  헬스체크: ./luckfox-health.sh"
echo ""
echo "💡 옵션:"
echo "   --build      이미지 다시 빌드"
echo ""
