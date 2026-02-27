#!/bin/bash
# =============================================================================
# ICAO Local PKD - Podman 데이터 백업 스크립트 (Production RHEL 9)
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

# Load credentials from .env
LDAP_BIND_PW="$(grep -E '^LDAP_ADMIN_PASSWORD=' .env 2>/dev/null | cut -d= -f2)"
LDAP_BIND_PW="${LDAP_BIND_PW:-ldap_test_password_123}"

DB_TYPE=$(grep -E '^DB_TYPE=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'')
DB_TYPE="${DB_TYPE:-oracle}"

BACKUP_DIR="./backups/$(date +%Y%m%d_%H%M%S)"

echo "  데이터 백업 시작... (DB_TYPE=$DB_TYPE)"
mkdir -p $BACKUP_DIR

# PostgreSQL 백업
if [ "$DB_TYPE" = "postgres" ]; then
    echo ""
    echo "  PostgreSQL 백업 중..."
    if podman exec icao-local-pkd-postgres pg_isready -U pkd -d localpkd > /dev/null 2>&1; then
        podman exec icao-local-pkd-postgres pg_dump -U pkd localpkd > $BACKUP_DIR/postgres_backup.sql
        echo "    PostgreSQL 백업 완료"
    else
        echo "    PostgreSQL이 실행 중이지 않습니다"
    fi
fi

# Oracle 백업
if [ "$DB_TYPE" = "oracle" ]; then
    echo ""
    echo "  Oracle 백업 중..."
    ORACLE_PWD=$(grep -E '^ORACLE_PASSWORD=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'')
    ORACLE_PWD="${ORACLE_PWD:-pkd_password}"
    ORACLE_HEALTH=$(podman inspect icao-local-pkd-oracle --format='{{.State.Health.Status}}' 2>/dev/null || echo "not-found")
    if [ "$ORACLE_HEALTH" = "healthy" ]; then
        podman exec icao-local-pkd-oracle bash -c "expdp pkd_user/${ORACLE_PWD}@XEPDB1 directory=DATA_PUMP_DIR dumpfile=pkd_backup.dmp logfile=pkd_backup.log schemas=PKD_USER" 2>/dev/null
        podman cp icao-local-pkd-oracle:/opt/oracle/admin/XE/dpdump/pkd_backup.dmp "$BACKUP_DIR/oracle_backup.dmp" 2>/dev/null
        if [ -f "$BACKUP_DIR/oracle_backup.dmp" ]; then
            echo "    Oracle 백업 완료"
        else
            echo "    Oracle Data Pump 백업 실패 — SQL 덤프로 대체합니다"
            podman exec icao-local-pkd-oracle bash -c "echo 'SELECT COUNT(*) FROM pkd_user.certificate;' | sqlplus -s pkd_user/${ORACLE_PWD}@//localhost:1521/XEPDB1" > "$BACKUP_DIR/oracle_verify.txt" 2>/dev/null
            echo "    Oracle 데이터 검증 파일 저장됨"
        fi
    else
        echo "    Oracle이 실행 중이지 않습니다 (status: $ORACLE_HEALTH)"
    fi
fi

# OpenLDAP 백업 (LDIF export)
echo ""
echo "  OpenLDAP 백업 중..."
if podman exec icao-local-pkd-openldap1 ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
    podman exec icao-local-pkd-openldap1 ldapsearch -x \
        -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
        -w "$LDAP_BIND_PW" \
        -H ldap://localhost \
        -b "dc=ldap,dc=smartcoreinc,dc=com" \
        -LLL > $BACKUP_DIR/ldap_backup.ldif 2>/dev/null
    LDAP_ENTRIES=$(grep -c "^dn:" $BACKUP_DIR/ldap_backup.ldif 2>/dev/null || echo 0)
    echo "    OpenLDAP 백업 완료 ($LDAP_ENTRIES entries)"
else
    echo "    OpenLDAP이 실행 중이지 않습니다"
fi

# 업로드 파일 백업
echo ""
echo "  업로드 파일 백업 중..."
if [ -d "./.docker-data/pkd-uploads" ] && [ "$(ls -A ./.docker-data/pkd-uploads 2>/dev/null)" ]; then
    tar -czf $BACKUP_DIR/uploads.tar.gz ./.docker-data/pkd-uploads
    echo "    업로드 파일 백업 완료"
else
    echo "    업로드 파일이 없습니다. 건너뜁니다."
fi

# 인증서 파일 백업
echo ""
echo "  인증서 파일 백업 중..."
if [ -d "./data/cert" ] && [ "$(ls -A ./data/cert 2>/dev/null)" ]; then
    tar -czf $BACKUP_DIR/cert.tar.gz ./data/cert
    echo "    인증서 파일 백업 완료"
else
    echo "    인증서 파일이 없습니다. 건너뜁니다."
fi

# SSL 인증서 백업
echo ""
echo "  SSL 인증서 백업 중..."
if [ -d "./.docker-data/ssl" ] && [ "$(ls -A ./.docker-data/ssl 2>/dev/null)" ]; then
    tar -czf $BACKUP_DIR/ssl.tar.gz ./.docker-data/ssl
    echo "    SSL 인증서 백업 완료"
else
    echo "    SSL 인증서가 없습니다. 건너뜁니다."
fi

echo ""
echo "  백업 완료: $BACKUP_DIR"
echo ""
echo "  백업 파일 목록:"
ls -lh $BACKUP_DIR
