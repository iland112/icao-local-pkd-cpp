# ICAO Local PKD - Deployment Process

**Last Updated**: 2026-03-18
**Version**: 2.37.0

---

## Overview

This document describes deployment processes for all three target environments:

| Environment | Host | Runtime | Method |
|-------------|------|---------|--------|
| **Local Development** | WSL2 | Docker | `docker-start.sh`, `scripts/build/rebuild-*.sh` |
| **Production** | 10.0.0.220 (RHEL 9) | Podman | `rsync` + `podman-compose build`, Podman scripts |
| **Luckfox (ARM64)** | 192.168.100.10 | Docker | GitHub Actions CI/CD, OCI artifacts, `from-github-artifacts.sh` |

### Services

| Service | Port | Technology | Notes |
|---------|------|------------|-------|
| PKD Management | :8081 | C++/Drogon | Core certificate management |
| PA Service | :8082 | C++/Drogon | Passive Authentication (ICAO 9303) |
| PKD Relay | :8083 | C++/Drogon | DB-LDAP sync, reconciliation |
| Monitoring Service | :8084 | C++/Drogon | System metrics, DB-independent |
| AI Analysis Service | :8085 | Python/FastAPI | ML anomaly detection |
| EAC Service | :8086 | C++/Drogon | BSI TR-03110 CVC (experimental) |
| Frontend | — | React 19 | Served via nginx |

### Supporting Infrastructure

- **API Gateway**: nginx (ports 80/443/8080)
- **Database**: PostgreSQL 15 (default) or Oracle XE 21c (via `DB_TYPE`)
- **LDAP**: OpenLDAP MMR cluster (2 nodes)
- **Swagger UI**: API documentation

---

## 1. Local Development (WSL2, Docker)

### Quick Start

```bash
# Start all services
./docker-start.sh

# Health check
./docker-health.sh

# Stop all services
./docker-stop.sh
```

### Rebuilding Services

```bash
# Rebuild specific service (cached, 2-3 min)
./scripts/build/rebuild-pkd-relay.sh
./scripts/build/rebuild-pkd-management.sh
./scripts/build/rebuild-frontend.sh

# Clean rebuild (no cache, 20-30 min)
./scripts/build/rebuild-pkd-relay.sh --no-cache

# Or use docker compose directly
docker compose -f docker/docker-compose.yaml build <service-name>
docker compose -f docker/docker-compose.yaml build --no-cache <service-name>
```

### Full Reset (Clean Install)

```bash
# Complete data reset + LDAP DIT initialization
./docker-clean-and-init.sh
```

### When to Use `--no-cache`

- CMakeLists.txt or vcpkg.json changes
- New library additions or dependency changes
- Dockerfile modifications
- Final pre-deployment verification

### Development Environment (Isolated)

```bash
# Separate containers/ports from main environment
docker compose -f docker/docker-compose.dev.yaml up -d
docker compose -f docker/docker-compose.dev.yaml build --no-cache pkd-management-dev
```

### Access URLs

| Service | URL |
|---------|-----|
| Frontend | http://localhost:13080 |
| API Gateway | http://localhost:18080/api |
| Swagger UI | http://localhost:18090 |

---

## 2. Production (10.0.0.220, RHEL 9, Podman)

Production uses **Podman** (rootless, daemonless) instead of Docker. For full Podman details, see [PODMAN_DEPLOYMENT.md](PODMAN_DEPLOYMENT.md). For server setup, see [SERVER_SETUP_10.0.0.220.md](SERVER_SETUP_10.0.0.220.md).

### Deployment Pipeline

```
1. Code Changes (Local WSL2)
   +-> Edit source files, test locally with Docker

2. Transfer to Production
   +-> rsync or git pull on server

3. Build on Server (Podman)
   +-> podman-compose -f docker/docker-compose.podman.yaml build <service>

4. Start/Restart
   +-> ./podman-start.sh or scripts/podman/restart.sh <service>
```

### Step-by-Step

#### Transfer Code

```bash
# Option A: rsync from local
rsync -avz --exclude='.docker-data' --exclude='node_modules' \
    . scpkd@10.0.0.220:/home/scpkd/icao-local-pkd/

# Option B: git pull on server
ssh scpkd@10.0.0.220 "cd ~/icao-local-pkd && git pull origin main"
```

