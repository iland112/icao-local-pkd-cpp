#!/bin/bash
# =============================================================================
# ICAO Local PKD - Shared Shell Library
# =============================================================================
# Docker/Podman 스크립트 공통 함수 라이브러리
#
# Usage:
#   RUNTIME="docker"   # or "podman"
#   source "$(dirname "$0")/../lib/common.sh"
#
# Required: RUNTIME variable must be set before sourcing this file.
# =============================================================================

# Prevent double-sourcing
if [ -n "$_COMMON_SH_LOADED" ]; then
    return 0
fi
readonly _COMMON_SH_LOADED=1

# Validate RUNTIME variable
if [ -z "$RUNTIME" ]; then
    echo "ERROR: RUNTIME variable must be set before sourcing common.sh (docker or podman)"
    exit 1
fi

# =============================================================================
# Constants
# =============================================================================
readonly CONTAINER_PREFIX="icao-local-pkd"

# =============================================================================
# parse_db_type - Parse DB_TYPE from .env file
# =============================================================================
# Sets: DB_TYPE (global), PROFILE_FLAG (global)
# Default: "postgres" for docker, "oracle" for podman
parse_db_type() {
    local default_db="${1:-postgres}"
    DB_TYPE=$(grep -E '^DB_TYPE=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'')
    DB_TYPE="${DB_TYPE:-$default_db}"

    if [ "$DB_TYPE" = "oracle" ]; then
        PROFILE_FLAG="--profile oracle"
    else
        PROFILE_FLAG="--profile postgres"
    fi
}

# =============================================================================
# load_credentials - Load LDAP/DB credentials from .env
# =============================================================================
# Sets: LDAP_BIND_DN, LDAP_BIND_PW, LDAP_CONFIG_PW (globals)
load_credentials() {
    LDAP_BIND_DN="cn=admin,dc=ldap,dc=smartcoreinc,dc=com"
    LDAP_BIND_PW="$(grep -E '^LDAP_ADMIN_PASSWORD=' .env 2>/dev/null | cut -d= -f2)"
    LDAP_BIND_PW="${LDAP_BIND_PW:-ldap_test_password_123}"

    LDAP_CONFIG_PW="$(grep -E '^LDAP_CONFIG_PASSWORD=' .env 2>/dev/null | cut -d= -f2)"
    LDAP_CONFIG_PW="${LDAP_CONFIG_PW:-config_test_123}"
}

# =============================================================================
# load_oracle_password - Load Oracle password from .env
# =============================================================================
# Sets: ORACLE_PWD (global)
load_oracle_password() {
    ORACLE_PWD=$(grep -E '^ORACLE_PASSWORD=' .env 2>/dev/null | cut -d= -f2 | tr -d ' "'"'"'')
    ORACLE_PWD="${ORACLE_PWD:-pkd_password}"
}

# =============================================================================
# detect_ssl - Check for SSL certificates
# =============================================================================
# Sets: SSL_MODE, SSL_DOMAIN (globals)
# Returns: 0 if SSL found, 1 if not
detect_ssl() {
    SSL_DOMAIN="${SSL_DOMAIN:-pkd.smartcoreinc.com}"
    if [ -f ".docker-data/ssl/server.crt" ] && [ -f ".docker-data/ssl/server.key" ]; then
        SSL_MODE="true"
        return 0
    else
        SSL_MODE=""
        return 1
    fi
}

# =============================================================================
# create_directories - Create common data directories
# =============================================================================
create_directories() {
    mkdir -p ./data/uploads ./data/cert ./logs ./backups 2>/dev/null || true
    mkdir -p ./.docker-data/pkd-logs ./.docker-data/pkd-uploads 2>/dev/null || true
    mkdir -p ./.docker-data/pa-logs ./.docker-data/sync-logs 2>/dev/null || true
    mkdir -p ./.docker-data/monitoring-logs ./.docker-data/gateway-logs 2>/dev/null || true
    mkdir -p ./.docker-data/ssl 2>/dev/null || true
}

# =============================================================================
# setup_permissions - chmod + sudo fallback for log directories
# =============================================================================
# Arguments: list of directories to chmod
setup_permissions() {
    local dirs="$@"
    if [ -z "$dirs" ]; then
        dirs="./.docker-data/pkd-logs ./.docker-data/pkd-uploads ./.docker-data/pa-logs ./.docker-data/sync-logs ./.docker-data/monitoring-logs ./.docker-data/gateway-logs"
    fi
    chmod -R 777 $dirs 2>/dev/null || \
    sudo chmod -R 777 $dirs 2>/dev/null || true
}

# =============================================================================
# wait_for_oracle_xepdb1 - Oracle XEPDB1 PDB ready check (polling loop)
# =============================================================================
# Arguments:
#   $1 - max wait seconds (default: 120)
#   $2 - poll interval seconds (default: 5)
wait_for_oracle_xepdb1() {
    local max_wait="${1:-120}"
    local interval="${2:-5}"
    local waited=0

    echo ""
    echo "  Oracle XEPDB1 준비 대기 중..."
    while [ $waited -lt $max_wait ]; do
        if $RUNTIME exec ${CONTAINER_PREFIX}-oracle bash -c \
            "echo 'SELECT 1 FROM DUAL;' | sqlplus -s sys/\"\$ORACLE_PWD\"@//localhost:1521/XEPDB1 as sysdba 2>/dev/null | grep -q 1" 2>/dev/null; then
            echo "  Oracle XEPDB1 준비 완료 (${waited}초)"
            return 0
        fi
        sleep $interval
        waited=$((waited + interval))
        echo "   대기 중... (${waited}/${max_wait}초)"
    done
    echo "  Oracle XEPDB1 타임아웃 (${max_wait}초) — 수동 확인 필요"
    return 1
}

# =============================================================================
# check_service_health - HTTP health check with curl
# =============================================================================
# Arguments:
#   $1 - service display name
#   $2 - URL to check
#   $3 - (optional) "show_body" to print response body
# Returns: 0 if healthy, 1 if not
check_service_health() {
    local name="$1"
    local url="$2"
    local show_body="${3:-}"

    if curl -sf "$url" > /dev/null 2>&1; then
        if [ "$show_body" = "show_body" ]; then
            local body
            body=$(curl -s "$url" 2>/dev/null)
            echo "    ${name}: 정상 ($url)"
            echo "     $body"
        else
            echo "    ${name}: 정상 ($url)"
        fi
        return 0
    fi
    return 1
}

# =============================================================================
# check_container_health - Health check via container exec (internal port)
# =============================================================================
# Arguments:
#   $1 - container name suffix (e.g., "management", "pa-service")
#   $2 - internal URL (e.g., "http://localhost:8081/api/health")
#   $3 - display name
# Returns: 0 if healthy, 1 if not
check_container_health() {
    local container="${CONTAINER_PREFIX}-${1}"
    local url="$2"
    local name="$3"

    if $RUNTIME exec "$container" curl -sf "$url" > /dev/null 2>&1; then
        local body
        body=$($RUNTIME exec "$container" curl -s "$url" 2>/dev/null)
        echo "    정상 ($url)"
        echo "     $body"
        return 0
    fi
    echo "    오류 (not responding)"
    return 1
}

# =============================================================================
# check_oracle_health - Check Oracle container health status
# =============================================================================
# Prints status and returns health string via ORACLE_HEALTH global
check_oracle_health() {
    ORACLE_HEALTH=$($RUNTIME inspect ${CONTAINER_PREFIX}-oracle --format='{{.State.Health.Status}}' 2>/dev/null || echo "not-found")
}

# =============================================================================
# check_ldap_node - Check a single LDAP node and count entries
# =============================================================================
# Arguments:
#   $1 - container name suffix (e.g., "openldap1")
#   $2 - variable name to store count (LDAP1_COUNT or LDAP2_COUNT)
# Returns: 0 if OK, 1 if not
# Sets: the count variable and result variable
check_ldap_node() {
    local container="${CONTAINER_PREFIX}-${1}"
    local display_name="$1"

    if $RUNTIME exec "$container" ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
        local result
        result=$($RUNTIME exec "$container" ldapsearch -x -H ldap://localhost \
            -D "$LDAP_BIND_DN" -w "$LDAP_BIND_PW" \
            -b "dc=ldap,dc=smartcoreinc,dc=com" -s sub "(objectClass=*)" dn 2>/dev/null)
        local count
        count=$(echo "$result" | grep "^dn:" | wc -l | xargs)
        # Return count via stdout
        echo "$count"
        return 0
    fi
    echo "0"
    return 1
}

# =============================================================================
# check_mmr_status - Check LDAP MMR replication configuration
# =============================================================================
# Returns: 0 if MMR enabled, 1 if not
check_mmr_status() {
    local mmr_config
    mmr_config=$($RUNTIME exec ${CONTAINER_PREFIX}-openldap1 ldapsearch -x -H ldap://localhost \
        -D "cn=admin,cn=config" -w "$LDAP_CONFIG_PW" \
        -b "olcDatabase={1}mdb,cn=config" "(objectClass=*)" olcMirrorMode 2>/dev/null | grep "olcMirrorMode" || echo "")
    if [ -n "$mmr_config" ]; then
        return 0
    fi
    return 1
}

# =============================================================================
# backup_ldap - Export LDAP data to LDIF file
# =============================================================================
# Arguments:
#   $1 - output file path
backup_ldap() {
    local output_file="$1"

    if $RUNTIME exec ${CONTAINER_PREFIX}-openldap1 ldapsearch -x -H ldap://localhost -b "" -s base > /dev/null 2>&1; then
        $RUNTIME exec ${CONTAINER_PREFIX}-openldap1 ldapsearch -x \
            -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
            -w "$LDAP_BIND_PW" \
            -H ldap://localhost \
            -b "dc=ldap,dc=smartcoreinc,dc=com" \
            -LLL > "$output_file" 2>/dev/null
        local count
        count=$(grep -c "^dn:" "$output_file" 2>/dev/null || echo 0)
        echo "    OpenLDAP 백업 완료 ($count entries)"
        return 0
    else
        echo "    OpenLDAP이 실행 중이지 않습니다"
        return 1
    fi
}

# =============================================================================
# backup_directory - Backup a directory as tar.gz
# =============================================================================
# Arguments:
#   $1 - source directory
#   $2 - output tar.gz path
#   $3 - display name
backup_directory() {
    local src_dir="$1"
    local output_file="$2"
    local name="$3"

    if [ -d "$src_dir" ] && [ "$(ls -A "$src_dir" 2>/dev/null)" ]; then
        tar -czf "$output_file" "$src_dir"
        echo "    ${name} 백업 완료"
        return 0
    else
        echo "    ${name}이(가) 없습니다. 건너뜁니다."
        return 1
    fi
}

# =============================================================================
# print_connection_info - Print connection info block
# =============================================================================
# Arguments:
#   $1 - SKIP_LDAP flag
#   $2 - SKIP_APP flag
print_connection_info() {
    local skip_ldap="$1"
    local skip_app="$2"

    echo ""
    echo "  접속 정보:"
    echo "   - Database:      DB_TYPE=$DB_TYPE"
    if [ "$DB_TYPE" = "oracle" ]; then
        echo "   - Oracle:        localhost:11521 (XEPDB1)"
    else
        echo "   - PostgreSQL:    localhost:15432 (pkd/pkd)"
    fi
    if [ -z "$skip_ldap" ]; then
        echo "   - OpenLDAP 1:    ldap://localhost:13891"
        echo "   - OpenLDAP 2:    ldap://localhost:13892"
    fi
    if [ -z "$skip_app" ]; then
        echo "   - Frontend:      http://localhost:13080"
        if [ -n "$SSL_MODE" ]; then
            echo "   - API Gateway:   https://$SSL_DOMAIN/api (HTTPS)"
            if [ "$RUNTIME" = "docker" ]; then
                echo "   - API Gateway:   http://$SSL_DOMAIN/api (HTTP)"
                echo "   - API Gateway:   http://localhost:18080/api (internal)"
            fi
        else
            echo "   - API Gateway:   http://localhost:18080/api"
        fi
        echo "   - Swagger UI:    http://localhost:18090"
    fi
}
