#!/bin/bash
# podman-stop.sh - Podman 컨테이너 중지 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SCRIPT_DIR"

echo "  ICAO PKD Podman 컨테이너 중지..."

podman-compose -f docker/docker-compose.podman.yaml down

echo "  컨테이너 중지 완료!"