#### Build & Deploy

```bash
# SSH to server
ssh scpkd@10.0.0.220
cd ~/icao-local-pkd

# Rebuild specific service
podman-compose -f docker/docker-compose.podman.yaml build pkd-management
podman-compose -f docker/docker-compose.podman.yaml up -d pkd-management

# Or rebuild without cache
podman-compose -f docker/docker-compose.podman.yaml build --no-cache pkd-management

# Restart (recommended: stop + start for dependency ordering)
scripts/podman/restart.sh pkd-management
```

#### Daily Operations

```bash
./podman-start.sh              # Start all
./podman-stop.sh               # Stop all
./podman-health.sh             # Health check
scripts/podman/logs.sh pkd-management 50   # Logs
scripts/podman/backup.sh       # Backup
scripts/podman/restore.sh ./backups/20260318_103000  # Restore
```

#### Full Reset

```bash
./podman-clean-and-init.sh     # Oracle + LDAP DIT + services
```

### Production Environment

| Item | Value |
|------|-------|
| OS | RHEL 9.7 (SELinux Enforcing) |
| Podman | 5.6.0 (rootless) |
| podman-compose | 1.5.0 (pip) |
| Database | Oracle XE 21c |
| Network | CNI + dnsname plugin |
| Compose file | `docker/docker-compose.podman.yaml` |

### Key Differences from Docker

| Item | Docker (Local) | Podman (Production) |
|------|---------------|---------------------|
| Daemon | dockerd (root) | Daemonless (rootless) |
| CLI | `docker` | `podman` |
| Compose | `docker compose` | `podman-compose` |
| DNS | 127.0.0.11 (built-in) | dnsname CNI plugin |
| `depends_on` condition | `service_healthy` | Script-based wait |
| SELinux volumes | `:Z` auto-label | `chcon` pre-labeling |
| Privileged ports | root daemon | sysctl required |

### Access URLs (Production)

| Service | URL |
|---------|-----|
| Frontend (HTTPS) | https://pkd.smartcoreinc.com |
| Frontend (HTTP) | http://pkd.smartcoreinc.com |
| API Gateway (HTTPS) | https://pkd.smartcoreinc.com/api |
| API Gateway (internal) | http://localhost:18080/api |
| Swagger UI | http://localhost:18090 |

---

## 3. Luckfox (ARM64, GitHub Actions CI/CD)

ARM64 images are cross-compiled via GitHub Actions and deployed to the Luckfox device. For Luckfox-specific configuration, see [LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md).

### Deployment Pipeline

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

### Step-by-Step

#### GitHub Actions Build

**Workflow File**: `.github/workflows/build-arm64.yml`

**Trigger**: Push to `main` branch

**Change Detection**: Only modified services are rebuilt (dorny/paths-filter).

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
- PostgreSQL only (no Oracle in ARM64 builds)

**Monitor Build**:
```bash
gh run list --limit 5
gh run watch <run-id>
```

#### Artifact Download

```bash
# List runs to find the latest
gh run list --limit 3

# Download specific service artifact
gh run download <run-id> --name pkd-frontend-arm64

# Or download all artifacts
gh run download <run-id>
```

**Important**: Delete old artifact directories before downloading to avoid deploying stale images.

#### Deploy to Luckfox

```bash
# Deploy single service
scripts/deploy/from-github-artifacts.sh frontend

# Deploy all services
scripts/deploy/from-github-artifacts.sh all

# Available targets:
# pkd-management | pa-service | pkd-relay | monitoring-service | frontend | all
```

### Build Times

| Scenario | Time | Notes |
|----------|------|-------|
| Source code change (cached) | 5-15 min | vcpkg-base from GHCR |
| vcpkg.json change | 30-40 min | Rebuild dependencies |
| Cold build (no cache) | 60-80 min | One-time vcpkg compilation |

### Deployment Times

| Operation | Time |
|-----------|------|
| Artifact download | 1-2 min |
| OCI to Docker conversion | 10-20 sec |
| Transfer to Luckfox | 30-60 sec |
| Docker load + start | 10-20 sec |
| **Total per service** | **~3 min** |

### Image Name Mapping

