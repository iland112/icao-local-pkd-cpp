# Build Standard Operating Procedure (SOP)

**Version**: 1.0
**Created**: 2026-01-25
**Purpose**: Prevent build verification mistakes and ensure accurate deployments

---

## Critical Lessons Learned

**Problem**: Multiple instances of claiming builds were complete when they actually used cached layers from old code, resulting in:
- Version string updated but binary containing old code
- Repeated upload tests failing
- Wasted time rebuilding after failed deployments

**Root Cause**: Docker build caching can reuse old build layers even when source code changes, especially when only version strings are modified.

---

## Build Procedure

### Step 1: Code Modification

1. Make your source code changes
2. **ALWAYS** update the version string in `services/pkd-management/src/main.cpp`:
   ```cpp
   // Line ~9283
   spdlog::info("====== ICAO Local PKD v{VERSION} {FEATURE-NAME} (Build {YYYYMMDD-HHMMSS}) ======");
   ```
3. Update `BUILD_ID` if using build invalidation:
   ```bash
   echo "20260125-183000" > services/pkd-management/BUILD_ID
   git add services/pkd-management/BUILD_ID
   ```

### Step 2: Build Image with --no-cache

**CRITICAL**: When deploying new functionality, ALWAYS use `--no-cache`:

```bash
# Start build in background
docker compose -f docker/docker-compose.yaml build --no-cache pkd-management

# Or if you need to monitor:
docker compose -f docker/docker-compose.yaml build --no-cache pkd-management 2>&1 | tee build.log
```

**When to use --no-cache**:
- ✅ New feature implementation
- ✅ Bug fixes in core logic
- ✅ After any C++ source file modification
- ✅ When in doubt

**When cache is acceptable**:
- Configuration-only changes (docker-compose.yaml, nginx.conf)
- Documentation updates
- Frontend-only changes

### Step 3: Verify Build BEFORE Deployment

**NEVER assume the build worked**. Use the verification script:

```bash
./scripts/verify-build.sh <service> <expected-version> <expected-function>

# Example:
./scripts/verify-build.sh pkd-management v2.0.7 parseMasterListEntryV2
```

The script checks:
1. ✅ Container is running
2. ✅ Image creation time (< 10 minutes = fresh build)
3. ✅ Version string appears in logs
4. ✅ Expected function exists in binary (using `strings`)
5. ✅ Container health status

**ALL checks must pass** before deployment.

### Step 4: Deploy

Only after verification passes:

```bash
# Recreate container with new image
docker compose -f docker/docker-compose.yaml up -d --force-recreate pkd-management

# Verify version in logs
docker logs icao-local-pkd-management 2>&1 | grep "======"
```

### Step 5: Test Functionality

After deployment:
1. Test the specific feature you modified
2. Don't skip this step
3. If it doesn't work, check the binary again (maybe cache was still used)

---

## Verification Script Usage

### Basic Usage

```bash
# For a new feature with a specific function name
./scripts/verify-build.sh pkd-management v2.0.7 parseMasterListEntryV2

# For a version update without specific function
./scripts/verify-build.sh pkd-management v2.0.7
```

### Expected Output (Success)

```
==================================
Build Verification for pkd-management
==================================

1. Container Status:
   ✅ Container is running

2. Image Build Time:
   Created: 2026-01-25 18:30:00 +0900 KST
   ✅ Image is fresh (2 minutes old)

3. Version String in Logs:
   ✅ Found: ====== ICAO Local PKD v2.0.7 CSCA-EXTRACTION (Build 20260125-183000) ======

4. Binary Function Check:
   ✅ Function 'parseMasterListEntryV2' found in binary

5. Container Health:
   ✅ Container is healthy

==================================
✅ All checks passed!
==================================
```

### Expected Output (Failure - Stale Build)

```
2. Image Build Time:
   Created: 2026-01-25 08:50:00 +0900 KST
   ⚠️  Image is 582 minutes old (expected < 10 minutes)
   Did you forget to rebuild with --no-cache?

4. Binary Function Check:
   ❌ Function 'parseMasterListEntryV2' NOT found in binary
   This indicates the code was not compiled!
```

---

## Troubleshooting

### Issue: "Function not found in binary"

**Symptoms**:
- Version string is correct in logs
- But function doesn't exist in binary
- Feature doesn't work

**Cause**: Docker used cached build layers

**Fix**:
1. Rebuild with `--no-cache`
2. Verify again
3. Don't deploy until verification passes

### Issue: "Image is old (> 10 minutes)"

**Symptoms**:
- Image creation time is before your code changes
- But you just "built" it

**Cause**: Docker used cached final stage

**Fix**:
1. Check if build actually ran or just used cache
2. Rebuild with `--no-cache`
3. Verify image creation time is recent

### Issue: Version in logs doesn't match

**Cause**: Running old container, not the newly built image

**Fix**:
```bash
docker compose -f docker/docker-compose.yaml up -d --force-recreate pkd-management
```

---

## Commit History

Always include build/deployment information in commit messages:

```bash
git commit -m "feat(pkd): Implement CSCA extraction from Master Lists (v2.0.7)

- Add parseMasterListEntryV2 function
- Update version to v2.0.7
- Build verified with verification script
- Deployed and tested on local environment"
```

---

## Remember

1. **Version changes alone don't guarantee new code is compiled**
2. **Docker cache is aggressive** - it will reuse layers when possible
3. **Always verify builds** before claiming they're complete
4. **Test functionality** after deployment, not just version strings
5. **When in doubt, --no-cache**

This SOP exists because of repeated mistakes. Follow it strictly.
