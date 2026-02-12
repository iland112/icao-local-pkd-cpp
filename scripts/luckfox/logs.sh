#!/bin/bash
# luckfox-logs.sh - Luckfox Docker 컨테이너 로그 확인 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$SCRIPT_DIR"
elif [ -f "$SCRIPT_DIR/../../docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
else
    echo "Error: docker-compose-luckfox.yaml not found"; exit 1
fi
cd "$PROJECT_DIR"

SERVICE=$1
LINES=${2:-50}

# Handle -f flag
FOLLOW=""
for arg in "$@"; do
    if [ "$arg" = "-f" ]; then
        FOLLOW="-f"
    fi
done

if [ -z "$SERVICE" ]; then
    echo "=== All container logs (last ${LINES} lines) ==="
    echo ""
    docker compose -f docker-compose-luckfox.yaml logs --tail=$LINES $FOLLOW
else
    echo "=== '$SERVICE' logs (last ${LINES} lines) ==="
    echo ""
    docker compose -f docker-compose-luckfox.yaml logs --tail=$LINES $FOLLOW $SERVICE
fi

echo ""
echo "Services: postgres, pkd-management, pa-service, pkd-relay, api-gateway, frontend, swagger-ui"
echo "Follow:   $0 <service> -f"
