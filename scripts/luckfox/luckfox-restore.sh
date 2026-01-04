#!/bin/bash
#
# Luckfox ICAO Local PKD - Restore from Backup
# Usage: ./luckfox-restore.sh [jvm|cpp] <backup_file.tar.gz>
#

set -e

# Source common configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/luckfox-common.sh"

# Check for help
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    print_usage "luckfox-restore.sh" "<backup_file.tar.gz>"
    exit 0
fi

# Parse version and get remaining args
REMAINING_ARGS=$(parse_version "$@")

COMPOSE_FILE=$(get_compose_file)
PROJECT_DIR=$(get_project_dir)

if [ -z "$REMAINING_ARGS" ]; then
    echo "=== ICAO Local PKD - Restore ==="
    print_version_info
    echo ""
    echo "Usage: ./luckfox-restore.sh [jvm|cpp] <backup_file.tar.gz>"
    echo ""
    echo "Available backups:"
    ls -lh /home/luckfox/backups/*.tar.gz 2>/dev/null || echo "  No backups found in /home/luckfox/backups/"
    exit 1
fi

BACKUP_FILE="$REMAINING_ARGS"
TEMP_DIR="/tmp/icao-pkd-restore-$$"

if [ ! -f "$BACKUP_FILE" ]; then
    echo "Error: Backup file not found: $BACKUP_FILE"
    exit 1
fi

echo "=== ICAO Local PKD - Restore ==="
print_version_info
echo "Backup file: $BACKUP_FILE"
echo ""
echo "WARNING: This will overwrite existing data!"
read -p "Continue? (y/N): " confirm
if [ "$confirm" != "y" ] && [ "$confirm" != "Y" ]; then
    echo "Cancelled."
    exit 0
fi

# Extract backup
echo ""
echo "=== Extracting Backup ==="
mkdir -p "$TEMP_DIR"
tar -xzf "$BACKUP_FILE" -C "$TEMP_DIR"
BACKUP_DIR=$(ls "$TEMP_DIR")

# PostgreSQL Restore
if [ -f "$TEMP_DIR/$BACKUP_DIR/postgresql.sql" ]; then
    echo ""
    echo "=== Restoring PostgreSQL ==="
    if [ "$VERSION" = "jvm" ]; then
        cat "$TEMP_DIR/$BACKUP_DIR/postgresql.sql" | docker exec -i icao-pkd-postgres psql -U pkd -d pkd
    else
        cat "$TEMP_DIR/$BACKUP_DIR/postgresql.sql" | docker exec -i icao-pkd-postgres psql -U pkd -d localpkd
    fi
    echo "PostgreSQL restored"
fi

# LDAP Restore (Warning: requires service restart)
if [ -f "$TEMP_DIR/$BACKUP_DIR/ldap.ldif" ]; then
    echo ""
    echo "=== Restoring LDAP ==="
    echo "Note: LDAP restore requires manual intervention."
    echo "LDIF file available at: $TEMP_DIR/$BACKUP_DIR/ldap.ldif"
    echo ""
    echo "To restore LDAP:"
    echo "  1. Stop OpenLDAP: docker compose -f $COMPOSE_FILE stop openldap"
    echo "  2. Clear data: docker exec icao-pkd-openldap rm -rf /var/lib/ldap/*"
    echo "  3. Import: docker exec -i icao-pkd-openldap slapadd < ldap.ldif"
    echo "  4. Restart: docker compose -f $COMPOSE_FILE start openldap"
fi

# Upload Files Restore
if [ -f "$TEMP_DIR/$BACKUP_DIR/uploads.tar.gz" ]; then
    echo ""
    echo "=== Restoring Upload Files ==="
    tar -xzf "$TEMP_DIR/$BACKUP_DIR/uploads.tar.gz" -C "$PROJECT_DIR/.docker-data/"
    echo "Upload files restored"
fi

# Cleanup
rm -rf "$TEMP_DIR"

echo ""
echo "=== Restore Complete ==="
echo "Note: You may need to restart services for changes to take effect."
echo "  ./luckfox-restart.sh $VERSION"
