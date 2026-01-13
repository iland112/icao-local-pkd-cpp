#!/bin/bash
# luckfox-health.sh - Luckfox Docker 헬스체크 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "❤️  ICAO PKD 시스템 헬스체크 (Luckfox)"
echo "========================================"
echo ""

# 1. 컨테이너 상태
echo "📊 컨테이너 상태:"
docker compose -f docker-compose-luckfox.yaml ps
echo ""

# 2. PostgreSQL 연결 테스트
echo "🗄️  PostgreSQL 연결 테스트:"
if docker exec icao-pkd-postgres pg_isready -U pkd -d localpkd &>/dev/null; then
    echo "   ✅ PostgreSQL: 정상"
    # 테이블 수 확인
    TABLE_COUNT=$(docker exec icao-pkd-postgres psql -U pkd -d localpkd -t -c "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='public';" 2>/dev/null | tr -d ' ')
    echo "      - 테이블 수: $TABLE_COUNT"

    # 인증서 통계
    CERT_COUNT=$(docker exec icao-pkd-postgres psql -U pkd -d localpkd -t -c "SELECT COUNT(*) FROM certificate;" 2>/dev/null | tr -d ' ')
    echo "      - 인증서 수: $CERT_COUNT"
else
    echo "   ❌ PostgreSQL: 연결 실패"
fi
echo ""

# 3. API Gateway 테스트
echo "🌐 API Gateway 테스트:"
if curl -s -f http://127.0.0.1:8080/api/health > /dev/null 2>&1; then
    echo "   ✅ API Gateway: 정상"
    VERSION=$(curl -s http://127.0.0.1:8080/api/health | grep -o '"version":"[^"]*"' | cut -d'"' -f4)
    echo "      - 버전: $VERSION"
else
    echo "   ❌ API Gateway: 응답 없음"
fi
echo ""

# 4. PKD Management 서비스 테스트
echo "🔧 PKD Management 서비스:"
if curl -s -f http://127.0.0.1:8081/api/health > /dev/null 2>&1; then
    echo "   ✅ PKD Management: 정상"
    # 로그에서 버전 확인
    VERSION=$(docker logs icao-pkd-management 2>&1 | grep "ICAO Local PKD" | tail -1)
    if [ -n "$VERSION" ]; then
        echo "      - $VERSION"
    fi
else
    echo "   ❌ PKD Management: 응답 없음"
fi
echo ""

# 5. PA Service 테스트
echo "🔐 PA Service:"
if curl -s -f http://127.0.0.1:8082/api/pa/health > /dev/null 2>&1; then
    echo "   ✅ PA Service: 정상"
else
    echo "   ❌ PA Service: 응답 없음"
fi
echo ""

# 6. Sync Service 테스트
echo "🔄 Sync Service:"
if curl -s -f http://127.0.0.1:8083/api/sync/health > /dev/null 2>&1; then
    echo "   ✅ Sync Service: 정상"
else
    echo "   ❌ Sync Service: 응답 없음"
fi
echo ""

# 7. Frontend 테스트
echo "🎨 Frontend:"
if curl -s -f http://127.0.0.1:3000 > /dev/null 2>&1; then
    echo "   ✅ Frontend: 정상"
else
    echo "   ❌ Frontend: 응답 없음"
fi
echo ""

# 8. 디스크 사용량
echo "💾 디스크 사용량:"
if [ -d "./.docker-data" ]; then
    POSTGRES_SIZE=$(du -sh ./.docker-data/postgres 2>/dev/null | cut -f1)
    UPLOADS_SIZE=$(du -sh ./.docker-data/pkd-uploads 2>/dev/null | cut -f1)
    echo "   - PostgreSQL 데이터: $POSTGRES_SIZE"
    echo "   - 업로드 파일: $UPLOADS_SIZE"
fi
echo ""

echo "========================================"
echo "✅ 헬스체크 완료!"
echo ""
echo "💡 상세 로그 확인: ./luckfox-logs.sh [서비스명]"
