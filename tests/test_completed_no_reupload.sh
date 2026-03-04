#!/bin/bash
# =============================================================================
# Test: COMPLETED 업로드 재업로드/재처리 차단 검증
# =============================================================================
# 테스트 시나리오:
#   1. COMPLETED 업로드 retry → 400 Bad Request (차단)
#   2. FAILED 업로드 retry → 200 OK (허용)
#   3. COMPLETED 파일 동일 파일 재업로드 → 409 Duplicate (차단, canReupload=false)
#   4. FAILED 파일 동일 파일 재업로드 → 409 DUPLICATE_REUPLOADABLE (허용, canReupload=true)
# =============================================================================

API_BASE="http://localhost:18080/api"
PASS=0
FAIL=0
TOTAL=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

assert_eq() {
    local test_name="$1"
    local expected="$2"
    local actual="$3"
    TOTAL=$((TOTAL + 1))
    if [ "$expected" = "$actual" ]; then
        echo -e "  ${GREEN}PASS${NC} $test_name (expected=$expected, actual=$actual)"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}FAIL${NC} $test_name (expected=$expected, actual=$actual)"
        FAIL=$((FAIL + 1))
    fi
}

assert_contains() {
    local test_name="$1"
    local expected="$2"
    local actual="$3"
    TOTAL=$((TOTAL + 1))
    if echo "$actual" | grep -q "$expected"; then
        echo -e "  ${GREEN}PASS${NC} $test_name (contains '$expected')"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}FAIL${NC} $test_name (expected to contain '$expected', got: ${actual:0:100})"
        FAIL=$((FAIL + 1))
    fi
}

assert_not_contains() {
    local test_name="$1"
    local unexpected="$2"
    local actual="$3"
    TOTAL=$((TOTAL + 1))
    if echo "$actual" | grep -q "$unexpected"; then
        echo -e "  ${RED}FAIL${NC} $test_name (should NOT contain '$unexpected')"
        FAIL=$((FAIL + 1))
    else
        echo -e "  ${GREEN}PASS${NC} $test_name (does not contain '$unexpected')"
        PASS=$((PASS + 1))
    fi
}

echo -e "${BLUE}=============================================${NC}"
echo -e "${BLUE}COMPLETED 업로드 재업로드 차단 테스트${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""

# =============================================================================
# 0-1. JWT 토큰 획득
# =============================================================================
echo -e "${YELLOW}[Setup] JWT 토큰 획득...${NC}"
LOGIN_RESP=$(curl -s -X POST "$API_BASE/auth/login" \
    -H "Content-Type: application/json" \
    -d '{"username":"admin","password":"admin123"}')
JWT_TOKEN=$(echo "$LOGIN_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('access_token',''))" 2>/dev/null)

if [ -z "$JWT_TOKEN" ]; then
    echo -e "${RED}JWT 토큰 획득 실패. 테스트 중단.${NC}"
    echo "  Response: $LOGIN_RESP"
    exit 1
fi
echo "  JWT 토큰 획득 완료 (${JWT_TOKEN:0:20}...)"
AUTH_HEADER="Authorization: Bearer $JWT_TOKEN"
echo ""

