# Docker Build Cache Management Guide

**Version**: 1.0
**Last Updated**: 2026-01-09
**Status**: Critical - Read Before Every Deployment

---

## ⚠️ CRITICAL ISSUE: Build Cache Can Hide Code Changes

### The Problem

**GitHub Actions 빌드 캐시가 소스 코드 변경을 무시할 수 있습니다!**

**실제 발생한 문제 (2026-01-09)**:
1. 중복 검사 기능 코드를 main.cpp에 추가
2. 커밋 및 푸시 완료
3. GitHub Actions 빌드 성공 (5분 48초)
4. **하지만 모든 레이어가 CACHED로 처리됨**
5. 결과: 새 코드가 컴파일되지 않고 이전 바이너리 사용
6. 배포 후 기능 작동하지 않음

### Why This Happens

현재 Dockerfile은 4단계 멀티스테이지 빌드를 사용:

```dockerfile
# Stage 1: vcpkg-base (시스템 의존성)
FROM debian:bookworm AS vcpkg-base
# ... install packages, clone vcpkg

# Stage 2: vcpkg-deps (패키지 의존성)
FROM vcpkg-base AS vcpkg-deps
COPY vcpkg.json ./
# ... vcpkg install

# Stage 3: builder (애플리케이션 빌드)
FROM vcpkg-deps AS builder
COPY CMakeLists.txt ./
COPY src/ ./src/
# ... cmake build

# Stage 4: runtime
FROM debian:bookworm-slim AS runtime
COPY --from=builder /app/build/bin/pkd-management /app/
```

**캐시 전략**:
- GitHub Actions는 `gha` 캐시를 사용
- 각 스테이지마다 별도의 캐시 scope 사용
- 파일 체크섬이 동일하면 캐시 사용

**문제점**:
- `COPY src/ ./src/` 이후 체크섬이 변경되어야 재빌드
- 하지만 **캐시 키가 너무 광범위**하게 설정됨
- BuildKit이 전체 디렉토리 컨텍스트를 캐시 키로 사용할 수 있음
- 결과: src/ 내부 파일이 변경되어도 캐시 히트 가능

---

## Detection: How to Check if Cache is the Problem

### 1. GitHub Actions 로그 확인

```bash
# 빌드 로그에서 CACHED 확인
gh run view <RUN_ID> --repo iland112/icao-local-pkd-cpp --log | grep "CACHED"

# 많은 CACHED가 보이면 의심
#12 CACHED
#13 CACHED
#14 CACHED
...
#25 CACHED
```

### 2. 배포 후 버전 확인

```bash
# Luckfox에서 로그 확인
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "docker logs icao-pkd-management --tail 5"

# 출력 예:
# [2026-01-09 17:52:58.332] [info] Starting ICAO Local PKD Application (v1.2.0 - Duplicate Detection)...

# 버전이 최신인지 확인!
```

### 3. 기능 테스트

```bash
# 새로 추가한 기능이 작동하는지 직접 테스트
# 작동하지 않으면 캐시 문제 의심
```

---

## Solutions: How to Force Cache Invalidation

### Solution 1: Version Bump (Recommended) ⭐

**가장 안전하고 확실한 방법**

```bash
# main.cpp에서 버전 문자열 변경
# Before:
spdlog::info("Starting ICAO Local PKD Application (v1.1.0 - Manual Mode Support)...");

# After:
spdlog::info("Starting ICAO Local PKD Application (v1.2.0 - Duplicate Detection)...");

# 커밋 및 푸시
git add services/pkd-management/src/main.cpp
git commit -m "fix: Force rebuild to include new features (v1.2.0)"
git push origin feature/openapi-support
```

**왜 효과적인가?**:
- main.cpp 파일 체크섬이 변경됨
- Docker COPY src/ 단계의 캐시 무효화
- builder 스테이지 전체 재빌드
- 확실하게 새 코드 컴파일

### Solution 2: Dummy File Update

```bash
# 더미 파일 생성 또는 수정
echo "# Build $(date +%s)" > services/pkd-management/BUILD_ID

# Dockerfile에 추가 (builder 스테이지 초반)
COPY BUILD_ID ./

# 커밋 및 푸시
git add services/pkd-management/BUILD_ID services/pkd-management/Dockerfile
git commit -m "fix: Force rebuild with BUILD_ID update"
git push
```

### Solution 3: GitHub Actions Cache 수동 삭제

```bash
# GitHub UI에서:
# Settings → Actions → Caches → Delete specific cache
# 또는 전체 삭제

# 주의: 다음 빌드가 cold build (60-80분)
```

### Solution 4: Workflow Dispatch with no-cache

**.github/workflows/build-arm64.yml 수정**:

```yaml
on:
  workflow_dispatch:
    inputs:
      no-cache:
        description: 'Force rebuild without cache'
        required: false
        type: boolean
        default: false

jobs:
  build-pkd-management:
    steps:
      - name: Build and export PKD Management Service
        uses: docker/build-push-action@v5
        with:
          no-cache: ${{ inputs.no-cache }}
          # ... other options
```

