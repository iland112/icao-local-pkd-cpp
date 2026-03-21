#!/bin/bash
# =============================================================================
# ICAO PKD Simulation - Populate from Local LDAP
# =============================================================================
# Exports certificates from local LDAP and imports them into the ICAO PKD
# simulation LDAP with ICAO-style DN format.
#
# Usage:
#   ./populate-from-local.sh [--max N] [--type csca|dsc|crl|dsc_nc]
#
# Environment:
#   LOCAL_LDAP_HOST   (default: openldap1)
#   LOCAL_LDAP_PORT   (default: 389)
#   ICAO_LDAP_HOST    (default: icao-pkd-ldap)
#   ICAO_LDAP_PORT    (default: 389)
# =============================================================================

set -euo pipefail

# --- Configuration ---
LOCAL_HOST="${LOCAL_LDAP_HOST:-openldap1}"
LOCAL_PORT="${LOCAL_LDAP_PORT:-389}"
LOCAL_BIND_DN="cn=admin,dc=ldap,dc=smartcoreinc,dc=com"
LOCAL_BIND_PW="${LDAP_BIND_PASSWORD:-ldap_test_password_123}"
LOCAL_BASE="dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"

ICAO_HOST="${ICAO_LDAP_HOST:-icao-pkd-ldap}"
ICAO_PORT="${ICAO_LDAP_PORT:-389}"
ICAO_BIND_DN="cn=admin,dc=icao,dc=int"
ICAO_BIND_PW="${ICAO_LDAP_ADMIN_PASSWORD:-icao_sim_password}"
ICAO_BASE="dc=download,dc=pkd,dc=icao,dc=int"

MAX_ENTRIES="${1:-0}"  # 0 = unlimited
FILTER_TYPE="${2:-all}"

TOTAL_ADDED=0
TOTAL_SKIPPED=0
TOTAL_FAILED=0

log() { echo "[$(date '+%H:%M:%S')] $*"; }
log_err() { echo "[$(date '+%H:%M:%S')] ERROR: $*" >&2; }

# --- Parse arguments ---
while [[ $# -gt 0 ]]; do
  case "$1" in
    --max) MAX_ENTRIES="$2"; shift 2 ;;
    --type) FILTER_TYPE="$2"; shift 2 ;;
    *) shift ;;
  esac
done

# --- Wait for ICAO LDAP to be ready ---
wait_for_ldap() {
  local host="$1" port="$2" bind_dn="$3" bind_pw="$4" max_wait=60 elapsed=0
  log "Waiting for LDAP $host:$port..."
  while [ $elapsed -lt $max_wait ]; do
    if ldapsearch -x -H "ldap://$host:$port" -D "$bind_dn" -w "$bind_pw" -b "" -s base "(objectClass=*)" dn > /dev/null 2>&1; then
      log "LDAP $host:$port is ready"
      return 0
    fi
    sleep 2
    elapsed=$((elapsed + 2))
  done
  log_err "LDAP $host:$port not ready after ${max_wait}s"
  return 1
}

# --- Initialize ICAO DIT structure ---
init_icao_dit() {
  log "Initializing ICAO PKD DIT structure..."

  # Check if DIT already exists
  if ldapsearch -x -H "ldap://$ICAO_HOST:$ICAO_PORT" -D "$ICAO_BIND_DN" -w "$ICAO_BIND_PW" \
     -b "dc=data,$ICAO_BASE" -s base "(objectClass=*)" dn > /dev/null 2>&1; then
    log "ICAO DIT already initialized"
    return 0
  fi

  local dit_file="/tmp/icao-dit.ldif"
  if [ -f "/icao-pkd-ldap/bootstrap/01-icao-pkd-dit.ldif" ]; then
    dit_file="/icao-pkd-ldap/bootstrap/01-icao-pkd-dit.ldif"
  elif [ -f "$(dirname "$0")/../bootstrap/01-icao-pkd-dit.ldif" ]; then
    dit_file="$(dirname "$0")/../bootstrap/01-icao-pkd-dit.ldif"
  else
    # Generate inline
    cat > "$dit_file" << 'DITEOF'
dn: dc=pkd,dc=icao,dc=int
objectClass: top
objectClass: dcObject
objectClass: organization
dc: pkd
o: ICAO PKD

dn: dc=download,dc=pkd,dc=icao,dc=int
objectClass: top
objectClass: dcObject
objectClass: organization
dc: download
o: PKD Download

dn: dc=data,dc=download,dc=pkd,dc=icao,dc=int
objectClass: top
objectClass: dcObject
objectClass: organization
dc: data
o: PKD Data

dn: dc=nc-data,dc=download,dc=pkd,dc=icao,dc=int
objectClass: top
objectClass: dcObject
objectClass: organization
dc: nc-data
o: PKD Non-Conformant Data
DITEOF
  fi

  ldapadd -x -H "ldap://$ICAO_HOST:$ICAO_PORT" -D "$ICAO_BIND_DN" -w "$ICAO_BIND_PW" \
    -f "$dit_file" 2>/dev/null || true
  log "ICAO DIT initialized"
}

