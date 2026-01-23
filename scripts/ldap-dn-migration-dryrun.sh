#!/bin/bash

# LDAP DN Migration - Dry Run
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
echo " LDAP DN Migration - Dry Run"
echo "============================================================================="
echo "Timestamp: $(date)"
echo "Database: $POSTGRES_DB@$POSTGRES_HOST:$POSTGRES_PORT"
echo "============================================================================="
echo ""

# Step 1: Check database connection
log_info "Step 1: Testing database connection..."
if psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB -c "SELECT 1;" > /dev/null 2>&1; then
    log_success "Database connection successful"
else
    log_error "Database connection failed"
    exit 1
fi
echo ""

# Step 2: Count total records
log_info "Step 2: Counting total records..."
TOTAL_CERTS=$(psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
  "SELECT COUNT(*) FROM certificate WHERE ldap_stored = true")
log_success "Total certificates in LDAP: $TOTAL_CERTS"
echo ""

# Step 3: Check migration status table
log_info "Step 3: Checking migration status table..."
psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
SELECT
    table_name,
    total_records,
    migrated_records,
    failed_records,
    status,
    migration_start,
    migration_end
FROM ldap_migration_status
ORDER BY created_at DESC
LIMIT 5;
SQL
echo ""

# Step 4: Identify serial number collisions
log_info "Step 4: Identifying serial number collisions..."
log_warning "These certificates have the same serial number across different issuers:"
psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
SELECT
    serial_number,
    COUNT(*) as collision_count,
    STRING_AGG(DISTINCT LEFT(issuer_dn, 60), ' | ') as issuers
FROM certificate
WHERE ldap_stored = true
GROUP BY serial_number
HAVING COUNT(*) > 1
ORDER BY collision_count DESC
LIMIT 10;
SQL
echo ""

# Step 5: Generate sample new DNs (dry-run)
log_info "Step 5: Generating sample new DNs (first 10 records)..."
psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
SELECT
    id AS cert_id,
    LEFT(fingerprint_sha256, 16) || '...' AS fingerprint_short,
    certificate_type,
    country_code,
    CASE
        WHEN certificate_type = 'CSCA' THEN
            'cn=' || fingerprint_sha256 || ',o=csca,c=' || country_code || ',dc=data,dc=download,dc=pkd,...'
        WHEN certificate_type = 'DSC_NC' THEN
            'cn=' || fingerprint_sha256 || ',o=dsc_nc,c=' || country_code || ',dc=nc-data,dc=download,dc=pkd,...'
        ELSE
            'cn=' || fingerprint_sha256 || ',o=dsc,c=' || country_code || ',dc=data,dc=download,dc=pkd,...'
    END AS new_dn_sample
FROM certificate
WHERE ldap_stored = true
LIMIT 10;
SQL
echo ""

# Step 6: Check DN length distribution
log_info "Step 6: Analyzing DN length distribution..."
psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
SELECT
    CASE
        WHEN certificate_type = 'CSCA' THEN 'CSCA'
        WHEN certificate_type = 'DSC_NC' THEN 'DSC_NC'
        ELSE 'DSC'
    END AS cert_type,
    COUNT(*) AS count,
    MIN(LENGTH('cn=' || fingerprint_sha256 || ',o=csca,c=' || country_code || ',dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com')) AS min_dn_length,
    MAX(LENGTH('cn=' || fingerprint_sha256 || ',o=csca,c=' || country_code || ',dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com')) AS max_dn_length,
    ROUND(AVG(LENGTH('cn=' || fingerprint_sha256 || ',o=csca,c=' || country_code || ',dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com'))) AS avg_dn_length
FROM certificate
WHERE ldap_stored = true
GROUP BY
    CASE
        WHEN certificate_type = 'CSCA' THEN 'CSCA'
        WHEN certificate_type = 'DSC_NC' THEN 'DSC_NC'
        ELSE 'DSC'
    END
ORDER BY cert_type;
SQL
echo ""

# Step 7: Verify fingerprint uniqueness
log_info "Step 7: Verifying fingerprint uniqueness..."
DUPLICATE_FINGERPRINTS=$(psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
  "SELECT COUNT(*) FROM (SELECT fingerprint_sha256 FROM certificate WHERE ldap_stored = true GROUP BY fingerprint_sha256 HAVING COUNT(*) > 1) AS duplicates")

if [ "$DUPLICATE_FINGERPRINTS" -eq 0 ]; then
    log_success "All fingerprints are unique - migration is safe"
else
    log_error "Found $DUPLICATE_FINGERPRINTS duplicate fingerprints - migration may fail!"
    log_warning "Investigating duplicates..."
    psql -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
SELECT
    fingerprint_sha256,
    COUNT(*) as count,
    STRING_AGG(id::text, ', ') as cert_ids
FROM certificate
WHERE ldap_stored = true
GROUP BY fingerprint_sha256
HAVING COUNT(*) > 1
LIMIT 5;
SQL
fi
echo ""

# Step 8: Estimate migration time
log_info "Step 8: Estimating migration time..."
BATCH_SIZE=100
BATCH_DELAY=2  # seconds
TOTAL_BATCHES=$(( ($TOTAL_CERTS + $BATCH_SIZE - 1) / $BATCH_SIZE ))
ESTIMATED_TIME=$(( $TOTAL_BATCHES * $BATCH_DELAY ))

log_info "Total batches: $TOTAL_BATCHES (batch size: $BATCH_SIZE)"
log_info "Estimated migration time: $ESTIMATED_TIME seconds (~$(($ESTIMATED_TIME / 60)) minutes)"
echo ""

# Step 9: Summary
echo "============================================================================="
echo " Dry Run Summary"
echo "============================================================================="
log_success "Total certificates to migrate: $TOTAL_CERTS"
log_success "Fingerprint uniqueness: $([ $DUPLICATE_FINGERPRINTS -eq 0 ] && echo 'PASS' || echo 'FAIL')"
log_success "Estimated DN length: 130-140 characters (under 255 limit)"
log_success "Estimated migration time: ~$(($ESTIMATED_TIME / 60)) minutes"
echo ""
log_info "Next steps:"
echo "  1. Review the generated sample DNs above"
echo "  2. Verify no duplicate fingerprints exist"
echo "  3. Run the live migration script: ./scripts/ldap-dn-migration.sh test"
echo "  4. After testing, run: ./scripts/ldap-dn-migration.sh production"
echo "============================================================================="
echo ""