**사용법**:
- GitHub → Actions → "Build ARM64 Docker Images" → Run workflow
- "Force rebuild without cache" 체크
- Run workflow 클릭

---

## Prevention: Automated Cache Validation

### Pre-Deployment Check Script

**파일**: `scripts/check-build-freshness.sh`

```bash
#!/bin/bash
# Check if the deployed build includes latest commits

set -e

LUCKFOX_HOST="192.168.100.11"
LUCKFOX_USER="luckfox"
LUCKFOX_PASS="luckfox"
SSH_CMD="sshpass -p $LUCKFOX_PASS ssh -o StrictHostKeyChecking=no $LUCKFOX_USER@$LUCKFOX_HOST"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== Build Freshness Check ===${NC}"
echo ""

# Get latest local commit
LOCAL_COMMIT=$(git rev-parse --short HEAD)
LOCAL_MSG=$(git log -1 --pretty=%B | head -1)

echo -e "${YELLOW}Local commit:${NC}"
echo "  $LOCAL_COMMIT - $LOCAL_MSG"
echo ""

# Get latest GitHub Actions build commit
LATEST_RUN=$(gh run list --repo iland112/icao-local-pkd-cpp --branch feature/openapi-support --limit 1 --json databaseId,headSha,displayTitle,conclusion,createdAt --jq '.[0]')
RUN_ID=$(echo "$LATEST_RUN" | jq -r '.databaseId')
RUN_COMMIT=$(echo "$LATEST_RUN" | jq -r '.headSha' | cut -c1-7)
RUN_TITLE=$(echo "$LATEST_RUN" | jq -r '.displayTitle')
RUN_STATUS=$(echo "$LATEST_RUN" | jq -r '.conclusion')
RUN_TIME=$(echo "$LATEST_RUN" | jq -r '.createdAt')

echo -e "${YELLOW}Latest GitHub Actions build:${NC}"
echo "  Run ID: $RUN_ID"
echo "  Commit: $RUN_COMMIT"
echo "  Title: $RUN_TITLE"
echo "  Status: $RUN_STATUS"
echo "  Time: $RUN_TIME"
echo ""

# Check if commits match
if [ "$LOCAL_COMMIT" != "$RUN_COMMIT" ]; then
    echo -e "${RED}⚠️  WARNING: Local commit differs from latest build!${NC}"
    echo -e "${RED}   Push your changes and wait for build to complete.${NC}"
    exit 1
fi

if [ "$RUN_STATUS" != "success" ]; then
    echo -e "${RED}⚠️  WARNING: Latest build failed!${NC}"
    echo -e "${RED}   Fix build errors before deploying.${NC}"
    exit 1
fi

# Check if build used cache excessively
echo -e "${YELLOW}Checking build cache usage...${NC}"
CACHE_COUNT=$(gh run view "$RUN_ID" --repo iland112/icao-local-pkd-cpp --log | grep "pkd-management" | grep -c "CACHED" || true)
echo "  CACHED layers: $CACHE_COUNT"

if [ "$CACHE_COUNT" -gt 15 ]; then
    echo -e "${YELLOW}⚠️  WARNING: Build used many cached layers ($CACHE_COUNT)${NC}"
    echo -e "${YELLOW}   This might indicate cache-related issues.${NC}"
    echo -e "${YELLOW}   Verify deployment includes your changes!${NC}"
    echo ""

    # Get deployed version from Luckfox
    echo -e "${YELLOW}Checking deployed version on Luckfox...${NC}"
    DEPLOYED_VERSION=$($SSH_CMD "docker logs icao-pkd-management 2>&1 | grep 'Starting ICAO Local PKD Application' | tail -1" || echo "")

    if [ -n "$DEPLOYED_VERSION" ]; then
        echo "  Luckfox: $DEPLOYED_VERSION"
    else
        echo "  (Could not retrieve version)"
    fi
    echo ""
fi

echo -e "${GREEN}✓ Build freshness check complete${NC}"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. Deploy: ./scripts/deploy-from-github-artifacts.sh pkd-management"
echo "  2. Verify: Test your new features work correctly"
echo ""
```

### Dockerfile Improvement

**현재 문제를 완화하는 Dockerfile 패턴**:

```dockerfile
# Stage 3: builder
FROM vcpkg-deps AS builder

# Build argument for cache busting
ARG BUILD_VERSION=unknown
ENV BUILD_VERSION=${BUILD_VERSION}

WORKDIR /app

# Copy only what's needed in order
COPY shared/ ./shared/
COPY services/pkd-management/CMakeLists.txt ./
COPY services/pkd-management/src/ ./src/

# Force cache invalidation with version check
RUN echo "Building version: ${BUILD_VERSION}"

# Configure
RUN cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-DBUILD_VERSION=\\\"${BUILD_VERSION}\\\""

# Build
RUN cmake --build build -j$(nproc)
```

