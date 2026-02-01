#!/bin/bash
# =============================================================================
# Combined Data Reset Script
# =============================================================================
# Purpose: Reset both PostgreSQL and LDAP data for fresh testing
# Date: 2026-01-30
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "🔄 Full System Data Reset"
echo "================================"
echo "This will delete all certificate and upload data from:"
echo "  - PostgreSQL database"
echo "  - LDAP (both dc=data and dc=nc-data)"
echo ""
read -p "Are you sure you want to continue? (yes/no): " confirm

if [ "$confirm" != "yes" ]; then
    echo "❌ Reset cancelled."
    exit 0
fi

echo ""
echo "================================"
echo "Step 1: Resetting PostgreSQL Database"
echo "================================"

# Execute database reset script
docker exec -i icao-local-pkd-postgres psql -U pkd -d localpkd < "${PROJECT_ROOT}/docker/db/migrations/reset_certificate_data.sql"

if [ $? -eq 0 ]; then
    echo "✅ PostgreSQL reset complete"
else
    echo "❌ PostgreSQL reset failed"
    exit 1
fi

echo ""
echo "================================"
echo "Step 2: Resetting LDAP Data"
echo "================================"

# Execute LDAP reset script
"${SCRIPT_DIR}/reset-ldap-data.sh"

if [ $? -eq 0 ]; then
    echo "✅ LDAP reset complete"
else
    echo "❌ LDAP reset failed"
    exit 1
fi

echo ""
echo "================================"
echo "✅ Full System Reset Complete!"
echo "================================"
echo ""
echo "Next steps:"
echo "  1. Upload Master List file (537 certificates)"
echo "  2. Upload Collection 001 LDIF (29,838 DSC + 69 CRL)"
echo "  3. Upload Collection 002 LDIF (5,017 CSCA)"
echo "  4. Upload Collection 003 LDIF (502 DSC_NC)"
echo "  5. Verify X.509 metadata fields are populated"
echo ""
echo "Use the Upload page at: http://localhost:3000/upload"
echo "================================"
