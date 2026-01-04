#!/bin/bash
#
# Luckfox ICAO Local PKD - View Logs
# Usage: ./luckfox-logs.sh [jvm|cpp] [service] [-f]
#

# Source common configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/luckfox-common.sh"

# Check for help or no args
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    print_usage "luckfox-logs.sh" "[service] [-f]"
    echo ""
    echo "CPP Services:"
    echo "  postgres       - PostgreSQL database"
    echo "  openldap       - OpenLDAP server"
    echo "  pkd-management - PKD Management service"
    echo "  pa-service     - PA Verification service"
    echo "  sync-service   - DB-LDAP Sync service"
    echo "  frontend       - React frontend"
    echo ""
    echo "JVM Services:"
    echo "  postgres       - PostgreSQL database"
    echo "  openldap       - OpenLDAP server"
    echo "  backend        - Spring Boot backend"
    echo "  frontend       - React frontend"
    echo ""
    echo "Options:"
    echo "  -f             - Follow log output"
    echo ""
    echo "Example:"
    echo "  ./luckfox-logs.sh cpp sync-service -f"
    echo "  ./luckfox-logs.sh jvm backend -f"
    exit 0
fi

# Parse version and get remaining args
REMAINING_ARGS=$(parse_version "$@")

COMPOSE_FILE=$(get_compose_file)
PROJECT_DIR=$(get_project_dir)

if [ -z "$REMAINING_ARGS" ]; then
    echo "=== ICAO Local PKD - Log Viewer ==="
    print_version_info
    echo ""
    echo "Usage: ./luckfox-logs.sh [jvm|cpp] [service] [-f]"
    echo ""
    echo "Use -h for help"
    exit 0
fi

cd "$PROJECT_DIR"
docker compose -f "$COMPOSE_FILE" logs $REMAINING_ARGS
