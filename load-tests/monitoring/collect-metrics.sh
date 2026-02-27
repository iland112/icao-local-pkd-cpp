#!/bin/bash
#
# collect-metrics.sh — 부하 테스트 중 서버 메트릭 수집 (5초 간격)
#
# 사용법: (Production 서버에서 실행)
#   ./collect-metrics.sh              # 포그라운드
#   ./collect-metrics.sh &            # 백그라운드
#   ./collect-metrics.sh stop         # 중지
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="$SCRIPT_DIR/../reports/$(date +%Y%m%d_%H%M%S)"
INTERVAL=5
PID_FILE="/tmp/loadtest-metrics.pid"

# Container runtime
if command -v podman &>/dev/null; then
    CONTAINER_CMD="podman"
elif command -v docker &>/dev/null; then
    CONTAINER_CMD="docker"
else
    echo "ERROR: podman/docker not found"
    exit 1
fi

stop_collection() {
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE")
        kill "$PID" 2>/dev/null || true
        rm -f "$PID_FILE"
        echo "메트릭 수집 중지 (PID: $PID)"
    else
        echo "실행 중인 수집기 없음"
    fi
    exit 0
}

if [ "${1:-}" = "stop" ]; then
    stop_collection
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"
echo $$ > "$PID_FILE"

echo "=== 부하 테스트 메트릭 수집 시작 ==="
echo "출력 디렉토리: $OUTPUT_DIR"
echo "수집 간격: ${INTERVAL}초"
echo "중지: ./collect-metrics.sh stop  또는  kill $$"
echo ""

# Cleanup on exit
trap 'rm -f "$PID_FILE"; echo "메트릭 수집 종료"' EXIT

# Header for CSV files
echo "timestamp,container,cpu_percent,mem_usage_mb,mem_limit_mb,net_input_mb,net_output_mb" > "$OUTPUT_DIR/container-stats.csv"
echo "timestamp,load_1m,load_5m,load_15m,mem_total_mb,mem_used_mb,mem_available_mb" > "$OUTPUT_DIR/host-stats.csv"
echo "timestamp,total,established,time_wait,close_wait,syn_recv" > "$OUTPUT_DIR/tcp-stats.csv"

collect_once() {
    local TS
    TS=$(date +%s)
    local TS_HUMAN
    TS_HUMAN=$(date '+%Y-%m-%d %H:%M:%S')

    # 1. Container stats
    $CONTAINER_CMD stats --no-stream --format '{{.Name}},{{.CPUPerc}},{{.MemUsage}}' 2>/dev/null | while IFS=',' read -r NAME CPU MEM; do
        # Parse memory: "123.4MiB / 15.58GiB" → used_mb, limit_mb
        USED=$(echo "$MEM" | awk -F'/' '{print $1}' | sed 's/[^0-9.]//g')
        UNIT=$(echo "$MEM" | awk -F'/' '{print $1}' | sed 's/[0-9. ]//g')
        LIMIT=$(echo "$MEM" | awk -F'/' '{print $2}' | sed 's/[^0-9.]//g')
        LUNIT=$(echo "$MEM" | awk -F'/' '{print $2}' | sed 's/[0-9. ]//g')

        # Convert to MB
        case "$UNIT" in
            GiB) USED_MB=$(echo "$USED * 1024" | bc 2>/dev/null || echo "$USED") ;;
            KiB) USED_MB=$(echo "$USED / 1024" | bc 2>/dev/null || echo "$USED") ;;
            *) USED_MB="$USED" ;;
        esac
        case "$LUNIT" in
            GiB) LIMIT_MB=$(echo "$LIMIT * 1024" | bc 2>/dev/null || echo "$LIMIT") ;;
            *) LIMIT_MB="$LIMIT" ;;
        esac

        CPU_NUM=$(echo "$CPU" | sed 's/%//')
        echo "$TS,$NAME,$CPU_NUM,$USED_MB,$LIMIT_MB,0,0" >> "$OUTPUT_DIR/container-stats.csv"
    done

    # 2. Host stats
    LOAD=$(uptime | awk -F'load average: ' '{print $2}' | tr -d ' ')
    LOAD_1=$(echo "$LOAD" | cut -d',' -f1)
    LOAD_5=$(echo "$LOAD" | cut -d',' -f2)
    LOAD_15=$(echo "$LOAD" | cut -d',' -f3)

    MEM_INFO=$(free -m | grep Mem)
    MEM_TOTAL=$(echo "$MEM_INFO" | awk '{print $2}')
    MEM_USED=$(echo "$MEM_INFO" | awk '{print $3}')
    MEM_AVAIL=$(echo "$MEM_INFO" | awk '{print $7}')
    echo "$TS,$LOAD_1,$LOAD_5,$LOAD_15,$MEM_TOTAL,$MEM_USED,$MEM_AVAIL" >> "$OUTPUT_DIR/host-stats.csv"

    # 3. TCP connection stats
    if command -v ss &>/dev/null; then
        TOTAL=$(ss -s 2>/dev/null | grep "TCP:" | awk '{print $2}')
        ESTAB=$(ss -s 2>/dev/null | grep "TCP:" | grep -oP 'estab \K[0-9]+' || echo "0")
        TW=$(ss -s 2>/dev/null | grep "TCP:" | grep -oP 'timewait \K[0-9]+' || echo "0")
        CW=$(ss -s 2>/dev/null | grep "TCP:" | grep -oP 'closewait \K[0-9]+' || echo "0")
        SR=$(ss -s 2>/dev/null | grep "TCP:" | grep -oP 'synrecv \K[0-9]+' || echo "0")
        echo "$TS,$TOTAL,$ESTAB,$TW,$CW,$SR" >> "$OUTPUT_DIR/tcp-stats.csv"
    fi

    # 4. Human-readable snapshot (every 30 seconds)
    if [ $((TS % 30)) -lt $INTERVAL ]; then
        echo "[$TS_HUMAN] Load: $LOAD_1/$LOAD_5/$LOAD_15 | Mem: ${MEM_USED}/${MEM_TOTAL}MB | TCP estab: $ESTAB TW: $TW"
    fi
}

# Main loop
while true; do
    collect_once
    sleep $INTERVAL
done
