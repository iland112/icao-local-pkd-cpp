#!/bin/bash
# docker-health.sh - 헬스 체크 스크립트
# Updated: 2026-01-03 - Added Sync Service health check

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "🏥 컨테이너 헬스 체크..."
echo ""

# PostgreSQL 체크
echo "🐘 PostgreSQL:"
if docker exec icao-local-pkd-postgres pg_isready -U pkd -d localpkd > /dev/null 2>&1; then
    VERSION=$(docker exec icao-local-pkd-postgres psql -U pkd -d localpkd -t -c "SELECT version();" 2>/dev/null | head -1 | xargs)
    echo "  ✅ 정상 (ready to accept connections)"
    echo "     Version: $VERSION"
else
    echo "  ❌ 오류 (not responding)"
fi

# OpenLDAP 체크
echo ""
echo "📂 OpenLDAP:"
LDAP1_OK=false
LDAP2_OK=false
LDAP1_COUNT=0
LDAP2_COUNT=0

LDAP_BIND_DN="cn=admin,dc=ldap,dc=smartcoreinc,dc=com"
LDAP_BIND_PW="admin"

if docker exec icao-local-pkd-openldap1 ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
    LDAP1_RESULT=$(docker exec icao-local-pkd-openldap1 ldapsearch -x -H ldap://localhost \
        -D "$LDAP_BIND_DN" -w "$LDAP_BIND_PW" \
        -b "dc=ldap,dc=smartcoreinc,dc=com" -s sub "(objectClass=*)" dn 2>/dev/null)
    LDAP1_COUNT=$(echo "$LDAP1_RESULT" | grep "^dn:" | wc -l | xargs)
    echo "  ✅ OpenLDAP1 정상 ($LDAP1_COUNT entries)"
    LDAP1_OK=true
else
    echo "  ❌ OpenLDAP1 오류"
fi

if docker exec icao-local-pkd-openldap2 ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
    LDAP2_RESULT=$(docker exec icao-local-pkd-openldap2 ldapsearch -x -H ldap://localhost \
        -D "$LDAP_BIND_DN" -w "$LDAP_BIND_PW" \
        -b "dc=ldap,dc=smartcoreinc,dc=com" -s sub "(objectClass=*)" dn 2>/dev/null)
    LDAP2_COUNT=$(echo "$LDAP2_RESULT" | grep "^dn:" | wc -l | xargs)
    echo "  ✅ OpenLDAP2 정상 ($LDAP2_COUNT entries)"
    LDAP2_OK=true
else
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

    # MMR 설정 확인
    MMR_CONFIG=$(docker exec icao-local-pkd-openldap1 ldapsearch -x -H ldap://localhost \
        -D "cn=admin,cn=config" -w config \
        -b "olcDatabase={1}mdb,cn=config" "(objectClass=*)" olcMirrorMode 2>/dev/null | grep "olcMirrorMode" || echo "")
    if [ -n "$MMR_CONFIG" ]; then
        echo "  ✅ MMR 설정 활성화됨"
    else
        echo "  ⚠️  MMR 설정 확인 필요"
    fi
else
    echo "  ❌ OpenLDAP 노드 확인 필요"
fi

# HAProxy removed - using direct OpenLDAP connections (v2.0.1+)

# PKD Management API 체크
echo ""
echo "🔧 PKD Management Service:"
if curl -sf http://localhost:8081/api/health > /dev/null 2>&1; then
    HEALTH=$(curl -s http://localhost:8081/api/health 2>/dev/null)
    echo "  ✅ 정상"
    echo "     $HEALTH"
else
    echo "  ❌ 오류 (not responding)"
fi

# PA Service API 체크 (내부 포트만 사용하므로 컨테이너 내부에서 확인)
echo ""
echo "🔐 PA Service:"
if docker exec icao-local-pkd-pa-service curl -sf http://localhost:8082/api/health > /dev/null 2>&1; then
    HEALTH=$(docker exec icao-local-pkd-pa-service curl -s http://localhost:8082/api/health 2>/dev/null)
    echo "  ✅ 정상 (내부 포트 8082)"
    echo "     $HEALTH"
else
    echo "  ❌ 오류 (not responding)"
fi

# Sync Service API 체크 (내부 포트만 사용하므로 컨테이너 내부에서 확인)
echo ""
echo "🔄 Sync Service:"
if docker exec icao-local-pkd-sync-service curl -sf http://localhost:8083/api/sync/health > /dev/null 2>&1; then
    HEALTH=$(docker exec icao-local-pkd-sync-service curl -s http://localhost:8083/api/sync/health 2>/dev/null)
    echo "  ✅ 정상 (내부 포트 8083)"
    echo "     $HEALTH"
else
    echo "  ❌ 오류 (not responding)"
fi

# Frontend 체크
echo ""
echo "🌐 Frontend:"
if curl -sf http://localhost:3000 > /dev/null 2>&1; then
    echo "  ✅ 정상 (http://localhost:3000)"
else
    echo "  ❌ 오류 (not responding)"
fi

# 컨테이너 상태
echo ""
echo "📊 컨테이너 상태:"
docker compose -f docker/docker-compose.yaml ps

# 리소스 사용량
echo ""
echo "💻 리소스 사용량:"
docker stats --no-stream --format "table {{.Container}}\t{{.CPUPerc}}\t{{.MemUsage}}" $(docker compose -f docker/docker-compose.yaml ps -q) 2>/dev/null || echo "  ⚠️  컨테이너가 실행 중이지 않습니다"

echo ""
echo "✅ 헬스 체크 완료!"
