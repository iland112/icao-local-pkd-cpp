#!/bin/bash
# docker-clean.sh - 완전 삭제 스크립트 (PostgreSQL + OpenLDAP + 애플리케이션)
# Updated: 2026-01-01 - Changed from Docker volumes to bind mounts (.docker-data/)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "⚠️  경고: 모든 데이터가 삭제됩니다!"
echo "   - PostgreSQL 데이터 (업로드 이력, PA 이력 등)"
echo "   - OpenLDAP 데이터 (인증서, CRL, Master List)"
echo "   - .docker-data/ 디렉토리 (bind mount 데이터)"
echo ""
read -p "계속하시겠습니까? (yes/no): " confirm

if [ "$confirm" != "yes" ]; then
    echo "취소되었습니다."
    exit 0
fi

echo ""
echo "🗑️  컨테이너 중지 중..."
docker compose -f docker/docker-compose.yaml down --remove-orphans

echo ""
echo "⏳ 컨테이너 완전 중지 대기 중..."
sleep 3

echo ""
echo "🗄️  Bind mount 데이터 삭제 중 (.docker-data/)..."
if [ -d ".docker-data" ]; then
    rm -rf .docker-data
    echo "   ✓ .docker-data/ 디렉토리 삭제 완료"
else
    echo "   ℹ️  .docker-data/ 디렉토리가 존재하지 않습니다"
fi

echo ""
echo "🖼️  Docker 이미지 삭제 (선택)..."
read -p "Docker 이미지도 삭제하시겠습니까? (yes/no): " confirm_image

if [ "$confirm_image" == "yes" ]; then
    docker images | grep -E "icao-local-pkd|docker-" | awk '{print $3}' | xargs -r docker rmi -f 2>/dev/null || true
    echo "   ✓ Docker 이미지 정리 완료"
else
    echo "   건너뜁니다."
fi

echo ""
echo "🌐 네트워크 정리 중..."
docker network ls | grep "icao-local-pkd" | awk '{print $1}' | xargs -r docker network rm 2>/dev/null || true
docker network prune -f > /dev/null 2>&1 || true

echo ""
echo "✅ 삭제 완료!"
echo ""
echo "📌 다음 단계:"
echo "   docker compose -f docker/docker-compose.yaml up -d"
echo ""
echo "   (ldap-init 서비스가 자동으로 PKD DIT 구조를 초기화합니다)"