**사용법**:

```yaml
# .github/workflows/build-arm64.yml
- name: Get version from commit
  id: version
  run: |
    VERSION="v1.$(git rev-list --count HEAD)"
    echo "version=$VERSION" >> $GITHUB_OUTPUT

- name: Build and export PKD Management Service
  uses: docker/build-push-action@v5
  with:
    build-args: |
      BUILD_VERSION=${{ steps.version.outputs.version }}
    # ... other options
```

---

## Best Practices

### 1. Always Version Your Changes

**커밋 메시지에 버전 업데이트 포함**:

```bash
# Good
git commit -m "feat(duplicate-detection): Add file upload duplicate detection (v1.2.0)"

# Bad
git commit -m "add feature"
```

### 2. Verify Build Logs

**빌드 완료 후 항상 로그 확인**:

```bash
# Check if compilation actually happened
gh run view <RUN_ID> --repo iland112/icao-local-pkd-cpp --log | grep -E "(Compiling|Building|cmake --build)"

# Should see actual compilation output, not just CACHED
```

### 3. Test Immediately After Deployment

**배포 직후 새 기능 테스트**:

```bash
# Don't wait! Test immediately
# If something doesn't work → cache problem
```

### 4. Document Version in Commit

**main.cpp에 버전 정보 유지**:

```cpp
// Always update this when making significant changes
spdlog::info("Starting ICAO Local PKD Application (v1.X.Y - Feature Name)...");
```

### 5. Use Check Script Before Deployment

```bash
# Run before every deployment
./scripts/check-build-freshness.sh

# Only deploy if check passes
./scripts/deploy-from-github-artifacts.sh pkd-management
```

---

## Cache Decision Matrix

| Scenario | Use Cache? | Action Required |
|----------|------------|-----------------|
| vcpkg.json changed | ❌ No | Automatic (Dockerfile handles) |
| CMakeLists.txt changed | ❌ No | Automatic (Dockerfile handles) |
| src/*.cpp changed | ⚠️ Maybe | **Version bump recommended** |
| New feature added | ❌ No | **Version bump REQUIRED** |
| Bug fix | ⚠️ Maybe | **Version bump recommended** |
| Comment only change | ✅ Yes | Cache OK |
| Documentation change | ✅ Yes | Cache OK (no rebuild needed) |

---

## Troubleshooting

### Q: How do I know if cache caused a problem?

**A**: Check these symptoms:
1. Code changed but feature doesn't work
2. Logs show old version number
3. Build completed very quickly (<10 min but with code changes)
4. GitHub Actions log shows many "CACHED" entries

### Q: What's the fastest way to fix cache issues?

**A**: Version bump in main.cpp (3 minutes total)
1. Edit version string (30 sec)
2. Commit and push (30 sec)
3. Wait for build (10-15 min)
4. Deploy (3-5 min)

### Q: Should I always disable cache?

**A**: No! Cache is good for:
- Fast iteration during development
- Rebuilding after minor changes
- Saving CI/CD minutes

Just be aware of its behavior and verify deployments.

### Q: Can I prevent this automatically?

**A**: Partially. Use:
1. Pre-deployment check script
2. Version argument in Dockerfile
3. Automated tests that verify new features

---

## Historical Issues

### 2026-01-09: Duplicate Detection Feature Missing

**Problem**:
- Added `checkDuplicateFile()` function to main.cpp
- Committed and pushed successfully
- GitHub Actions build succeeded (5m 48s)
- Deployed to Luckfox
- **Feature did not work** - function never called

**Root Cause**:
- All builder stage layers were CACHED
- New source code was not compiled
- Previous binary was used

**Solution**:
- Changed version from v1.1.0 to v1.2.0
- Pushed version bump
- New build compiled fresh code (22m)
- Deployed successfully
- Feature worked correctly

**Lesson Learned**:
- Always verify build logs for CACHED entries
- Version bump for significant features
- Test immediately after deployment

---

## Scripts Summary

| Script | Purpose | When to Use |
|--------|---------|-------------|
| `check-build-freshness.sh` | Verify build matches local code | Before every deployment |
| `deploy-from-github-artifacts.sh` | Deploy to Luckfox | After build complete |

---

## Quick Reference

**When you change source code**:

```bash
# 1. Update version in main.cpp
spdlog::info("Starting ICAO Local PKD Application (v1.X.Y - Feature Name)...");

# 2. Commit with version in message
git commit -m "feat: Your feature (v1.X.Y)"

# 3. Push
git push origin feature/openapi-support

# 4. Wait for build (10-15 min)

# 5. Check build freshness
./scripts/check-build-freshness.sh

# 6. Deploy
./scripts/deploy-from-github-artifacts.sh pkd-management

# 7. Verify version
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "docker logs icao-pkd-management --tail 5"

# 8. Test new feature immediately
```

---

**Document Owner**: AI Assistant & kbjung
**Last Incident**: 2026-01-09
**Next Review**: After each cache-related issue
