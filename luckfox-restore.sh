#!/bin/bash
# luckfox-restore.sh - Luckfox 데이터 복구 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BACKUP_FILE=$1

if [ -z "$BACKUP_FILE" ]; then
    echo "사용법: $0 <백업파일.tar.gz>"
    echo ""
    echo "사용 가능한 백업:"
    ls -lh backups/*.tar.gz 2>/dev/null || echo "  (백업 파일 없음)"
    exit 1
fi

if [ ! -f "$BACKUP_FILE" ]; then
    echo "❌ 백업 파일을 찾을 수 없습니다: $BACKUP_FILE"
    exit 1
fi

echo "⚠️  경고: 현재 데이터가 백업 데이터로 덮어씌워집니다!"
echo ""
read -p "계속하시겠습니까? (yes/no): " CONFIRM

if [ "$CONFIRM" != "yes" ]; then
    echo "취소되었습니다."
    exit 0
fi

echo ""
echo "📦 백업 파일 압축 해제 중..."
TEMP_DIR=$(mktemp -d)
tar -xzf "$BACKUP_FILE" -C "$TEMP_DIR"
BACKUP_DIR=$(ls -d "$TEMP_DIR"/luckfox_* | head -1)

# 1. 컨테이너 중지
echo "🛑 컨테이너 중지 중..."
docker compose -f docker-compose-luckfox.yaml stop

# 2. PostgreSQL 복구
echo "📥 PostgreSQL 데이터베이스 복구 중..."
if [ -f "$BACKUP_DIR/localpkd.sql" ]; then
    docker compose -f docker-compose-luckfox.yaml start postgres
    sleep 5
    # Drop and recreate database for clean restore
    docker exec -i icao-pkd-postgres psql -U pkd -d postgres -c "DROP DATABASE IF EXISTS localpkd;" 2>/dev/null || true
    docker exec -i icao-pkd-postgres psql -U pkd -d postgres -c "CREATE DATABASE localpkd;" 2>/dev/null || true
    sleep 2
    # Restore from backup
    docker exec -i icao-pkd-postgres psql -U pkd -d localpkd < "$BACKUP_DIR/localpkd.sql" 2>&1 | grep -v "^ERROR:" | grep -v "^DETAIL:" | grep -v "^CONTEXT:" || true
    echo "   ✅ PostgreSQL 복구 완료"
else
    echo "   ⚠️  PostgreSQL 백업 파일 없음"
fi

# 3. 업로드 파일 복구
echo "📥 업로드 파일 복구 중..."
if [ -d "$BACKUP_DIR/pkd-uploads" ]; then
    rm -rf ./.docker-data/pkd-uploads/*
    cp -r "$BACKUP_DIR/pkd-uploads/"* ./.docker-data/pkd-uploads/ 2>/dev/null || true
    echo "   ✅ 업로드 파일 복구 완료"
else
    echo "   ⚠️  업로드 파일 백업 없음"
fi

# 4. 컨테이너 재시작
echo "🔄 컨테이너 재시작 중..."
docker compose -f docker-compose-luckfox.yaml up -d

# 5. 임시 디렉토리 정리
rm -rf "$TEMP_DIR"

echo ""
echo "✅ 복구 완료!"
echo ""
echo "💡 시스템 상태 확인:"
echo "   ./luckfox-health.sh"
echo ""
