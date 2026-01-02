#!/bin/bash
# =============================================================================
# OpenLDAP Multi-Master Replication (MMR) Setup Script
# =============================================================================
# This script configures MMR between two OpenLDAP nodes
# Must be run after both OpenLDAP containers are healthy
# =============================================================================

set -e

LDAP_CONFIG_PASSWORD="${LDAP_CONFIG_PASSWORD:-config}"
LDAP_ADMIN_PASSWORD="${LDAP_ADMIN_PASSWORD:-admin}"
SERVER_ID="${SERVER_ID:-1}"

echo "=============================================="
echo "OpenLDAP MMR Setup - Server ID: ${SERVER_ID}"
echo "=============================================="

# Wait for slapd to be ready
echo "Waiting for slapd to be ready..."
for i in {1..30}; do
    if ldapsearch -x -H ldap://localhost -b "" -s base "(objectClass=*)" > /dev/null 2>&1; then
        echo "slapd is ready."
        break
    fi
    echo "Waiting... ($i/30)"
    sleep 2
done

# Check if syncprov module is already loaded
echo "Checking syncprov module..."
if ldapsearch -x -H ldap://localhost -D "cn=admin,cn=config" -w "${LDAP_CONFIG_PASSWORD}" \
    -b "cn=module{0},cn=config" "(olcModuleLoad=syncprov)" 2>/dev/null | grep -q "syncprov"; then
    echo "[OK] syncprov module already loaded"
else
    echo "[ADD] Loading syncprov module..."
    ldapmodify -x -H ldap://localhost -D "cn=admin,cn=config" -w "${LDAP_CONFIG_PASSWORD}" <<EOF
dn: cn=module{0},cn=config
changetype: modify
add: olcModuleLoad
olcModuleLoad: syncprov
EOF
    echo "[OK] syncprov module loaded"
fi

# Check if syncprov overlay exists
echo "Checking syncprov overlay..."
if ldapsearch -x -H ldap://localhost -D "cn=admin,cn=config" -w "${LDAP_CONFIG_PASSWORD}" \
    -b "olcOverlay=syncprov,olcDatabase={1}mdb,cn=config" "(objectClass=*)" 2>/dev/null | grep -q "dn:"; then
    echo "[OK] syncprov overlay already exists"
else
    echo "[ADD] Creating syncprov overlay..."
    ldapadd -x -H ldap://localhost -D "cn=admin,cn=config" -w "${LDAP_CONFIG_PASSWORD}" <<EOF
dn: olcOverlay=syncprov,olcDatabase={1}mdb,cn=config
objectClass: olcOverlayConfig
objectClass: olcSyncProvConfig
olcOverlay: syncprov
olcSpSessionLog: 100
EOF
    echo "[OK] syncprov overlay created"
fi

# Check if serverID is set
echo "Checking serverID..."
if ldapsearch -x -H ldap://localhost -D "cn=admin,cn=config" -w "${LDAP_CONFIG_PASSWORD}" \
    -b "cn=config" "(olcServerID=*)" 2>/dev/null | grep -q "olcServerID"; then
    echo "[OK] serverID already set"
else
    echo "[ADD] Setting serverID to ${SERVER_ID}..."
    ldapmodify -x -H ldap://localhost -D "cn=admin,cn=config" -w "${LDAP_CONFIG_PASSWORD}" <<EOF
dn: cn=config
changetype: modify
add: olcServerID
olcServerID: ${SERVER_ID}
EOF
    echo "[OK] serverID set to ${SERVER_ID}"
fi

# Check if MMR replication is configured
echo "Checking MMR replication..."
if ldapsearch -x -H ldap://localhost -D "cn=admin,cn=config" -w "${LDAP_CONFIG_PASSWORD}" \
    -b "olcDatabase={1}mdb,cn=config" "(olcSyncRepl=*)" 2>/dev/null | grep -q "olcSyncRepl"; then
    echo "[OK] MMR replication already configured"
else
    echo "[ADD] Configuring MMR replication..."
    ldapmodify -x -H ldap://localhost -D "cn=admin,cn=config" -w "${LDAP_CONFIG_PASSWORD}" <<EOF
dn: olcDatabase={1}mdb,cn=config
changetype: modify
add: olcSyncRepl
olcSyncRepl: {0}rid=001 provider=ldap://openldap1:389 binddn="cn=admin,dc=ldap,dc=smartcoreinc,dc=com" bindmethod=simple credentials=${LDAP_ADMIN_PASSWORD} searchbase="dc=ldap,dc=smartcoreinc,dc=com" scope=sub schemachecking=on type=refreshAndPersist retry="30 5 300 3" timeout=1
olcSyncRepl: {1}rid=002 provider=ldap://openldap2:389 binddn="cn=admin,dc=ldap,dc=smartcoreinc,dc=com" bindmethod=simple credentials=${LDAP_ADMIN_PASSWORD} searchbase="dc=ldap,dc=smartcoreinc,dc=com" scope=sub schemachecking=on type=refreshAndPersist retry="30 5 300 3" timeout=1
-
add: olcMirrorMode
olcMirrorMode: TRUE
EOF
    echo "[OK] MMR replication configured"
fi

echo ""
echo "=============================================="
echo "MMR Setup Complete - Server ID: ${SERVER_ID}"
echo "=============================================="

# Verify configuration
echo ""
echo "Verifying MMR configuration..."
ldapsearch -x -H ldap://localhost -D "cn=admin,cn=config" -w "${LDAP_CONFIG_PASSWORD}" \
    -b "olcDatabase={1}mdb,cn=config" "(objectClass=*)" olcSyncRepl olcMirrorMode 2>/dev/null | grep -E "^(olcSyncRepl|olcMirrorMode):" || echo "No MMR config found"

exit 0
