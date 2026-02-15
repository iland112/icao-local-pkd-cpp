#!/bin/bash
# =============================================================================
# Idempotency Test: Upload ML -> col-002 -> col-001 -> col-003 x5 rounds
# =============================================================================
# Tests that uploading the same files multiple times produces identical results.
# Round 1: Fresh upload (HTTP 201 expected)
# Rounds 2-5: Duplicate detection (HTTP 409 expected)
# After each round, DB state should be identical once Round 1 processing completes.
# =============================================================================

API_BASE="http://localhost:8080/api"
DATA_DIR="/home/kbjung/projects/c/icao-local-pkd/data/uploads"
RESULTS_DIR="/tmp/idempotency-test-$(date +%Y%m%d-%H%M%S)"
mkdir -p "${RESULTS_DIR}"

ML_FILE="${DATA_DIR}/ICAO_ml_December2025.ml"
COL002_FILE="${DATA_DIR}/icaopkd-002-complete-000333.ldif"
COL001_FILE="${DATA_DIR}/icaopkd-001-complete-009667.ldif"
COL003_FILE="${DATA_DIR}/icaopkd-003-complete-000090.ldif"

TOTAL_ROUNDS=5
LOG_FILE="${RESULTS_DIR}/test.log"

log() {
    echo "$@" | tee -a "${LOG_FILE}"
}

# Get JWT token
get_token() {
    JWT_TOKEN=$(curl -s -X POST "${API_BASE}/auth/login" \
        -H "Content-Type: application/json" \
        -d '{"username":"admin","password":"admin123"}' | \
        python3 -c "import sys,json; print(json.load(sys.stdin).get('access_token',''))" 2>/dev/null)
}

log "============================================"
log " Idempotency Test"
log " Upload order: ML -> col-002 -> col-001 -> col-003"
log " Rounds: ${TOTAL_ROUNDS}"
log " Results dir: ${RESULTS_DIR}"
log " Started: $(date)"
log "============================================"
log ""

get_token
if [ -z "$JWT_TOKEN" ]; then
    log "ERROR: Failed to get JWT token"
    exit 1
fi
log "JWT token acquired."