# --- Ensure country OU exists in ICAO LDAP ---
ensure_country_ou() {
  local country="$1" data_container="$2"
  local country_dn="c=$country,$data_container,$ICAO_BASE"

  if ! ldapsearch -x -H "ldap://$ICAO_HOST:$ICAO_PORT" -D "$ICAO_BIND_DN" -w "$ICAO_BIND_PW" \
       -b "$country_dn" -s base "(objectClass=*)" dn > /dev/null 2>&1; then
    cat << EOF | ldapadd -x -H "ldap://$ICAO_HOST:$ICAO_PORT" -D "$ICAO_BIND_DN" -w "$ICAO_BIND_PW" 2>/dev/null || true
dn: $country_dn
objectClass: top
objectClass: country
c: $country
EOF
  fi
}

# --- Ensure type OU exists ---
ensure_type_ou() {
  local country="$1" type="$2" data_container="$3"
  local ou_dn="o=$type,c=$country,$data_container,$ICAO_BASE"

  ensure_country_ou "$country" "$data_container"

  if ! ldapsearch -x -H "ldap://$ICAO_HOST:$ICAO_PORT" -D "$ICAO_BIND_DN" -w "$ICAO_BIND_PW" \
       -b "$ou_dn" -s base "(objectClass=*)" dn > /dev/null 2>&1; then
    cat << EOF | ldapadd -x -H "ldap://$ICAO_HOST:$ICAO_PORT" -D "$ICAO_BIND_DN" -w "$ICAO_BIND_PW" 2>/dev/null || true
dn: $ou_dn
objectClass: top
objectClass: organization
o: $type
EOF
  fi
}

