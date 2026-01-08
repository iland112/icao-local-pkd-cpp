#!/bin/bash
# Deploy ARM64 images from GitHub Actions artifacts to Luckfox

set -e

LUCKFOX_HOST="luckfox@192.168.100.11"
LUCKFOX_DIR="~/icao-local-pkd-cpp-v2"
ARTIFACTS_DIR="./github-artifacts"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Luckfox ARM64 Deployment Script ===${NC}"
echo ""

# Service selection
SERVICE=${1:-all}
if [[ "$SERVICE" != "all" && "$SERVICE" != "pkd-management" && "$SERVICE" != "pa-service" && "$SERVICE" != "sync-service" && "$SERVICE" != "frontend" ]]; then
    echo -e "${RED}Usage: $0 [all|pkd-management|pa-service|sync-service|frontend]${NC}"
    exit 1
fi

echo -e "${YELLOW}Service to deploy: $SERVICE${NC}"
echo ""

# Check if artifacts directory exists
if [ ! -d "$ARTIFACTS_DIR" ]; then
    echo -e "${YELLOW}Artifacts directory not found. Please download from GitHub Actions first.${NC}"
    echo "Go to: https://github.com/iland112/icao-local-pkd-cpp/actions"
    echo "Download the latest 'arm64-docker-images-all' artifact and extract to: $ARTIFACTS_DIR"
    exit 1
fi

# Function to deploy a single service
deploy_service() {
    local service=$1
    local image_name=$2
    local artifact_name=$3

    echo -e "${GREEN}--- Deploying $service ---${NC}"

    # Find artifact file
    ARTIFACT_FILE=$(find "$ARTIFACTS_DIR" -name "$artifact_name" -type f | head -1)

    if [ -z "$ARTIFACT_FILE" ]; then
        echo -e "${RED}Error: Artifact $artifact_name not found in $ARTIFACTS_DIR${NC}"
        return 1
    fi

    echo "Found artifact: $ARTIFACT_FILE"

    # Step 1: Clean old artifacts on Luckfox
    echo -e "${YELLOW}Cleaning old $service on Luckfox...${NC}"
    ssh $LUCKFOX_HOST "
        cd $LUCKFOX_DIR
        docker compose -f docker-compose-luckfox.yaml down $service 2>/dev/null || true
        docker rmi $image_name 2>/dev/null || true
        rm -f /tmp/$artifact_name
    " || echo -e "${YELLOW}Warning: Some cleanup commands failed (may be expected)${NC}"

    # Step 2: Transfer artifact
    echo -e "${YELLOW}Transferring $artifact_name to Luckfox...${NC}"
    scp "$ARTIFACT_FILE" "$LUCKFOX_HOST:/tmp/"

    # Step 3: Load image on Luckfox
    echo -e "${YELLOW}Loading Docker image on Luckfox...${NC}"
    ssh $LUCKFOX_HOST "
        cd /tmp
        gunzip -c $artifact_name | docker load
        rm -f $artifact_name
    "

    # Step 4: Start service
    echo -e "${YELLOW}Starting $service on Luckfox...${NC}"
    ssh $LUCKFOX_HOST "
        cd $LUCKFOX_DIR
        docker compose -f docker-compose-luckfox.yaml up -d $service
    "

    echo -e "${GREEN}✓ $service deployed successfully${NC}"
    echo ""
}

# Deploy based on selection
if [ "$SERVICE" == "all" ] || [ "$SERVICE" == "pkd-management" ]; then
    deploy_service "pkd-management" "icao-local-pkd-management:arm64" "pkd-management-arm64.tar.gz"
fi

if [ "$SERVICE" == "all" ] || [ "$SERVICE" == "pa-service" ]; then
    deploy_service "pa-service" "icao-local-pkd-pa:arm64" "pkd-pa-arm64.tar.gz"
fi

if [ "$SERVICE" == "all" ] || [ "$SERVICE" == "sync-service" ]; then
    deploy_service "sync-service" "icao-local-pkd-sync:arm64" "pkd-sync-arm64.tar.gz"
fi

if [ "$SERVICE" == "all" ] || [ "$SERVICE" == "frontend" ]; then
    deploy_service "frontend" "icao-local-pkd-frontend:arm64" "pkd-frontend-arm64.tar.gz"
fi

echo -e "${GREEN}=== Deployment Complete ===${NC}"
echo ""
echo "Check service status on Luckfox:"
echo "  ssh $LUCKFOX_HOST 'cd $LUCKFOX_DIR && docker compose -f docker-compose-luckfox.yaml ps'"
echo ""
echo "View logs:"
echo "  ssh $LUCKFOX_HOST 'cd $LUCKFOX_DIR && docker compose -f docker-compose-luckfox.yaml logs -f $SERVICE'"
