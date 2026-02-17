# ICAO Local PKD - Deployment Process

**Last Updated**: 2026-02-17
**Version**: 2.12.0

---

## Overview

This document describes the CI/CD pipeline from code changes to production deployment on Luckfox ARM64 device.

For Luckfox-specific configuration details, see [LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md).

## Deployment Pipeline

```
1. Code Modification (Local)
   +-> Edit source files

2. Git Commit & Push (Local)
   +-> git add && git commit && git push origin main

3. GitHub Actions Build (Automated - Cloud)
   +-> Change detection (dorny/paths-filter@v3)
   +-> Docker buildx (ARM64 cross-compilation)
   +-> Multi-stage caching (GHCR vcpkg-base + GHA cache)
   +-> Save artifacts as OCI format (30-day retention)

4. Artifact Download (Local)
   +-> gh run download <run-id> --name <artifact-name>

5. Deploy to Luckfox (Automated Script)
   +-> [1/5] OCI to Docker format conversion (skopeo)
   +-> [2/5] Cleanup old containers/images on Luckfox
   +-> [3/5] Transfer Docker archive (scp)
   +-> [4/5] Load image (docker load)
   +-> [5/5] Recreate container (docker compose up -d)
```

---

## Step-by-Step Guide

### 1. Code Modification

Source code locations:

| Service | Path |
|---------|------|
| PKD Management | `services/pkd-management/src/` |
| PA Service | `services/pa-service/src/` |
| PKD Relay | `services/pkd-relay-service/src/` |
| Monitoring | `services/monitoring-service/src/` |
| Shared Libraries | `shared/lib/` |
| Frontend | `frontend/src/` |

### 2. Git Commit & Push

```bash
git add <files>
git commit -m "feat: description"
git push origin main
```

### 3. GitHub Actions Build

**Workflow File**: `.github/workflows/build-arm64.yml`

**Trigger**: Push to `main` branch

**Change Detection**:
The workflow uses `dorny/paths-filter@v3` to detect which services changed. Only modified services are rebuilt.

```yaml
# Detected service changes:
pkd-management: services/pkd-management/** or shared/**
pa-service: services/pa-service/** or shared/**
pkd-relay: services/pkd-relay-service/** or shared/**
monitoring: services/monitoring-service/** or shared/**
frontend: frontend/** or nginx/** or shared/static/**
```

**Build Strategy**:
- GHCR-hosted `vcpkg-base` image (pre-built ARM64 dependencies)
- Multi-stage Dockerfile with BuildKit inline cache
- ARM64 cross-compilation via `docker/setup-buildx-action`
- Only PostgreSQL client library (no Oracle in ARM64 builds)

**Build Times**:

| Scenario | Time | Notes |
|----------|------|-------|
| Source code change (cached) | 5-15min | vcpkg-base from GHCR |
| vcpkg.json change | 30-40min | Rebuild dependencies |
| Cold build (no cache) | 60-80min | One-time vcpkg compilation |

**Artifacts**: Each service produces a separate OCI archive artifact:
- `pkd-management-arm64`
- `pa-service-arm64`
- `pkd-relay-arm64`
- `monitoring-service-arm64`
- `pkd-frontend-arm64`

**Monitor Build**:
```bash
# List recent runs
gh run list --limit 5

# Watch specific run
gh run watch <run-id>
```

### 4. Artifact Download

```bash
# List runs to find the latest
gh run list --limit 3

# Download specific service artifact
gh run download <run-id> --name pkd-frontend-arm64

# Or download all artifacts
gh run download <run-id>

# Verify downloaded files
ls -lh pkd-frontend-arm64/
```

**Important**: Always delete old `github-artifacts/` or specific artifact directories before downloading to avoid deploying stale images.

### 5. Deploy to Luckfox

**Deployment Script**:
```bash
# Deploy script location
scripts/deploy/from-github-artifacts.sh [service-name|all]

# Deploy single service
scripts/deploy/from-github-artifacts.sh frontend

# Deploy all services
scripts/deploy/from-github-artifacts.sh all
```

The script performs:
1. **OCI to Docker conversion** via `skopeo`
2. **Cleanup** old containers and images on Luckfox
3. **Transfer** Docker archive via `scp`
4. **Load** image via `docker load`
5. **Recreate** container via `docker compose up -d`

---

## Image Name Mapping

| CI Artifact | Docker Image on Luckfox | Compose Service |
|-------------|------------------------|-----------------|
| `pkd-management-arm64` | `icao-pkd-management:arm64` | pkd-management |
| `pa-service-arm64` | `icao-pkd-pa-service:arm64` | pa-service |
| `pkd-relay-arm64` | `icao-pkd-relay:arm64` | pkd-relay |
| `monitoring-service-arm64` | `icao-pkd-monitoring:arm64` | monitoring |
| `pkd-frontend-arm64` | `icao-pkd-frontend:arm64` | frontend |

---

## Verification

### Verify Deployment

```bash
# SSH shortcut (from deploy script)
LUCKFOX_IP=10.0.0.167

# Check container status
ssh luckfox@$LUCKFOX_IP "cd ~/icao-local-pkd && docker compose ps"

# Check service health
curl http://$LUCKFOX_IP:8080/api/health
curl http://$LUCKFOX_IP:8080/api/pa/health
curl http://$LUCKFOX_IP:8080/api/sync/health

# Check logs
ssh luckfox@$LUCKFOX_IP "docker logs icao-pkd-management --tail 20"
```

### Test Endpoints

| Endpoint | URL |
|----------|-----|
| Frontend | `http://<LUCKFOX_IP>/` |
| API Gateway | `http://<LUCKFOX_IP>:8080/api` |
| Swagger UI | `http://<LUCKFOX_IP>:8080/api-docs/` |

---

## Troubleshooting

### Old version deployed despite new build

1. **Delete old artifacts**: `rm -rf github-artifacts/ pkd-*-arm64/`
2. **Download fresh**: `gh run download <latest-run-id> --name <service>-arm64`
3. **Verify timestamp**: `ls -lh pkd-frontend-arm64/`
4. **Redeploy**: `scripts/deploy/from-github-artifacts.sh frontend`

### Build cache prevents code changes

Update version string in `main.cpp` or use `CACHE_BUST` build arg.
See [DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md) for details.

### CORS errors on Luckfox

Frontend must use relative API URLs (`/api/...`) not hardcoded `http://localhost:8080/api/...`.
The Luckfox nginx config proxies `/api` to `127.0.0.1:8080`.

---

## References

- [LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md) - Luckfox-specific configuration
- [DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md) - Build cache troubleshooting
- [BUILD_SOP.md](BUILD_SOP.md) - Build verification procedures
