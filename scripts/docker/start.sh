#!/bin/bash
# docker-start.sh - Docker 컨테이너 시작 스크립트
# Updated: 2026-01-03 - Added Sync Service

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
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

# Read DB_TYPE from .env
DB_TYPE=$(grep -E '^DB_TYPE=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'')
DB_TYPE="${DB_TYPE:-postgres}"

if [ "$DB_TYPE" = "oracle" ]; then
    PROFILE_FLAG="--profile oracle"
else
    PROFILE_FLAG="--profile postgres"
fi

echo "🚀 ICAO PKD Docker 컨테이너 시작... (DB_TYPE=$DB_TYPE)"
echo ""

# 1. 필요한 디렉토리 생성
echo "📁 디렉토리 생성 중..."
mkdir -p ./data/uploads
mkdir -p ./data/cert
mkdir -p ./logs
mkdir -p ./backups

# Docker bind mount 디렉토리 생성 (권한 문제 방지)
mkdir -p ./.docker-data/pkd-logs
mkdir -p ./.docker-data/pkd-uploads
mkdir -p ./.docker-data/pa-logs
mkdir -p ./.docker-data/sync-logs
mkdir -p ./.docker-data/monitoring-logs
mkdir -p ./.docker-data/gateway-logs

# 권한 설정 (Docker 컨테이너에서 쓰기 가능하도록)
echo "🔒 로그 디렉토리 권한 설정 중..."
if [ -w ./.docker-data ]; then
    chmod -R 777 ./.docker-data/pkd-logs ./.docker-data/pkd-uploads ./.docker-data/pa-logs ./.docker-data/sync-logs ./.docker-data/monitoring-logs ./.docker-data/gateway-logs 2>/dev/null || \
    sudo chmod -R 777 ./.docker-data/pkd-logs ./.docker-data/pkd-uploads ./.docker-data/pa-logs ./.docker-data/sync-logs ./.docker-data/monitoring-logs ./.docker-data/gateway-logs
else
    echo "⚠️  .docker-data 디렉토리 쓰기 권한 필요 - sudo 사용"
    sudo chmod -R 777 ./.docker-data/pkd-logs ./.docker-data/pkd-uploads ./.docker-data/pa-logs ./.docker-data/sync-logs ./.docker-data/monitoring-logs ./.docker-data/gateway-logs
fi

# 2. Docker Compose 시작
echo "🐳 Docker Compose 시작..."
cd docker

if [ -n "$SKIP_APP" ]; then
    if [ -n "$SKIP_LDAP" ]; then
        # DB만 시작 (profile이 postgres/oracle 선택)
        docker compose $PROFILE_FLAG up -d $BUILD_FLAG
    else
        # DB + OpenLDAP 시작
        docker compose $PROFILE_FLAG up -d $BUILD_FLAG openldap1 openldap2
    fi
elif [ -n "$LEGACY" ]; then
    # Legacy 단일 앱 모드
    docker compose --profile legacy up -d $BUILD_FLAG
else
    # 마이크로서비스 모드 (frontend + pkd-management + pa-service + sync-service)
    # 서비스 의존성 순서:
    #   openldap1/2 -> ldap-mmr-setup1/2 -> ldap-init -> apps
    # Profile selects: --profile postgres (postgres + monitoring) or --profile oracle (oracle)
    docker compose $PROFILE_FLAG up -d $BUILD_FLAG
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
    echo "🔧 LDAP MMR 및 DIT 초기화 확인 중..."
    echo "   1. ldap-mmr-setup1/2: MMR (Multi-Master Replication) 설정"
    echo "   2. ldap-init: PKD DIT 구조 초기화"
    echo ""
    # MMR setup 로그
    echo "📋 MMR Setup 결과:"
    docker compose -f docker/docker-compose.yaml logs ldap-mmr-setup1 2>/dev/null | tail -3
    docker compose -f docker/docker-compose.yaml logs ldap-mmr-setup2 2>/dev/null | tail -3
    echo ""
    echo "📋 LDAP Init 결과:"
    docker compose -f docker/docker-compose.yaml logs ldap-init 2>/dev/null | tail -5
fi

echo ""
echo "📌 접속 정보:"
echo "   - Database:      DB_TYPE=$DB_TYPE"
echo "   - PostgreSQL:    localhost:15432 (pkd/pkd)"
if [ "$DB_TYPE" = "oracle" ]; then
    echo "   - Oracle:        localhost:11521 (XEPDB1)"
fi
if [ -z "$SKIP_LDAP" ]; then
    echo "   - OpenLDAP 1:    ldap://localhost:13891 (직접 연결)"
    echo "   - OpenLDAP 2:    ldap://localhost:13892 (직접 연결)"
    echo "   Note: Application uses direct connections to both LDAP servers"
fi
if [ -z "$SKIP_APP" ]; then
    echo "   - Frontend:      http://localhost:13080"
    echo "   - API Gateway:   http://localhost:18080/api"
    echo "   - Swagger UI:    http://localhost:18090"
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
