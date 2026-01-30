#!/bin/bash
# luckfox-stop.sh - Luckfox Docker 컨테이너 중지 스크립트

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "🛑 ICAO PKD Docker 컨테이너 중지 (Luckfox)..."
docker compose -f docker-compose-luckfox.yaml stop

echo ""
echo "✅ 컨테이너 중지 완료!"
echo "   - 데이터는 보존됩니다."
echo "   - 재시작: ./luckfox-start.sh"
echo ""
