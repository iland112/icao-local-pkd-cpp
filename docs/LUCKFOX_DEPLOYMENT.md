# Luckfox ARM64 Deployment Guide

**Version**: 3.1
**Last Updated**: 2026-02-23
**Status**: Degraded — 192.168.100.11 하드웨어 장애로 단일 노드(192.168.100.10) 운영 중

---

## Overview

ARM64 Docker images are built via GitHub Actions CI/CD and deployed to Luckfox device.
All services use `network_mode: host` (luckfox kernel lacks iptables DNAT).

### Architecture

```
192.168.100.10: OpenLDAP1 (:389) - Master Node (현재 유일한 활성 노드)
192.168.100.11: OpenLDAP2 (:389) + Docker Apps - Luckfox (하드웨어 장애로 정지)
```

> **⚠ 현재 상태 (2026-02-23)**: `192.168.100.11` (Luckfox) 하드웨어 장애로 동작 정지.
> OpenLDAP MMR 복제는 `192.168.100.10` 단일 노드로 운영 중.
> Docker 서비스 배포 대상도 `192.168.100.10`으로 변경 필요.

### Services

| Service | Port | Image | Container |
|---------|------|-------|-----------|
| API Gateway | 8080 | nginx:alpine | icao-pkd-api-gateway |
| Frontend | 80 | icao-local-pkd-frontend:arm64 | icao-pkd-frontend |
| PKD Management | 8081 | icao-local-management:arm64 | icao-pkd-management |
| PA Service | 8082 | icao-local-pa:arm64 | icao-pkd-pa-service |
| PKD Relay | 8083 | icao-local-pkd-relay:arm64 | icao-pkd-relay |
| Monitoring | 8084 | icao-local-monitoring:arm64 | icao-pkd-monitoring |
| Swagger UI | 8888 | swaggerapi/swagger-ui | icao-pkd-swagger |
| PostgreSQL | 5432 | postgres:15 | icao-pkd-postgres |

---

## Prerequisites

### Local Machine

```bash
# Required tools
sudo apt-get install sshpass skopeo gh  # Debian/Ubuntu

# Verify
command -v sshpass && command -v skopeo && command -v gh && echo "All OK"
```

### Luckfox

- Docker installed and running
- ~~SSH access: `luckfox@192.168.100.11` (password: luckfox)~~ — **하드웨어 장애로 사용 불가**
- 현재 활성 노드: `luckfox@192.168.100.10` (password: luckfox)
- Project directory: `/home/luckfox/icao-local-pkd-cpp-v2`

---

## CI/CD Pipeline

### GitHub Actions Workflow

File: `.github/workflows/build-arm64.yml`

```
Push to main → Detect Changes → Build vcpkg-base (GHCR) → Build Services → Upload Artifacts
```

**Change detection**: Only builds services with actual file changes (dorny/paths-filter).

**Triggers**:
- Push to `main` branch (paths: services/, shared/, frontend/, nginx/, docker/)
- Manual dispatch with `force_build_all` and `no_cache` options

### Build Artifacts

| Artifact | Image Tag |
|----------|-----------|
| pkd-management-arm64.tar.gz | icao-local-management:arm64 |
| pkd-pa-arm64.tar.gz | icao-local-pa:arm64 |
| pkd-relay-arm64.tar.gz | icao-local-pkd-relay:arm64 |
| monitoring-service-arm64.tar.gz | icao-local-monitoring:arm64 |
| pkd-frontend-arm64.tar.gz | icao-local-pkd-frontend:arm64 |

---

## Deployment

### Automated Deployment

```bash
# Deploy specific service
bash scripts/deploy/from-github-artifacts.sh pkd-management

# Deploy all services
bash scripts/deploy/from-github-artifacts.sh all

# Available targets: pkd-management | pa-service | pkd-relay | monitoring-service | frontend | all
```

**What it does:**
1. Downloads artifacts from latest GitHub Actions run (via `gh` CLI)
2. Converts OCI format → Docker archive (via `skopeo`)
3. Stops old container on Luckfox
4. Transfers Docker archive via SCP
5. Loads image and starts service via `docker compose`
6. Verifies health check

### Manual Deployment

```bash
# 1. Download artifact
gh run download <RUN_ID> -n monitoring-service-arm64 --dir github-artifacts

# 2. Convert OCI → Docker
mkdir -p /tmp/oci-dir
tar -xzf github-artifacts/monitoring-service-arm64/monitoring-service-arm64.tar.gz -C /tmp/oci-dir
skopeo copy --override-arch arm64 \
    oci:/tmp/oci-dir \
    docker-archive:/tmp/monitoring-docker.tar:icao-local-monitoring:arm64

# 3. Transfer to luckfox
sshpass -p luckfox scp /tmp/monitoring-docker.tar luckfox@192.168.100.11:/home/luckfox/

# 4. Load and start
sshpass -p luckfox ssh luckfox@192.168.100.11 "
    docker load < /home/luckfox/monitoring-docker.tar
    rm -f /home/luckfox/monitoring-docker.tar
    cd /home/luckfox/icao-local-pkd-cpp-v2
    docker compose -f docker-compose-luckfox.yaml up -d monitoring-service
"
```

