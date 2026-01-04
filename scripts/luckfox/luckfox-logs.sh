#!/bin/bash
#
# Luckfox ICAO Local PKD - View Logs
# Usage: ./luckfox-logs.sh [service] [-f]
#

COMPOSE_FILE="/home/luckfox/icao-local-pkd-cpp-v2/docker-compose-luckfox.yaml"
cd /home/luckfox/icao-local-pkd-cpp-v2

if [ $# -eq 0 ]; then
    echo "Usage: ./luckfox-logs.sh [service] [-f]"
    echo ""
    echo "Services:"
    echo "  postgres       - PostgreSQL database"
    echo "  openldap       - OpenLDAP server"
    echo "  pkd-management - PKD Management service"
    echo "  pa-service     - PA Verification service"
    echo "  sync-service   - DB-LDAP Sync service"
    echo "  frontend       - React frontend"
    echo ""
    echo "Options:"
    echo "  -f             - Follow log output"
    echo ""
    echo "Example:"
    echo "  ./luckfox-logs.sh sync-service -f"
    exit 0
fi

docker compose -f $COMPOSE_FILE logs "$@"
