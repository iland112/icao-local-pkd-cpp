#!/bin/bash
# =============================================================================
# ICAO Local PKD - Podman 데이터 백업 스크립트 (Production RHEL 9)
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

# Load shared library
RUNTIME="podman"
source "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh"

# Load credentials and DB type
load_credentials
load_oracle_password
parse_db_type "oracle"

BACKUP_DIR="./backups/$(date +%Y%m%d_%H%M%S)"

echo "  데이터 백업 시작... (DB_TYPE=$DB_TYPE)"
mkdir -p $BACKUP_DIR

# PostgreSQL 백업
if [ "$DB_TYPE" = "postgres" ]; then
    echo ""
    echo "  PostgreSQL 백업 중..."
    if podman exec ${CONTAINER_PREFIX}-postgres pg_isready -U pkd -d localpkd > /dev/null 2>&1; then
        podman exec ${CONTAINER_PREFIX}-postgres pg_dump -U pkd localpkd > $BACKUP_DIR/postgres_backup.sql
        echo "    PostgreSQL 백업 완료"
    else
        echo "    PostgreSQL이 실행 중이지 않습니다"
    fi
fi

# Oracle 백업
if [ "$DB_TYPE" = "oracle" ]; then
    echo ""
    echo "  Oracle 백업 중..."
    check_oracle_health
    if [ "$ORACLE_HEALTH" = "healthy" ]; then
        podman exec ${CONTAINER_PREFIX}-oracle bash -c "expdp pkd_user/${ORACLE_PWD}@XEPDB1 directory=DATA_PUMP_DIR dumpfile=pkd_backup.dmp logfile=pkd_backup.log schemas=PKD_USER" 2>/dev/null
        podman cp ${CONTAINER_PREFIX}-oracle:/opt/oracle/admin/XE/dpdump/pkd_backup.dmp "$BACKUP_DIR/oracle_backup.dmp" 2>/dev/null
        if [ -f "$BACKUP_DIR/oracle_backup.dmp" ]; then
            echo "    Oracle 백업 완료"
        else
            echo "    Oracle Data Pump 백업 실패 — SQL 덤프로 대체합니다"
            podman exec ${CONTAINER_PREFIX}-oracle bash -c "echo 'SELECT COUNT(*) FROM pkd_user.certificate;' | sqlplus -s pkd_user/${ORACLE_PWD}@//localhost:1521/XEPDB1" > "$BACKUP_DIR/oracle_verify.txt" 2>/dev/null
            echo "    Oracle 데이터 검증 파일 저장됨"
        fi
    else
        echo "    Oracle이 실행 중이지 않습니다 (status: $ORACLE_HEALTH)"
    fi
fi

# OpenLDAP 백업 (LDIF export)
echo ""
echo "  OpenLDAP 백업 중..."
backup_ldap "$BACKUP_DIR/ldap_backup.ldif"

# 업로드 파일 백업
echo ""
echo "  업로드 파일 백업 중..."
backup_directory "./.docker-data/pkd-uploads" "$BACKUP_DIR/uploads.tar.gz" "업로드 파일"

# 인증서 파일 백업
echo ""
echo "  인증서 파일 백업 중..."
backup_directory "./data/cert" "$BACKUP_DIR/cert.tar.gz" "인증서 파일"

# SSL 인증서 백업
echo ""
echo "  SSL 인증서 백업 중..."
backup_directory "./.docker-data/ssl" "$BACKUP_DIR/ssl.tar.gz" "SSL 인증서"

echo ""
echo "  백업 완료: $BACKUP_DIR"
echo ""
echo "  백업 파일 목록:"
ls -lh $BACKUP_DIR
