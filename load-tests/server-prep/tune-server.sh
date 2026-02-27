#!/bin/bash
#
# tune-server.sh — Production 서버 부하 테스트 사전 튜닝
#
# 사용법: (Production 서버 10.0.0.220에서 root 또는 sudo로 실행)
#   sudo ./tune-server.sh apply    # 부하 테스트용 튜닝 적용
#   sudo ./tune-server.sh status   # 현재 설정 확인
#
set -euo pipefail

ACTION="${1:-status}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== ICAO Local PKD — 부하 테스트 서버 튜닝 ==="
echo "Action: $ACTION"
echo ""

show_status() {
    echo "--- OS 커널 파라미터 ---"
    echo "  net.core.somaxconn        = $(sysctl -n net.core.somaxconn)"
    echo "  net.ipv4.tcp_max_syn_backlog = $(sysctl -n net.ipv4.tcp_max_syn_backlog)"
    echo "  net.ipv4.ip_local_port_range = $(sysctl -n net.ipv4.ip_local_port_range)"
    echo "  net.core.netdev_max_backlog  = $(sysctl -n net.core.netdev_max_backlog)"
    echo "  fs.file-max                = $(sysctl -n fs.file-max)"
    echo ""

    echo "--- File Descriptor Limits ---"
    echo "  ulimit -n (soft) = $(ulimit -Sn)"
    echo "  ulimit -n (hard) = $(ulimit -Hn)"
    echo "  Current open FDs = $(cat /proc/sys/fs/file-nr | awk '{print $1}')"
    echo ""

    echo "--- TCP 연결 상태 ---"
    ss -s 2>/dev/null | head -5 || echo "  (ss not available)"
    echo ""
}

apply_tuning() {
    echo "[1/5] OS 커널 파라미터 튜닝..."
    sysctl -w net.core.somaxconn=65535
    sysctl -w net.ipv4.tcp_max_syn_backlog=65535
    sysctl -w net.ipv4.ip_local_port_range="1024 65535"
    sysctl -w net.core.netdev_max_backlog=65535
    sysctl -w fs.file-max=655350
    # TCP TIME_WAIT 재사용 (부하 테스트 시 유용)
    sysctl -w net.ipv4.tcp_tw_reuse=1
    echo "  -> 완료"
    echo ""

    echo "[2/5] File Descriptor Limits..."
    ulimit -n 65535 2>/dev/null || echo "  -> ulimit 변경은 /etc/security/limits.conf 수정 필요"
    echo ""

    echo "[3/5] nginx 부하 테스트 설정 배포..."
    if command -v podman &>/dev/null; then
        CONTAINER_CMD="podman"
    elif command -v docker &>/dev/null; then
        CONTAINER_CMD="docker"
    else
        echo "  -> ERROR: podman/docker 없음"
        return 1
    fi

    NGINX_CONTAINER=$($CONTAINER_CMD ps --format '{{.Names}}' | grep -i 'api-gateway' | head -1)
    if [ -n "$NGINX_CONTAINER" ]; then
        # 원본 백업
        $CONTAINER_CMD cp "$NGINX_CONTAINER:/etc/nginx/nginx.conf" "$SCRIPT_DIR/nginx-original-backup.conf" 2>/dev/null || true
        # 부하 테스트 설정 배포
        $CONTAINER_CMD cp "$SCRIPT_DIR/nginx-loadtest.conf" "$NGINX_CONTAINER:/etc/nginx/nginx.conf"
        $CONTAINER_CMD exec "$NGINX_CONTAINER" nginx -t && \
            $CONTAINER_CMD exec "$NGINX_CONTAINER" nginx -s reload
        echo "  -> nginx 부하 테스트 설정 적용 완료"
    else
        echo "  -> WARNING: api-gateway 컨테이너를 찾을 수 없음"
    fi
    echo ""

    echo "[4/5] 현재 설정 확인..."
    show_status

    echo "[5/5] 완료!"
    echo ""
    echo "=========================================="
    echo "  부하 테스트 튜닝 적용 완료"
    echo "  복구: ./restore-production.sh"
    echo "=========================================="
}

case "$ACTION" in
    apply)
        apply_tuning
        ;;
    status)
        show_status
        ;;
    *)
        echo "Usage: $0 {apply|status}"
        exit 1
        ;;
esac
