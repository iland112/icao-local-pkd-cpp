#!/bin/bash
#
# Luckfox ICAO Local PKD - Clean All Data
# Usage: ./luckfox-clean.sh [jvm|cpp]
# WARNING: This will delete all containers, volumes, and data!
#

set -e

# Source common configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/luckfox-common.sh"

# Check for help
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    print_usage "luckfox-clean.sh" ""
    echo ""
    echo "WARNING: This script will:"
    echo "  1. Stop all containers"
    echo "  2. Remove all containers"
    echo "  3. Remove all volumes"
    echo "  4. Delete .docker-data directory"
    echo ""
    echo "All data will be permanently deleted!"
    exit 0
fi

# Parse version
parse_version "$@" > /dev/null

COMPOSE_FILE=$(get_compose_file)
PROJECT_DIR=$(get_project_dir)

echo "╔════════════════════════════════════════════════════════════╗"
echo "║         ICAO Local PKD - COMPLETE DATA CLEAN              ║"
echo "╚════════════════════════════════════════════════════════════╝"
print_version_info
echo ""
echo "⚠️  WARNING: This will delete ALL data including:"
echo "   - All Docker containers"
echo "   - All Docker volumes"
echo "   - PostgreSQL data"
echo "   - Uploaded files"
echo "   - LDAP data (if using OpenLDAP)"
echo ""
echo "This action CANNOT be undone!"
echo ""

# Confirmation prompt
read -p "Are you sure? Type 'yes' to confirm: " confirm
if [ "$confirm" != "yes" ]; then
    echo "❌ Cancelled."
    exit 0
fi

echo ""
echo "=== Starting Clean Process ==="
echo ""

# Stop all services
echo "=== Stopping Services ==="
cd "$PROJECT_DIR"
docker compose -f "$COMPOSE_FILE" down -v 2>/dev/null || true
echo "✓ Services stopped and volumes removed"

# Remove .docker-data directory if it exists
echo ""
echo "=== Removing Data Directories ==="
if [ -d "$PROJECT_DIR/.docker-data" ]; then
    rm -rf "$PROJECT_DIR/.docker-data"
    echo "✓ Removed .docker-data directory"
else
    echo "- .docker-data directory not found"
fi

# Optional: Remove backup directory if empty
if [ -d "/home/luckfox/backups" ]; then
    if [ ! "$(ls -A /home/luckfox/backups)" ]; then
        rmdir /home/luckfox/backups 2>/dev/null || true
        echo "✓ Removed empty backups directory"
    else
        echo "- Kept /home/luckfox/backups (contains files)"
    fi
fi

echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║                     CLEAN COMPLETE                        ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "All data has been removed."
echo ""
echo "To restart services with fresh data:"
echo "  cd $PROJECT_DIR"
echo "  /home/luckfox/scripts/luckfox-start.sh $VERSION"
echo ""
