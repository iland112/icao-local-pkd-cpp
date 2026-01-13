# ICAO Local PKD - Deployment Process

**Last Updated**: 2026-01-13
**Version**: 1.5.10

---

## Overview

This document describes the complete deployment workflow from code changes to production deployment on Luckfox ARM64 device.

## Complete Deployment Pipeline

```
┌─────────────────────────────────────────────────────────────────────┐
│                        DEPLOYMENT PIPELINE                           │
└─────────────────────────────────────────────────────────────────────┘

1. Code Modification (Local)
   └─> Edit source files

2. Git Commit & Push (Local)
   └─> git add . && git commit -m "..." && git push origin feature/...

3. GitHub Actions Build (Automated - Cloud)
   ├─> Change detection (dorny/paths-filter@v3)
   ├─> Docker buildx (ARM64 cross-compilation)
   ├─> Multi-stage caching (vcpkg, dependencies, code)
   └─> Save artifacts as OCI format (30-day retention)

4. Artifact Download (Local)
   └─> gh run download <run-id>

5. Deploy to Luckfox (Automated Script)
   ├─> [1/5] OCI to Docker format conversion (skopeo)
   ├─> [2/5] Cleanup old containers/images on Luckfox
   ├─> [3/5] Transfer Docker archive (scp)
   ├─> [4/5] Load image (docker load)
   └─> [5/5] Recreate container (docker compose up -d)
```

---

## Step-by-Step Guide

### 1. Code Modification

Edit source files locally:
- Backend: `services/{pkd-management,pa-service,sync-service}/src/`
- Frontend: `frontend/src/`
- Configuration: `nginx/`, `docker-compose-luckfox.yaml`

```bash
vim frontend/src/components/layout/Sidebar.tsx
```

### 2. Git Commit & Push

```bash
git add .
git commit -m "feat: add new feature"
git push origin feature/openapi-support
```

**Important**: Update version string in source code to prevent cache issues:
```cpp
// services/*/src/main.cpp
spdlog::info("Starting Service v1.X.Y - FEATURE_NAME");
```

### 3. GitHub Actions Build

**Workflow File**: `.github/workflows/build-arm64.yml`

**Trigger Branches**:
- `feature/arm64-support`
- `feature/openapi-support`

**Change Detection**:
```yaml
detect-changes:
  uses: dorny/paths-filter@v3
  with:
    base: ${{ github.event.before }}
    filters: |
      pkd-management:
        - 'services/pkd-management/**'
        - 'shared/**'
      frontend:
        - 'frontend/**'
        - 'nginx/**'
```

**Build Strategy**:
- Multi-stage Dockerfile caching
- Separate cache scopes per service
- BuildKit inline cache enabled
- ARM64 cross-compilation via buildx

**Build Times**:
| Scenario | Time | Notes |
|----------|------|-------|
| Cold build | 60-80min | One-time vcpkg compilation |
| vcpkg.json change | 30-40min | Rebuild dependencies |
| Source code change | 10-15min | 90% cache hit |

**Artifact Format**: OCI (Open Container Initiative)
- Standard container image format
- Requires conversion for Docker

**Monitor Build**:
```bash
# List recent runs
gh run list --branch feature/openapi-support --limit 5

# Watch specific run
gh run watch <run-id>

# Check build freshness
./scripts/check-build-freshness.sh
```

### 4. Artifact Download

```bash
# Download latest artifacts
gh run list --branch feature/openapi-support --limit 1
gh run download <run-id>

# Verify downloaded files
ls -lh github-artifacts/arm64-docker-images-all/
```

**Artifact Structure**:
```
github-artifacts/
└── arm64-docker-images-all/
    ├── pkd-management-arm64/
    │   └── pkd-management-arm64.tar.gz (OCI format)
    ├── pkd-frontend-arm64/
    │   └── pkd-frontend-arm64.tar.gz (OCI format)
    ├── pa-service-arm64/
    │   └── pa-service-arm64.tar.gz (OCI format)
    └── sync-service-arm64/
        └── sync-service-arm64.tar.gz (OCI format)
```

### 5. Deploy to Luckfox

**Official Deployment Script**:
```bash
./scripts/deploy-from-github-artifacts.sh [service-name]

# Deploy single service
./scripts/deploy-from-github-artifacts.sh frontend

# Deploy all services
./scripts/deploy-from-github-artifacts.sh all
```

**Deployment Steps**:

