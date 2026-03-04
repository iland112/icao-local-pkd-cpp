#!/bin/bash
# docker-stop.sh - Docker 컨테이너 중지 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

# Load shared library
RUNTIME="docker"
source "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh"

# Read DB_TYPE from .env
parse_db_type "postgres"

echo "  ICAO PKD Docker 컨테이너 중지... (DB_TYPE=$DB_TYPE)"

docker compose -f docker/docker-compose.yaml $PROFILE_FLAG down

echo "  컨테이너 중지 완료!"
