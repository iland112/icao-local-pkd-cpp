#!/bin/bash
# luckfox-health.sh - Luckfox Docker 헬스체크 스크립트

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$SCRIPT_DIR"
elif [ -f "$SCRIPT_DIR/../../docker-compose-luckfox.yaml" ]; then
    PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
else
    echo "Error: docker-compose-luckfox.yaml not found"; exit 1
fi
cd "$PROJECT_DIR"

echo "=== ICAO PKD Health Check (Luckfox) ==="
echo ""

# 1. Container status
echo "[Containers]"
docker compose -f docker-compose-luckfox.yaml ps
echo ""

# 2. PostgreSQL
echo "[PostgreSQL]"
if docker exec icao-pkd-postgres pg_isready -U pkd -d localpkd &>/dev/null; then
    TABLE_COUNT=$(docker exec icao-pkd-postgres psql -U pkd -d localpkd -t -c "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='public';" 2>/dev/null | tr -d ' ')
    CERT_COUNT=$(docker exec icao-pkd-postgres psql -U pkd -d localpkd -t -c "SELECT COUNT(*) FROM certificate;" 2>/dev/null | tr -d ' ')
    CRL_COUNT=$(docker exec icao-pkd-postgres psql -U pkd -d localpkd -t -c "SELECT COUNT(*) FROM crl;" 2>/dev/null | tr -d ' ')
    echo "  OK - Tables: $TABLE_COUNT, Certificates: $CERT_COUNT, CRLs: $CRL_COUNT"
else
    echo "  FAIL - Connection refused"
fi
echo ""

# 3. API Services
echo "[API Services]"
# PKD Management
HEALTH=$(curl -s --connect-timeout 3 http://127.0.0.1:8081/api/health 2>/dev/null)
if [ $? -eq 0 ] && [ -n "$HEALTH" ]; then
    VERSION=$(echo "$HEALTH" | grep -o '"version":"[^"]*"' | cut -d'"' -f4)
    echo "  PKD Management (8081): UP (version: $VERSION)"
else
    echo "  PKD Management (8081): DOWN"
fi

# PA Service
PA_RESP=$(curl -s --connect-timeout 3 -o /dev/null -w "%{http_code}" http://127.0.0.1:8082/api/pa/statistics 2>/dev/null)
if [ "$PA_RESP" = "200" ]; then
    echo "  PA Service     (8082): UP"
else
    echo "  PA Service     (8082): DOWN (HTTP $PA_RESP)"
fi

# PKD Relay
RELAY_RESP=$(curl -s --connect-timeout 3 -o /dev/null -w "%{http_code}" http://127.0.0.1:8083/api/sync/status 2>/dev/null)
if [ "$RELAY_RESP" = "200" ]; then
    echo "  PKD Relay      (8083): UP"
else
    echo "  PKD Relay      (8083): DOWN (HTTP $RELAY_RESP)"
fi

# AI Analysis
AI_RESP=$(curl -s --connect-timeout 3 http://127.0.0.1:8085/api/ai/health 2>/dev/null)
if [ $? -eq 0 ] && [ -n "$AI_RESP" ]; then
    AI_DB=$(echo "$AI_RESP" | grep -o '"db_type":"[^"]*"' | cut -d'"' -f4)
    echo "  AI Analysis    (8085): UP (db: $AI_DB)"
else
    echo "  AI Analysis    (8085): DOWN"
fi

# API Gateway
GW_RESP=$(curl -s --connect-timeout 3 -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/api/health 2>/dev/null)
if [ "$GW_RESP" = "200" ]; then
    echo "  API Gateway    (8080): UP"
else
    echo "  API Gateway    (8080): DOWN (HTTP $GW_RESP)"
fi

# Frontend
FE_RESP=$(curl -s --connect-timeout 3 -o /dev/null -w "%{http_code}" http://127.0.0.1:80/ 2>/dev/null)
if [ "$FE_RESP" = "200" ]; then
    echo "  Frontend         (80): UP"
else
    echo "  Frontend         (80): DOWN (HTTP $FE_RESP)"
fi

# Swagger UI
SW_RESP=$(curl -s --connect-timeout 3 -o /dev/null -w "%{http_code}" http://127.0.0.1:8888/api-docs/ 2>/dev/null)
if [ "$SW_RESP" = "200" ] || [ "$SW_RESP" = "301" ]; then
    echo "  Swagger UI     (8888): UP"
else
    echo "  Swagger UI     (8888): DOWN (HTTP $SW_RESP)"
fi
echo ""

# 4. Disk usage
echo "[Disk Usage]"
if [ -d "./.docker-data" ]; then
    POSTGRES_SIZE=$(du -sh ./.docker-data/postgres 2>/dev/null | cut -f1)
    UPLOADS_SIZE=$(du -sh ./.docker-data/pkd-uploads 2>/dev/null | cut -f1)
    LOGS_SIZE=$(du -sh ./.docker-data/pkd-logs ./.docker-data/pa-logs ./.docker-data/sync-logs 2>/dev/null | awk '{s+=$1}END{print s"K"}' 2>/dev/null || echo "0K")
    echo "  PostgreSQL data: $POSTGRES_SIZE"
    echo "  Upload files:    $UPLOADS_SIZE"
fi

DOCKER_SIZE=$(docker system df --format '{{.Type}}\t{{.Size}}' 2>/dev/null)
if [ -n "$DOCKER_SIZE" ]; then
    echo "  Docker images:   $(echo "$DOCKER_SIZE" | grep Images | cut -f2)"
    echo "  Docker containers: $(echo "$DOCKER_SIZE" | grep Containers | cut -f2)"
fi
echo ""

echo "=== Health Check Complete ==="