#### 5.1 OCI to Docker Conversion
```bash
skopeo copy \
  oci-archive:pkd-frontend-arm64.tar.gz \
  docker-archive:pkd-frontend-arm64.tar
```

**Why conversion is needed**:
- GitHub Actions buildx saves as OCI format (standard)
- Docker CLI only accepts Docker archive format
- `skopeo` tool handles the conversion

#### 5.2 Cleanup on Luckfox
```bash
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
  docker stop icao-pkd-frontend
  docker rm icao-pkd-frontend
  docker rmi icao-local-pkd-frontend:arm64-fixed
"
```

**Purpose**: Clean state before deployment

#### 5.3 Transfer Docker Archive
```bash
sshpass -p "luckfox" scp \
  pkd-frontend-arm64.tar \
  luckfox@192.168.100.11:/tmp/
```

**File Size**: Typically 60-70MB per service

#### 5.4 Load Image on Luckfox
```bash
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
  docker load < /tmp/pkd-frontend-arm64.tar
"
```

**Output**:
```
Loaded image: icao-local-pkd-frontend:arm64-fixed
```

#### 5.5 Recreate Container
```bash
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "
  cd ~/icao-local-pkd-cpp-v2
  docker compose -f docker-compose-luckfox.yaml up -d frontend
"
```

**Docker Compose Actions**:
1. Stop old container
2. Remove old container
3. Create new container from new image
4. Start new container

---

## Image Name Mapping

**Critical**: Deployment script image names MUST match docker-compose-luckfox.yaml

| Service | Image Name | Note |
|---------|------------|------|
| pkd-management | `icao-local-management:arm64` | Main service |
| pa-service | `icao-local-pa:arm64-v3` | PA verification |
| sync-service | `icao-local-sync:arm64-v1.2.0` | DB-LDAP sync |
| frontend | `icao-local-pkd-frontend:arm64-fixed` | React UI |

**Version Update Checklist**:
1. Update `scripts/deploy-from-github-artifacts.sh` - `deploy_service()` function
2. Update `docker-compose-luckfox.yaml` - service `image:` field
3. Deploy updated docker-compose to Luckfox
4. Rebuild and redeploy service

---

## Verification

### Verify Deployment Success

```bash
# Check container status
sshpass -p "luckfox" ssh luckfox@192.168.100.11 \
  "cd ~/icao-local-pkd-cpp-v2 && docker compose -f docker-compose-luckfox.yaml ps"

# Check logs
sshpass -p "luckfox" ssh luckfox@192.168.100.11 \
  "docker logs icao-pkd-frontend --tail 20"

# Verify version (backend services)
sshpass -p "luckfox" ssh luckfox@192.168.100.11 \
  "docker logs icao-pkd-management --tail 5 | grep 'Starting'"

# Verify file timestamps (frontend)
sshpass -p "luckfox" ssh luckfox@192.168.100.11 \
  "docker exec icao-pkd-frontend ls -la /usr/share/nginx/html/assets/"
```

### Test Endpoints

**Frontend**: http://192.168.100.11/

**Backend APIs** (via API Gateway):
- Health: http://192.168.100.11:8080/api/health
- PA Service: http://192.168.100.11:8080/api/pa/health
- Sync Service: http://192.168.100.11:8080/api/sync/health

**Swagger UI**:
- http://192.168.100.11:8080/api-docs/

---

## Troubleshooting

### Issue: Old version deployed despite new build

**Symptom**: Deployment completes but application shows old version

**Causes**:
1. Browser cache (frontend)
2. Wrong artifact downloaded
3. Docker cache issue

**Solution**:
```bash
# 1. Hard refresh browser (Ctrl+Shift+R or Ctrl+F5)

# 2. Verify artifact timestamp
ls -lh github-artifacts/arm64-docker-images-all/pkd-frontend-arm64/

# 3. Re-download latest artifact
rm -rf github-artifacts
gh run list --branch feature/openapi-support --limit 1
gh run download <latest-run-id>

# 4. Redeploy
./scripts/deploy-from-github-artifacts.sh frontend
```

### Issue: Build cache prevents code changes

**Symptom**: Code changes don't appear in build despite clean build

**Causes**: Docker BuildKit cache reuses old layers

**Solution**:
```cpp
// Update version string in main.cpp
spdlog::info("Starting Service v1.X.Y - NEW_FEATURE_NAME");
```

**Reference**: [docs/DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md)

### Issue: Image name mismatch

**Symptom**: Deployment succeeds but container uses old image

**Causes**: deployment script image name ≠ docker-compose image name

