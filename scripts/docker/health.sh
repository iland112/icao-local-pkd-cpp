#!/bin/bash
# docker-health.sh - 헬스 체크 스크립트
# Updated: 2026-02-11 - Fixed container names, LDAP passwords, port references

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

# Load shared library
RUNTIME="docker"
source "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh"

# Load credentials and DB type
load_credentials
parse_db_type "postgres"

echo "🏥 컨테이너 헬스 체크... (DB_TYPE=$DB_TYPE)"
echo ""

# Database 체크
if [ "$DB_TYPE" = "oracle" ]; then
    echo "🔶 Oracle:"
    check_oracle_health
    if [ "$ORACLE_HEALTH" = "healthy" ]; then
        echo "  ✅ 정상 (healthy)"
    elif [ "$ORACLE_HEALTH" = "starting" ]; then
        echo "  ⏳ 시작 중 (starting...)"
    elif [ "$ORACLE_HEALTH" = "not-found" ]; then
        echo "  ❌ Oracle 컨테이너가 없습니다"
    else
        echo "  ❌ 오류 (status: $ORACLE_HEALTH)"
    fi
else
    echo "🐘 PostgreSQL:"
    if docker exec ${CONTAINER_PREFIX}-postgres pg_isready -U pkd -d localpkd > /dev/null 2>&1; then
        VERSION=$(docker exec ${CONTAINER_PREFIX}-postgres psql -U pkd -d localpkd -t -c "SELECT version();" 2>/dev/null | head -1 | xargs)
        echo "  ✅ 정상 (ready to accept connections)"
        echo "     Version: $VERSION"
    else
        echo "  ❌ 오류 (not responding)"
    fi
fi

# OpenLDAP 체크
echo ""
echo "📂 OpenLDAP:"
LDAP1_OK=false
LDAP2_OK=false
LDAP1_COUNT=0
LDAP2_COUNT=0

if LDAP1_COUNT=$(check_ldap_node "openldap1"); then
    echo "  ✅ OpenLDAP1 정상 ($LDAP1_COUNT entries)"
    LDAP1_OK=true
else
    LDAP1_COUNT=0
    echo "  ❌ OpenLDAP1 오류"
fi

if LDAP2_COUNT=$(check_ldap_node "openldap2"); then
    echo "  ✅ OpenLDAP2 정상 ($LDAP2_COUNT entries)"
    LDAP2_OK=true
else
    LDAP2_COUNT=0
    echo "  ❌ OpenLDAP2 오류"
fi

# MMR 복제 상태 체크
echo ""
echo "🔄 MMR 복제 상태:"
if [ "$LDAP1_OK" = true ] && [ "$LDAP2_OK" = true ]; then
    if [ "$LDAP1_COUNT" -eq "$LDAP2_COUNT" ]; then
        echo "  ✅ 동기화됨 (OpenLDAP1: $LDAP1_COUNT, OpenLDAP2: $LDAP2_COUNT)"
    else
        echo "  ⚠️  동기화 중 (OpenLDAP1: $LDAP1_COUNT, OpenLDAP2: $LDAP2_COUNT)"
    fi

    if check_mmr_status; then
        echo "  ✅ MMR 설정 활성화됨"
    else
        echo "  ⚠️  MMR 설정 확인 필요"
    fi
else
    echo "  ❌ OpenLDAP 노드 확인 필요"
fi

# PKD Management API 체크 (via API Gateway)
echo ""
echo "🔧 PKD Management Service:"
if ! check_service_health "via API Gateway :18080" "http://localhost:18080/api/health" "show_body"; then
    check_container_health "management" "http://localhost:8081/api/health" "내부 포트 8081" || true
fi

# PA Service API 체크 (내부 포트만 사용하므로 컨테이너 내부에서 확인)
echo ""
echo "🔐 PA Service:"
check_container_health "pa-service" "http://localhost:8082/api/health" "PA Service" || true

# PKD Relay Service 체크 (내부 포트만 사용하므로 컨테이너 내부에서 확인)
echo ""
echo "🔄 PKD Relay Service:"
check_container_health "relay" "http://localhost:8083/api/sync/health" "PKD Relay" || true

# API Gateway 체크
echo ""
echo "🌐 API Gateway:"
if curl -sf http://localhost:18080/health > /dev/null 2>&1; then
    echo "  ✅ 정상 (http://localhost:18080)"
else
    echo "  ❌ 오류 (not responding)"
fi

# Frontend 체크
echo ""
echo "🖥️  Frontend:"
if curl -sf http://localhost:13080 > /dev/null 2>&1; then
    echo "  ✅ 정상 (http://localhost:13080)"
else
    echo "  ❌ 오류 (not responding)"
fi

# 컨테이너 상태
echo ""
echo "📊 컨테이너 상태:"
docker compose -f docker/docker-compose.yaml $PROFILE_FLAG ps

# 리소스 사용량
echo ""
echo "💻 리소스 사용량:"
docker stats --no-stream --format "table {{.Container}}\t{{.CPUPerc}}\t{{.MemUsage}}" $(docker compose -f docker/docker-compose.yaml $PROFILE_FLAG ps -q 2>/dev/null) 2>/dev/null || echo "  ⚠️  컨테이너가 실행 중이지 않습니다"

echo ""
echo "✅ 헬스 체크 완료!"
