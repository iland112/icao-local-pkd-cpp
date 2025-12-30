#!/bin/bash
# docker-clean.sh - 완전 삭제 스크립트 (PostgreSQL + OpenLDAP + 애플리케이션)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "⚠️  경고: 모든 데이터가 삭제됩니다!"
echo "   - PostgreSQL 데이터 (업로드 이력, PA 이력 등)"
echo "   - OpenLDAP 데이터 (인증서, CRL, Master List)"
echo "   - Docker 볼륨 및 이미지"
echo ""
read -p "계속하시겠습니까? (yes/no): " confirm

if [ "$confirm" != "yes" ]; then
    echo "취소되었습니다."
    exit 0
fi

echo ""
echo "🗑️  컨테이너 중지 및 볼륨 삭제 중..."
docker compose -f docker/docker-compose.yaml down -v --remove-orphans

echo ""
echo "⏳ 컨테이너 완전 중지 대기 중..."
sleep 3

echo ""
echo "🗄️  기존 Docker 볼륨 삭제 중..."
# Docker Compose 프로젝트 볼륨 삭제
docker volume ls -q | grep "icao-local-pkd" | xargs -r docker volume rm 2>/dev/null || true
echo "   ✓ Docker 볼륨 정리 완료"

echo ""
echo "🖼️  Docker 이미지 삭제 (선택)..."
read -p "Docker 이미지도 삭제하시겠습니까? (yes/no): " confirm_image

if [ "$confirm_image" == "yes" ]; then
    docker images | grep "icao-local-pkd" | awk '{print $3}' | xargs -r docker rmi -f 2>/dev/null || true
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
echo "   1. ./docker-start.sh --skip-app  # 인프라만 시작"
echo "   2. ./docker-ldap-init.sh         # LDAP 스키마 및 DIT 초기화"
echo "   3. ./docker-start.sh             # 전체 서비스 시작"
