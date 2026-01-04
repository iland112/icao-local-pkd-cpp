#!/bin/bash
#
# Luckfox ICAO Local PKD - Health Check
# Usage: ./luckfox-health.sh
#

set -e

COMPOSE_FILE="/home/luckfox/icao-local-pkd-cpp-v2/docker-compose-luckfox.yaml"
HOST="192.168.100.11"

echo "=== ICAO Local PKD - Health Check ==="
echo ""

# Container Status
echo "=== Container Status ==="
cd /home/luckfox/icao-local-pkd-cpp-v2
docker compose -f $COMPOSE_FILE ps
echo ""

# API Health Checks
echo "=== API Health Checks ==="

echo -n "PKD Management (8081): "
curl -s http://$HOST:8081/api/health | jq -r '.status // "ERROR"' 2>/dev/null || echo "UNREACHABLE"

echo -n "PA Service (8082):     "
curl -s http://$HOST:8082/api/pa/health | jq -r '.status // "ERROR"' 2>/dev/null || echo "UNREACHABLE"

echo -n "Sync Service (8083):   "
curl -s http://$HOST:8083/api/sync/health | jq -r '.status // "ERROR"' 2>/dev/null || echo "UNREACHABLE"

echo -n "Frontend (3000):       "
curl -s -o /dev/null -w "%{http_code}" http://$HOST:3000 2>/dev/null && echo " OK" || echo "UNREACHABLE"
echo ""

# Database Check
echo "=== Database Check ==="
echo -n "PostgreSQL: "
docker exec icao-pkd-postgres pg_isready -U pkd -d localpkd 2>/dev/null && echo "READY" || echo "NOT READY"
echo ""

# Sync Status
echo "=== Sync Status ==="
curl -s http://$HOST:8083/api/sync/status | jq '{
  status: .status,
  db: {csca: .dbStats.cscaCount, dsc: .dbStats.dscCount, dscNc: .dbStats.dscNcCount},
  ldap: {csca: .ldapStats.cscaCount, dsc: .ldapStats.dscCount, dscNc: .ldapStats.dscNcCount}
}' 2>/dev/null || echo "Unable to fetch sync status"
echo ""

# LDAP Entry Count
echo "=== LDAP Entry Count ==="
docker exec icao-pkd-openldap ldapsearch -x -H ldap://localhost:389 \
    -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" -w admin123 \
    -b "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" "(objectClass=*)" dn 2>/dev/null | grep -c "^dn:" || echo "0"
echo ""

echo "=== Health Check Complete ==="