# =============================================================================
# 0-2. 사전 준비: 업로드 목록에서 COMPLETED/FAILED 건 조회
# =============================================================================
echo -e "${YELLOW}[Setup] 기존 업로드 목록 조회...${NC}"
UPLOADS=$(curl -s "$API_BASE/upload/history?limit=10")
COMPLETED_ID=$(echo "$UPLOADS" | python3 -c "
import sys, json
data = json.load(sys.stdin)
for u in data.get('content', []):
    if u['status'] == 'COMPLETED':
        print(u['id'])
        break
" 2>/dev/null)

echo "  COMPLETED upload ID: ${COMPLETED_ID:-없음}"

if [ -z "$COMPLETED_ID" ]; then
    echo -e "${RED}COMPLETED 상태의 업로드가 없습니다. 테스트 불가.${NC}"
    exit 1
fi

# COMPLETED 업로드의 파일 해시 확인
COMPLETED_DETAIL=$(curl -s "$API_BASE/upload/detail/$COMPLETED_ID")
COMPLETED_FILENAME=$(echo "$COMPLETED_DETAIL" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('data',{}).get('fileName',''))" 2>/dev/null)
COMPLETED_FORMAT=$(echo "$COMPLETED_DETAIL" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('data',{}).get('fileFormat',''))" 2>/dev/null)
echo "  COMPLETED file: $COMPLETED_FILENAME (format: $COMPLETED_FORMAT)"
echo ""

# =============================================================================
# Test 1: COMPLETED 업로드 retry → 400 Bad Request
# =============================================================================
echo -e "${YELLOW}[Test 1] COMPLETED 업로드 retry 시도 → 400 예상${NC}"
RETRY_RESP=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$API_BASE/upload/$COMPLETED_ID/retry")
RETRY_BODY=$(curl -s -X POST "$API_BASE/upload/$COMPLETED_ID/retry")

assert_eq "HTTP status code" "400" "$RETRY_RESP"
assert_contains "에러 메시지에 'Only FAILED' 포함" "Only FAILED" "$RETRY_BODY"
assert_not_contains "COMPLETED 허용 메시지 없음" "COMPLETED.*can be retried" "$RETRY_BODY"
echo ""

# =============================================================================
# Test 2: PROCESSING 상태 업로드 retry → 400 Bad Request
# =============================================================================
echo -e "${YELLOW}[Test 2] PROCESSING 상태 업로드 retry (존재하면) → 400 예상${NC}"
PROCESSING_ID=$(echo "$UPLOADS" | python3 -c "
import sys, json
data = json.load(sys.stdin)
for u in data.get('content', []):
    if u['status'] == 'PROCESSING':
        print(u['id'])
        break
" 2>/dev/null)

if [ -n "$PROCESSING_ID" ]; then
    PROC_RESP=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$API_BASE/upload/$PROCESSING_ID/retry")
    assert_eq "PROCESSING retry → 400" "400" "$PROC_RESP"
else
    echo -e "  ${BLUE}SKIP${NC} PROCESSING 상태 업로드 없음"
fi
echo ""

# =============================================================================
# Test 3: COMPLETED 파일과 동일한 파일 재업로드 → 409 Duplicate (재업로드 불가)
# =============================================================================
echo -e "${YELLOW}[Test 3] COMPLETED 파일 동일 내용 재업로드 → 409 DUPLICATE 예상 (canReupload=false)${NC}"

# 원본 파일 경로 확인 (컨테이너 내부에서 파일을 가져옴)
FILE_PATH="/app/uploads/${COMPLETED_ID}.ldif"
TEMP_FILE="/tmp/test_reupload_$$.ldif"

# 컨테이너에서 파일 복사
docker cp "icao-local-pkd-management:$FILE_PATH" "$TEMP_FILE" 2>/dev/null
if [ -f "$TEMP_FILE" ]; then
    REUPLOAD_HTTP=$(curl -s -o /tmp/reupload_resp_$$.json -w "%{http_code}" \
        -X POST "$API_BASE/upload/ldif" \
        -H "$AUTH_HEADER" \
        -F "file=@${TEMP_FILE};filename=${COMPLETED_FILENAME}")
    REUPLOAD_BODY=$(cat /tmp/reupload_resp_$$.json)

    assert_eq "HTTP status code" "409" "$REUPLOAD_HTTP"

    # canReupload가 없거나 false여야 함 (COMPLETED는 재업로드 불가)
    CAN_REUPLOAD=$(echo "$REUPLOAD_BODY" | python3 -c "
import sys, json
data = json.load(sys.stdin)
print(str(data.get('canReupload', 'NOT_PRESENT')).lower())
" 2>/dev/null)

    # canReupload가 없거나 false → PASS (COMPLETED 파일은 재업로드 차단)
    if [ "$CAN_REUPLOAD" = "not_present" ] || [ "$CAN_REUPLOAD" = "false" ]; then
        TOTAL=$((TOTAL + 1))
        echo -e "  ${GREEN}PASS${NC} canReupload=$CAN_REUPLOAD (COMPLETED 재업로드 차단됨)"
        PASS=$((PASS + 1))
    else
        TOTAL=$((TOTAL + 1))
        echo -e "  ${RED}FAIL${NC} canReupload=$CAN_REUPLOAD (COMPLETED 파일인데 재업로드 허용됨!)"
        FAIL=$((FAIL + 1))
    fi

    # error.code 확인 — DUPLICATE_FILE이어야 함 (DUPLICATE_REUPLOADABLE이 아님)
    ERROR_CODE=$(echo "$REUPLOAD_BODY" | python3 -c "
import sys, json
data = json.load(sys.stdin)
print(data.get('error', {}).get('code', 'NONE'))
" 2>/dev/null)
    assert_eq "error code" "DUPLICATE_FILE" "$ERROR_CODE"

    rm -f "$TEMP_FILE" /tmp/reupload_resp_$$.json
else
    echo -e "  ${BLUE}SKIP${NC} 원본 파일을 컨테이너에서 복사할 수 없음"
fi
echo ""

# =============================================================================
# Test 4: FAILED 상태 업로드 생성 → retry 허용 검증
# =============================================================================
echo -e "${YELLOW}[Test 4] FAILED 업로드 retry → 200 예상${NC}"

# DB에서 FAILED 상태의 업로드 찾기
FAILED_ID=$(echo "$UPLOADS" | python3 -c "
import sys, json
data = json.load(sys.stdin)
for u in data.get('content', []):
    if u['status'] == 'FAILED':
        print(u['id'])
        break
" 2>/dev/null)

if [ -n "$FAILED_ID" ]; then
    FAILED_RETRY_HTTP=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$API_BASE/upload/$FAILED_ID/retry")
    assert_eq "FAILED retry → 200" "200" "$FAILED_RETRY_HTTP"
else
    # FAILED가 없으면 COMPLETED 하나를 임시로 FAILED로 변경하여 테스트
    # DB 직접 조작: 특정 COMPLETED 업로드를 FAILED로 임시 변경
    echo "  FAILED 업로드 없음 → COMPLETED 건을 DB에서 임시 FAILED 변경"

    # 2번째 COMPLETED 업로드 사용 (첫번째는 Test 1에서 사용)
    SECOND_COMPLETED=$(echo "$UPLOADS" | python3 -c "
import sys, json
data = json.load(sys.stdin)
found = 0
for u in data.get('content', []):
    if u['status'] == 'COMPLETED':
        found += 1
        if found == 2:
            print(u['id'])
            break
" 2>/dev/null)

    if [ -n "$SECOND_COMPLETED" ]; then
        # DB 임시 변경
        docker exec icao-local-pkd-postgres psql -U pkd -d localpkd -c \
            "UPDATE uploaded_file SET status='FAILED' WHERE id='$SECOND_COMPLETED'" 2>/dev/null

        FAILED_RETRY_HTTP=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$API_BASE/upload/$SECOND_COMPLETED/retry")
        assert_eq "FAILED retry → 200" "200" "$FAILED_RETRY_HTTP"

        # 원복 (PROCESSING으로 바뀌었을 수 있으므로 대기 후 확인)
        sleep 2
        docker exec icao-local-pkd-postgres psql -U pkd -d localpkd -c \
            "UPDATE uploaded_file SET status='COMPLETED' WHERE id='$SECOND_COMPLETED' AND status IN ('PENDING','PROCESSING','FAILED')" 2>/dev/null
    else
        echo -e "  ${BLUE}SKIP${NC} 테스트용 업로드 부족"
    fi
fi
echo ""

# =============================================================================
# Test 5: 존재하지 않는 업로드 retry → 404
# =============================================================================
echo -e "${YELLOW}[Test 5] 존재하지 않는 업로드 retry → 404 예상${NC}"
FAKE_ID="00000000-0000-0000-0000-000000000000"
NOTFOUND_HTTP=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$API_BASE/upload/$FAKE_ID/retry")
assert_eq "Not found retry → 404" "404" "$NOTFOUND_HTTP"
echo ""

# =============================================================================
# Test 6: PENDING 상태 업로드 retry → 400
# =============================================================================
echo -e "${YELLOW}[Test 6] PENDING 상태 업로드 retry → 400 예상${NC}"
PENDING_ID=$(echo "$UPLOADS" | python3 -c "
import sys, json
data = json.load(sys.stdin)
for u in data.get('content', []):
    if u['status'] == 'PENDING':
        print(u['id'])
        break
" 2>/dev/null)

if [ -n "$PENDING_ID" ]; then
    PENDING_HTTP=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$API_BASE/upload/$PENDING_ID/retry")
    assert_eq "PENDING retry → 400" "400" "$PENDING_HTTP"
else
    echo -e "  ${BLUE}SKIP${NC} PENDING 상태 업로드 없음"
fi
echo ""

# =============================================================================
# 결과 요약
# =============================================================================
echo -e "${BLUE}=============================================${NC}"
echo -e "${BLUE}테스트 결과 요약${NC}"
echo -e "${BLUE}=============================================${NC}"
echo -e "  전체: $TOTAL"
echo -e "  ${GREEN}PASS: $PASS${NC}"
echo -e "  ${RED}FAIL: $FAIL${NC}"
echo ""

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}모든 테스트 통과!${NC}"
    exit 0
else
    echo -e "${RED}실패한 테스트가 있습니다.${NC}"
    exit 1
fi
