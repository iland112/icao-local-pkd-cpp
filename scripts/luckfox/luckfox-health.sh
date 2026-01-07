#!/bin/bash
#
# Luckfox ICAO Local PKD - Health Check
# Usage: ./luckfox-health.sh [jvm|cpp]
#

set -e

# Source common configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/luckfox-common.sh"

# Check for help
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    print_usage "luckfox-health.sh" ""
    exit 0
fi

# Parse version
parse_version "$@" > /dev/null

COMPOSE_FILE=$(get_compose_file)
PROJECT_DIR=$(get_project_dir)

echo "=== ICAO Local PKD - Health Check ==="
print_version_info
echo ""

# Container Status
echo "=== Container Status ==="
cd "$PROJECT_DIR"
docker compose -f "$COMPOSE_FILE" ps
echo ""

# API Health Checks
echo "=== API Health Checks ==="

# Helper function to extract status from JSON (works without jq)
get_status() {
    local response="$1"
    if command -v jq &> /dev/null; then
        echo "$response" | jq -r '.status // "ERROR"' 2>/dev/null
    else
        # Simple grep fallback
        echo "$response" | grep -oP '"status"\s*:\s*"\K[^"]+' 2>/dev/null || echo "ERROR"
    fi
}

if [ "$VERSION" = "jvm" ]; then
    # JVM version health checks
    echo -n "Backend (8080):        "
    response=$(curl -s --connect-timeout 5 http://$HOST:8080/api/health 2>/dev/null)
    if [ -n "$response" ]; then
        get_status "$response"
    else
        echo "UNREACHABLE"
    fi

    echo -n "Frontend (80):         "
    http_code=$(curl -s -o /dev/null -w "%{http_code}" --connect-timeout 5 http://$HOST:80 2>/dev/null)
    if [ "$http_code" = "200" ]; then echo "OK"; else echo "UNREACHABLE ($http_code)"; fi
else
    # CPP version health checks
    echo -n "PKD Management (8081): "
    response=$(curl -s --connect-timeout 5 http://$HOST:8081/api/health 2>/dev/null)
    if [ -n "$response" ]; then
        get_status "$response"
    else
        echo "UNREACHABLE"
    fi

    echo -n "PA Service (8082):     "
    response=$(curl -s --connect-timeout 5 http://$HOST:8082/api/health 2>/dev/null)
    if [ -n "$response" ]; then
        get_status "$response"
    else
        echo "UNREACHABLE"
    fi

    echo -n "Sync Service (8083):   "
    response=$(curl -s --connect-timeout 5 http://$HOST:8083/api/sync/health 2>/dev/null)
    if [ -n "$response" ]; then
        get_status "$response"
    else
        echo "UNREACHABLE"
    fi

    echo -n "Frontend (80):         "
    http_code=$(curl -s -o /dev/null -w "%{http_code}" --connect-timeout 5 http://$HOST:80 2>/dev/null)
    if [ "$http_code" = "200" ]; then echo "OK"; else echo "UNREACHABLE ($http_code)"; fi
fi
echo ""

# Database Check
echo "=== Database Check ==="
echo -n "PostgreSQL: "
if [ "$VERSION" = "jvm" ]; then
    docker exec icao-pkd-postgres pg_isready -U pkd -d pkd 2>/dev/null && echo "READY" || echo "NOT READY"
else
    docker exec icao-pkd-postgres pg_isready -U pkd -d localpkd 2>/dev/null && echo "READY" || echo "NOT READY"
fi
echo ""

# Sync Status (CPP only)
if [ "$VERSION" = "cpp" ]; then
    echo "=== Sync Status ==="
    sync_response=$(curl -s --connect-timeout 5 http://$HOST:8083/api/sync/status 2>/dev/null)
    if [ -n "$sync_response" ]; then
        if command -v jq &> /dev/null; then
            echo "$sync_response" | jq '{
              status: .status,
              db: {csca: .dbStats.cscaCount, dsc: .dbStats.dscCount, dscNc: .dbStats.dscNcCount},
              ldap: {csca: .ldapStats.cscaCount, dsc: .ldapStats.dscCount, dscNc: .ldapStats.dscNcCount}
            }' 2>/dev/null
        else
            # Simple output without jq
            echo "$sync_response" | grep -oP '"status"\s*:\s*"\K[^"]+' 2>/dev/null | head -1 | xargs -I{} echo "Status: {}"
            echo "(Install jq for detailed output)"
        fi
    else
        echo "Unable to fetch sync status"
    fi
    echo ""
fi

# LDAP Entry Count (via HAProxy)
echo "=== LDAP Entry Count (via HAProxy) ==="
ldapsearch -x -H ldap://localhost:389 \
    -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" -w admin123 \
    -b "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" "(objectClass=*)" dn 2>/dev/null | grep -c "^dn:" || echo "0 (LDAP unreachable)"
echo ""

echo "=== Health Check Complete ==="
