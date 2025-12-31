#!/bin/bash
# =============================================================================
# ICAO Local PKD - LDAP DIT Initialization Script
# =============================================================================
# This script initializes the PKD DIT structure in OpenLDAP
# It should be run after OpenLDAP has started
# =============================================================================

# Don't exit on error - we handle "already exists" gracefully
set +e

LDAP_HOST="${LDAP_HOST:-localhost}"
LDAP_PORT="${LDAP_PORT:-389}"
LDAP_ADMIN_DN="${LDAP_ADMIN_DN:-cn=admin,dc=ldap,dc=smartcoreinc,dc=com}"
LDAP_ADMIN_PASSWORD="${LDAP_ADMIN_PASSWORD:-admin}"
LDAP_BASE_DN="${LDAP_BASE_DN:-dc=ldap,dc=smartcoreinc,dc=com}"

LDAP_URI="ldap://${LDAP_HOST}:${LDAP_PORT}"

echo "=============================================="
echo "ICAO Local PKD - LDAP DIT Initialization"
echo "=============================================="
echo "LDAP Host: ${LDAP_HOST}:${LDAP_PORT}"
echo "Admin DN: ${LDAP_ADMIN_DN}"
echo "Base DN: ${LDAP_BASE_DN}"
echo "=============================================="

# Wait for OpenLDAP to be ready
echo "Waiting for OpenLDAP to be ready..."
for i in {1..60}; do
    if ldapsearch -x -H "${LDAP_URI}" -b "" -s base "(objectClass=*)" namingContexts > /dev/null 2>&1; then
        echo "OpenLDAP is accepting connections."
        break
    fi
    echo "Waiting... ($i/60)"
    sleep 2
done

# Additional wait to ensure LDAP is fully initialized
sleep 5

# Function to check if entry exists (using authenticated search)
entry_exists() {
    local dn="$1"
    ldapsearch -x -H "${LDAP_URI}" -D "${LDAP_ADMIN_DN}" -w "${LDAP_ADMIN_PASSWORD}" \
        -b "${dn}" -s base "(objectClass=*)" dn 2>/dev/null | grep -q "^dn:"
}

# Function to add LDIF entry with retry
add_entry() {
    local dn="$1"
    local ldif="$2"
    local max_retries=3
    local retry=0

    while [ $retry -lt $max_retries ]; do
        # Check if entry already exists
        if entry_exists "${dn}"; then
            echo "[OK] Entry exists: ${dn}"
            return 0
        fi

        # Try to add the entry
        echo "[ADD] Creating: ${dn}"
        echo "${ldif}" | ldapadd -x -H "${LDAP_URI}" -D "${LDAP_ADMIN_DN}" -w "${LDAP_ADMIN_PASSWORD}" 2>&1
        local result=$?

        if [ $result -eq 0 ]; then
            echo "[OK] Created: ${dn}"
            return 0
        elif [ $result -eq 68 ]; then
            # Already exists - verify with authenticated search
            if entry_exists "${dn}"; then
                echo "[OK] Entry verified: ${dn}"
                return 0
            fi
            echo "[INFO] Already exists error but cannot verify: ${dn}"
        else
            echo "[WARN] Failed to create (code ${result}): ${dn}"
        fi

        retry=$((retry + 1))
        sleep 2
    done

    # Final check
    if entry_exists "${dn}"; then
        echo "[OK] Entry verified: ${dn}"
        return 0
    fi

    echo "[ERROR] Failed to create entry after ${max_retries} retries: ${dn}"
    return 1
}

# Create Base DN (osixia may have already created this)
add_entry "${LDAP_BASE_DN}" "dn: ${LDAP_BASE_DN}
objectClass: top
objectClass: dcObject
objectClass: organization
dc: ldap
o: SmartCore Inc"

# Create PKD root
add_entry "dc=pkd,${LDAP_BASE_DN}" "dn: dc=pkd,${LDAP_BASE_DN}
objectClass: top
objectClass: dcObject
objectClass: organization
dc: pkd
o: ICAO PKD"

# Create download container
add_entry "dc=download,dc=pkd,${LDAP_BASE_DN}" "dn: dc=download,dc=pkd,${LDAP_BASE_DN}
objectClass: top
objectClass: dcObject
objectClass: organization
dc: download
o: PKD Download"

# Create data container (for compliant certificates)
add_entry "dc=data,dc=download,dc=pkd,${LDAP_BASE_DN}" "dn: dc=data,dc=download,dc=pkd,${LDAP_BASE_DN}
objectClass: top
objectClass: dcObject
objectClass: organization
dc: data
o: PKD Data"

# Create nc-data container (for non-compliant certificates)
add_entry "dc=nc-data,dc=download,dc=pkd,${LDAP_BASE_DN}" "dn: dc=nc-data,dc=download,dc=pkd,${LDAP_BASE_DN}
objectClass: top
objectClass: dcObject
objectClass: organization
dc: nc-data
o: PKD Non-Compliant Data"

echo ""
echo "=============================================="
echo "PKD DIT Structure Verification"
echo "=============================================="

# Final verification using authenticated search
EXPECTED_ENTRIES=(
    "${LDAP_BASE_DN}"
    "dc=pkd,${LDAP_BASE_DN}"
    "dc=download,dc=pkd,${LDAP_BASE_DN}"
    "dc=data,dc=download,dc=pkd,${LDAP_BASE_DN}"
    "dc=nc-data,dc=download,dc=pkd,${LDAP_BASE_DN}"
)

all_ok=true
for entry in "${EXPECTED_ENTRIES[@]}"; do
    if entry_exists "${entry}"; then
        echo "[VERIFIED] ${entry}"
    else
        echo "[MISSING] ${entry}"
        all_ok=false
    fi
done

echo ""
if [ "$all_ok" = true ]; then
    echo "=============================================="
    echo "PKD DIT structure initialized successfully!"
    echo "=============================================="

    # Show all entries
    echo ""
    echo "Current PKD DIT entries:"
    ldapsearch -x -H "${LDAP_URI}" -D "${LDAP_ADMIN_DN}" -w "${LDAP_ADMIN_PASSWORD}" \
        -b "${LDAP_BASE_DN}" "(objectClass=*)" dn 2>&1 | grep "^dn:"

    exit 0
else
    echo "=============================================="
    echo "[ERROR] Some entries could not be created"
    echo "=============================================="
    exit 1
fi
