#!/bin/bash
# Deploy ARM64 images from GitHub Actions artifacts to Luckfox
# Handles OCI format conversion and uses sshpass for authentication

set -e

LUCKFOX_HOST="${LUCKFOX_HOST:-192.168.100.10}"
LUCKFOX_USER="luckfox"
LUCKFOX_PASS="luckfox"
LUCKFOX_DIR="/home/luckfox/icao-local-pkd-cpp-v2"
ARTIFACTS_DIR="./github-artifacts"
TEMP_DIR="/tmp/icao-deploy-$$"

# SSH and SCP commands with sshpass
SSH_CMD="sshpass -p $LUCKFOX_PASS ssh -o StrictHostKeyChecking=no $LUCKFOX_USER@$LUCKFOX_HOST"
SCP_CMD="sshpass -p $LUCKFOX_PASS scp -o StrictHostKeyChecking=no"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Luckfox ARM64 Deployment Script (OCI-aware) ===${NC}"
echo ""

# Check dependencies
echo -e "${BLUE}Checking dependencies...${NC}"
command -v sshpass >/dev/null 2>&1 || { echo -e "${RED}Error: sshpass is required but not installed.${NC}"; exit 1; }
command -v skopeo >/dev/null 2>&1 || { echo -e "${RED}Error: skopeo is required but not installed.${NC}"; exit 1; }
echo -e "${GREEN}✓ Dependencies OK${NC}"
echo ""

# Service selection
SERVICE=${1:-all}
if [[ "$SERVICE" != "all" && "$SERVICE" != "pkd-management" && "$SERVICE" != "pa-service" && "$SERVICE" != "pkd-relay" && "$SERVICE" != "monitoring-service" && "$SERVICE" != "frontend" ]]; then
    echo -e "${RED}Usage: $0 [all|pkd-management|pa-service|pkd-relay|monitoring-service|frontend]${NC}"
    exit 1
fi

echo -e "${YELLOW}Service to deploy: $SERVICE${NC}"
echo ""

# Backup current deployment before starting
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Backing up current Luckfox deployment${NC}"
echo -e "${BLUE}========================================${NC}"
BACKUP_TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BACKUP_DIR="icao-backup-${BACKUP_TIMESTAMP}"
echo -e "${YELLOW}Creating backup: ${BACKUP_DIR}${NC}"

$SSH_CMD "
    cd /home/luckfox
    if [ -d 'icao-local-pkd-cpp-v2' ]; then
        # Create backup directory
        mkdir -p ${BACKUP_DIR}

        # Backup docker-compose file and configs
        cp -r icao-local-pkd-cpp-v2/docker-compose-luckfox.yaml ${BACKUP_DIR}/ 2>/dev/null || true
        cp -r icao-local-pkd-cpp-v2/nginx ${BACKUP_DIR}/ 2>/dev/null || true
        cp -r icao-local-pkd-cpp-v2/docs/openapi ${BACKUP_DIR}/ 2>/dev/null || true

        # Backup container logs
        mkdir -p ${BACKUP_DIR}/logs
        docker logs icao-pkd-management > ${BACKUP_DIR}/logs/pkd-management.log 2>&1 || true
        docker logs icao-pkd-pa-service > ${BACKUP_DIR}/logs/pa-service.log 2>&1 || true
        docker logs icao-pkd-relay > ${BACKUP_DIR}/logs/sync-service.log 2>&1 || true
        docker logs icao-pkd-frontend > ${BACKUP_DIR}/logs/frontend.log 2>&1 || true

        # Save current image versions
        docker images --format '{{.Repository}}:{{.Tag}}\t{{.ID}}\t{{.CreatedAt}}' | grep icao-local > ${BACKUP_DIR}/images.txt || true

        echo '${BACKUP_TIMESTAMP}' > ${BACKUP_DIR}/backup_timestamp.txt
        echo 'Backup created successfully'
    else
        echo 'No existing deployment found - skipping backup'
    fi
"
echo -e "${GREEN}✓ Backup complete: /home/luckfox/${BACKUP_DIR}${NC}"
echo ""

# Check if artifacts directory exists
if [ ! -d "$ARTIFACTS_DIR" ]; then
    echo -e "${YELLOW}Artifacts directory not found. Attempting to download from GitHub Actions...${NC}"

    # Check if gh CLI is available and authenticated
    if command -v gh >/dev/null 2>&1; then
        echo -e "${BLUE}Downloading latest artifacts...${NC}"
        LATEST_RUN=$(gh run list --repo iland112/icao-local-pkd-cpp --branch main --limit 1 --json databaseId --jq '.[0].databaseId')

        if [ -n "$LATEST_RUN" ]; then
            echo "Found workflow run: $LATEST_RUN"
            rm -rf "$ARTIFACTS_DIR"
            gh run download "$LATEST_RUN" --repo iland112/icao-local-pkd-cpp --dir "$ARTIFACTS_DIR"
            echo -e "${GREEN}✓ Artifacts downloaded${NC}"
        else
            echo -e "${RED}Error: No workflow runs found${NC}"
            exit 1
        fi
    else
        echo -e "${RED}Error: gh CLI not found. Please install it or manually download artifacts.${NC}"
        echo "Go to: https://github.com/iland112/icao-local-pkd-cpp/actions"
        echo "Download the latest 'arm64-docker-images-all' artifact and extract to: $ARTIFACTS_DIR"
        exit 1
    fi
fi

# Create temporary directory
mkdir -p "$TEMP_DIR"
trap "rm -rf $TEMP_DIR" EXIT

