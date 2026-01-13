#!/bin/bash
# luckfox-clean.sh - Luckfox Docker 완전 초기화 스크립트
# 주의: 모든 데이터가 삭제됩니다!

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "⚠️  경고: 모든 컨테이너와 데이터를 삭제합니다!"
echo ""
read -p "계속하시겠습니까? (yes/no): " CONFIRM

if [ "$CONFIRM" != "yes" ]; then
    echo "취소되었습니다."
    exit 0
fi

echo ""
echo "🗑️  컨테이너 중지 및 삭제..."
docker compose -f docker-compose-luckfox.yaml down

echo ""
echo "🗑️  데이터 디렉토리 삭제..."
sudo rm -rf ./.docker-data/postgres/* 2>/dev/null || rm -rf ./.docker-data/postgres/* 2>/dev/null || echo "   - PostgreSQL 데이터 디렉토리 비어있거나 삭제 권한 필요"
sudo rm -rf ./.docker-data/pkd-uploads/* 2>/dev/null || rm -rf ./.docker-data/pkd-uploads/* 2>/dev/null || echo "   - 업로드 파일 디렉토리 비어있거나 삭제 권한 필요"
echo "   - 데이터 정리 완료"

echo ""
echo "✅ 완전 초기화 완료!"
echo ""
echo "💡 새로 시작하려면:"
echo "   ./luckfox-start.sh"
echo ""
