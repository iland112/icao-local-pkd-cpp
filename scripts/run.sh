#!/bin/bash
# =============================================================================
# ICAO Local PKD - Run Script
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BINARY="$PROJECT_DIR/build/bin/icao-local-pkd"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo -e "${GREEN}=== ICAO Local PKD Run Script ===${NC}"
echo ""

# Check if binary exists
if [ ! -f "$BINARY" ]; then
    echo -e "${RED}Error: Binary not found at $BINARY${NC}"
    echo "Please run ./scripts/build.sh first"
    exit 1
fi

# Create required directories
mkdir -p "$PROJECT_DIR/logs"
mkdir -p "$PROJECT_DIR/uploads"

# Run the application
echo "Starting ICAO Local PKD..."
echo ""
cd "$PROJECT_DIR"
exec "$BINARY" "$@"
