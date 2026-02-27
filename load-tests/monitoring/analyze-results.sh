#!/bin/bash
#
# analyze-results.sh — 부하 테스트 결과 분석
#
# 사용법:
#   ./analyze-results.sh reports/20260227_143000/     # 특정 결과 분석
#   ./analyze-results.sh                               # 최신 결과 분석
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPORTS_DIR="$SCRIPT_DIR/../reports"

# Find results directory
if [ -n "${1:-}" ]; then
    RESULT_DIR="$1"
else
    RESULT_DIR=$(ls -dt "$REPORTS_DIR"/20* 2>/dev/null | head -1)
fi

if [ -z "$RESULT_DIR" ] || [ ! -d "$RESULT_DIR" ]; then
    echo "ERROR: 결과 디렉토리를 찾을 수 없음"
    echo "Usage: $0 [results-dir]"
    exit 1
fi

echo "=== 부하 테스트 결과 분석 ==="
echo "결과 디렉토리: $RESULT_DIR"
echo ""

# 1. Container peak stats
if [ -f "$RESULT_DIR/container-stats.csv" ]; then
    echo "--- Container 최대 리소스 사용 ---"
    echo ""
    # Skip header, find max CPU and Memory per container
    tail -n +2 "$RESULT_DIR/container-stats.csv" | \
        awk -F',' '{
            if ($3+0 > max_cpu[$2]) max_cpu[$2] = $3+0;
            if ($4+0 > max_mem[$2]) max_mem[$2] = $4+0;
        }
        END {
            printf "  %-30s %10s %12s\n", "Container", "Max CPU%", "Max Mem(MB)"
            printf "  %-30s %10s %12s\n", "------------------------------", "----------", "------------"
            for (name in max_cpu)
                printf "  %-30s %10.1f %12.1f\n", name, max_cpu[name], max_mem[name]
        }'
    echo ""
fi

# 2. Host resource summary
if [ -f "$RESULT_DIR/host-stats.csv" ]; then
    echo "--- Host 리소스 요약 ---"
    tail -n +2 "$RESULT_DIR/host-stats.csv" | \
        awk -F',' '{
            if ($2+0 > max_load1) max_load1 = $2+0;
            if ($5+0 > max_mem_used) max_mem_used = $5+0;
            if ($6+0 < min_mem_avail || NR==1) min_mem_avail = $6+0;
            mem_total = $4;
        }
        END {
            printf "  Max Load (1m):     %.2f\n", max_load1
            printf "  Max Memory Used:   %d MB / %d MB\n", max_mem_used, mem_total
            printf "  Min Memory Avail:  %d MB\n", min_mem_avail
        }'
    echo ""
fi

# 3. TCP connection peaks
if [ -f "$RESULT_DIR/tcp-stats.csv" ]; then
    echo "--- TCP 연결 최대값 ---"
    tail -n +2 "$RESULT_DIR/tcp-stats.csv" | \
        awk -F',' '{
            if ($3+0 > max_estab) max_estab = $3+0;
            if ($4+0 > max_tw) max_tw = $4+0;
            if ($5+0 > max_cw) max_cw = $5+0;
        }
        END {
            printf "  Max ESTABLISHED: %d\n", max_estab
            printf "  Max TIME_WAIT:   %d\n", max_tw
            printf "  Max CLOSE_WAIT:  %d\n", max_cw
        }'
    echo ""
fi

# 4. nginx log analysis (if available on the server)
echo "--- nginx 로그 분석 명령 (서버에서 실행) ---"
echo ""
echo '  # 응답 코드 분포'
echo '  podman exec api-gateway awk '"'"'{print $9}'"'"' /var/log/nginx/access.log | sort | uniq -c | sort -rn'
echo ""
echo '  # 가장 느린 요청 top 20'
echo '  podman exec api-gateway grep "rt=" /var/log/nginx/access.log | sed "s/.*rt=//" | sort -t" " -k1 -rn | head -20'
echo ""
echo '  # Rate limit (429) 요청 수'
echo '  podman exec api-gateway grep " 429 " /var/log/nginx/access.log | wc -l'
echo ""
echo '  # 서버 에러 (5xx) 요청 수'
echo '  podman exec api-gateway grep -E " 50[0-9] " /var/log/nginx/access.log | wc -l'
echo ""

echo "=== 분석 완료 ==="
