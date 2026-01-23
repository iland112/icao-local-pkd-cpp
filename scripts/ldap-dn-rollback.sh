#!/bin/bash

# LDAP DN Migration - Rollback Script
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

LDAP_HOST=${LDAP_WRITE_HOST:-openldap1}
LDAP_PORT=${LDAP_WRITE_PORT:-389}
LDAP_BIND_DN=${LDAP_BIND_DN:-cn=admin,dc=ldap,dc=smartcoreinc,dc=com}
LDAP_BIND_PASSWORD=${LDAP_BIND_PASSWORD:-admin}

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

# =============================================================================
# Main Script
# =============================================================================

echo "============================================================================="
echo " LDAP DN Migration - Rollback"
echo "============================================================================="
echo "Timestamp: $(date)"
echo "Database: $POSTGRES_DB@$POSTGRES_HOST:$POSTGRES_PORT"
echo "LDAP: $LDAP_HOST:$LDAP_PORT"
echo "============================================================================="
echo ""

log_error "⚠️  WARNING: This script will delete all migrated LDAP entries!"
log_error "⚠️  WARNING: This action cannot be undone!"
echo ""
read -p "Are you sure you want to rollback the migration? (type 'yes' to confirm): " CONFIRM

if [ "$CONFIRM" != "yes" ]; then
    log_info "Rollback cancelled"
    exit 0
fi
echo ""

# Step 1: Check migration status
log_info "Step 1: Checking migration status..."
MIGRATION_STATUS=$(psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
  "SELECT status FROM ldap_migration_status WHERE table_name = 'certificate' ORDER BY created_at DESC LIMIT 1")

if [ -z "$MIGRATION_STATUS" ]; then
    log_error "No migration status found - nothing to rollback"
    exit 1
fi

log_info "Current migration status: $MIGRATION_STATUS"

if [ "$MIGRATION_STATUS" != "COMPLETED" ] && [ "$MIGRATION_STATUS" != "IN_PROGRESS" ] && [ "$MIGRATION_STATUS" != "FAILED" ]; then
    log_error "Migration status is $MIGRATION_STATUS - cannot rollback"
    exit 1
fi
echo ""

# Step 2: Count records to rollback
log_info "Step 2: Counting records to rollback..."
MIGRATED_COUNT=$(psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
  "SELECT COUNT(*) FROM certificate WHERE ldap_dn_v2 IS NOT NULL")

if [ "$MIGRATED_COUNT" -eq 0 ]; then
    log_warning "No migrated records found (ldap_dn_v2 is NULL for all records)"
    log_info "Skipping LDAP deletion step"
else
    log_warning "Found $MIGRATED_COUNT migrated records to rollback"
fi
echo ""

# Step 3: Export DNs to temporary file
if [ "$MIGRATED_COUNT" -gt 0 ]; then
    log_info "Step 3: Exporting new DNs to temporary file..."
    TMP_FILE="/tmp/ldap_dn_rollback_$(date +%Y%m%d_%H%M%S).txt"

    psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
      "SELECT ldap_dn_v2 FROM certificate WHERE ldap_dn_v2 IS NOT NULL" > "$TMP_FILE"

    DN_COUNT=$(wc -l < "$TMP_FILE")
    log_success "Exported $DN_COUNT DNs to $TMP_FILE"
    echo ""

    # Step 4: Delete entries from LDAP
    log_info "Step 4: Deleting entries from LDAP..."
    log_warning "This may take several minutes for large datasets..."

    SUCCESS_COUNT=0
    FAIL_COUNT=0
    TOTAL_COUNT=0

    while IFS= read -r dn; do
        TOTAL_COUNT=$((TOTAL_COUNT + 1))

        # Progress indicator
        if [ $((TOTAL_COUNT % 100)) -eq 0 ]; then
            log_info "Progress: $TOTAL_COUNT/$DN_COUNT DNs processed..."
        fi

        # Delete from LDAP
        if ldapdelete -x -H "ldap://$LDAP_HOST:$LDAP_PORT" \
             -D "$LDAP_BIND_DN" \
             -w "$LDAP_BIND_PASSWORD" \
             "$dn" > /dev/null 2>&1; then
            SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        else
            FAIL_COUNT=$((FAIL_COUNT + 1))
            log_warning "Failed to delete: $dn"
        fi
    done < "$TMP_FILE"

    log_success "LDAP deletion complete"
    log_info "  Success: $SUCCESS_COUNT"
    log_info "  Failed: $FAIL_COUNT"
    log_info "  Total: $TOTAL_COUNT"
    echo ""

    # Cleanup temporary file
    rm -f "$TMP_FILE"
    log_success "Temporary file cleaned up"
    echo ""
fi

# Step 5: Clear ldap_dn_v2 column in database
log_info "Step 5: Clearing ldap_dn_v2 column in database..."
psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
UPDATE certificate SET ldap_dn_v2 = NULL WHERE ldap_dn_v2 IS NOT NULL;
UPDATE master_list SET ldap_dn_v2 = NULL WHERE ldap_dn_v2 IS NOT NULL;
UPDATE crl SET ldap_dn_v2 = NULL WHERE ldap_dn_v2 IS NOT NULL;
SQL
log_success "ldap_dn_v2 columns cleared"
echo ""

# Step 6: Update migration status
log_info "Step 6: Updating migration status..."
psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
UPDATE ldap_migration_status
SET status = 'ROLLED_BACK',
    migration_end = NOW(),
    error_log = 'Rollback completed at ' || NOW()::text
WHERE table_name = 'certificate'
  AND status IN ('IN_PROGRESS', 'COMPLETED', 'FAILED')
  AND id = (SELECT id FROM ldap_migration_status WHERE table_name = 'certificate' ORDER BY created_at DESC LIMIT 1);
SQL
log_success "Migration status updated to ROLLED_BACK"
echo ""

# Step 7: Verify rollback
log_info "Step 7: Verifying rollback..."
REMAINING_V2_DNS=$(psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
  "SELECT COUNT(*) FROM certificate WHERE ldap_dn_v2 IS NOT NULL")

if [ "$REMAINING_V2_DNS" -eq 0 ]; then
    log_success "Rollback verification PASSED - No ldap_dn_v2 records remain"
else
    log_error "Rollback verification FAILED - $REMAINING_V2_DNS ldap_dn_v2 records still exist"
fi
echo ""

# Summary
echo "============================================================================="
echo " Rollback Summary"
echo "============================================================================="
log_success "Rollback completed successfully"
log_info "Records rolled back: $MIGRATED_COUNT"
if [ "$MIGRATED_COUNT" -gt 0 ]; then
    log_info "LDAP entries deleted: $SUCCESS_COUNT (failed: $FAIL_COUNT)"
fi
log_info "Database columns cleared: certificate, master_list, crl"
log_info "Migration status: ROLLED_BACK"
echo ""
log_warning "Next steps:"
echo "  1. Verify LDAP entries are gone: ldapsearch -x -H ldap://$LDAP_HOST:$LDAP_PORT -b 'dc=download,dc=pkd,...' '(cn=*)'"
echo "  2. Re-run migration if needed: ./scripts/ldap-dn-migration.sh test"
echo "============================================================================="
echo ""