**Solution**: See "Image Name Mapping" section above

---

## Performance Optimization

### Build Optimization

**Multi-stage Dockerfile**:
```dockerfile
# Stage 1: System dependencies (rarely changes)
FROM debian:bookworm AS vcpkg-base

# Stage 2: vcpkg packages (changes with vcpkg.json)
FROM vcpkg-base AS vcpkg-deps

# Stage 3: Application build (frequent changes)
FROM vcpkg-deps AS builder

# Stage 4: Runtime image (production)
FROM debian:bookworm-slim AS runtime
```

**GitHub Actions Cache**:
```yaml
cache-from: |
  type=gha,scope=frontend-vcpkg-base
  type=gha,scope=frontend-vcpkg-deps
cache-to: |
  type=gha,mode=max,scope=frontend-vcpkg-base
  type=gha,mode=max,scope=frontend-vcpkg-deps
```

**CACHE_BUST Mechanism**:
```yaml
build-args: |
  CACHE_BUST=${{ github.sha }}
```

```dockerfile
ARG CACHE_BUST=unknown
RUN echo "=== Cache Bust: $CACHE_BUST ==="
```

### Deployment Optimization

**Parallel Deployment**:
```bash
# Deploy multiple services in parallel
./scripts/deploy-from-github-artifacts.sh pkd-management &
./scripts/deploy-from-github-artifacts.sh pa-service &
./scripts/deploy-from-github-artifacts.sh sync-service &
wait
```

**Selective Deployment**:
- Change detection ensures only modified services are built
- Manual deployment of specific services only
- No unnecessary container restarts

---

## Security Notes

### SSH Authentication

**Current Method**: sshpass (non-interactive)
```bash
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "command"
```

**Production Recommendation**: Use SSH key authentication
```bash
# Generate key pair
ssh-keygen -t ed25519 -f ~/.ssh/luckfox_deploy

# Copy to Luckfox
ssh-copy-id -i ~/.ssh/luckfox_deploy.pub luckfox@192.168.100.11

# Update deployment script
ssh -i ~/.ssh/luckfox_deploy luckfox@192.168.100.11 "command"
```

### Container Security

**Network Mode**: Host network (for simplicity on Luckfox)
```yaml
network_mode: host
```

**Secrets Management**: Environment variables
- PostgreSQL credentials: `POSTGRES_USER`, `POSTGRES_PASSWORD`
- LDAP credentials: `LDAP_BIND_DN`, `LDAP_BIND_PASSWORD`

**Production Recommendation**: Use Docker secrets or external secrets manager

---

## References

- [LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md) - Luckfox-specific deployment guide
- [DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md) - Build cache troubleshooting
- [LUCKFOX_README.md](../LUCKFOX_README.md) - Luckfox management scripts
- [CLAUDE.md](../CLAUDE.md) - Project overview and architecture

---

## Appendix: Quick Reference

### Essential Commands

```bash
# Check build status
gh run list --branch feature/openapi-support --limit 3

# Download artifacts
gh run download <run-id>

# Deploy single service
./scripts/deploy-from-github-artifacts.sh frontend

# Deploy all services
./scripts/deploy-from-github-artifacts.sh all

# Verify deployment
sshpass -p "luckfox" ssh luckfox@192.168.100.11 \
  "cd ~/icao-local-pkd-cpp-v2 && docker compose -f docker-compose-luckfox.yaml ps"

# Check logs
./luckfox-logs.sh frontend

# Health check
./luckfox-health.sh
```

### Service Ports

| Service | Internal Port | External Access |
|---------|---------------|-----------------|
| Frontend | 80 | http://192.168.100.11/ |
| API Gateway | 8080 | http://192.168.100.11:8080/api |
| PKD Management | 8081 | Via API Gateway |
| PA Service | 8082 | Via API Gateway |
| Sync Service | 8083 | Via API Gateway |
| Swagger UI | 8888 | Via API Gateway |
| PostgreSQL | 5432 | Internal only |
| HAProxy (LDAP) | 10389 | Internal only |

### File Locations

| Component | Path |
|-----------|------|
| Luckfox project root | `/home/luckfox/icao-local-pkd-cpp-v2/` |
| Docker compose | `docker-compose-luckfox.yaml` |
| Management scripts | `luckfox-*.sh` |
| PostgreSQL data | `.docker-data/postgres/` |
| Upload files | `.docker-data/pkd-uploads/` |
| Logs | `.docker-data/*/logs/` |
