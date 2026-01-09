# Luckfox ARM64 Deployment Guide

**Version**: 2.0
**Last Updated**: 2026-01-09
**Status**: Production Ready

---

## Overview

This document describes the automated deployment process for deploying ARM64 Docker images from GitHub Actions to Luckfox device.

### Key Components

- **GitHub Actions**: Builds ARM64 Docker images (OCI format)
- **Deployment Script**: `scripts/deploy-from-github-artifacts.sh`
- **OCI Format Handling**: Automatic conversion to Docker format using `skopeo`
- **Authentication**: `sshpass` for automated SSH/SCP operations
- **Target Device**: Luckfox Pico (192.168.100.11)

---

## Prerequisites

### Local Machine Requirements

```bash
# 1. sshpass (for automated SSH authentication)
sudo apt-get install sshpass  # Debian/Ubuntu
brew install hudochenkov/sshpass/sshpass  # macOS

# 2. skopeo (for OCI to Docker conversion)
sudo apt-get install skopeo  # Debian/Ubuntu
brew install skopeo  # macOS

# 3. GitHub CLI (for artifact download)
sudo apt-get install gh  # Debian/Ubuntu
brew install gh  # macOS

# Verify installations
command -v sshpass && echo "✓ sshpass OK"
command -v skopeo && echo "✓ skopeo OK"
command -v gh && echo "✓ gh CLI OK"
```

### Luckfox Requirements

- Docker installed and running
- SSH access enabled (user: luckfox, password: luckfox)
- Network connectivity to local machine
- Sufficient disk space (~500MB per service)

---

## Deployment Process

### Step 1: Code Changes and Push

```bash
# 1. Make code changes in services/
git add .
git commit -m "feat: your feature description"
git push origin feature/openapi-support

# 2. Wait for GitHub Actions build to complete
# Monitor at: https://github.com/iland112/icao-local-pkd-cpp/actions
# Expected build time: 10-15 minutes for source changes
```

### Step 2: Automated Deployment

The deployment script handles everything automatically:

```bash
# Deploy specific service
./scripts/deploy-from-github-artifacts.sh pkd-management

# Deploy all services
./scripts/deploy-from-github-artifacts.sh all

# Deploy multiple specific services (run separately)
./scripts/deploy-from-github-artifacts.sh pkd-management
./scripts/deploy-from-github-artifacts.sh pa-service
```

**What the script does:**

1. **Downloads artifacts** from latest GitHub Actions run (if not present)
2. **Converts OCI format** to Docker archive format
3. **Cleans old deployment** on Luckfox (stops container, removes image)
4. **Transfers Docker archive** to Luckfox via SCP
5. **Loads Docker image** on Luckfox
6. **Starts services** using `luckfox-start.sh` script
7. **Verifies health** and displays service status

---

## Technical Details

### OCI Format Problem and Solution

**Problem**: GitHub Actions outputs Docker images in OCI format (Open Container Initiative), which Docker cannot directly load.

```bash
# This fails:
docker load < pkd-management-arm64.tar.gz
# Error: invalid archive: does not contain a manifest.json
```

**Solution**: Convert OCI to Docker archive using `skopeo`:

```bash
# Extract OCI archive
tar -xzf pkd-management-arm64.tar.gz -C /tmp/oci-dir

# Convert to Docker format
skopeo copy --override-arch arm64 \
    oci:/tmp/oci-dir \
    docker-archive:/tmp/output.tar:icao-pkd-management:arm64

# Load into Docker
docker load < /tmp/output.tar
```

### Why OCI Format?

GitHub Actions `docker/build-push-action@v5` with `outputs: type=tar` creates OCI format by default. This format supports multi-platform images but requires conversion for Docker.

### Authentication Strategy

**sshpass** is used for non-interactive SSH/SCP authentication:

```bash
# Without sshpass (requires manual password entry)
ssh luckfox@192.168.100.11 "docker ps"

# With sshpass (automated)
sshpass -p "luckfox" ssh -o StrictHostKeyChecking=no luckfox@192.168.100.11 "docker ps"
```

**Security Note**: Password is stored in script. For production, consider:
- SSH key-based authentication
- Environment variables for credentials
- Secret management tools

---

## Script Configuration

Edit `scripts/deploy-from-github-artifacts.sh` to customize:

```bash
# Luckfox connection
LUCKFOX_HOST="192.168.100.11"
LUCKFOX_USER="luckfox"
LUCKFOX_PASS="luckfox"
LUCKFOX_DIR="/home/luckfox/icao-local-pkd-cpp-v2"

# Artifact location
ARTIFACTS_DIR="./github-artifacts"

# Temporary directory for OCI conversion
TEMP_DIR="/tmp/icao-deploy-$$"
```

---

## Deployment Steps Breakdown

### Phase 1: Artifact Download (if needed)

