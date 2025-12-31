#!/bin/bash
# docker-start.sh - Docker 컨테이너 시작 스크립트

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 옵션 파싱
BUILD_FLAG=""
SKIP_APP=""
SKIP_LDAP=""
LEGACY=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --build)
            BUILD_FLAG="--build"
            shift
            ;;
        --skip-app)
            SKIP_APP="true"
            shift
            ;;
        --skip-ldap)
            SKIP_LDAP="true"
            shift
            ;;
        --legacy)
            LEGACY="true"
            shift
            ;;
        *)
            shift
            ;;
    esac
done

echo "🚀 ICAO PKD Docker 컨테이너 시작..."
echo ""

# 1. 필요한 디렉토리 생성
echo "📁 디렉토리 생성 중..."
mkdir -p ./data/uploads
mkdir -p ./data/cert
mkdir -p ./logs
mkdir -p ./backups

# 2. Docker Compose 시작
echo "🐳 Docker Compose 시작..."
cd docker

if [ -n "$SKIP_APP" ]; then
    if [ -n "$SKIP_LDAP" ]; then
        # PostgreSQL만 시작
        docker compose up -d $BUILD_FLAG postgres
    else
        # PostgreSQL, OpenLDAP, HAProxy 시작 (ldap-init이 PKD DIT 자동 초기화)
        docker compose up -d $BUILD_FLAG postgres openldap1 openldap2 ldap-init haproxy
    fi
elif [ -n "$LEGACY" ]; then
    # Legacy 단일 앱 모드
    docker compose --profile legacy up -d $BUILD_FLAG
else
    # 마이크로서비스 모드 (frontend + pkd-management + pa-service)
    # ldap-init 서비스가 PKD DIT 구조를 자동으로 초기화합니다
    docker compose up -d $BUILD_FLAG frontend pkd-management pa-service postgres openldap1 openldap2 ldap-init haproxy
fi

cd ..

# 3. 컨테이너 상태 확인
echo ""
echo "⏳ 컨테이너 시작 대기 중..."
sleep 5

echo ""
echo "📊 컨테이너 상태:"
docker compose -f docker/docker-compose.yaml ps

echo ""
echo "✅ 컨테이너 시작 완료!"

# 4. LDAP 초기화 확인
if [ -z "$SKIP_LDAP" ]; then
    echo ""
    echo "🔧 LDAP 초기화 확인 중..."
    echo "   (ldap-init 서비스가 자동으로 PKD DIT 구조를 초기화합니다)"
    # ldap-init 서비스 완료 대기
    docker compose -f docker/docker-compose.yaml logs ldap-init 2>/dev/null | tail -5
fi

echo ""
echo "📌 접속 정보:"
echo "   - PostgreSQL:    localhost:5432 (pkd/pkd)"
if [ -z "$SKIP_LDAP" ]; then
    echo "   - LDAP (HAProxy): ldap://localhost:389 (로드밸런싱)"
    echo "   - OpenLDAP 1:    ldap://localhost:3891 (직접 연결)"
    echo "   - OpenLDAP 2:    ldap://localhost:3892 (직접 연결)"
    echo "   - HAProxy Stats: http://localhost:8404"
fi
if [ -z "$SKIP_APP" ]; then
    echo "   - Frontend:      http://localhost:3000"
    echo "   - PKD Management: http://localhost:8081/api"
    echo "   - PA Service:    http://localhost:8082/api"
fi
echo ""
echo "🔍 로그 확인: ./docker-logs.sh [서비스명]"
echo "🛑 중지:     ./docker-stop.sh"
echo "🔄 재시작:   ./docker-restart.sh"
echo ""
echo "💡 옵션:"
echo "   --build      이미지 다시 빌드"
echo "   --skip-app   애플리케이션 제외 (인프라만 시작)"
echo "   --skip-ldap  OpenLDAP 제외"
echo "   --legacy     Legacy 단일 앱 모드"
echo ""
if [ -z "$SKIP_LDAP" ]; then
    echo "📝 LDAP DIT 재초기화가 필요하면:"
    echo "   docker compose -f docker/docker-compose.yaml restart ldap-init"
fi
