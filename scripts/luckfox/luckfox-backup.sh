#!/bin/bash
#
# Luckfox ICAO Local PKD - Backup
# Usage: ./luckfox-backup.sh [jvm|cpp] [backup_dir]
#

set -e

# Source common configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/luckfox-common.sh"

# Check for help
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    print_usage "luckfox-backup.sh" "[backup_dir]"
    exit 0
fi

# Parse version and get remaining args
REMAINING_ARGS=$(parse_version "$@")

PROJECT_DIR=$(get_project_dir)

# Get backup directory from remaining args or use default
BACKUP_DIR="${REMAINING_ARGS:-/home/luckfox/backups}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BACKUP_PATH="$BACKUP_DIR/icao-pkd-${VERSION}-backup-$TIMESTAMP"

echo "=== ICAO Local PKD - Backup ==="
print_version_info
echo "Backup directory: $BACKUP_PATH"
echo ""

# Create backup directory
mkdir -p "$BACKUP_PATH"

# PostgreSQL Backup
echo "=== Backing up PostgreSQL ==="
if [ "$VERSION" = "jvm" ]; then
    docker exec icao-pkd-postgres pg_dump -U pkd pkd > "$BACKUP_PATH/postgresql.sql"
else
    docker exec icao-pkd-postgres pg_dump -U pkd localpkd > "$BACKUP_PATH/postgresql.sql"
fi
echo "PostgreSQL backup: $BACKUP_PATH/postgresql.sql"

# LDAP Backup
echo ""
echo "=== Backing up LDAP ==="
docker exec icao-pkd-openldap slapcat > "$BACKUP_PATH/ldap.ldif"
echo "LDAP backup: $BACKUP_PATH/ldap.ldif"

# Upload Files Backup
echo ""
echo "=== Backing up Upload Files ==="
UPLOAD_DIR="$PROJECT_DIR/.docker-data/pkd-uploads"
if [ -d "$UPLOAD_DIR" ]; then
    tar -czf "$BACKUP_PATH/uploads.tar.gz" -C "$PROJECT_DIR/.docker-data" pkd-uploads
    echo "Uploads backup: $BACKUP_PATH/uploads.tar.gz"
else
    echo "No upload files found at $UPLOAD_DIR"
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
tar -czf "icao-pkd-${VERSION}-backup-$TIMESTAMP.tar.gz" "icao-pkd-${VERSION}-backup-$TIMESTAMP"
rm -rf "$BACKUP_PATH"
echo "Final backup: $BACKUP_DIR/icao-pkd-${VERSION}-backup-$TIMESTAMP.tar.gz"
