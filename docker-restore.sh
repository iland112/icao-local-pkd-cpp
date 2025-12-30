#!/bin/bash
# docker-restore.sh - 데이터 복구 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BACKUP_DIR=${1:-}

if [ -z "$BACKUP_DIR" ] || [ ! -d "$BACKUP_DIR" ]; then
    echo "❌ 사용법: $0 <백업_디렉토리>"
    echo "예: $0 ./backups/20251231_103000"
    echo ""
    echo "📂 사용 가능한 백업:"
    ls -1dt ./backups/*/ 2>/dev/null | head -5 || echo "  백업이 없습니다."
    exit 1
fi

echo "⚠️  경고: 현재 데이터가 복구 데이터로 대체됩니다!"
echo ""
echo "복구할 백업: $BACKUP_DIR"
echo ""
read -p "계속하시겠습니까? (yes/no): " confirm

if [ "$confirm" != "yes" ]; then
    echo "취소되었습니다."
    exit 0
fi

echo ""
echo "♻️  데이터 복구 시작..."

# PostgreSQL 복구
if [ -f "$BACKUP_DIR/postgres_backup.sql" ]; then
    echo ""
    echo "📦 PostgreSQL 복구 중..."
    docker exec -i icao-local-pkd-postgres psql -U pkd localpkd < $BACKUP_DIR/postgres_backup.sql
    echo "  ✅ PostgreSQL 복구 완료"
else
    echo ""
    echo "  ⚠️  PostgreSQL 백업 파일을 찾을 수 없습니다."
fi

# OpenLDAP 복구
if [ -f "$BACKUP_DIR/ldap_backup.ldif" ]; then
    echo ""
    echo "📦 OpenLDAP 복구 중..."
    docker exec -i icao-local-pkd-openldap1 ldapadd -x \
        -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
        -w admin \
        -H ldap://localhost \
        -c < $BACKUP_DIR/ldap_backup.ldif 2>/dev/null || true
    echo "  ✅ OpenLDAP 복구 완료"
else
    echo ""
    echo "  ⚠️  OpenLDAP 백업 파일을 찾을 수 없습니다."
fi

# 업로드 파일 복구
if [ -f "$BACKUP_DIR/uploads.tar.gz" ]; then
    echo ""
    echo "📦 업로드 파일 복구 중..."
    tar -xzf $BACKUP_DIR/uploads.tar.gz -C .
    echo "  ✅ 업로드 파일 복구 완료"
else
    echo ""
    echo "  ⚠️  업로드 파일 백업을 찾을 수 없습니다."
fi

# 인증서 파일 복구
if [ -f "$BACKUP_DIR/cert.tar.gz" ]; then
    echo ""
    echo "📦 인증서 파일 복구 중..."
    tar -xzf $BACKUP_DIR/cert.tar.gz -C .
    echo "  ✅ 인증서 파일 복구 완료"
else
    echo ""
    echo "  ⚠️  인증서 파일 백업을 찾을 수 없습니다."
fi

echo ""
echo "✅ 복구 완료!"