# --- Transfer certificates of a given type ---
transfer_certs() {
  local local_type="$1"       # csca, dsc, mlsc, crl
  local icao_type="$2"        # dsc, crl, ml
  local data_container="$3"   # dc=data or dc=nc-data
  local object_filter="$4"    # LDAP search filter
  local count=0

  log "Searching local LDAP for $local_type certificates (filter: $data_container)..."

  local search_base="$data_container,$LOCAL_BASE"

  # Get list of entries (DNs only)
  local dns
  dns=$(ldapsearch -x -H "ldap://$LOCAL_HOST:$LOCAL_PORT" -D "$LOCAL_BIND_DN" -w "$LOCAL_BIND_PW" \
    -b "$search_base" -s sub "($object_filter)" dn 2>/dev/null | grep "^dn:" | head -n ${MAX_ENTRIES:-99999} || true)

  local total_found
  total_found=$(echo "$dns" | grep -c "^dn:" 2>/dev/null || echo "0")
  log "Found $total_found $local_type entries in local LDAP"

  echo "$dns" | while IFS= read -r dn_line; do
    [ -z "$dn_line" ] && continue
    local src_dn="${dn_line#dn: }"
    [ -z "$src_dn" ] && continue

    # Extract country from DN (c=XX)
    local country
    country=$(echo "$src_dn" | grep -oP 'c=\K[A-Z]{2,3}' | head -1)
    [ -z "$country" ] && continue

    # Extract cn (fingerprint in local format)
    local cn
    cn=$(echo "$src_dn" | grep -oP 'cn=\K[^,]+' | head -1)
    [ -z "$cn" ] && continue

    # Ensure parent OUs exist
    ensure_type_ou "$country" "$icao_type" "$data_container"

    # Build ICAO-style DN (using fingerprint as cn for simulation)
    local icao_dn="cn=$cn,o=$icao_type,c=$country,$data_container,$ICAO_BASE"

    # Check if already exists
    if ldapsearch -x -H "ldap://$ICAO_HOST:$ICAO_PORT" -D "$ICAO_BIND_DN" -w "$ICAO_BIND_PW" \
       -b "$icao_dn" -s base "(objectClass=*)" dn > /dev/null 2>&1; then
      TOTAL_SKIPPED=$((TOTAL_SKIPPED + 1))
      continue
    fi

    # Fetch full entry from local LDAP (binary attributes included)
    local entry_ldif
    entry_ldif=$(ldapsearch -x -H "ldap://$LOCAL_HOST:$LOCAL_PORT" -D "$LOCAL_BIND_DN" -w "$LOCAL_BIND_PW" \
      -b "$src_dn" -s base "(objectClass=*)" -LLL 2>/dev/null || true)

    [ -z "$entry_ldif" ] && continue

    # Transform LDIF: replace DN and add pkdVersion
    local transformed="/tmp/icao_entry_$$.ldif"
    echo "dn: $icao_dn" > "$transformed"
    echo "$entry_ldif" | grep -v "^dn:" | grep -v "^$" >> "$transformed"

    # Add pkdVersion if not present
    if ! grep -q "pkdVersion:" "$transformed"; then
      echo "pkdVersion: 1" >> "$transformed"
    fi
    echo "" >> "$transformed"

    # Import to ICAO LDAP
    if ldapadd -x -H "ldap://$ICAO_HOST:$ICAO_PORT" -D "$ICAO_BIND_DN" -w "$ICAO_BIND_PW" \
       -f "$transformed" > /dev/null 2>&1; then
      TOTAL_ADDED=$((TOTAL_ADDED + 1))
      count=$((count + 1))
    else
      TOTAL_FAILED=$((TOTAL_FAILED + 1))
    fi

    rm -f "$transformed"

    # Progress
    if [ $((count % 100)) -eq 0 ] && [ $count -gt 0 ]; then
      log "  ... $count $local_type entries transferred"
    fi

    # Max entries limit
    if [ "$MAX_ENTRIES" -gt 0 ] && [ $count -ge "$MAX_ENTRIES" ]; then
      log "  Reached max entries limit ($MAX_ENTRIES)"
      break
    fi
  done

  log "Transferred $count $local_type entries"
}

# === Main ===
log "========================================="
log "ICAO PKD Simulation - Data Population"
log "========================================="
log "Local LDAP:  $LOCAL_HOST:$LOCAL_PORT"
log "ICAO LDAP:   $ICAO_HOST:$ICAO_PORT"
log "Filter type: $FILTER_TYPE"
log "Max entries: ${MAX_ENTRIES:-unlimited}"
log ""

wait_for_ldap "$LOCAL_HOST" "$LOCAL_PORT" "$LOCAL_BIND_DN" "$LOCAL_BIND_PW"
wait_for_ldap "$ICAO_HOST" "$ICAO_PORT" "$ICAO_BIND_DN" "$ICAO_BIND_PW"
init_icao_dit

# Transfer by type
if [ "$FILTER_TYPE" = "all" ] || [ "$FILTER_TYPE" = "csca" ]; then
  transfer_certs "csca" "csca" "dc=data" "objectClass=pkdDownload"
fi

if [ "$FILTER_TYPE" = "all" ] || [ "$FILTER_TYPE" = "dsc" ]; then
  transfer_certs "dsc" "dsc" "dc=data" "objectClass=pkdDownload"
fi

if [ "$FILTER_TYPE" = "all" ] || [ "$FILTER_TYPE" = "crl" ]; then
  transfer_certs "crl" "crl" "dc=data" "objectClass=cRLDistributionPoint"
fi

if [ "$FILTER_TYPE" = "all" ] || [ "$FILTER_TYPE" = "dsc_nc" ]; then
  transfer_certs "dsc_nc" "dsc" "dc=nc-data" "objectClass=pkdDownload"
fi

log ""
log "========================================="
log "Population Complete"
log "  Added:   $TOTAL_ADDED"
log "  Skipped: $TOTAL_SKIPPED (already exists)"
log "  Failed:  $TOTAL_FAILED"
log "========================================="
