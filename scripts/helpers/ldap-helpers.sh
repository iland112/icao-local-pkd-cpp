#!/bin/bash
# LDAP Helper Functions
# Source this file: source scripts/ldap-helpers.sh

# LDAP Credentials (consistent across all operations)
LDAP_ADMIN_DN="cn=admin,dc=ldap,dc=smartcoreinc,dc=com"
LDAP_ADMIN_PASSWORD="ldap_test_password_123"
LDAP_BASE_DN="dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"
LDAP_DATA_DN="dc=data,${LDAP_BASE_DN}"
LDAP_HOST="openldap1"

# Count certificates by type
ldap_count_certs() {
    local cert_type=$1
    local object_class=""

    case $cert_type in
        CSCA|DSC|ML)
            object_class="pkdDownload"
            ;;
        CRL)
            object_class="cRLDistributionPoint"
            ;;
        *)
            echo "Usage: ldap_count_certs [CSCA|DSC|CRL|ML]"
            return 1
            ;;
    esac

    echo "Counting ${cert_type} certificates..."
    docker exec icao-local-pkd-${LDAP_HOST} \
        ldapsearch -x \
        -D "${LDAP_ADMIN_DN}" \
        -w "${LDAP_ADMIN_PASSWORD}" \
        -b "${LDAP_DATA_DN}" \
        "(objectClass=${object_class})" dn 2>&1 | \
        grep "^dn:" | wc -l
}

# Count all certificates
ldap_count_all() {
    echo "=========================================="
    echo "LDAP Certificate Count"
    echo "=========================================="
    echo "CSCA: $(ldap_count_certs CSCA)"
    echo "DSC:  $(ldap_count_certs DSC)"
    echo "CRL:  $(ldap_count_certs CRL)"
    echo "ML:   $(ldap_count_certs ML)"
    echo "=========================================="
}

# Search certificates by country
ldap_search_country() {
    local country=$1
    if [[ -z "$country" ]]; then
        echo "Usage: ldap_search_country <COUNTRY_CODE>"
        return 1
    fi

    docker exec icao-local-pkd-${LDAP_HOST} \
        ldapsearch -x \
        -D "${LDAP_ADMIN_DN}" \
        -w "${LDAP_ADMIN_PASSWORD}" \
        -LLL \
        -b "c=${country},${LDAP_DATA_DN}" \
        "(objectClass=pkdDownload)" dn 2>&1 | \
        grep "^dn:" | wc -l
}

# Delete all CRLs (for testing)
ldap_delete_all_crls() {
    echo "⚠️  WARNING: This will delete ALL CRLs from LDAP"
    read -p "Are you sure? (yes/no): " confirm
    if [[ "$confirm" != "yes" ]]; then
        echo "Cancelled"
        return 1
    fi

    echo "Deleting all CRLs..."
    docker exec icao-local-pkd-${LDAP_HOST} \
        ldapsearch -x \
        -D "${LDAP_ADMIN_DN}" \
        -w "${LDAP_ADMIN_PASSWORD}" \
        -LLL \
        -b "${LDAP_DATA_DN}" \
        "(objectClass=cRLDistributionPoint)" dn 2>/dev/null | \
        awk '/^dn:/ {print substr($0, 5)}' | \
        while IFS= read -r dn; do
            [ -z "$dn" ] && continue
            docker exec icao-local-pkd-${LDAP_HOST} \
                ldapdelete -x \
                -D "${LDAP_ADMIN_DN}" \
                -w "${LDAP_ADMIN_PASSWORD}" \
                "$dn" 2>&1 | grep -v "^$"
        done

    echo "Done. Remaining CRLs: $(ldap_count_certs CRL)"
}

# Show LDAP connection info
ldap_info() {
    echo "=========================================="
    echo "LDAP Connection Info"
    echo "=========================================="
    echo "Host:     ${LDAP_HOST}"
    echo "Admin DN: ${LDAP_ADMIN_DN}"
    echo "Password: ${LDAP_ADMIN_PASSWORD}"
    echo "Base DN:  ${LDAP_BASE_DN}"
    echo "Data DN:  ${LDAP_DATA_DN}"
    echo "=========================================="
}

# Export functions
export -f ldap_count_certs
export -f ldap_count_all
export -f ldap_search_country
export -f ldap_delete_all_crls
export -f ldap_info

echo "LDAP Helper functions loaded:"
echo "  ldap_info              - Show connection info"
echo "  ldap_count_all         - Count all certificates"
echo "  ldap_count_certs TYPE  - Count by type (CSCA/DSC/CRL/ML)"
echo "  ldap_search_country CC - Search by country code"
echo "  ldap_delete_all_crls   - Delete all CRLs (testing only)"
