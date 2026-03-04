#!/bin/bash
# =============================================================================
# ICAO Local PKD - Podman 헬스 체크 스크립트 (Production RHEL 9)
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

# Load shared library
RUNTIME="podman"
source "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh"

# Load credentials and DB type
load_credentials
parse_db_type "oracle"

echo "  컨테이너 헬스 체크... (DB_TYPE=$DB_TYPE)"
echo ""

# Database 체크
if [ "$DB_TYPE" = "oracle" ]; then
    echo "  Oracle:"
    check_oracle_health
    if [ "$ORACLE_HEALTH" = "healthy" ]; then
        echo "    컨테이너: 정상 (healthy)"
    elif [ "$ORACLE_HEALTH" = "starting" ]; then
        echo "    컨테이너: 시작 중 (starting...)"
    elif [ "$ORACLE_HEALTH" = "not-found" ]; then
        echo "    Oracle 컨테이너가 없습니다"
    else
        echo "    컨테이너: 오류 (status: $ORACLE_HEALTH)"
    fi
    # XEPDB1 PDB 실제 연결 체크
    if [ "$ORACLE_HEALTH" != "not-found" ]; then
        if podman exec ${CONTAINER_PREFIX}-oracle bash -c \
            "echo 'SELECT 1 FROM DUAL;' | sqlplus -s sys/\"\$ORACLE_PWD\"@//localhost:1521/XEPDB1 as sysdba 2>/dev/null | grep -q 1" 2>/dev/null; then
            echo "    XEPDB1: OPEN (정상)"
        else
            echo "    XEPDB1: 미준비 (PDB 미오픈)"
        fi
    fi
else
    echo "  PostgreSQL:"
    if podman exec ${CONTAINER_PREFIX}-postgres pg_isready -U pkd -d localpkd > /dev/null 2>&1; then
        VERSION=$(podman exec ${CONTAINER_PREFIX}-postgres psql -U pkd -d localpkd -t -c "SELECT version();" 2>/dev/null | head -1 | xargs)
        echo "    정상 (ready to accept connections)"
        echo "     Version: $VERSION"
    else
        echo "    오류 (not responding)"
    fi
fi

# OpenLDAP 체크
echo ""
echo "  OpenLDAP:"
LDAP1_OK=false
LDAP2_OK=false
LDAP1_COUNT=0
LDAP2_COUNT=0

if LDAP1_COUNT=$(check_ldap_node "openldap1"); then
    echo "    OpenLDAP1 정상 ($LDAP1_COUNT entries)"
    LDAP1_OK=true
else
    LDAP1_COUNT=0
    echo "    OpenLDAP1 오류"
fi

if LDAP2_COUNT=$(check_ldap_node "openldap2"); then
    echo "    OpenLDAP2 정상 ($LDAP2_COUNT entries)"
    LDAP2_OK=true
else
    LDAP2_COUNT=0
    echo "    OpenLDAP2 오류"
fi

# MMR 복제 상태 체크
echo ""
echo "  MMR 복제 상태:"
if [ "$LDAP1_OK" = true ] && [ "$LDAP2_OK" = true ]; then
    if [ "$LDAP1_COUNT" -eq "$LDAP2_COUNT" ]; then
        echo "    동기화됨 (OpenLDAP1: $LDAP1_COUNT, OpenLDAP2: $LDAP2_COUNT)"
    else
        echo "    동기화 중 (OpenLDAP1: $LDAP1_COUNT, OpenLDAP2: $LDAP2_COUNT)"
    fi

    if check_mmr_status; then
        echo "    MMR 설정 활성화됨"
    else
        echo "    MMR 설정 확인 필요"
    fi
else
    echo "    OpenLDAP 노드 확인 필요"
fi

# PKD Management API 체크
echo ""
echo "  PKD Management Service:"
if ! check_service_health "via API Gateway :80" "http://localhost/api/health" "show_body"; then
    check_container_health "management" "http://localhost:8081/api/health" "PKD Management" || true
fi

# PA Service API 체크
echo ""
echo "  PA Service:"
check_container_health "pa-service" "http://localhost:8082/api/health" "PA Service" || true

# PKD Relay Service 체크
echo ""
echo "  PKD Relay Service:"
check_container_health "relay" "http://localhost:8083/api/sync/health" "PKD Relay" || true

# API Gateway 체크
echo ""
echo "  API Gateway:"
SSL_DOMAIN="${SSL_DOMAIN:-pkd.smartcoreinc.com}"
if curl -skf https://localhost/health > /dev/null 2>&1; then
    echo "    정상 (https://$SSL_DOMAIN)"
else
    echo "    오류 (not responding)"
fi

# Frontend 체크
echo ""
echo "  Frontend:"
if curl -sf http://localhost:13080 > /dev/null 2>&1; then
    echo "    정상 (http://localhost:13080)"
else
    echo "    오류 (not responding)"
fi

# 컨테이너 상태
echo ""
echo "  컨테이너 상태:"
podman ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}" \
    --filter "name=icao-local-pkd" 2>/dev/null || \
    podman-compose -f docker/docker-compose.podman.yaml ps

# 리소스 사용량
echo ""
echo "  리소스 사용량:"
podman stats --no-stream --format "table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}" \
    $(podman ps --filter "name=icao-local-pkd" -q 2>/dev/null) 2>/dev/null || \
    echo "    컨테이너가 실행 중이지 않습니다"

echo ""
echo "  헬스 체크 완료!"
