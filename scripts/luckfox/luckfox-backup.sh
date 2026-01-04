#!/bin/bash
#
# Luckfox ICAO Local PKD - Backup
# Usage: ./luckfox-backup.sh [backup_dir]
#

set -e

BACKUP_DIR="${1:-/home/luckfox/backups}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BACKUP_PATH="$BACKUP_DIR/icao-pkd-backup-$TIMESTAMP"

echo "=== ICAO Local PKD - Backup ==="
echo "Backup directory: $BACKUP_PATH"
echo ""

# Create backup directory
mkdir -p "$BACKUP_PATH"

# PostgreSQL Backup
echo "=== Backing up PostgreSQL ==="
docker exec icao-pkd-postgres pg_dump -U pkd localpkd > "$BACKUP_PATH/postgresql.sql"
echo "PostgreSQL backup: $BACKUP_PATH/postgresql.sql"

# LDAP Backup
echo ""
echo "=== Backing up LDAP ==="
docker exec icao-pkd-openldap slapcat > "$BACKUP_PATH/ldap.ldif"
echo "LDAP backup: $BACKUP_PATH/ldap.ldif"

# Upload Files Backup
echo ""
echo "=== Backing up Upload Files ==="
if [ -d "/home/luckfox/icao-local-pkd-cpp-v2/.docker-data/pkd-uploads" ]; then
    tar -czf "$BACKUP_PATH/uploads.tar.gz" -C /home/luckfox/icao-local-pkd-cpp-v2/.docker-data pkd-uploads
    echo "Uploads backup: $BACKUP_PATH/uploads.tar.gz"
else
    echo "No upload files found"
fi

# Backup Summary
echo ""
echo "=== Backup Complete ==="
echo "Location: $BACKUP_PATH"
ls -lh "$BACKUP_PATH"

# Compress entire backup
echo ""
echo "=== Compressing Backup ==="
cd "$BACKUP_DIR"
tar -czf "icao-pkd-backup-$TIMESTAMP.tar.gz" "icao-pkd-backup-$TIMESTAMP"
rm -rf "$BACKUP_PATH"
echo "Final backup: $BACKUP_DIR/icao-pkd-backup-$TIMESTAMP.tar.gz"
