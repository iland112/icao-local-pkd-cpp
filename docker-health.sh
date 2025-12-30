#!/bin/bash
# docker-health.sh - 헬스 체크 스크립트

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
echo "📂 OpenLDAP (HAProxy):"
if docker exec icao-local-pkd-openldap1 ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
    LDAP1_COUNT=$(docker exec icao-local-pkd-openldap1 ldapsearch -x -H ldap://localhost -b "dc=ldap,dc=smartcoreinc,dc=com" -s sub "(objectClass=*)" dn 2>/dev/null | grep -c "^dn:" || echo 0)
    echo "  ✅ OpenLDAP1 정상 ($LDAP1_COUNT entries)"
else
    echo "  ❌ OpenLDAP1 오류"
fi

if docker exec icao-local-pkd-openldap2 ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
    LDAP2_COUNT=$(docker exec icao-local-pkd-openldap2 ldapsearch -x -H ldap://localhost -b "dc=ldap,dc=smartcoreinc,dc=com" -s sub "(objectClass=*)" dn 2>/dev/null | grep -c "^dn:" || echo 0)
    echo "  ✅ OpenLDAP2 정상 ($LDAP2_COUNT entries)"
else
    echo "  ❌ OpenLDAP2 오류"
fi

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

# PA Service API 체크
echo ""
echo "🔐 PA Service:"
if curl -sf http://localhost:8082/api/health > /dev/null 2>&1; then
    HEALTH=$(curl -s http://localhost:8082/api/health 2>/dev/null)
    echo "  ✅ 정상"
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