```bash
# Script checks for ./github-artifacts directory
if [ ! -d "$ARTIFACTS_DIR" ]; then
    # Download using gh CLI
    gh run list --repo iland112/icao-local-pkd-cpp --branch feature/openapi-support --limit 1
    gh run download <RUN_ID> --dir ./github-artifacts
fi
```

### Phase 2: OCI to Docker Conversion

```bash
# For each service artifact:
# 1. Extract OCI archive
tar -xzf pkd-management-arm64.tar.gz -C /tmp/oci-dir

# 2. Convert to Docker format
skopeo copy --override-arch arm64 \
    oci:/tmp/oci-dir \
    docker-archive:/tmp/pkd-management-docker.tar:icao-pkd-management:arm64
```

### Phase 3: Luckfox Deployment

```bash
# 1. Stop and remove old deployment
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
    docker compose -f docker-compose-luckfox.yaml stop pkd-management
    docker rm icao-pkd-management
    docker rmi icao-pkd-management:arm64
"

# 2. Transfer Docker archive
sshpass -p "luckfox" scp \
    /tmp/pkd-management-docker.tar \
    luckfox@192.168.100.11:/home/luckfox/

# 3. Load image
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
    docker load < /home/luckfox/pkd-management-docker.tar
    rm -f /home/luckfox/pkd-management-docker.tar
"

# 4. Start service
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
    cd /home/luckfox/icao-local-pkd-cpp-v2
    /home/luckfox/scripts/luckfox-start.sh
"
```

---

## Service Naming Convention

| Service | Image Name | Artifact Name |
|---------|------------|---------------|
| PKD Management | `icao-pkd-management:arm64` | `pkd-management-arm64.tar.gz` |
| PA Service | `icao-pa-service:arm64` | `pkd-pa-arm64.tar.gz` |
| Sync Service | `icao-sync-service:arm64` | `pkd-sync-arm64.tar.gz` |
| Frontend | `icao-frontend:arm64` | `pkd-frontend-arm64.tar.gz` |

---

## Troubleshooting

### Error: sshpass not found

```bash
# Install sshpass
sudo apt-get install sshpass  # Linux
brew install hudochenkov/sshpass/sshpass  # macOS
```

### Error: skopeo not found

```bash
# Install skopeo
sudo apt-get install skopeo  # Linux
brew install skopeo  # macOS
```

### Error: Permission denied (publickey,password)

```bash
# Test SSH connection manually
sshpass -p "luckfox" ssh -o StrictHostKeyChecking=no luckfox@192.168.100.11 "echo test"

# Verify Luckfox credentials
# Default: user=luckfox, password=luckfox
```

### Error: invalid archive: does not contain a manifest.json

This error occurs when trying to load OCI format directly into Docker. The deployment script handles this automatically, but if you're loading manually:

```bash
# DO NOT use docker load directly on GitHub Actions artifacts
# They are OCI format, not Docker format

# Use the deployment script instead:
./scripts/deploy-from-github-artifacts.sh pkd-management
```

### Error: Artifact not found

```bash
# Ensure artifacts are downloaded
ls -la ./github-artifacts/

# If missing, download manually
gh run list --repo iland112/icao-local-pkd-cpp --branch feature/openapi-support --limit 1
gh run download <RUN_ID> --dir ./github-artifacts

# Or let the script download automatically
./scripts/deploy-from-github-artifacts.sh pkd-management
```

### Service not starting

```bash
# Check service logs on Luckfox
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
    cd /home/luckfox/icao-local-pkd-cpp-v2
    docker compose -f docker-compose-luckfox.yaml logs pkd-management
"

# Check service status
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
    docker ps --filter name=pkd-management
"
```

---

## Manual Deployment (Emergency)

If the automated script fails, follow these manual steps:

```bash
# 1. Download artifacts (if needed)
gh run list --repo iland112/icao-local-pkd-cpp --branch feature/openapi-support --limit 1
gh run download <RUN_ID> --dir ./github-artifacts

# 2. Convert OCI to Docker locally
cd /tmp
rm -rf oci-dir && mkdir oci-dir
tar -xzf /path/to/pkd-management-arm64.tar.gz -C oci-dir
skopeo copy --override-arch arm64 \
    oci:oci-dir \
    docker-archive:pkd-management-docker.tar:icao-pkd-management:arm64

# 3. Transfer to Luckfox
sshpass -p "luckfox" scp pkd-management-docker.tar luckfox@192.168.100.11:/home/luckfox/

# 4. Load and start on Luckfox
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
    cd /home/luckfox/icao-local-pkd-cpp-v2
    docker compose -f docker-compose-luckfox.yaml stop pkd-management
    docker rm icao-pkd-management 2>/dev/null || true
    docker rmi icao-pkd-management:arm64 2>/dev/null || true
    docker load < /home/luckfox/pkd-management-docker.tar
    rm -f /home/luckfox/pkd-management-docker.tar
    /home/luckfox/scripts/luckfox-start.sh
"
```

