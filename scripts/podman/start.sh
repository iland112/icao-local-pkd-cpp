#!/bin/bash
# =============================================================================
# ICAO Local PKD - Podman 컨테이너 시작 스크립트 (Production RHEL 9)
# =============================================================================
# Docker용 docker-compose.yaml 대신 docker-compose.podman.yaml 사용
# nginx DNS resolver를 Podman aardvark-dns에 맞게 자동 설정
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

COMPOSE_FILE="docker/docker-compose.podman.yaml"
COMPOSE="podman-compose -f $COMPOSE_FILE"

# 옵션 파싱
BUILD_FLAG=""
SKIP_APP=""
SKIP_LDAP=""
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
        *)
            shift
            ;;
    esac
done

# Read DB_TYPE from .env
DB_TYPE=$(grep -E '^DB_TYPE=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'')
DB_TYPE="${DB_TYPE:-oracle}"

if [ "$DB_TYPE" = "oracle" ]; then
    PROFILE_FLAG="--profile oracle"
else
    PROFILE_FLAG="--profile postgres"
fi

# SSL 인증서 감지 (Private CA)
SSL_DOMAIN="${SSL_DOMAIN:-pkd.smartcoreinc.com}"
if [ -f ".docker-data/ssl/server.crt" ] && [ -f ".docker-data/ssl/server.key" ]; then
    SSL_SOURCE="nginx/api-gateway-ssl.conf"
    SSL_MODE="true"
    echo "  SSL 인증서 감지 — HTTPS + HTTP 모드로 시작 ($SSL_DOMAIN)"
else
    SSL_SOURCE="nginx/api-gateway.conf"
    SSL_MODE=""
    echo "  SSL 인증서 없음 — HTTP 모드로 시작"
    echo "   인증서 생성: scripts/ssl/init-cert.sh"
fi

echo "  ICAO PKD Podman 컨테이너 시작... (DB_TYPE=$DB_TYPE)"
echo ""

# =============================================================================
# 1. 필요한 디렉토리 생성
# =============================================================================
echo "  디렉토리 생성 중..."
mkdir -p ./data/uploads ./data/cert ./logs ./backups 2>/dev/null || true
mkdir -p ./.docker-data/pkd-logs ./.docker-data/pkd-uploads 2>/dev/null || true
mkdir -p ./.docker-data/pa-logs ./.docker-data/sync-logs 2>/dev/null || true
mkdir -p ./.docker-data/monitoring-logs ./.docker-data/gateway-logs 2>/dev/null || true
mkdir -p ./.docker-data/ssl ./.docker-data/nginx 2>/dev/null || true
mkdir -p ./.docker-data/ai-analysis-logs 2>/dev/null || true

# 권한 설정
echo "  로그 디렉토리 권한 설정 중..."
chmod -R 777 ./.docker-data/pkd-logs ./.docker-data/pkd-uploads \
    ./.docker-data/pa-logs ./.docker-data/sync-logs \
    ./.docker-data/monitoring-logs ./.docker-data/gateway-logs \
    ./.docker-data/ai-analysis-logs 2>/dev/null || \
sudo chmod -R 777 ./.docker-data/pkd-logs ./.docker-data/pkd-uploads \
    ./.docker-data/pa-logs ./.docker-data/sync-logs \
    ./.docker-data/monitoring-logs ./.docker-data/gateway-logs \
    ./.docker-data/ai-analysis-logs

# SELinux context (RHEL 9 Enforcing)
# Rootless Podman needs container_file_t type AND no MCS categories (s0 only)
if command -v chcon > /dev/null 2>&1 && [ "$(getenforce 2>/dev/null)" = "Enforcing" ]; then
    echo "  SELinux 컨텍스트 설정 중..."
    SELINUX_DIRS=".docker-data/ data/cert nginx/ docs/openapi docker/db-oracle/init docker/init-scripts"
    for d in $SELINUX_DIRS; do
        if [ -e "$d" ]; then
            chcon -Rt container_file_t "$d" 2>/dev/null || true
            chcon -R -l s0 "$d" 2>/dev/null || true
        fi
    done
fi

# =============================================================================
# 2. Podman DNS resolver 설정 + nginx 설정 생성
# =============================================================================
echo "  nginx 설정 생성 중 (Podman DNS)..."

# Podman 네트워크 생성 (없으면)
# podman-compose 프로젝트명: docker (compose 파일 디렉토리명) → docker_pkd-network
NETWORK_NAME="docker_pkd-network"
if ! podman network exists "$NETWORK_NAME" 2>/dev/null; then
    podman network create "$NETWORK_NAME" 2>/dev/null || true
fi

# Podman aardvark-dns gateway IP 감지
PODMAN_DNS=""
if podman network exists "$NETWORK_NAME" 2>/dev/null; then
    PODMAN_DNS=$(podman network inspect "$NETWORK_NAME" 2>/dev/null | \
        python3 -c "import sys,json; nets=json.load(sys.stdin); print(nets[0]['subnets'][0]['gateway'])" 2>/dev/null || echo "")
fi

if [ -z "$PODMAN_DNS" ]; then
    # 네트워크 아직 없으면 기본 Podman DNS 사용
    # podman-compose 첫 실행 시 네트워크 자동 생성됨
    PODMAN_DNS="10.89.0.1"
    echo "  Podman DNS 자동 감지 실패 — 기본값 사용: $PODMAN_DNS"
else
    echo "  Podman DNS: $PODMAN_DNS"
fi

# Docker용 nginx 설정을 복사하고 resolver만 교체
sed "s|resolver 127.0.0.11|resolver $PODMAN_DNS|g" \
    "$SSL_SOURCE" > ".docker-data/nginx/api-gateway.conf"

echo "  nginx 설정 생성 완료: .docker-data/nginx/api-gateway.conf"

# NGINX_CONF를 생성된 설정으로 지정
export NGINX_CONF="../.docker-data/nginx/api-gateway.conf"

# =============================================================================
# 3. Podman Compose 시작
# =============================================================================
echo ""
echo "  Podman Compose 시작..."

if [ -n "$SKIP_APP" ]; then
    if [ -n "$SKIP_LDAP" ]; then
        $COMPOSE $PROFILE_FLAG up -d $BUILD_FLAG
    else
        $COMPOSE $PROFILE_FLAG up -d $BUILD_FLAG openldap1 openldap2
    fi
else
    $COMPOSE $PROFILE_FLAG up -d $BUILD_FLAG
fi

# =============================================================================
# 4. 컨테이너 상태 확인
# =============================================================================
echo ""
echo "  컨테이너 시작 대기 중..."
sleep 5

echo ""
echo "  컨테이너 상태:"
podman ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}" \
    --filter "name=icao-local-pkd" 2>/dev/null || $COMPOSE ps

