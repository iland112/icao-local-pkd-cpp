#!/bin/bash
#
# restore-production.sh — 부하 테스트 후 Production 설정 복구
#
# 사용법: (Production 서버에서 실행)
#   sudo ./restore-production.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=== Production 설정 복구 ==="
echo ""

# Container runtime detection
if command -v podman &>/dev/null; then
    CONTAINER_CMD="podman"
elif command -v docker &>/dev/null; then
    CONTAINER_CMD="docker"
else
    echo "ERROR: podman/docker 없음"
    exit 1
fi

# 1. nginx 설정 복구
echo "[1/3] nginx Production 설정 복구..."
NGINX_CONTAINER=$($CONTAINER_CMD ps --format '{{.Names}}' | grep -i 'api-gateway' | head -1)
if [ -n "$NGINX_CONTAINER" ]; then
    # 백업에서 복구 시도, 없으면 원본 사용
    if [ -f "$SCRIPT_DIR/nginx-original-backup.conf" ]; then
        $CONTAINER_CMD cp "$SCRIPT_DIR/nginx-original-backup.conf" "$NGINX_CONTAINER:/etc/nginx/nginx.conf"
        echo "  -> 백업에서 복구 완료"
    elif [ -f "$PROJECT_ROOT/nginx/api-gateway-ssl.conf" ]; then
        $CONTAINER_CMD cp "$PROJECT_ROOT/nginx/api-gateway-ssl.conf" "$NGINX_CONTAINER:/etc/nginx/nginx.conf"
        echo "  -> 원본 파일에서 복구 완료"
    else
        echo "  -> WARNING: 복구할 설정 파일 없음"
    fi
    $CONTAINER_CMD exec "$NGINX_CONTAINER" nginx -t && \
        $CONTAINER_CMD exec "$NGINX_CONTAINER" nginx -s reload
    echo "  -> nginx reload 완료"
else
    echo "  -> WARNING: api-gateway 컨테이너를 찾을 수 없음"
fi
echo ""

# 2. OS 커널 파라미터 복구 (기본값)
echo "[2/3] OS 커널 파라미터 복구..."
sysctl -w net.core.somaxconn=4096 2>/dev/null || true
sysctl -w net.ipv4.tcp_max_syn_backlog=4096 2>/dev/null || true
sysctl -w net.ipv4.ip_local_port_range="32768 60999" 2>/dev/null || true
sysctl -w net.core.netdev_max_backlog=1000 2>/dev/null || true
sysctl -w net.ipv4.tcp_tw_reuse=0 2>/dev/null || true
echo "  -> 완료"
echo ""

# 3. 확인
echo "[3/3] 현재 설정 확인..."
echo "  net.core.somaxconn        = $(sysctl -n net.core.somaxconn)"
echo "  net.ipv4.tcp_max_syn_backlog = $(sysctl -n net.ipv4.tcp_max_syn_backlog)"
echo "  net.ipv4.ip_local_port_range = $(sysctl -n net.ipv4.ip_local_port_range)"
echo ""

echo "=== Production 설정 복구 완료 ==="
