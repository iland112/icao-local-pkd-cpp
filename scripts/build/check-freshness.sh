#!/bin/bash
# Check if the GitHub Actions build includes latest commits
# Run this before deploying to catch cache-related issues

set -e

LUCKFOX_HOST="192.168.100.11"
LUCKFOX_USER="luckfox"
LUCKFOX_PASS="luckfox"
SSH_CMD="sshpass -p $LUCKFOX_PASS ssh -o StrictHostKeyChecking=no $LUCKFOX_USER@$LUCKFOX_HOST"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${GREEN}╔════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║   Build Freshness Validation Check        ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════╝${NC}"
echo ""

# Check dependencies
command -v gh >/dev/null 2>&1 || { echo -e "${RED}Error: gh CLI is required${NC}"; exit 1; }
command -v jq >/dev/null 2>&1 || { echo -e "${RED}Error: jq is required${NC}"; exit 1; }
command -v sshpass >/dev/null 2>&1 || { echo -e "${RED}Error: sshpass is required${NC}"; exit 1; }

# Get latest local commit
LOCAL_COMMIT=$(git rev-parse --short HEAD)
LOCAL_MSG=$(git log -1 --pretty=%B | head -1)
LOCAL_AUTHOR=$(git log -1 --pretty=%an)
LOCAL_TIME=$(git log -1 --pretty=%ci)

echo -e "${BLUE}📝 Local Repository:${NC}"
echo "   Commit:  $LOCAL_COMMIT"
echo "   Message: $LOCAL_MSG"
echo "   Author:  $LOCAL_AUTHOR"
echo "   Time:    $LOCAL_TIME"
echo ""

# Check if there are uncommitted changes
if ! git diff-index --quiet HEAD --; then
    echo -e "${YELLOW}⚠️  WARNING: You have uncommitted changes!${NC}"
    echo -e "${YELLOW}   Commit and push them before deploying.${NC}"
    echo ""
    git status --short
    echo ""
    exit 1
fi

# Check if local is ahead of remote
LOCAL_AHEAD=$(git rev-list --count origin/feature/openapi-support..HEAD 2>/dev/null || echo "0")
if [ "$LOCAL_AHEAD" -gt 0 ]; then
    echo -e "${YELLOW}⚠️  WARNING: Local branch is $LOCAL_AHEAD commit(s) ahead of remote!${NC}"
    echo -e "${YELLOW}   Push your changes: git push origin feature/openapi-support${NC}"
    echo ""
    exit 1
fi

# Get latest GitHub Actions build
echo -e "${BLUE}🔨 GitHub Actions Build:${NC}"
LATEST_RUN=$(gh run list --repo iland112/icao-local-pkd-cpp --branch feature/openapi-support --limit 1 --json databaseId,headSha,displayTitle,conclusion,createdAt,updatedAt,status --jq '.[0]')

if [ -z "$LATEST_RUN" ] || [ "$LATEST_RUN" = "null" ]; then
    echo -e "${RED}Error: No GitHub Actions runs found${NC}"
    exit 1
fi

RUN_ID=$(echo "$LATEST_RUN" | jq -r '.databaseId')
RUN_COMMIT=$(echo "$LATEST_RUN" | jq -r '.headSha' | cut -c1-7)
RUN_TITLE=$(echo "$LATEST_RUN" | jq -r '.displayTitle')
RUN_STATUS=$(echo "$LATEST_RUN" | jq -r '.conclusion')
RUN_STATE=$(echo "$LATEST_RUN" | jq -r '.status')
RUN_TIME=$(echo "$LATEST_RUN" | jq -r '.createdAt')
RUN_UPDATED=$(echo "$LATEST_RUN" | jq -r '.updatedAt')

echo "   Run ID:  $RUN_ID"
echo "   Commit:  $RUN_COMMIT"
echo "   Title:   $RUN_TITLE"
echo "   Status:  $RUN_STATUS ($RUN_STATE)"
echo "   Started: $RUN_TIME"
echo "   Updated: $RUN_UPDATED"
echo ""

# Check if build is still running
if [ "$RUN_STATE" = "in_progress" ] || [ "$RUN_STATE" = "queued" ]; then
    echo -e "${YELLOW}⏳ Build is still running...${NC}"
    echo -e "${YELLOW}   Wait for build to complete before deploying.${NC}"
    echo ""
    echo "   Monitor: https://github.com/iland112/icao-local-pkd-cpp/actions/runs/$RUN_ID"
    echo ""
    exit 1
fi

# Check if commits match
echo -e "${BLUE}🔍 Commit Comparison:${NC}"
if [ "$LOCAL_COMMIT" != "$RUN_COMMIT" ]; then
    echo -e "${RED}   ✗ MISMATCH: Local ($LOCAL_COMMIT) ≠ Build ($RUN_COMMIT)${NC}"
    echo -e "${RED}     Push your changes and wait for build to complete!${NC}"
    echo ""
    exit 1
else
    echo -e "${GREEN}   ✓ Match: Both at $LOCAL_COMMIT${NC}"
fi
echo ""