# LDAP 초기화 확인
if [ -z "$SKIP_LDAP" ]; then
    echo ""
    echo "  LDAP 상태:"
    # Podman에서는 LDAP init 컨테이너가 없으므로 (스크립트로 처리)
    # clean-and-init.sh를 통해 초기화했는지 확인
    if podman exec icao-local-pkd-openldap1 ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
        echo "    OpenLDAP1: 정상"
    else
        echo "    OpenLDAP1: 시작 대기 중..."
    fi
fi

echo ""
echo "  접속 정보:"
echo "   - Database:      DB_TYPE=$DB_TYPE"
if [ "$DB_TYPE" = "oracle" ]; then
    echo "   - Oracle:        localhost:11521 (XEPDB1)"
else
    echo "   - PostgreSQL:    localhost:15432 (pkd/pkd)"
fi
if [ -z "$SKIP_LDAP" ]; then
    echo "   - OpenLDAP 1:    ldap://localhost:13891"
    echo "   - OpenLDAP 2:    ldap://localhost:13892"
fi
if [ -z "$SKIP_APP" ]; then
    echo "   - Frontend:      http://localhost:13080"
    if [ -n "$SSL_MODE" ]; then
        echo "   - API Gateway:   https://$SSL_DOMAIN/api (HTTPS)"
        echo "   - API Gateway:   http://$SSL_DOMAIN/api (HTTP)"
        echo "   - API Gateway:   http://localhost:18080/api (internal)"
    else
        echo "   - API Gateway:   http://localhost:18080/api"
    fi
    echo "   - Swagger UI:    http://localhost:18090"
fi
echo ""
echo "  로그 확인: scripts/podman/logs.sh [서비스명]"
echo "  중지:     scripts/podman/stop.sh"
echo "  재시작:   scripts/podman/restart.sh"
echo ""
echo "  옵션:"
echo "   --build      이미지 다시 빌드"
echo "   --skip-app   애플리케이션 제외 (인프라만 시작)"
echo "   --skip-ldap  OpenLDAP 제외"
echo ""