### Config Update (without rebuild)

When only config files change (nginx, docker-compose):

```bash
# Copy configs
sshpass -p luckfox scp docker-compose-luckfox.yaml luckfox@192.168.100.11:/home/luckfox/icao-local-pkd-cpp-v2/
sshpass -p luckfox scp nginx/api-gateway-luckfox.conf luckfox@192.168.100.11:/home/luckfox/icao-local-pkd-cpp-v2/nginx/

# Reload nginx (no downtime)
sshpass -p luckfox ssh luckfox@192.168.100.11 "docker exec icao-pkd-api-gateway nginx -s reload"

# Or restart specific service
sshpass -p luckfox ssh luckfox@192.168.100.11 "
    cd /home/luckfox/icao-local-pkd-cpp-v2
    docker compose -f docker-compose-luckfox.yaml up -d monitoring-service
"
```

---

## Management Scripts

Located in `scripts/luckfox/` (also copied to luckfox project root):

| Script | Purpose |
|--------|---------|
| `start.sh` | Start all services, create data directories |
| `stop.sh` | Stop all services |
| `restart.sh` | Restart all or specific service |
| `health.sh` | Health check all services, DB stats |
| `logs.sh` | View service logs (supports -f follow) |
| `clean.sh` | Full data reset (--force for non-interactive) |
| `backup.sh` | Backup configs, logs, image info |
| `restore.sh` | Restore from backup |

```bash
# On luckfox directly
cd /home/luckfox/icao-local-pkd-cpp-v2
./scripts/luckfox/health.sh

# Or from dev machine
sshpass -p luckfox ssh luckfox@192.168.100.11 "cd /home/luckfox/icao-local-pkd-cpp-v2 && ./scripts/luckfox/health.sh"
```

---

## Key Configuration Files

| File | Purpose |
|------|---------|
| `docker-compose-luckfox.yaml` | Service definitions (host network, 127.0.0.1 for DB) |
| `nginx/api-gateway-luckfox.conf` | API routing (8080 → backend services) |
| `frontend/nginx-luckfox.conf` | Frontend nginx (port 80, proxy to 8080) |
| `docker/init-scripts/` | PostgreSQL schema initialization |

### API Gateway Routes

| Route | Backend |
|-------|---------|
| `/api/upload`, `/api/certificates`, `/api/progress` | PKD Management (:8081) |
| `/api/auth`, `/api/audit`, `/api/icao` | PKD Management (:8081) |
| `/api/health` | PKD Management (:8081) |
| `/api/pa/*` | PA Service (:8082) |
| `/api/sync/*` | PKD Relay (:8083) |
| `/api/monitoring/*` | Monitoring (:8084) |
| `/api-docs/`, `/api/docs/` | Swagger UI (:8888) |

---

## Troubleshooting

### OCI format error: "does not contain a manifest.json"
GitHub Actions outputs OCI format. Convert with skopeo before `docker load`.

### Service shows DOWN in monitoring
Check the health endpoint URL in `docker-compose-luckfox.yaml`. PA Service health is `/api/health` (not `/api/pa/health`).

### 404 through API Gateway
Check `nginx/api-gateway-luckfox.conf` has the route. Reload nginx after config changes.

### Login fails with 404
Ensure `/api/auth` route exists in API gateway config.

### Default credentials
- Admin login: `admin` / `admin123`
- PostgreSQL: `pkd` / `pkd`
- SSH: `luckfox` / `luckfox`

---

## Performance

### Build Times

| Scenario | Time |
|----------|------|
| First build (cold cache) | 60-80 min |
| vcpkg dependency change | 30-40 min |
| Source code change only | 10-15 min |
| No changes (cache hit) | ~5 min |

### Deployment Times

| Operation | Time |
|-----------|------|
| Artifact download | 1-2 min |
| OCI → Docker conversion | 10-20 sec |
| Transfer to luckfox | 30-60 sec |
| Docker load + start | 10-20 sec |
| **Total per service** | **~3 min** |

---

## Change Log

### 2026-02-23: v3.1 - Hardware Failure Status Update
- `192.168.100.11` (Luckfox) 하드웨어 장애로 동작 정지
- 현재 `192.168.100.10` 단일 노드로만 연결 가능
- OpenLDAP MMR 복제 단일 노드 운영 상태 반영
- 배포 대상 노드 정보 업데이트

### 2026-02-13: v3.0 - Full Pipeline + Monitoring Service
- GitHub Actions CI/CD with change detection (dorny/paths-filter)
- vcpkg-base image pushed to GHCR (shared by all C++ services)
- Monitoring service added to pipeline (DB-independent)
- All 8 services deployed and verified on luckfox
- Management scripts modernized (project root auto-detection)
- API gateway: added auth/audit/icao/monitoring routes
- Fixed PA health endpoint URL in monitoring config

### 2026-01-09: v2.0 - Automated OCI Deployment
- OCI → Docker conversion via skopeo
- sshpass for automated SSH/SCP
- Automated artifact download via gh CLI
- Single-command deployment per service
