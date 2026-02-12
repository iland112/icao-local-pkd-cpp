#!/bin/bash
# luckfox-restore.sh - Luckfox 데이터 복구 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$SCRIPT_DIR"
elif [ -f "$SCRIPT_DIR/../../docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
else
    echo "Error: docker-compose-luckfox.yaml not found"; exit 1
fi
cd "$PROJECT_DIR"

BACKUP_FILE=$1

if [ -z "$BACKUP_FILE" ]; then
    echo "Usage: $0 <backup-file.tar.gz>"
    echo ""
    echo "Available backups:"
    ls -lh backups/*.tar.gz 2>/dev/null || echo "  (no backup files found)"
    exit 1
fi

if [ ! -f "$BACKUP_FILE" ]; then
    echo "Error: Backup file not found: $BACKUP_FILE"
    exit 1
fi

echo "WARNING: Current data will be overwritten with backup data!"
echo ""
read -p "Continue? (yes/no): " CONFIRM

if [ "$CONFIRM" != "yes" ]; then
    echo "Cancelled."
    exit 0
fi

echo ""
echo "[1/4] Extracting backup..."
TEMP_DIR=$(mktemp -d)
tar -xzf "$BACKUP_FILE" -C "$TEMP_DIR"
BACKUP_DIR=$(ls -d "$TEMP_DIR"/luckfox_* | head -1)

# Stop containers
echo "[2/4] Stopping containers..."
docker compose -f docker-compose-luckfox.yaml stop

# PostgreSQL restore
echo "[3/4] Restoring PostgreSQL..."
if [ -f "$BACKUP_DIR/localpkd.sql" ]; then
    docker compose -f docker-compose-luckfox.yaml start postgres
    sleep 5
    docker exec -i icao-pkd-postgres psql -U pkd -d postgres -c "DROP DATABASE IF EXISTS localpkd;" 2>/dev/null || true
    docker exec -i icao-pkd-postgres psql -U pkd -d postgres -c "CREATE DATABASE localpkd;" 2>/dev/null || true
    sleep 2
    docker exec -i icao-pkd-postgres psql -U pkd -d localpkd < "$BACKUP_DIR/localpkd.sql" 2>&1 | grep -v "^ERROR:" | grep -v "^DETAIL:" | grep -v "^CONTEXT:" || true
    echo "  PostgreSQL restored."
else
    echo "  No PostgreSQL dump in backup."
fi

# Upload files restore
if [ -d "$BACKUP_DIR/pkd-uploads" ]; then
    rm -rf ./.docker-data/pkd-uploads/*
    cp -r "$BACKUP_DIR/pkd-uploads/"* ./.docker-data/pkd-uploads/ 2>/dev/null || true
    echo "  Upload files restored."
fi

# Restart all services
echo "[4/4] Starting all services..."
docker compose -f docker-compose-luckfox.yaml up -d

# Cleanup
rm -rf "$TEMP_DIR"

echo ""
echo "=== Restore Complete ==="
echo ""
echo "Run health check: ./luckfox-health.sh"
echo ""
