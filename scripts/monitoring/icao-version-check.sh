#!/bin/bash

###############################################################################
# ICAO PKD Auto Sync - Daily Version Check Script
#
# Purpose: Automatically check for new ICAO PKD versions daily
# Schedule: 0 8 * * * (Every day at 8:00 AM)
# Author: SmartCore Inc.
# Version: 1.0.0
# Date: 2026-01-20
###############################################################################

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
LOG_DIR="${PROJECT_ROOT}/logs/icao-sync"
API_GATEWAY_URL="${API_GATEWAY_URL:-http://localhost:8080}"
ICAO_API_ENDPOINT="${API_GATEWAY_URL}/api/icao/check-updates"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="${LOG_DIR}/icao-version-check-${TIMESTAMP}.log"
MAX_LOG_RETENTION_DAYS=30

# Colors for terminal output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

###############################################################################
# Functions
###############################################################################

log() {
    local level=$1
    shift
    local message="$*"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[${timestamp}] [${level}] ${message}" | tee -a "$LOG_FILE"
}

log_info() {
    log "INFO" "$*"
    echo -e "${BLUE}[INFO]${NC} $*"
}

log_success() {
    log "SUCCESS" "$*"
    echo -e "${GREEN}[SUCCESS]${NC} $*"
}

log_warning() {
    log "WARNING" "$*"
    echo -e "${YELLOW}[WARNING]${NC} $*"
}

log_error() {
    log "ERROR" "$*"
    echo -e "${RED}[ERROR]${NC} $*"
}

check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check if curl is installed
    if ! command -v curl &> /dev/null; then
        log_error "curl is not installed. Please install it first."
        exit 1
    fi

    # Check if jq is installed (optional but recommended)
    if ! command -v jq &> /dev/null; then
        log_warning "jq is not installed. JSON parsing will be limited."
    fi

    # Check if API Gateway is reachable
    if ! curl -sf "${API_GATEWAY_URL}/health" > /dev/null 2>&1; then
        log_error "API Gateway at ${API_GATEWAY_URL} is not reachable"
        exit 1
    fi

    log_success "Prerequisites check passed"
}

trigger_version_check() {
    log_info "Triggering ICAO version check via API Gateway..."

    local response_code
    local response_body

    # Make API request
    response_code=$(curl -s -o /tmp/icao-response.json -w "%{http_code}" \
        "${ICAO_API_ENDPOINT}" \
        -H "Content-Type: application/json" \
        -H "User-Agent: ICAO-Sync-Cron/1.0.0")

    response_body=$(cat /tmp/icao-response.json 2>/dev/null || echo "{}")

    log_info "API Response Code: ${response_code}"
    log_info "API Response Body: ${response_body}"

    # Check response
    if [[ "$response_code" -eq 200 ]]; then
        log_success "Version check triggered successfully"
        log_info "Async processing started. Check /api/icao/latest for results."
        return 0
    else
        log_error "Failed to trigger version check. HTTP ${response_code}"
        log_error "Response: ${response_body}"
        return 1
    fi
}

wait_for_processing() {
    log_info "Waiting for async processing to complete (5 seconds)..."
    sleep 5
}

fetch_latest_versions() {
    log_info "Fetching latest detected versions..."

    local response
    response=$(curl -s "${API_GATEWAY_URL}/api/icao/latest")

    if command -v jq &> /dev/null; then
        # Parse with jq if available
        local success=$(echo "$response" | jq -r '.success // false')
        local count=$(echo "$response" | jq -r '.count // 0')

        if [[ "$success" == "true" ]]; then
            log_success "Found ${count} version(s)"

            # Log each version
            echo "$response" | jq -r '.versions[] | "  - \(.collection_type): \(.file_name) (v\(.file_version)) - Status: \(.status)"' | while read -r line; do
                log_info "$line"
            done
        else
            log_error "Failed to fetch versions"
            log_error "Response: ${response}"
        fi
    else
        # Simple parsing without jq
        log_info "Response: ${response}"
    fi
}

cleanup_old_logs() {
    log_info "Cleaning up old log files (retention: ${MAX_LOG_RETENTION_DAYS} days)..."

    if [[ -d "$LOG_DIR" ]]; then
        local deleted_count=0
        while IFS= read -r -d '' file; do
            rm -f "$file"
            ((deleted_count++))
        done < <(find "$LOG_DIR" -name "icao-version-check-*.log" -type f -mtime +${MAX_LOG_RETENTION_DAYS} -print0)

        if [[ $deleted_count -gt 0 ]]; then
            log_info "Deleted ${deleted_count} old log file(s)"
        else
            log_info "No old log files to delete"
        fi
    fi
}

###############################################################################
# Main
###############################################################################

main() {
    # Create log directory if not exists
    mkdir -p "$LOG_DIR"

    log_info "========================================="
    log_info "ICAO PKD Auto Sync - Daily Version Check"
    log_info "========================================="
    log_info "Timestamp: $(date '+%Y-%m-%d %H:%M:%S')"
    log_info "API Gateway: ${API_GATEWAY_URL}"
    log_info "Log File: ${LOG_FILE}"
    log_info ""

    # Check prerequisites
    check_prerequisites
    echo ""

    # Trigger version check
    if trigger_version_check; then
        echo ""

        # Wait for processing
        wait_for_processing
        echo ""

        # Fetch and display results
        fetch_latest_versions
        echo ""

        log_success "Version check completed successfully"
    else
        log_error "Version check failed"
        exit 1
    fi

    # Cleanup old logs
    cleanup_old_logs

    log_info "========================================="
    log_info "Script execution completed"
    log_info "========================================="
}

# Run main function
main "$@"

exit 0
