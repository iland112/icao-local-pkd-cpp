#!/bin/bash
# luckfox-backup.sh - Luckfox 데이터 백업 스크립트

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$SCRIPT_DIR"
elif [ -f "$SCRIPT_DIR/../../docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
else
    echo "Error: docker-compose-luckfox.yaml not found"; exit 1
fi
cd "$PROJECT_DIR"

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BACKUP_DIR="./backups/luckfox_${TIMESTAMP}"

echo "=== ICAO PKD Backup (Luckfox) ==="
echo ""

mkdir -p "$BACKUP_DIR"

# 1. PostgreSQL backup
echo "[1/3] Backing up PostgreSQL..."
docker exec icao-pkd-postgres pg_dump -U pkd localpkd > "$BACKUP_DIR/localpkd.sql"
echo "  Done."

# 2. Upload files backup
echo "[2/3] Backing up upload files..."
if [ -d "./.docker-data/pkd-uploads" ] && [ "$(ls -A ./.docker-data/pkd-uploads 2>/dev/null)" ]; then
    cp -r ./.docker-data/pkd-uploads "$BACKUP_DIR/"
    echo "  Done."
else
    echo "  No upload files."
fi

# 3. Config backup
echo "[3/3] Backing up config..."
cp docker-compose-luckfox.yaml "$BACKUP_DIR/"
cp nginx/api-gateway-luckfox.conf "$BACKUP_DIR/" 2>/dev/null || true
cp frontend/nginx-luckfox.conf "$BACKUP_DIR/" 2>/dev/null || true
docker images --format '{{.Repository}}:{{.Tag}}\t{{.Size}}\t{{.CreatedAt}}' | grep -E "icao-local|postgres|nginx" > "$BACKUP_DIR/images.txt" 2>/dev/null || true
echo "  Done."

# Compress
echo ""
echo "Compressing..."
cd backups
tar -czf "luckfox_${TIMESTAMP}.tar.gz" "luckfox_${TIMESTAMP}"
rm -rf "luckfox_${TIMESTAMP}"
cd ..

BACKUP_SIZE=$(du -sh "backups/luckfox_${TIMESTAMP}.tar.gz" | cut -f1)

echo ""
echo "=== Backup Complete ==="
echo "  File: backups/luckfox_${TIMESTAMP}.tar.gz"
echo "  Size: $BACKUP_SIZE"
echo ""
echo "Restore: ./luckfox-restore.sh backups/luckfox_${TIMESTAMP}.tar.gz"
echo ""
