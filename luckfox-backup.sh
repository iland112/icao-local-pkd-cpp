#!/bin/bash
# luckfox-backup.sh - Luckfox 데이터 백업 스크립트

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BACKUP_DIR="./backups/luckfox_${TIMESTAMP}"

echo "💾 ICAO PKD 데이터 백업 (Luckfox)..."
echo ""

# 백업 디렉토리 생성
mkdir -p "$BACKUP_DIR"

# 1. PostgreSQL 백업
echo "📦 PostgreSQL 데이터베이스 백업 중..."
docker exec icao-pkd-postgres pg_dump -U pkd localpkd > "$BACKUP_DIR/localpkd.sql"
echo "   ✅ PostgreSQL 백업 완료"

# 2. 업로드 파일 백업
echo "📦 업로드 파일 백업 중..."
if [ -d "./.docker-data/pkd-uploads" ]; then
    cp -r ./.docker-data/pkd-uploads "$BACKUP_DIR/"
    echo "   ✅ 업로드 파일 백업 완료"
else
    echo "   ⚠️  업로드 파일 없음"
fi

# 3. Docker Compose 설정 백업
echo "📦 설정 파일 백업 중..."
cp docker-compose-luckfox.yaml "$BACKUP_DIR/"
echo "   ✅ 설정 파일 백업 완료"

# 4. 백업 압축
echo "📦 백업 압축 중..."
cd backups
tar -czf "luckfox_${TIMESTAMP}.tar.gz" "luckfox_${TIMESTAMP}"
rm -rf "luckfox_${TIMESTAMP}"
cd ..

BACKUP_SIZE=$(du -sh "backups/luckfox_${TIMESTAMP}.tar.gz" | cut -f1)

echo ""
echo "✅ 백업 완료!"
echo "   - 파일: backups/luckfox_${TIMESTAMP}.tar.gz"
echo "   - 크기: $BACKUP_SIZE"
echo ""
echo "💡 복구하려면:"
echo "   ./luckfox-restore.sh backups/luckfox_${TIMESTAMP}.tar.gz"
echo ""
