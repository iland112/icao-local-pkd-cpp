#!/bin/bash
# =============================================================================
# Oracle XE SPFILE 복구 스크립트
# =============================================================================
# 사용 시나리오: 이전 00-ee-tuning.sql이 EE 파라미터(SGA 4GB, PGA 2GB, PROCESSES 1000)를
# SPFILE에 기록하여 ORA-56752로 Oracle이 기동 불가한 상태를 복구합니다.
#
# XE 21c 하드 제한: PROCESSES≤150, SESSIONS≤248, SGA≤2GB, PGA≤1GB
#
# 사용법:
#   scripts/podman/fix-oracle-memory.sh
#
# 주의: Oracle 컨테이너가 실행 중이어야 합니다.
#       SPFILE 변경 후 Oracle 재시작이 필요합니다.
# =============================================================================

set -e

CONTAINER="icao-local-pkd-oracle"

echo "  Oracle XE SPFILE 복구"
echo ""

# 컨테이너 상태 확인
if ! podman ps --format "{{.Names}}" | grep -q "^${CONTAINER}$"; then
    echo "  오류: ${CONTAINER} 컨테이너가 실행 중이지 않습니다."
    echo "  'podman start ${CONTAINER}' 로 먼저 시작하세요."
    exit 1
fi

echo "  현재 SPFILE 파라미터 확인 중..."
podman exec "$CONTAINER" bash -c '
sqlplus -s / as sysdba <<EOF
SET LINESIZE 80
SET PAGESIZE 20
COLUMN NAME FORMAT A30
COLUMN VALUE FORMAT A20
SELECT name, value FROM v\$parameter
WHERE name IN ('"'"'sga_target'"'"', '"'"'pga_aggregate_target'"'"', '"'"'processes'"'"', '"'"'open_cursors'"'"')
ORDER BY name;
EXIT;
EOF
' 2>/dev/null || echo "  (Oracle 미기동 상태 — SPFILE 직접 수정 필요)"

echo ""
echo "  XE 호환 값으로 SPFILE 수정 중..."
podman exec "$CONTAINER" bash -c '
sqlplus -s / as sysdba <<EOF
ALTER SYSTEM SET SGA_TARGET=1G SCOPE=SPFILE;
ALTER SYSTEM SET PGA_AGGREGATE_TARGET=512M SCOPE=SPFILE;
ALTER SYSTEM SET PROCESSES=150 SCOPE=SPFILE;
ALTER SYSTEM SET OPEN_CURSORS=300 SCOPE=SPFILE;
EXIT;
EOF
' 2>/dev/null

if [ $? -eq 0 ]; then
    echo "  SPFILE 수정 완료!"
    echo ""
    echo "  Oracle 재시작이 필요합니다:"
    echo "  podman restart ${CONTAINER}"
else
    echo "  SPFILE 수정 실패 — Oracle이 기동되지 않은 상태일 수 있습니다."
    echo ""
    echo "  수동 복구 절차:"
    echo "  1. podman exec -it ${CONTAINER} bash"
    echo "  2. sqlplus / as sysdba"
    echo "  3. CREATE PFILE='/tmp/initXE.ora' FROM SPFILE;"
    echo "     (또는 수동 pfile 작성)"
    echo "  4. pfile에서 sga_target, pga_aggregate_target, processes 값 수정"
    echo "  5. CREATE SPFILE FROM PFILE='/tmp/initXE.ora';"
    echo "  6. STARTUP"
fi