---

## Verification

### Check Deployment Success

```bash
# 1. Service status
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "docker ps --filter name=pkd-management"

# Expected: Up X seconds (healthy)

# 2. Service logs
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
    docker logs icao-pkd-management --tail 20
"

# Expected: "Server starting on http://0.0.0.0:8081"

# 3. Health check
curl http://192.168.100.11:8080/api/health

# Expected: {"status": "ok", ...}
```

### Test Application

```bash
# Frontend
curl http://192.168.100.11/

# API Gateway
curl http://192.168.100.11:8080/api/health

# PKD Management (direct, internal only)
curl http://192.168.100.11:8081/api/health
```

---

## Performance Notes

### Build Times

| Scenario | Time | Notes |
|----------|------|-------|
| First build (cold cache) | 60-80min | vcpkg compilation |
| vcpkg.json change | 30-40min | Rebuild dependencies |
| Source code change | **10-15min** | Optimal ⚡ |
| No changes (rerun) | ~5min | Full cache hit |

### Deployment Times

| Operation | Time | Size |
|-----------|------|------|
| Artifact download | 1-2min | ~150MB compressed |
| OCI to Docker conversion | 10-20sec | ~40MB → ~120MB |
| Transfer to Luckfox | 30-60sec | ~120MB |
| Docker load | 10-20sec | Extract and import |
| Service start | 5-10sec | Health check |
| **Total** | **~3-5min** | **End-to-end** |

---

## Security Considerations

### Current Implementation

- **Plaintext credentials**: Password stored in script
- **No encryption**: SSH traffic is encrypted, but password is visible in script
- **StrictHostKeyChecking disabled**: Accepts any SSH host key

### Production Recommendations

```bash
# 1. Use SSH key-based authentication
ssh-keygen -t ed25519 -f ~/.ssh/luckfox_deploy
ssh-copy-id -i ~/.ssh/luckfox_deploy.pub luckfox@192.168.100.11

# 2. Update script to use key
SSH_CMD="ssh -i ~/.ssh/luckfox_deploy luckfox@$LUCKFOX_HOST"
SCP_CMD="scp -i ~/.ssh/luckfox_deploy"

# 3. Enable StrictHostKeyChecking
SSH_CMD="ssh -o StrictHostKeyChecking=yes ..."

# 4. Use environment variables
LUCKFOX_PASS="${LUCKFOX_PASSWORD:-luckfox}"
```

---

## Best Practices

### Development Workflow

1. **Make changes** in local development environment
2. **Test locally** using Docker Compose
3. **Commit and push** to trigger GitHub Actions build
4. **Wait for build** to complete (~10-15min for code changes)
5. **Run deployment script** for specific service
6. **Verify deployment** on Luckfox
7. **Test functionality** via web UI or API

### Deployment Strategy

- **Single service deployment**: Use when only one service changed
- **All services deployment**: Use after infrastructure changes
- **Rollback**: Keep previous Docker images for quick rollback

### Monitoring

```bash
# Always check logs after deployment
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
    docker logs icao-pkd-management --tail 50 --follow
"

# Monitor service health
watch -n 5 'sshpass -p "luckfox" ssh luckfox@192.168.100.11 "docker ps"'
```

---

## Quick Reference

### Common Commands

```bash
# Deploy single service
./scripts/deploy-from-github-artifacts.sh pkd-management

# Check service status
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "docker ps"

# View logs
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "docker logs icao-pkd-management"

# Restart service
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
    cd /home/luckfox/icao-local-pkd-cpp-v2
    docker compose -f docker-compose-luckfox.yaml restart pkd-management
"

# Stop all services
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
    cd /home/luckfox/icao-local-pkd-cpp-v2
    /home/luckfox/scripts/luckfox-stop.sh
"
```

---

## Change Log

### 2026-01-09: Automated OCI Deployment

**Changes:**
- Implemented OCI to Docker format conversion using `skopeo`
- Added `sshpass` for automated SSH/SCP authentication
- Automated artifact download from GitHub Actions
- Consolidated deployment into single script
- Added comprehensive error handling and logging

**Previous Issues Resolved:**
- ❌ Manual password entry for each SSH/SCP command
- ❌ OCI format compatibility errors with `docker load`
- ❌ Manual artifact download and extraction
- ❌ Inconsistent deployment procedures

**New Capabilities:**
- ✅ Fully automated deployment (no manual intervention)
- ✅ OCI format handling (automatic conversion)
- ✅ One-command deployment per service
- ✅ Automatic cleanup of old deployments
- ✅ Health check and status verification

---

**Document Maintainer**: AI Assistant
**Last Review**: 2026-01-09
**Next Review**: When deployment process changes
