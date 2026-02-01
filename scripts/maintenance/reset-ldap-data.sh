#!/bin/bash
# =============================================================================
# LDAP Data Reset Script
# =============================================================================
# Purpose: Delete all certificate and CRL data from LDAP
# Scope: Removes all entries under dc=data and dc=nc-data
# Date: 2026-01-30
# =============================================================================

set -e

LDAP_HOST="localhost"
LDAP_PORT="389"
LDAP_ADMIN_DN="cn=admin,dc=ldap,dc=smartcoreinc,dc=com"
LDAP_PASSWORD="ldap_test_password_123"
BASE_DN="dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"

echo "🗑️  LDAP Data Reset Starting..."
echo "================================"

# Function to delete all entries under a DN
delete_entries() {
    local base_dn="$1"
    local description="$2"

    echo "Deleting $description from: $base_dn"

    # Search for all entries (excluding the base DN itself)
    ldapsearch -x -H ldap://${LDAP_HOST}:${LDAP_PORT} \
        -D "${LDAP_ADMIN_DN}" -w "${LDAP_PASSWORD}" \
        -b "${base_dn}" \
        "(objectClass=*)" dn 2>/dev/null | \
        grep "^dn:" | \
        grep -v "^dn: ${base_dn}$" | \
        sed 's/^dn: //' | \
        while IFS= read -r entry_dn; do
            ldapdelete -x -H ldap://${LDAP_HOST}:${LDAP_PORT} \
                -D "${LDAP_ADMIN_DN}" -w "${LDAP_PASSWORD}" \
                "${entry_dn}" 2>/dev/null && echo "  ✓ Deleted: ${entry_dn}" || true
        done
}

# Delete certificates from dc=data (all countries)
echo ""
echo "1. Deleting certificates from dc=data..."
delete_entries "dc=data,${BASE_DN}" "CSCA/MLSC/DSC/CRL from all countries"

# Delete non-conformant certificates from dc=nc-data
echo ""
echo "2. Deleting certificates from dc=nc-data..."
delete_entries "dc=nc-data,${BASE_DN}" "DSC_NC from all countries"

# Count remaining entries
echo ""
echo "================================"
echo "Verification:"
echo "================================"

# Count in dc=data
data_count=$(ldapsearch -x -H ldap://${LDAP_HOST}:${LDAP_PORT} \
    -D "${LDAP_ADMIN_DN}" -w "${LDAP_PASSWORD}" \
    -b "dc=data,${BASE_DN}" \
    "(objectClass=*)" dn 2>/dev/null | grep -c "^dn:" || true)

# Count in dc=nc-data
nc_data_count=$(ldapsearch -x -H ldap://${LDAP_HOST}:${LDAP_PORT} \
    -D "${LDAP_ADMIN_DN}" -w "${LDAP_PASSWORD}" \
    -b "dc=nc-data,${BASE_DN}" \
    "(objectClass=*)" dn 2>/dev/null | grep -c "^dn:" || true)

# Subtract base DN entries (dc=data and dc=nc-data themselves)
data_count=$((data_count - 1))
nc_data_count=$((nc_data_count - 1))

echo "Remaining entries in dc=data: $data_count"
echo "Remaining entries in dc=nc-data: $nc_data_count"

if [ $data_count -eq 0 ] && [ $nc_data_count -eq 0 ]; then
    echo ""
    echo "✅ LDAP reset complete!"
    echo "All certificate data has been deleted."
    echo "Ready for fresh data upload."
else
    echo ""
    echo "⚠️  Warning: Some entries still remain."
    echo "You may need to manually check LDAP."
fi

echo "================================"