# Function to convert OCI to Docker archive
convert_oci_to_docker() {
    local oci_archive=$1
    local image_name=$2
    local output_tar=$3

    echo -e "${BLUE}Converting OCI format to Docker archive...${NC}"

    local oci_dir="$TEMP_DIR/oci-$(basename $oci_archive .tar.gz)"
    mkdir -p "$oci_dir"

    # Extract OCI archive
    tar -xzf "$oci_archive" -C "$oci_dir"

    # Convert to Docker archive using skopeo
    skopeo copy --override-arch arm64 \
        "oci:$oci_dir" \
        "docker-archive:$output_tar:$image_name" \
        2>&1 | grep -v "^Copying" || true

    echo -e "${GREEN}✓ Converted to Docker format${NC}"
}

# Function to deploy a single service
deploy_service() {
    local service=$1
    local image_name=$2
    local artifact_name=$3

    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Deploying: $service${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""

    # Find artifact file
    ARTIFACT_FILE=$(find "$ARTIFACTS_DIR" -name "$artifact_name" -type f | head -1)

    if [ -z "$ARTIFACT_FILE" ]; then
        echo -e "${RED}Error: Artifact $artifact_name not found in $ARTIFACTS_DIR${NC}"
        return 1
    fi

    echo -e "${BLUE}Found artifact: $ARTIFACT_FILE${NC}"

    # Step 1: Convert OCI to Docker archive
    echo -e "${YELLOW}[1/5] Converting OCI format to Docker archive...${NC}"
    DOCKER_ARCHIVE="$TEMP_DIR/$(basename $artifact_name .tar.gz)-docker.tar"
    convert_oci_to_docker "$ARTIFACT_FILE" "$image_name" "$DOCKER_ARCHIVE"

    # Step 2: Clean old artifacts on Luckfox
    echo -e "${YELLOW}[2/5] Cleaning old $service on Luckfox...${NC}"
    $SSH_CMD "
        cd $LUCKFOX_DIR
        docker compose -f docker-compose-luckfox.yaml stop $service 2>/dev/null || true
        docker rm icao-$service 2>/dev/null || true
        docker rmi $image_name 2>/dev/null || true
        rm -rf /home/luckfox/${service}-* 2>/dev/null || true
    " || echo -e "${YELLOW}Warning: Some cleanup commands failed (may be expected)${NC}"
    echo -e "${GREEN}✓ Cleanup complete${NC}"

    # Step 3: Transfer Docker archive
    echo -e "${YELLOW}[3/5] Transferring Docker archive to Luckfox...${NC}"
    $SCP_CMD "$DOCKER_ARCHIVE" "$LUCKFOX_USER@$LUCKFOX_HOST:/home/luckfox/$(basename $DOCKER_ARCHIVE)"
    echo -e "${GREEN}✓ Transfer complete ($(du -h $DOCKER_ARCHIVE | cut -f1))${NC}"

    # Step 4: Load image on Luckfox
    echo -e "${YELLOW}[4/5] Loading Docker image on Luckfox...${NC}"
    $SSH_CMD "
        docker load < /home/luckfox/$(basename $DOCKER_ARCHIVE)
        rm -f /home/luckfox/$(basename $DOCKER_ARCHIVE)
    "
    echo -e "${GREEN}✓ Image loaded${NC}"

    # Step 5: Start service
    echo -e "${YELLOW}[5/5] Starting $service on Luckfox...${NC}"
    $SSH_CMD "
        cd $LUCKFOX_DIR
        docker compose -f docker-compose-luckfox.yaml up -d $service
    "
    echo -e "${GREEN}✓ Service started${NC}"

    echo ""
    echo -e "${GREEN}✅ $service deployed successfully!${NC}"
    echo ""

    # Wait for health check
    echo -e "${BLUE}Waiting for service health check...${NC}"
    sleep 5
    SERVICE_STATUS=$($SSH_CMD "docker ps --filter name=$service --format '{{.Status}}'")
    echo -e "${GREEN}Status: $SERVICE_STATUS${NC}"
    echo ""
}

# Deploy based on selection
# Image names MUST match docker-compose-luckfox.yaml
if [ "$SERVICE" == "all" ] || [ "$SERVICE" == "pkd-management" ]; then
    deploy_service "pkd-management" "icao-local-management:arm64" "pkd-management-arm64.tar.gz"
fi

if [ "$SERVICE" == "all" ] || [ "$SERVICE" == "pa-service" ]; then
    deploy_service "pa-service" "icao-local-pa:arm64" "pkd-pa-arm64.tar.gz"
fi

if [ "$SERVICE" == "all" ] || [ "$SERVICE" == "pkd-relay" ]; then
    deploy_service "pkd-relay" "icao-local-pkd-relay:arm64" "pkd-relay-arm64.tar.gz"
fi

if [ "$SERVICE" == "all" ] || [ "$SERVICE" == "monitoring-service" ]; then
    deploy_service "monitoring-service" "icao-local-monitoring:arm64" "monitoring-service-arm64.tar.gz"
fi

if [ "$SERVICE" == "all" ] || [ "$SERVICE" == "frontend" ]; then
    deploy_service "frontend" "icao-local-pkd-frontend:arm64" "pkd-frontend-arm64.tar.gz"
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}🎉 Deployment Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${BLUE}Useful commands:${NC}"
echo ""
echo -e "${YELLOW}Check service status:${NC}"
echo "  $SSH_CMD 'cd $LUCKFOX_DIR && docker compose -f docker-compose-luckfox.yaml ps'"
echo ""
echo -e "${YELLOW}View logs:${NC}"
echo "  $SSH_CMD 'cd $LUCKFOX_DIR && docker compose -f docker-compose-luckfox.yaml logs -f $SERVICE'"
echo ""
echo -e "${YELLOW}Access services:${NC}"
echo "  Frontend:     http://$LUCKFOX_HOST/"
echo "  API Gateway:  http://$LUCKFOX_HOST:8080/api"
echo ""
