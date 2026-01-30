#!/bin/bash

# LDAP DN Migration - Live Script
# Sprint 1: Week 5 - LDAP Storage Fix
# Version: 1.0.0
# Created: 2026-01-23

set -e

# =============================================================================
# Configuration
# =============================================================================

POSTGRES_HOST=${DB_HOST:-postgres}
POSTGRES_PORT=${DB_PORT:-5432}
POSTGRES_DB=${DB_NAME:-pkd}
POSTGRES_USER=${DB_USER:-pkd}
POSTGRES_PASSWORD=${DB_PASSWORD:-pkd123}

PKD_MANAGEMENT_URL=${PKD_MANAGEMENT_URL:-http://localhost:8081}
BATCH_SIZE=${BATCH_SIZE:-100}
BATCH_DELAY=${BATCH_DELAY:-2}  # seconds

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# =============================================================================
# Functions
# =============================================================================

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_progress() {
    echo -e "${CYAN}[PROGRESS]${NC} $1"
}

# =============================================================================
# Argument Parsing
# =============================================================================

MIGRATION_MODE=${1:-"test"}

if [ "$MIGRATION_MODE" != "test" ] && [ "$MIGRATION_MODE" != "production" ]; then
    log_error "Invalid mode: $MIGRATION_MODE"
    echo "Usage: $0 [test|production]"
    echo ""
    echo "Modes:"
    echo "  test       - Update database only (no LDAP write)"
    echo "  production - Update database AND LDAP"
    exit 1
fi

# =============================================================================
# Main Script
# =============================================================================

echo "============================================================================="
echo " LDAP DN Migration - Live Mode"
echo "============================================================================="
echo "Mode: $MIGRATION_MODE"
echo "Batch Size: $BATCH_SIZE"
echo "Batch Delay: $BATCH_DELAY seconds"
echo "Timestamp: $(date)"
echo "Database: $POSTGRES_DB@$POSTGRES_HOST:$POSTGRES_PORT"
echo "API Endpoint: $PKD_MANAGEMENT_URL/api/internal/migrate-ldap-dns"
echo "============================================================================="
echo ""

if [ "$MIGRATION_MODE" == "production" ]; then
    log_error "⚠️  PRODUCTION MODE - Will write to LDAP!"
    log_error "⚠️  This action cannot be undone without rollback!"
    echo ""
    read -p "Are you sure you want to proceed? (type 'yes' to confirm): " CONFIRM

    if [ "$CONFIRM" != "yes" ]; then
        log_info "Migration cancelled"
        exit 0
    fi
    echo ""
fi

# Step 1: Check API connectivity
log_info "Step 1: Checking API connectivity..."
if curl -s -f "$PKD_MANAGEMENT_URL/api/health" > /dev/null 2>&1; then
    log_success "API is reachable"
else
    log_error "API is not reachable at $PKD_MANAGEMENT_URL"
    exit 1
fi
echo ""

# Step 2: Initialize migration status
log_info "Step 2: Initializing migration status..."

TOTAL_CERTS=$(psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
  "SELECT COUNT(*) FROM certificate WHERE ldap_stored = true AND ldap_dn_v2 IS NULL")

if [ "$TOTAL_CERTS" -eq 0 ]; then
    log_warning "No certificates to migrate (all already have ldap_dn_v2)"
    exit 0
fi

log_success "Found $TOTAL_CERTS certificates to migrate"

# Update or create migration status record
psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
INSERT INTO ldap_migration_status (table_name, total_records, status, migration_start)
VALUES ('certificate', $TOTAL_CERTS, 'IN_PROGRESS', NOW())
ON CONFLICT (table_name)
WHERE status IN ('IN_PROGRESS', 'COMPLETED')
DO UPDATE SET
    total_records = EXCLUDED.total_records,
    migrated_records = 0,
    failed_records = 0,
    status = 'IN_PROGRESS',
    migration_start = NOW(),
    migration_end = NULL,
    error_log = NULL;
SQL

log_success "Migration status initialized"
echo ""

# Step 3: Batch migration loop
log_info "Step 3: Starting batch migration..."
echo ""

OFFSET=0
TOTAL_SUCCESS=0
TOTAL_FAILED=0
BATCH_NUMBER=0

while true; do
    BATCH_NUMBER=$((BATCH_NUMBER + 1))

    # Check remaining records
    REMAINING=$(psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
      "SELECT COUNT(*) FROM certificate WHERE ldap_stored = true AND ldap_dn_v2 IS NULL")

    if [ "$REMAINING" -eq 0 ]; then
        log_success "All certificates migrated!"
        break
    fi

    log_progress "Batch #$BATCH_NUMBER - Processing $REMAINING remaining records (offset: $OFFSET)..."

    # Call migration API
    RESPONSE=$(curl -s -X POST "$PKD_MANAGEMENT_URL/api/internal/migrate-ldap-dns" \
      -H "Content-Type: application/json" \
      -d "{\"offset\": $OFFSET, \"limit\": $BATCH_SIZE, \"mode\": \"$MIGRATION_MODE\"}")

    # Check if API call succeeded
    if [ -z "$RESPONSE" ]; then
        log_error "API call failed - empty response"
        break
    fi

    # Parse response using jq (if available) or grep
    if command -v jq &> /dev/null; then
        SUCCESS=$(echo "$RESPONSE" | jq -r '.success_count // 0')
        FAILED=$(echo "$RESPONSE" | jq -r '.failed_count // 0')
        ERRORS=$(echo "$RESPONSE" | jq -r '.errors // []')
    else
        # Fallback to grep (less accurate)
        SUCCESS=$(echo "$RESPONSE" | grep -oP '"success_count":\s*\K\d+' || echo "0")
        FAILED=$(echo "$RESPONSE" | grep -oP '"failed_count":\s*\K\d+' || echo "0")
    fi

    TOTAL_SUCCESS=$((TOTAL_SUCCESS + SUCCESS))
    TOTAL_FAILED=$((TOTAL_FAILED + FAILED))

    log_success "  Batch #$BATCH_NUMBER complete - Success: $SUCCESS, Failed: $FAILED"

    # Show errors if any
    if [ "$FAILED" -gt 0 ]; then
        if command -v jq &> /dev/null; then
            echo "$RESPONSE" | jq -r '.errors[]' | while read -r error; do
                log_warning "    Error: $error"
            done
        fi
    fi

    # Progress indicator
    MIGRATED=$((TOTAL_SUCCESS + TOTAL_FAILED))
    PERCENTAGE=$(( (MIGRATED * 100) / TOTAL_CERTS ))
    log_info "  Overall progress: $MIGRATED/$TOTAL_CERTS ($PERCENTAGE%)"
    echo ""

    # Increment offset for next batch
    OFFSET=$((OFFSET + BATCH_SIZE))

    # Delay between batches to avoid overwhelming LDAP
    if [ "$REMAINING" -gt "$BATCH_SIZE" ]; then
        sleep $BATCH_DELAY
    fi
done

echo ""

# Step 4: Finalize migration status
log_info "Step 4: Finalizing migration status..."

FINAL_STATUS="COMPLETED"
if [ "$TOTAL_FAILED" -gt 0 ]; then
    FINAL_STATUS="PARTIAL"
    log_warning "Migration completed with errors"
fi

psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
UPDATE ldap_migration_status
SET status = '$FINAL_STATUS',
    migration_end = NOW(),
    error_log = CASE
        WHEN $TOTAL_FAILED > 0 THEN '$TOTAL_FAILED records failed during migration'
        ELSE NULL
    END
WHERE table_name = 'certificate' AND status = 'IN_PROGRESS';
SQL

log_success "Migration status updated to $FINAL_STATUS"
echo ""

# Step 5: Verification
log_info "Step 5: Verifying migration..."

MIGRATED_COUNT=$(psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
  "SELECT COUNT(*) FROM certificate WHERE ldap_dn_v2 IS NOT NULL")

REMAINING_COUNT=$(psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
  "SELECT COUNT(*) FROM certificate WHERE ldap_stored = true AND ldap_dn_v2 IS NULL")

log_success "Migrated: $MIGRATED_COUNT certificates"
log_success "Remaining: $REMAINING_COUNT certificates"

# Check for DN duplicates
DUPLICATE_DNS=$(psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
  "SELECT COUNT(*) FROM (SELECT ldap_dn_v2 FROM certificate WHERE ldap_dn_v2 IS NOT NULL GROUP BY ldap_dn_v2 HAVING COUNT(*) > 1) AS dups")

if [ "$DUPLICATE_DNS" -eq 0 ]; then
    log_success "No duplicate DNs found - migration integrity verified"
else
    log_error "Found $DUPLICATE_DNS duplicate DNs - migration has integrity issues!"
fi
echo ""

# Step 6: Summary
echo "============================================================================="
echo " Migration Summary"
echo "============================================================================="
log_success "Migration mode: $MIGRATION_MODE"
log_success "Total certificates: $TOTAL_CERTS"
log_success "Successfully migrated: $TOTAL_SUCCESS"
if [ "$TOTAL_FAILED" -gt 0 ]; then
    log_warning "Failed: $TOTAL_FAILED"
fi
log_success "Migration status: $FINAL_STATUS"
echo ""

if [ "$MIGRATION_MODE" == "production" ]; then
    log_info "Next steps (PRODUCTION):"
    echo "  1. Verify LDAP entries: ldapsearch -x -H ldap://openldap1:389 -b 'dc=download,dc=pkd,...' '(cn=*)'"
    echo "  2. Test certificate search: curl http://localhost:8080/api/certificates/search?country=KR"
    echo "  3. Monitor application logs for any LDAP lookup issues"
    echo "  4. If issues found, run rollback: ./scripts/ldap-dn-rollback.sh"
else
    log_info "Next steps (TEST MODE):"
    echo "  1. Review migration results in database (ldap_dn_v2 column)"
    echo "  2. Verify DN format: SELECT ldap_dn_v2 FROM certificate WHERE ldap_dn_v2 IS NOT NULL LIMIT 10;"
    echo "  3. Run production migration: ./scripts/ldap-dn-migration.sh production"
fi
echo "============================================================================="
echo ""

# Return appropriate exit code
if [ "$REMAINING_COUNT" -eq 0 ] && [ "$DUPLICATE_DNS" -eq 0 ]; then
    exit 0
else
    exit 1
fi