# Check build status
echo -e "${BLUE}✅ Build Status:${NC}"
if [ "$RUN_STATUS" != "success" ]; then
    echo -e "${RED}   ✗ FAILED: Build did not succeed (status: $RUN_STATUS)${NC}"
    echo -e "${RED}     Fix build errors before deploying!${NC}"
    echo ""
    echo "   View logs: gh run view $RUN_ID --repo iland112/icao-local-pkd-cpp --log"
    echo ""
    exit 1
else
    echo -e "${GREEN}   ✓ Success: Build completed successfully${NC}"
fi
echo ""

# Check build cache usage
echo -e "${BLUE}💾 Build Cache Analysis:${NC}"
echo "   Analyzing build logs for cache usage..."

# Get pkd-management build log
CACHE_COUNT=$(gh run view "$RUN_ID" --repo iland112/icao-local-pkd-cpp --log 2>/dev/null | grep "pkd-management" | grep -c "CACHED" || echo "0")
TOTAL_LAYERS=$(gh run view "$RUN_ID" --repo iland112/icao-local-pkd-cpp --log 2>/dev/null | grep "pkd-management" | grep -cE "^#[0-9]+" || echo "0")

echo "   CACHED layers: $CACHE_COUNT"
echo "   Total layers:  $TOTAL_LAYERS"

if [ "$TOTAL_LAYERS" -gt 0 ]; then
    CACHE_PERCENT=$((CACHE_COUNT * 100 / TOTAL_LAYERS))
    echo "   Cache ratio:   $CACHE_PERCENT%"
fi
echo ""

# Warning if too many cached layers
if [ "$CACHE_COUNT" -gt 15 ]; then
    echo -e "${YELLOW}   ⚠️  HIGH CACHE USAGE DETECTED!${NC}"
    echo -e "${YELLOW}      Build used $CACHE_COUNT cached layers.${NC}"
    echo -e "${YELLOW}      This might indicate your code changes were not compiled!${NC}"
    echo ""
    echo -e "${YELLOW}   Recommendation:${NC}"
    echo -e "${YELLOW}   1. Check if your feature uses version bump (v1.X.Y)${NC}"
    echo -e "${YELLOW}   2. Verify build logs show actual compilation${NC}"
    echo -e "${YELLOW}   3. Test thoroughly after deployment${NC}"
    echo ""
    echo "   View build logs: gh run view $RUN_ID --repo iland112/icao-local-pkd-cpp --log"
    echo ""
fi

# Check deployed version on Luckfox (if accessible)
echo -e "${BLUE}🚀 Luckfox Deployment Status:${NC}"
DEPLOYED_VERSION=$($SSH_CMD "docker logs icao-pkd-management 2>&1 | grep 'Starting ICAO Local PKD Application' | tail -1" 2>/dev/null || echo "")

if [ -n "$DEPLOYED_VERSION" ]; then
    echo "   Current: $DEPLOYED_VERSION"

    # Extract version number
    CURRENT_VER=$(echo "$DEPLOYED_VERSION" | grep -oP 'v\d+\.\d+\.\d+' || echo "unknown")
    echo "   Version: $CURRENT_VER"
    echo ""

    # Compare with commit message
    if echo "$LOCAL_MSG" | grep -q "v[0-9]"; then
        EXPECTED_VER=$(echo "$LOCAL_MSG" | grep -oP 'v\d+\.\d+\.\d+' | head -1 || echo "unknown")
        if [ "$CURRENT_VER" != "$EXPECTED_VER" ] && [ "$EXPECTED_VER" != "unknown" ]; then
            echo -e "${YELLOW}   ⚠️  Version mismatch:${NC}"
            echo -e "${YELLOW}      Expected: $EXPECTED_VER (from commit)${NC}"
            echo -e "${YELLOW}      Current:  $CURRENT_VER (on Luckfox)${NC}"
            echo -e "${YELLOW}      You need to deploy the new build!${NC}"
            echo ""
        fi
    fi
else
    echo "   (Could not retrieve deployed version - Luckfox might be offline)"
    echo ""
fi

# Final summary
echo -e "${GREEN}╔════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║           Validation Complete ✓            ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${BLUE}📋 Summary:${NC}"
echo "   ✓ Local and remote commits match"
echo "   ✓ Build completed successfully"
if [ "$CACHE_COUNT" -gt 15 ]; then
    echo "   ⚠ High cache usage detected - verify after deployment"
else
    echo "   ✓ Cache usage within normal range"
fi
echo ""

echo -e "${GREEN}✅ Safe to deploy!${NC}"
echo ""
echo -e "${BLUE}Next steps:${NC}"
echo "   1. Deploy: ${YELLOW}./scripts/deploy-from-github-artifacts.sh pkd-management${NC}"
echo "   2. Verify: ${YELLOW}ssh luckfox@192.168.100.11 'docker logs icao-pkd-management --tail 10'${NC}"
echo "   3. Test:   ${YELLOW}curl http://192.168.100.11:8080/api/health${NC}"
echo ""

# Save validation result
mkdir -p .build-checks
echo "{
  \"timestamp\": \"$(date -Iseconds)\",
  \"local_commit\": \"$LOCAL_COMMIT\",
  \"build_commit\": \"$RUN_COMMIT\",
  \"build_id\": \"$RUN_ID\",
  \"build_status\": \"$RUN_STATUS\",
  \"cache_count\": $CACHE_COUNT,
  \"validation\": \"passed\"
}" > .build-checks/last-validation.json

exit 0
