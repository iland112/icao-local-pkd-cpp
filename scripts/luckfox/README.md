# Luckfox Docker Management Scripts

Luckfox ARM64 환경에서 ICAO Local PKD 서비스를 운영하기 위한 스크립트.

**Updated: 2026-02-23** - GitHub Actions CI/CD 기반 배포

> **⚠ 현재 상태 (2026-02-23)**: `192.168.100.11` 하드웨어 장애로 정지. `192.168.100.10`으로만 접근 가능.

## Architecture

```
Browser → Frontend (nginx:80) → API Gateway (nginx:8080) → Backend Services → DB/LDAP
                                      |
                                      +-- PKD Management (:8081)
                                      +-- PA Service (:8082)
                                      +-- PKD Relay (:8083)
                                      +-- Swagger UI (:8888)

DB: PostgreSQL (:5432) on localhost (network_mode: host)
LDAP: OpenLDAP (192.168.100.10:389)
```

All containers use `network_mode: host` (luckfox kernel lacks iptables DNAT).

## Scripts

| Script | Description |
|--------|-------------|
| `start.sh` | Start all services |
| `stop.sh` | Stop all services (preserves data) |
| `restart.sh` | Restart all or specific service |
| `health.sh` | Health check for all services |
| `logs.sh` | View container logs |
| `clean.sh` | Full data reset (PostgreSQL + uploads + logs) |
| `backup.sh` | Backup PostgreSQL + uploads |
| `restore.sh` | Restore from backup file |

## Deployment

### From GitHub Actions (recommended)

```bash
# On dev machine:
cd /path/to/icao-local-pkd

# Deploy all services (downloads latest artifacts, converts OCI, transfers to luckfox)
bash scripts/deploy/from-github-artifacts.sh all

# Deploy single service
bash scripts/deploy/from-github-artifacts.sh pkd-management
bash scripts/deploy/from-github-artifacts.sh pa-service
bash scripts/deploy/from-github-artifacts.sh pkd-relay
bash scripts/deploy/from-github-artifacts.sh frontend
```

### Copy scripts to luckfox

```bash
# Copy management scripts (rename to luckfox-*.sh at project root)
for f in scripts/luckfox/*.sh; do
  sshpass -p luckfox scp "$f" \
    luckfox@192.168.100.10:/home/luckfox/icao-local-pkd-cpp-v2/luckfox-$(basename "$f")
done

# Set executable
sshpass -p luckfox ssh luckfox@192.168.100.10 \
  'chmod +x /home/luckfox/icao-local-pkd-cpp-v2/luckfox-*.sh'
```

## Usage

```bash
# SSH to luckfox
ssh luckfox@192.168.100.10
cd ~/icao-local-pkd-cpp-v2

# Start / Stop / Restart
./luckfox-start.sh
./luckfox-stop.sh
./luckfox-restart.sh              # all services
./luckfox-restart.sh pkd-management  # specific service

# Health check
./luckfox-health.sh

# Logs
./luckfox-logs.sh                 # all services
./luckfox-logs.sh pkd-relay       # specific service
./luckfox-logs.sh pkd-relay -f    # follow mode

# Clean (full reset)
./luckfox-clean.sh
./luckfox-clean.sh --force        # skip confirmation

# Backup / Restore
./luckfox-backup.sh
./luckfox-restore.sh backups/luckfox_20260212_120000.tar.gz
```

## Services & Ports

| Service | Port | Container | Image |
|---------|------|-----------|-------|
| Frontend | 80 | icao-pkd-frontend | icao-local-pkd-frontend:arm64 |
| API Gateway | 8080 | icao-pkd-api-gateway | nginx:alpine |
| PKD Management | 8081 | icao-pkd-management | icao-local-management:arm64 |
| PA Service | 8082 | icao-pkd-pa-service | icao-local-pa:arm64 |
| PKD Relay | 8083 | icao-pkd-relay | icao-local-pkd-relay:arm64 |
| Swagger UI | 8888 | icao-pkd-swagger | swaggerapi/swagger-ui:latest |
| PostgreSQL | 5432 | icao-pkd-postgres | postgres:15 |

## Docker Images

ARM64 images are built via GitHub Actions (`.github/workflows/build-arm64.yml`):
- `push` to `main` triggers automatic builds (with change detection)
- Manual trigger via `workflow_dispatch`
- vcpkg-base image cached on GHCR (`ghcr.io/iland112/icao-vcpkg-base:arm64`)
- All C++ services built with `ENABLE_ORACLE=OFF` (PostgreSQL-only)

## Troubleshooting

```bash
# Check container status
docker ps -a

# View specific service logs
docker logs icao-pkd-management --tail 100

# Check PostgreSQL
docker exec icao-pkd-postgres pg_isready -U pkd -d localpkd
docker exec icao-pkd-postgres psql -U pkd -d localpkd -c "SELECT COUNT(*) FROM certificate;"

# Check disk space
df -h
docker system df

# Prune unused Docker resources
docker system prune -f
docker image prune -a -f
```
