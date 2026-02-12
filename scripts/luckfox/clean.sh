#!/bin/bash
# luckfox-clean.sh - Luckfox Docker 완전 초기화 스크립트
# WARNING: Deletes ALL containers and data!

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$SCRIPT_DIR"
elif [ -f "$SCRIPT_DIR/../../docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
else
    echo "Error: docker-compose-luckfox.yaml not found"; exit 1
fi
cd "$PROJECT_DIR"

# --force flag skips confirmation (for scripted use)
FORCE=false
for arg in "$@"; do
    if [ "$arg" = "--force" ] || [ "$arg" = "-f" ]; then
        FORCE=true
    fi
done

if [ "$FORCE" = false ]; then
    echo "WARNING: This will delete ALL containers and data!"
    echo ""
    echo "  Data to be deleted:"
    echo "    - PostgreSQL database (.docker-data/postgres)"
    echo "    - Upload files (.docker-data/pkd-uploads)"
    echo "    - Service logs (.docker-data/pkd-logs, pa-logs, sync-logs)"
    echo ""
    read -p "Continue? (yes/no): " CONFIRM
    if [ "$CONFIRM" != "yes" ]; then
        echo "Cancelled."
        exit 0
    fi
fi

echo ""
echo "[1/3] Stopping and removing containers..."
docker compose -f docker-compose-luckfox.yaml down

echo ""
echo "[2/3] Deleting data directories..."
sudo rm -rf ./.docker-data/postgres/* 2>/dev/null || rm -rf ./.docker-data/postgres/* 2>/dev/null || true
sudo rm -rf ./.docker-data/pkd-uploads/* 2>/dev/null || rm -rf ./.docker-data/pkd-uploads/* 2>/dev/null || true
sudo rm -rf ./.docker-data/pkd-logs/* 2>/dev/null || rm -rf ./.docker-data/pkd-logs/* 2>/dev/null || true
sudo rm -rf ./.docker-data/pa-logs/* 2>/dev/null || rm -rf ./.docker-data/pa-logs/* 2>/dev/null || true
sudo rm -rf ./.docker-data/sync-logs/* 2>/dev/null || rm -rf ./.docker-data/sync-logs/* 2>/dev/null || true
echo "  Data directories cleaned."

echo ""
echo "[3/3] Pruning unused Docker resources..."
docker image prune -f 2>/dev/null || true
echo "  Docker cleanup done."

echo ""
echo "=== Clean Complete ==="
echo "To restart: ./luckfox-start.sh"
echo ""