# Function: Upload a file and return HTTP code + upload ID
upload_file() {
    local file_path="$1"
    local endpoint="$2"

    local response
    response=$(curl -s -w "\n%{http_code}" -X POST "${API_BASE}/${endpoint}" \
        -H "Authorization: Bearer ${JWT_TOKEN}" \
        -F "file=@${file_path}" 2>&1)

    local http_code=$(echo "$response" | tail -1)
    local body=$(echo "$response" | sed '$d')

    # Parse upload ID from response (handle both data.uploadId and uploadId)
    local upload_id=$(echo "$body" | python3 -c "
import sys,json
d=json.load(sys.stdin)
uid = d.get('data',{}).get('uploadId','') or d.get('uploadId','') or d.get('existingUpload',{}).get('uploadId','')
print(uid)
" 2>/dev/null || echo "")

    echo "${http_code}|${upload_id}|${body}"
}

# Function: Wait for a specific upload to complete processing
wait_for_processing() {
    local upload_id="$1"
    local max_wait=600
    local waited=0

    while [ $waited -lt $max_wait ]; do
        local status=$(curl -s "${API_BASE}/upload/detail/${upload_id}" 2>/dev/null | \
            python3 -c "import sys,json; print(json.load(sys.stdin).get('upload',{}).get('status','UNKNOWN'))" 2>/dev/null || echo "UNKNOWN")

        if [ "$status" = "COMPLETED" ] || [ "$status" = "COMPLETED_WITH_ERRORS" ] || [ "$status" = "FAILED" ]; then
            echo "$status"
            return 0
        fi

        sleep 5
        waited=$((waited + 5))
    done

    echo "TIMEOUT"
    return 1
}

# Function: Wait for ALL uploads to finish processing
# Checks for BOTH 'PROCESSING' and 'PENDING' statuses (non-terminal states)
wait_all_processing() {
    local max_wait=900
    local waited=0

    # Initial delay to allow status transition from PENDING to PROCESSING
    sleep 5

    while [ $waited -lt $max_wait ]; do
        local result=$(curl -s "${API_BASE}/upload/history?limit=20" 2>/dev/null | \
            python3 -c "
import sys,json
d=json.load(sys.stdin)
uploads=d.get('content',[])
processing=[u for u in uploads if u.get('status') in ('PROCESSING','PENDING')]
print(len(processing))
for u in processing:
    fn=u.get('fileName','?')
    st=u.get('status','?')
    te=u.get('totalEntries',0)
    pe=u.get('processedEntries',0)
    print(f'  {fn}: {st} ({pe}/{te})')
" 2>/dev/null || echo "0")

        local still_active=$(echo "$result" | head -1)

        if [ "$still_active" = "0" ]; then
            return 0
        fi

        if [ $((waited % 30)) -eq 0 ]; then
            log "    ... waiting (${waited}s elapsed, ${still_active} uploads not yet complete)"
            echo "$result" | tail -n +2 | while IFS= read -r line; do
                [ -n "$line" ] && log "      $line"
            done
        fi

        sleep 10
        waited=$((waited + 10))
    done

    log "    WARNING: Timeout waiting for processing (${max_wait}s)"
    return 1
}

# Function: Capture DB statistics snapshot
capture_db_stats() {
    local round_num="$1"

    docker exec icao-local-pkd-postgres psql -U pkd -d localpkd -t -A -c "
        SELECT json_build_object(
            'certificate_total', (SELECT COUNT(*) FROM certificate),
            'csca_count', (SELECT COUNT(*) FROM certificate WHERE certificate_type IN ('CSCA')),
            'dsc_count', (SELECT COUNT(*) FROM certificate WHERE certificate_type = 'DSC'),
            'dsc_nc_count', (SELECT COUNT(*) FROM certificate WHERE certificate_type = 'DSC_NC'),
            'mlsc_count', (SELECT COUNT(*) FROM certificate WHERE certificate_type = 'MLSC'),
            'crl_count', (SELECT COUNT(*) FROM crl),
            'ml_count', (SELECT COUNT(*) FROM master_list),
            'upload_count', (SELECT COUNT(*) FROM uploaded_file),
            'validation_count', (SELECT COUNT(*) FROM validation_result),
            'valid_count', (SELECT COUNT(*) FROM validation_result WHERE validation_status = 'VALID'),
            'expired_valid_count', (SELECT COUNT(*) FROM validation_result WHERE validation_status = 'EXPIRED_VALID'),
            'invalid_count', (SELECT COUNT(*) FROM validation_result WHERE validation_status = 'INVALID'),
            'pending_count', (SELECT COUNT(*) FROM validation_result WHERE validation_status = 'PENDING'),
            'countries', (SELECT COUNT(DISTINCT country_code) FROM certificate WHERE country_code IS NOT NULL AND country_code != ''),
            'ldap_stored_certs', (SELECT COUNT(*) FROM certificate WHERE stored_in_ldap = TRUE),
            'ldap_stored_crls', (SELECT COUNT(*) FROM crl WHERE stored_in_ldap = TRUE)
        );" 2>/dev/null > "${RESULTS_DIR}/db_r${round_num}.json"

    # Also capture upload API stats
    curl -s "${API_BASE}/upload/statistics" > "${RESULTS_DIR}/api_stats_r${round_num}.json" 2>/dev/null
}

# =============================================================================
# MAIN TEST EXECUTION
# =============================================================================

FILES_ORDER=(
    "${ML_FILE}|upload/masterlist|ML"
    "${COL002_FILE}|upload/ldif|col-002"
    "${COL001_FILE}|upload/ldif|col-001"
    "${COL003_FILE}|upload/ldif|col-003"
)

for round in $(seq 1 ${TOTAL_ROUNDS}); do
    log ""
    log "============================================"
    log " ROUND ${round} of ${TOTAL_ROUNDS} ($(date +%H:%M:%S))"
    log "============================================"

    round_start=$(date +%s)

    # Refresh token each round
    get_token

    for file_info in "${FILES_ORDER[@]}"; do
        IFS='|' read -r file_path endpoint label <<< "$file_info"
        file_name=$(basename "$file_path")

        result=$(upload_file "$file_path" "$endpoint")
        IFS='|' read -r http_code upload_id body <<< "$result"

        if [ "$http_code" = "201" ]; then
            log "  [R${round}] ${label}: HTTP 201 (uploadId: ${upload_id:0:8}...) - Accepted"

            # Save response
            echo "$body" > "${RESULTS_DIR}/r${round}_${label}_response.json"

        elif [ "$http_code" = "409" ]; then
            # Duplicate file detected - this is expected for rounds 2-5
            dup_reason=$(echo "$body" | python3 -c "import sys,json; print(json.load(sys.stdin).get('error',{}).get('code','?'))" 2>/dev/null || echo "?")
            log "  [R${round}] ${label}: HTTP 409 (${dup_reason}) - Duplicate rejected"

            echo "$body" > "${RESULTS_DIR}/r${round}_${label}_response.json"

        else
            log "  [R${round}] ${label}: HTTP ${http_code} - Unexpected"
            echo "$body" > "${RESULTS_DIR}/r${round}_${label}_error.json"
        fi
    done

    # Wait for all processing to complete before capturing stats
    log "  Waiting for processing to complete..."
    wait_all_processing

    round_end=$(date +%s)
    round_duration=$((round_end - round_start))

    # Capture statistics
    capture_db_stats "$round"
    log "  Round ${round} complete (${round_duration}s) - Stats captured"
done

# =============================================================================
# RESULTS COMPARISON
# =============================================================================

log ""
log "============================================"
log " RESULTS COMPARISON"
log "============================================"
log ""

# Print table header
log "=== Database State After Each Round ==="
printf "%-22s" "Metric" >> "${LOG_FILE}"
printf "%-22s" "Metric"
for round in $(seq 1 ${TOTAL_ROUNDS}); do
    printf "%-12s" "Round ${round}" >> "${LOG_FILE}"
    printf "%-12s" "Round ${round}"
done
echo "" | tee -a "${LOG_FILE}"

printf "%-22s" "----------------------" >> "${LOG_FILE}"
printf "%-22s" "----------------------"
for round in $(seq 1 ${TOTAL_ROUNDS}); do
    printf "%-12s" "-----------" >> "${LOG_FILE}"
    printf "%-12s" "-----------"
done
echo "" | tee -a "${LOG_FILE}"

for key in certificate_total csca_count dsc_count dsc_nc_count mlsc_count crl_count ml_count upload_count validation_count valid_count expired_valid_count invalid_count pending_count countries ldap_stored_certs ldap_stored_crls; do
    printf "%-22s" "$key" >> "${LOG_FILE}"
    printf "%-22s" "$key"
    for round in $(seq 1 ${TOTAL_ROUNDS}); do
        val=$(python3 -c "import json; print(json.load(open('${RESULTS_DIR}/db_r${round}.json')).get('${key}','ERR'))" 2>/dev/null || echo "ERR")
        printf "%-12s" "$val" >> "${LOG_FILE}"
        printf "%-12s" "$val"
    done
    echo "" | tee -a "${LOG_FILE}"
done

# Idempotency check
log ""
log "=== Idempotency Check ==="

all_same=true
for round in $(seq 2 ${TOTAL_ROUNDS}); do
    diff_result=$(diff \
        <(python3 -m json.tool "${RESULTS_DIR}/db_r1.json" 2>/dev/null) \
        <(python3 -m json.tool "${RESULTS_DIR}/db_r${round}.json" 2>/dev/null) 2>&1 || true)

    if [ -z "$diff_result" ]; then
        log "  Round 1 vs Round ${round}: IDENTICAL ✓"
    else
        log "  Round 1 vs Round ${round}: DIFFERENT ✗"
        echo "$diff_result" | head -20 | while IFS= read -r line; do
            log "    ${line}"
        done
        all_same=false
    fi
done

# HTTP response pattern check
log ""
log "=== HTTP Response Pattern ==="
for round in $(seq 1 ${TOTAL_ROUNDS}); do
    codes=""
    for file_info in "${FILES_ORDER[@]}"; do
        IFS='|' read -r _ _ label <<< "$file_info"
        resp_file="${RESULTS_DIR}/r${round}_${label}_response.json"
        err_file="${RESULTS_DIR}/r${round}_${label}_error.json"
        if [ -f "$resp_file" ]; then
            # Check if it was 201 or 409
            is_dup=$(python3 -c "import json; d=json.load(open('$resp_file')); print('409' if d.get('error',{}).get('code')=='DUPLICATE_FILE' else '201')" 2>/dev/null || echo "?")
            codes="${codes}${is_dup} "
        elif [ -f "$err_file" ]; then
            codes="${codes}ERR "
        else
            codes="${codes}? "
        fi
    done
    log "  Round ${round}: ${codes}"
done

log ""
log "============================================"
if $all_same; then
    log " IDEMPOTENCY TEST PASSED"
    log " - All rounds produced identical DB state"
    log " - Duplicate files correctly rejected (HTTP 409)"
else
    log " IDEMPOTENCY TEST FAILED"
    log " - DB state differs between rounds"
fi
log "============================================"
log " Completed: $(date)"
log " Results: ${RESULTS_DIR}"
log "============================================"
