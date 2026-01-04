#!/bin/bash
#
# Luckfox ICAO Local PKD - Restore from Backup
# Usage: ./luckfox-restore.sh <backup_file.tar.gz>
#

set -e

if [ $# -eq 0 ]; then
    echo "Usage: ./luckfox-restore.sh <backup_file.tar.gz>"
    echo ""
    echo "Available backups:"
    ls -lh /home/luckfox/backups/*.tar.gz 2>/dev/null || echo "  No backups found in /home/luckfox/backups/"
    exit 1
fi

BACKUP_FILE="$1"
TEMP_DIR="/tmp/icao-pkd-restore-$$"

if [ ! -f "$BACKUP_FILE" ]; then
    echo "Error: Backup file not found: $BACKUP_FILE"
    exit 1
fi

echo "=== ICAO Local PKD - Restore ==="
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
    cat "$TEMP_DIR/$BACKUP_DIR/postgresql.sql" | docker exec -i icao-pkd-postgres psql -U pkd -d localpkd
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
    echo "  1. Stop OpenLDAP: docker compose -f docker-compose-luckfox.yaml stop openldap"
    echo "  2. Clear data: docker exec icao-pkd-openldap rm -rf /var/lib/ldap/*"
    echo "  3. Import: docker exec -i icao-pkd-openldap slapadd < ldap.ldif"
    echo "  4. Restart: docker compose -f docker-compose-luckfox.yaml start openldap"
fi

# Upload Files Restore
if [ -f "$TEMP_DIR/$BACKUP_DIR/uploads.tar.gz" ]; then
    echo ""
    echo "=== Restoring Upload Files ==="
    tar -xzf "$TEMP_DIR/$BACKUP_DIR/uploads.tar.gz" -C /home/luckfox/icao-local-pkd-cpp-v2/.docker-data/
    echo "Upload files restored"
fi

# Cleanup
rm -rf "$TEMP_DIR"

echo ""
echo "=== Restore Complete ==="
echo "Note: You may need to restart services for changes to take effect."
echo "  ./luckfox-restart.sh"