| CI Artifact | Docker Image on Luckfox | Compose Service |
|-------------|------------------------|-----------------|
| `pkd-management-arm64` | `icao-pkd-management:arm64` | pkd-management |
| `pa-service-arm64` | `icao-pkd-pa-service:arm64` | pa-service |
| `pkd-relay-arm64` | `icao-pkd-relay:arm64` | pkd-relay |
| `monitoring-service-arm64` | `icao-pkd-monitoring:arm64` | monitoring |
| `pkd-frontend-arm64` | `icao-pkd-frontend:arm64` | frontend |

### Luckfox Verification

```bash
LUCKFOX_IP=192.168.100.10

# Check container status
ssh luckfox@$LUCKFOX_IP "cd ~/icao-local-pkd-cpp-v2 && docker compose ps"

# Check service health
curl http://$LUCKFOX_IP:8080/api/health
curl http://$LUCKFOX_IP:8080/api/pa/health
curl http://$LUCKFOX_IP:8080/api/sync/health

# Check logs
ssh luckfox@$LUCKFOX_IP "docker logs icao-pkd-management --tail 20"
```

### Luckfox Status

> **Current Status (2026-02-23)**: `192.168.100.11` hardware failure. Single-node operation on `192.168.100.10`.

---

## Source Code Locations

| Service | Path |
|---------|------|
| PKD Management | `services/pkd-management/src/` |
| PA Service | `services/pa-service/src/` |
| PKD Relay | `services/pkd-relay-service/src/` |
| Monitoring | `services/monitoring-service/src/` |
| AI Analysis | `services/ai-analysis/` |
| EAC Service | `services/eac-service/src/` |
| Shared Libraries | `shared/lib/` |
| Frontend | `frontend/src/` |

---

## Script Structure

```
scripts/
+-- docker/          # Docker management (start, stop, restart, health, logs, backup)
+-- podman/          # Podman management for Production RHEL 9 (same structure)
+-- luckfox/         # ARM64 deployment (same structure)
+-- build/           # Build scripts (rebuild-*, check-freshness, verify-*)
+-- deploy/          # Deployment (from-github-artifacts.sh)
+-- ssl/             # SSL certificate management (init-cert, renew-cert)
+-- helpers/         # Utility functions (db-helpers.sh, ldap-helpers.sh)
+-- maintenance/     # Data management (reset-all-data, reset-ldap)
+-- lib/             # Shared shell library (common.sh)
+-- dev/             # Development scripts (dev environment)
+-- client/          # Client setup (setup-pkd-access.bat/.ps1)

# Root convenience wrappers
docker-start.sh, docker-stop.sh, docker-health.sh, docker-clean-and-init.sh
podman-start.sh, podman-stop.sh, podman-health.sh, podman-clean-and-init.sh
```

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

### nginx 502 Bad Gateway (Production Podman)

```bash
# Check DNS resolver
grep resolver .docker-data/nginx/api-gateway.conf

# Restart nginx to refresh DNS cache
podman restart icao-local-pkd-api-gateway

# Full restart (DNS re-detection)
./podman-stop.sh && ./podman-start.sh
```

### SELinux permission errors (Production RHEL 9)

```bash
# Pre-label volumes for rootless Podman
chcon -Rt container_file_t .docker-data/ docker/db-oracle/init data/cert nginx/ docs/openapi
chcon -R -l s0 .docker-data/ docker/db-oracle/init data/cert nginx/ docs/openapi
```

### OCI format error: "does not contain a manifest.json"

GitHub Actions outputs OCI format. Convert with skopeo before `docker load`:
```bash
skopeo copy --override-arch arm64 \
    oci:/tmp/oci-dir \
    docker-archive:/tmp/service-docker.tar:image-name:arm64
```

---

## References

- [PODMAN_DEPLOYMENT.md](PODMAN_DEPLOYMENT.md) - Production Podman deployment (RHEL 9, SELinux, DNS)
- [SERVER_SETUP_10.0.0.220.md](SERVER_SETUP_10.0.0.220.md) - Production server setup
- [LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md) - Luckfox ARM64 configuration
- [DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md) - Build cache troubleshooting
- [BUILD_SOP.md](BUILD_SOP.md) - Build verification procedures
