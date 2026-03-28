# ICAO Local PKD — Deployment Guide

**Version**: v2.41.0 | **Last Updated**: 2026-03-27
> Updated for v2.41.0 서비스 기능 재배치 (Sync↔Upload 교차 이동)

---

## 1. Overview

This document describes deployment processes for all three target environments.

### Environments

| Environment | Host | Runtime | Method |
|-------------|------|---------|--------|
| **Local Development** | WSL2 | Docker | `docker-start.sh`, `scripts/build/rebuild-*.sh` |
| **Production** | 10.0.0.220 (RHEL 9) | Podman | `rsync` + `podman-compose build`, Podman scripts |
| **Luckfox (ARM64)** | 192.168.100.10 | Docker | GitHub Actions CI/CD, OCI artifacts, `from-github-artifacts.sh` |

### Services

| Service | Port | Technology | Notes |
|---------|------|------------|-------|
| PKD Management | :8081 | C++/Drogon | 로컬 PKD 운영/관리 (DB-LDAP Sync, Reconciliation, Search, Auth) |
| PA Service | :8082 | C++/Drogon | Passive Authentication (ICAO 9303) |
| PKD Relay | :8083 | C++/Drogon | 외부 ICAO PKD 연계 (LDIF/ML Import, ICAO Sync, Version Detection) |
| Monitoring Service | :8084 | C++/Drogon | System metrics, DB-independent |
| AI Analysis Service | :8085 | Python/FastAPI | ML anomaly detection |
| EAC Service | :8086 | C++/Drogon | BSI TR-03110 CVC (experimental) |
| Frontend | — | React 19 | Served via nginx |

### Supporting Infrastructure

- **API Gateway**: nginx (ports 80/443/8080)
- **Database**: PostgreSQL 15 (default) or Oracle XE 21c (via `DB_TYPE`)
- **LDAP**: OpenLDAP MMR cluster (2 nodes)
- **Swagger UI**: API documentation

### Source Code Locations

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

## 2. Local Development (WSL2, Docker)

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

## 3. Production (10.0.0.220, RHEL 9, Podman)

Production uses **Podman** (rootless, daemonless) instead of Docker.

### 3.1 Server Hardware & OS

| Item | Value |
|------|-------|
| Hardware | Dell PowerEdge R440 |
| Firmware | 1.6.11 |
| CPU | Intel Xeon Silver 4110 @ 2.10GHz |
| Socket / Core / Thread | 1 socket / 8 cores / 16 threads (HT) |
| Memory | 16 GB (used 2.1GB / available 12GB) |
| Swap | 7.7 GB |
| Disk | 558.4 GB (sda) |
| Architecture | x86_64 |

#### Disk Partitions (LVM)

| Partition | Size | Mount |
|-----------|------|-------|
| sda1 | 1 GB | /boot |
| rhel_friendlyelec-root | 70 GB | / (used 5.4GB, free 65GB) |
| rhel_friendlyelec-swap | 7.7 GB | [SWAP] |
| rhel_friendlyelec-home | 479.7 GB | /home |

#### OS Information

| Item | Value |
|------|-------|
| OS | Red Hat Enterprise Linux 9.7 (Plow) |
| Kernel | 5.14.0-611.5.1.el9_7.x86_64 |
| Machine ID | 4604d25bd0f04c788af3815ec535cada |
| SELinux | enforcing (targeted) |
| Firewall | running (firewalld) |

### 3.2 Network & Firewall

| Item | Value |
|------|-------|
| Hostname | pkd.smartcoreinc.com |
| IP Address | 10.0.0.220/24 (static) |
| Interface | eno1 |
| Gateway | 10.0.0.255 (broadcast) |

#### /etc/hosts

```
127.0.0.1   pkd.smartcoreinc.com pkd localhost localhost.localdomain localhost4 localhost4.localdomain4
::1         localhost localhost.localdomain localhost6 localhost6.localdomain6
```

#### Hostname Change

```bash
sudo hostnamectl set-hostname pkd.smartcoreinc.com
```

- Before: `FriendlyELEC.smartcore.kr`
- After: `pkd.smartcoreinc.com`
- `/etc/hosts` updated: `127.0.0.1 pkd.smartcoreinc.com pkd localhost ...`

#### Red Hat Subscription Manager

```bash
sudo subscription-manager register --username=kbjung112 --password='****'
```

**Registration Result**:

| Item | Value |
|------|-------|
| System ID | `541a236f-c759-41c3-a911-527a0655ac22` |
| System Name | pkd.smartcoreinc.com |
| Organization | 20252037 |
| Registration Server | subscription.rhsm.redhat.com:443/subscription |
| Content Access | Simple Content Access (SCA) |

> **SCA (Simple Content Access)** mode: no individual subscription attachment required, `dnf` packages available immediately

#### Registration Verification

```bash
sudo subscription-manager status
sudo subscription-manager identity
sudo subscription-manager repos --list-enabled
```

#### Firewall Configuration

```bash
sudo firewall-cmd --permanent --add-service=http    # 80/tcp
sudo firewall-cmd --permanent --add-service=https   # 443/tcp
sudo firewall-cmd --permanent --add-port=8080/tcp   # API Gateway (internal)
sudo firewall-cmd --reload
```

Open ports: `http`, `https`, `ssh`, `cockpit`, `8080/tcp`

### 3.3 SSH Access

| Item | Value |
|------|-------|
| SSH | `ssh scpkd@10.0.0.220` |
| Account | scpkd |
| Password | core |
| sudo | available (password: core) |

### 3.4 Podman Setup

```bash
# Podman is included by default in RHEL 9
sudo dnf install -y podman

# podman-compose (pip install — dnf version is outdated)
pip3 install --user podman-compose
# Ensure ~/.local/bin is in PATH

# CNI DNS plugin (required for container hostname resolution)
sudo dnf install -y podman-plugins

# Allow rootless binding of ports 80/443
echo 'net.ipv4.ip_unprivileged_port_start=80' | sudo tee -a /etc/sysctl.conf
sudo sysctl -w net.ipv4.ip_unprivileged_port_start=80

# Verify versions
podman --version          # 5.6.0+
podman-compose --version  # 1.5.0+
```

#### Installed Package Versions

| Package | Version | Notes |
|---------|------|------|
| Podman | 5.6.0 | Included in RHEL 9 |
| podman-compose | 1.5.0 | pip3 install |
| podman-plugins | 5.6.0-14 | dnsname CNI plugin |
| aardvark-dns | 1.16.0 | Installed (unused, CNI backend) |
| netavark | 1.16.0 | Installed (unused, CNI backend) |

> **Network backend**: CNI (not netavark). `podman-plugins` dnsname plugin handles DNS resolution.

### 3.5 Docker to Podman Migration

```bash
# 1. Stop Docker containers
sudo docker compose -f docker/docker-compose.yaml down

# 2. Copy Docker images to Podman (skopeo)
for img in docker-pkd-management docker-pa-service docker-pkd-relay \
           docker-frontend docker-monitoring-service docker-ai-analysis; do
    sudo skopeo copy docker-daemon:$img:latest \
         containers-storage:docker.io/library/$img:latest
done

# 3. Remove Docker CE
sudo systemctl stop docker
sudo dnf remove -y docker-ce docker-ce-cli containerd.io \
    docker-buildx-plugin docker-compose-plugin
sudo rm -rf /var/lib/docker /var/lib/containerd
```

#### Key Differences from Docker

| Item | Docker (Local) | Podman (Production) | Notes |
|------|---------------|---------------------|-------|
| Daemon | dockerd (root) | Daemonless (rootless) | Security improvement |
| CLI | `docker` | `podman` | Compatible |
| Compose | `docker compose` | `podman-compose` | Separate install |
| DNS | `127.0.0.11` (built-in) | dnsname CNI plugin | `podman-plugins` required |
| `depends_on` condition | `service_healthy` | Script-based wait | **Script change** |
| Init containers | `service_completed_successfully` | Script-based | **Script change** |
| Rootless | Supported | **Default** | SELinux MCS note |
| SELinux volumes | `:Z` auto-label | `chcon` pre-labeling | `:Z` not usable |
| Privileged ports | root daemon | sysctl required | `ip_unprivileged_port_start=80` |
| Network names | `{project}_{network}` | Same | project = compose file directory name |

#### Compose File Comparison

**Docker (`docker-compose.yaml`)**:
```yaml
depends_on:
  openldap1:
    condition: service_healthy
  openldap2:
    condition: service_healthy
```

**Podman (`docker-compose.podman.yaml`)**:
```yaml
depends_on:
  - openldap1
  - openldap2
```

> Podman version removes `condition:`. Service readiness is ensured by script wait logic.

#### nginx DNS Resolution

Docker uses built-in DNS (127.0.0.11), but Podman CNI uses the dnsname plugin.
`scripts/podman/start.sh` automatically:
1. Detects the gateway IP of the Podman network (`docker_pkd-network`)
2. Copies the Docker nginx config + replaces the resolver IP (e.g., `resolver 10.89.1.1`)
3. Mounts the generated config at `.docker-data/nginx/api-gateway.conf`

> **Note**: nginx upstream blocks resolve DNS only once at startup. After network recreation, the api-gateway container must also be restarted.

### 3.6 SELinux + Rootless Podman

RHEL 9 SELinux Enforcing mode with rootless Podman requires special handling:

1. **`:Z`/`:z` volume labels not usable** — rootless Podman lacks `CAP_MAC_ADMIN`, causing `lsetxattr` call failure
2. **MCS category mismatch** — each rootless container runs with a unique MCS label (e.g., `s0:c737,c1010`), mismatching host file MCS categories
3. **Solution**: Pre-`chcon` labeling (2 steps)
   ```bash
   # Step 1: Set SELinux type to container_file_t
   chcon -Rt container_file_t .docker-data/ docker/db-oracle/init data/cert nginx/ docs/openapi
   # Step 2: Remove MCS categories (leave only s0 — allows access from all containers)
   chcon -R -l s0 .docker-data/ docker/db-oracle/init data/cert nginx/ docs/openapi
   ```
4. **OpenLDAP data directories** — files created by rootless containers can only be deleted on host with `podman unshare rm`

### 3.7 .env Configuration

Project deployment on server:

```bash
sudo dnf install -y git
cd /home/scpkd
git clone https://github.com/iland112/icao-local-pkd-cpp.git icao-local-pkd
cd icao-local-pkd
git checkout main
```

#### .env File (`/home/scpkd/icao-local-pkd/.env`)

```
DB_TYPE=oracle
ORACLE_USER=pkd_user
ORACLE_PASSWORD=pkd_password
LDAP_READ_HOSTS=openldap1:389,openldap2:389
LDAP_WRITE_HOST=openldap1
LDAP_WRITE_PORT=389
LDAP_BIND_DN=cn=admin,dc=ldap,dc=smartcoreinc,dc=com
LDAP_BIND_PASSWORD=ldap_test_password_123
LDAP_BASE_DN=dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
POSTGRES_USER=pkd
POSTGRES_PASSWORD=pkd_test_password_123
LDAP_ADMIN_PASSWORD=ldap_test_password_123
LDAP_CONFIG_PASSWORD=config_test_123
JWT_SECRET_KEY=ab19525f...
JWT_ISSUER=icao-pkd
JWT_EXPIRATION_SECONDS=3600
AUTH_ENABLED=true
NGINX_CONF=../nginx/api-gateway-ssl.conf
```

> `.env` must also be copied to `docker/` directory (docker compose working directory issue)

### 3.8 SSL Certificates

Private CA-based self-signed certificate generation:

```bash
cd /home/scpkd/icao-local-pkd
mkdir -p .docker-data/ssl
# CA (RSA 4096, 10 years) + Server cert (RSA 2048, 1 year)
# SAN: pkd.smartcoreinc.com, localhost, 127.0.0.1, 10.0.0.220

# Generate certificates
scripts/ssl/init-cert.sh --ip 10.0.0.220
```

| File | Purpose |
|------|------|
| `.docker-data/ssl/ca.key` | CA private key (keep secret) |
| `.docker-data/ssl/ca.crt` | CA certificate (distribute to clients) |
| `.docker-data/ssl/server.key` | Server private key |
| `.docker-data/ssl/server.crt` | Server certificate (CA-signed) |

- CA validity: 2026-02-27 ~ 2036-02-25
- Server validity: 2026-02-27 ~ 2027-02-27
- Renewal: `scripts/ssl/renew-cert.sh`

### 3.9 System Startup (Podman)

```bash
cd /home/scpkd/icao-local-pkd

# First install: complete initialization (Oracle + LDAP DIT + services)
./podman-clean-and-init.sh

# Normal start
./podman-start.sh

# Health check
./podman-health.sh
```

#### Container Status (after Podman migration)

| Container | Status | Runtime |
|----------|------|--------|
| icao-local-pkd-api-gateway | healthy | Podman |
| icao-local-pkd-management | healthy | Podman |
| icao-local-pkd-pa-service | healthy | Podman |
| icao-local-pkd-relay | healthy | Podman |
| icao-local-pkd-monitoring | healthy | Podman |
| icao-local-pkd-ai-analysis | healthy | Podman |
| icao-local-pkd-frontend | Up | Podman |
| icao-local-pkd-oracle | healthy | Podman |
| icao-local-pkd-openldap1 | healthy | Podman |
| icao-local-pkd-openldap2 | healthy | Podman |
| icao-local-pkd-swagger | healthy | Podman |

### 3.10 Deployment Procedure

#### Deployment Pipeline

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

### 3.11 LDAP Initialization

#### Docker: Init Container Approach

```yaml
# Defined in docker-compose.yaml
ldap-mmr-setup1:
  depends_on:
    openldap1:
      condition: service_healthy
  ...
  restart: "no"

ldap-init:
  depends_on:
    ldap-mmr-setup1:
      condition: service_completed_successfully
  ...
```

#### Podman: Script Approach

```bash
# clean-and-init.sh Step 6
# 1. Wait for OpenLDAP readiness (podman exec ldapsearch)
# 2. Configure MMR (podman exec ldapmodify — both nodes)
# 3. Initialize DIT (podman exec — init-pkd-dit.sh execution)
# 4. Verify (ldapsearch to check entries)
```

### 3.12 Client PC HTTPS Setup

Since certificates use a Private CA, the CA certificate must be trusted on client PCs.

#### Automated Setup Script

Copy the two files from `scripts/client/` to the client PC and run:

| File | Purpose |
|------|------|
| `setup-pkd-access.bat` | Double-click to run (auto-requests admin privileges) |
| `setup-pkd-access.ps1` | Actual setup script |

**Usage**: Copy both files to the same folder -> double-click `setup-pkd-access.bat` -> UAC "Yes"

**Script actions**:
1. Add `10.0.0.220 pkd.smartcoreinc.com` to `hosts` file
2. Delete all existing "ICAO Local PKD Private CA" certificates (Subject CN-based)
3. Register new Private CA certificate in Windows Trusted Root Certification Authorities
4. Flush DNS cache
5. Test connection to `https://pkd.smartcoreinc.com/health`

> **Note**: When CA certificate is reissued (`init-cert.sh --force`) or server IP changes, re-running the script will automatically delete old certificates and register new ones. (v2.24.1)

> **Note**: Firefox does not use the Windows certificate store and requires separate certificate import (Settings -> Privacy & Security -> Certificates -> View Certificates -> Authorities -> Import)

#### Manual Setup

1. `hosts` file (`C:\Windows\System32\drivers\etc\hosts`):
   ```
   10.0.0.220    pkd.smartcoreinc.com
   ```

2. CA certificate registration: Double-click `.docker-data/ssl/ca.crt` -> "Install Certificate" -> "Local Machine" -> "Trusted Root Certification Authorities"

### 3.13 Production Environment Summary

| Item | Value |
|------|-------|
| OS | RHEL 9.7 (SELinux Enforcing) |
| Podman | 5.6.0 (rootless) |
| podman-compose | 1.5.0 (pip) |
| Database | Oracle XE 21c |
| Network | CNI + dnsname plugin |
| Network name | docker_pkd-network |
| Compose file | `docker/docker-compose.podman.yaml` |

### 3.14 Service Names (Compose)

When specifying services in `logs.sh`, `restart.sh`, use compose file service names:

| Service Name | Container Name |
|---------|-----------|
| `pkd-management` | `icao-local-pkd-management` |
| `pa-service` | `icao-local-pkd-pa-service` |
| `pkd-relay` | `icao-local-pkd-relay` |
| `api-gateway` | `icao-local-pkd-api-gateway` |
| `monitoring-service` | `icao-local-pkd-monitoring` |
| `ai-analysis` | `icao-local-pkd-ai-analysis` |
| `frontend` | `icao-local-pkd-frontend` |
| `openldap1` / `openldap2` | `icao-local-pkd-openldap1` / `openldap2` |
| `swagger-ui` | `icao-local-pkd-swagger` |

### 3.15 --profile Flag

`stop.sh`, `start.sh`, `restart.sh` read `DB_TYPE` from `.env` and automatically add `--profile oracle` or `--profile postgres`. Running `podman-compose down` without the profile flag will not stop the DB container, so always use the scripts.

### 3.16 Access URLs (Production)

| Service | URL |
|---------|-----|
| Frontend (HTTPS) | https://pkd.smartcoreinc.com |
| Frontend (HTTP) | http://pkd.smartcoreinc.com |
| API Gateway (HTTPS) | https://pkd.smartcoreinc.com/api |
| API Gateway (HTTP) | http://pkd.smartcoreinc.com/api |
| API Gateway (internal) | http://localhost:18080/api |
| Frontend (direct) | http://localhost:13080 |
| Swagger UI | http://localhost:18090 |
| Oracle | localhost:11521 (XEPDB1) |
| OpenLDAP 1 | ldap://localhost:13891 |
| OpenLDAP 2 | ldap://localhost:13892 |

### 3.17 Production Setup Checklist

- [x] Hostname change (`pkd.smartcoreinc.com`)
- [x] `/etc/hosts` update
- [x] Red Hat Developer account registration (SCA active)
- [x] Docker CE 29.2.1 install -> **Migrated to Podman 5.6.0**
- [x] Docker CE removal, image skopeo migration
- [x] podman-compose 1.5.0 install (pip)
- [x] podman-plugins install (CNI dnsname DNS)
- [x] sysctl privileged port allow (80/443)
- [x] Firewall port setup (80, 443, 8080)
- [x] Project source deployment (git clone main)
- [x] SSL certificate generation (Private CA, SAN: 10.0.0.220)
- [x] All services started (11 containers, Podman rootless)
- [x] SELinux MCS labeling resolved
- [x] API health check verified (HTTP + HTTPS)
- [x] Client HTTPS setup script (`scripts/client/`)
- [ ] Data upload (LDIF/Master List)
- [ ] DNS configuration (pkd.smartcoreinc.com -> 10.0.0.220)

---

## 4. Luckfox (ARM64)

ARM64 Docker images are built via GitHub Actions CI/CD and deployed to Luckfox device.
All services use `network_mode: host` (luckfox kernel lacks iptables DNAT).

### 4.1 Architecture

```
192.168.100.10: OpenLDAP1 (:389) - Master Node (currently the only active node)
192.168.100.11: OpenLDAP2 (:389) + Docker Apps - Luckfox (hardware failure, stopped)
```

> **Current Status (2026-03-18)**: `192.168.100.11` (Luckfox) stopped due to hardware failure.
> OpenLDAP MMR replication is operating on `192.168.100.10` single node.
> Docker service deployment target also needs to be changed to `192.168.100.10`.

### 4.2 Hardware Specs & Services

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

### 4.3 Prerequisites

#### Local Machine

```bash
# Required tools
sudo apt-get install sshpass skopeo gh  # Debian/Ubuntu

# Verify
command -v sshpass && command -v skopeo && command -v gh && echo "All OK"
```

#### Luckfox

- Docker installed and running
- ~~SSH access: `luckfox@192.168.100.11` (password: luckfox)~~ -- **Hardware failure, unavailable**
- Currently active node: `luckfox@192.168.100.10` (password: luckfox)
- Project directory: `/home/luckfox/icao-local-pkd-cpp-v2`

### 4.4 GitHub Actions CI/CD Pipeline

#### Workflow

File: `.github/workflows/build-arm64.yml`

```
Push to main -> Detect Changes -> Build vcpkg-base (GHCR) -> Build Services -> Upload Artifacts
```

**Change detection**: Only builds services with actual file changes (dorny/paths-filter).

**Triggers**:
- Push to `main` branch (paths: services/, shared/, frontend/, nginx/, docker/)
- Manual dispatch with `force_build_all` and `no_cache` options

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

#### Build Artifacts

| Artifact | Image Tag |
|----------|-----------|
| pkd-management-arm64.tar.gz | icao-local-management:arm64 |
| pkd-pa-arm64.tar.gz | icao-local-pa:arm64 |
| pkd-relay-arm64.tar.gz | icao-local-pkd-relay:arm64 |
| monitoring-service-arm64.tar.gz | icao-local-monitoring:arm64 |
| pkd-frontend-arm64.tar.gz | icao-local-pkd-frontend:arm64 |

### 4.5 Artifact Download

```bash
# List runs to find the latest
gh run list --limit 3

# Download specific service artifact
gh run download <run-id> --name pkd-frontend-arm64

# Or download all artifacts
gh run download <run-id>
```

**Important**: Delete old artifact directories before downloading to avoid deploying stale images.

### 4.6 Deployment

#### Automated Deployment

```bash
# Deploy specific service
bash scripts/deploy/from-github-artifacts.sh pkd-management

# Deploy all services
bash scripts/deploy/from-github-artifacts.sh all

# Available targets: pkd-management | pa-service | pkd-relay | monitoring-service | frontend | all
```

**What it does:**
1. Downloads artifacts from latest GitHub Actions run (via `gh` CLI)
2. Converts OCI format -> Docker archive (via `skopeo`)
3. Stops old container on Luckfox
4. Transfers Docker archive via SCP
5. Loads image and starts service via `docker compose`
6. Verifies health check

#### Manual Deployment

```bash
# 1. Download artifact
gh run download <RUN_ID> -n monitoring-service-arm64 --dir github-artifacts

# 2. Convert OCI -> Docker
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

#### Config Update (without rebuild)

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

### 4.7 Luckfox Management Scripts

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

### 4.8 Key Configuration Files

| File | Purpose |
|------|---------|
| `docker-compose-luckfox.yaml` | Service definitions (host network, 127.0.0.1 for DB) |
| `nginx/api-gateway-luckfox.conf` | API routing (8080 -> backend services) |
| `frontend/nginx-luckfox.conf` | Frontend nginx (port 80, proxy to 8080) |
| `docker/init-scripts/` | PostgreSQL schema initialization |

#### API Gateway Routes

| Route | Backend |
|-------|---------|
| `/api/certificates`, `/api/progress`, `/api/sync/*` | PKD Management (:8081) |
| `/api/auth`, `/api/audit`, `/api/health` | PKD Management (:8081) |
| `/api/pa/*` | PA Service (:8082) |
| `/api/upload`, `/api/icao` | PKD Relay (:8083) |
| `/api/monitoring/*` | Monitoring (:8084) |
| `/api-docs/`, `/api/docs/` | Swagger UI (:8888) |

### 4.9 Image Name Mapping

| CI Artifact | Docker Image on Luckfox | Compose Service |
|-------------|------------------------|-----------------|
| `pkd-management-arm64` | `icao-pkd-management:arm64` | pkd-management |
| `pa-service-arm64` | `icao-pkd-pa-service:arm64` | pa-service |
| `pkd-relay-arm64` | `icao-pkd-relay:arm64` | pkd-relay |
| `monitoring-service-arm64` | `icao-pkd-monitoring:arm64` | monitoring |
| `pkd-frontend-arm64` | `icao-pkd-frontend:arm64` | frontend |

### 4.10 Luckfox Verification

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

### 4.11 Build & Deployment Times

#### Build Times

| Scenario | Time | Notes |
|----------|------|-------|
| First build (cold cache) | 60-80 min | One-time vcpkg compilation |
| vcpkg dependency change | 30-40 min | Rebuild dependencies |
| Source code change only | 10-15 min | vcpkg-base from GHCR |
| No changes (cache hit) | ~5 min | |

#### Deployment Times

| Operation | Time |
|-----------|------|
| Artifact download | 1-2 min |
| OCI -> Docker conversion | 10-20 sec |
| Transfer to luckfox | 30-60 sec |
| Docker load + start | 10-20 sec |
| **Total per service** | **~3 min** |

### 4.12 Default Credentials

- Admin login: `admin` / `admin123`
- PostgreSQL: `pkd` / `pkd`
- SSH: `luckfox` / `luckfox`

### 4.13 Luckfox Change Log

#### 2026-02-23: v3.1 - Hardware Failure Status Update
- `192.168.100.11` (Luckfox) hardware failure, operation stopped
- Only `192.168.100.10` single node available
- OpenLDAP MMR replication single node operation status reflected
- Deployment target node information updated

#### 2026-02-13: v3.0 - Full Pipeline + Monitoring Service
- GitHub Actions CI/CD with change detection (dorny/paths-filter)
- vcpkg-base image pushed to GHCR (shared by all C++ services)
- Monitoring service added to pipeline (DB-independent)
- All 8 services deployed and verified on luckfox
- Management scripts modernized (project root auto-detection)
- API gateway: added auth/audit/icao/monitoring routes
- Fixed PA health endpoint URL in monitoring config

#### 2026-01-09: v2.0 - Automated OCI Deployment
- OCI -> Docker conversion via skopeo
- sshpass for automated SSH/SCP
- Automated artifact download via gh CLI
- Single-command deployment per service

---

## 5. Build Procedures

### 5.1 Build Verification SOP

**Purpose**: Prevent build verification mistakes and ensure accurate deployments.

#### Critical Lessons Learned

**Problem**: Docker build caching can reuse old build layers even when source code changes, resulting in:
- Version string updated but binary containing old code
- Features not working despite "successful" build
- Wasted time rebuilding after failed deployments

#### Step 1: Code Modification

1. Make source code changes
2. **ALWAYS** update the version string in `services/<service>/src/main.cpp`

#### Step 2: Build

**Quick rebuild (development):**
```bash
# Using rebuild scripts
./scripts/build/rebuild-pkd-relay.sh
./scripts/build/rebuild-frontend.sh

# Or via docker compose
docker compose -f docker/docker-compose.yaml build <service-name>
docker compose -f docker/docker-compose.yaml up -d <service-name>
```

**Force fresh build (when cache issues suspected):**
```bash
./scripts/build/rebuild-pkd-relay.sh --no-cache

# Or directly
docker compose -f docker/docker-compose.yaml build --no-cache <service-name>
```

#### Step 3: Verify Build BEFORE Deployment

```bash
# Check container is running
docker compose -f docker/docker-compose.yaml ps <service-name>

# Check version in logs
docker logs icao-local-pkd-<service> --tail 10 | grep -i version

# Check image creation time (should be recent)
docker inspect icao-local-pkd-<service> --format='{{.Created}}'
```

#### Step 4: Test Functionality

After deployment, **always** test the specific feature you modified. Don't rely solely on version strings.

#### Service-Specific Build Commands

| Service | Rebuild Script | Container Name |
|---------|---------------|----------------|
| PKD Management | `./scripts/build/rebuild-pkd-relay.sh` (generic) | `icao-local-pkd-management` |
| PA Service | `docker compose build pa-service` | `icao-local-pkd-pa-service` |
| PKD Relay | `./scripts/build/rebuild-pkd-relay.sh` | `icao-local-pkd-relay` |
| Monitoring | `docker compose build monitoring` | `icao-local-pkd-monitoring` |
| Frontend | `./scripts/build/rebuild-frontend.sh` | `icao-local-pkd-frontend` |

### 5.2 When to Use --no-cache

| Scenario | Cache OK? | Notes |
|----------|-----------|-------|
| Minor code change | Yes | Docker cache usually works |
| CMakeLists.txt change | **No** | Must rebuild |
| vcpkg.json change | **No** | Must rebuild dependencies |
| New shared library | **No** | Must rebuild |
| Dockerfile change | **No** | Must rebuild |
| Version mismatch | **No** | Force fresh build |
| Config-only changes | Yes | nginx.conf, docker-compose |
| Frontend changes | Yes | Vite build is fast |
| Final pre-deployment verification | **No** | Always verify clean |

### 5.3 Docker Build Cache Issues

For detailed information on build cache troubleshooting, see [DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md).

**Key reminders**:
1. **Version changes alone don't guarantee new code is compiled**
2. **Docker cache is aggressive** — it will reuse layers when possible
3. **Always verify builds** before claiming they're complete
4. **Test functionality** after deployment, not just version strings
5. **When in doubt, --no-cache**

Update version string in `main.cpp` or use `CACHE_BUST` build arg to bust cache.

---

## 6. Troubleshooting

### 6.1 Production Podman Issues

#### nginx 502 Bad Gateway / Service Unavailable

```bash
# 1. Check DNS resolver
grep resolver .docker-data/nginx/api-gateway.conf
# -> resolver 10.89.x.x valid=10s ipv6=off;

# 2. Check DNS resolution within container
podman exec icao-local-pkd-api-gateway getent hosts pkd-management
# -> 10.89.1.xxx  pkd-management.dns.podman

# 3. If nginx upstream is using cached old IP, restart
podman restart icao-local-pkd-api-gateway

# 4. Full restart (DNS re-detection)
./podman-stop.sh && ./podman-start.sh
```

#### DNS Resolution Failure (TNS:no listener, Host is unreachable)

```bash
# Check podman-plugins installed
rpm -q podman-plugins
# -> If not installed: sudo dnf install -y podman-plugins

# Check network DNS enabled
podman network inspect docker_pkd-network | python3 -c \
  "import sys,json; print('dns_enabled:', json.load(sys.stdin)[0]['dns_enabled'])"
# -> dns_enabled: True is normal

# If DNS disabled: recreate network
podman-compose -f docker/docker-compose.podman.yaml down
podman network rm docker_pkd-network
podman network create docker_pkd-network  # DNS auto-enabled after podman-plugins install
```

#### Oracle Startup Timeout / ORA-01017

```bash
# Oracle takes 3-5 minutes on first run
podman logs -f icao-local-pkd-oracle

# Manual health check
podman inspect icao-local-pkd-oracle --format='{{.State.Health.Status}}'

# ORA-01017 (invalid username/password) — init script not executed
# SELinux MCS label issue may prevent reading /opt/oracle/scripts/startup/
podman exec icao-local-pkd-oracle ls /opt/oracle/scripts/startup/
# -> Permission denied: fix MCS labels on host
chcon -R -l s0 docker/db-oracle/init/

# Manual init script execution
podman exec icao-local-pkd-oracle bash -c \
  'cd /opt/oracle/scripts/startup && for f in *.sql; do echo "@$f" | sqlplus -s sys/"$ORACLE_PWD"@//localhost:1521/XEPDB1 as sysdba; done'
```

#### SELinux Permission Errors (RHEL 9)

```bash
# Symptoms: Permission denied, lsetxattr operation not permitted

# Rootless Podman cannot use :Z/:z volume labels (no CAP_MAC_ADMIN)
# Use pre-labeling instead:
chcon -Rt container_file_t .docker-data/ docker/db-oracle/init data/cert nginx/ docs/openapi
chcon -R -l s0 .docker-data/ docker/db-oracle/init data/cert nginx/ docs/openapi

# Deleting OpenLDAP data (files created by rootless containers)
podman unshare rm -rf .docker-data/openldap1 .docker-data/openldap2
```

#### Privileged Port Binding (80/443)

```bash
# Symptom: rootlessport cannot expose privileged port 80
# Fix:
sudo sysctl -w net.ipv4.ip_unprivileged_port_start=80
echo 'net.ipv4.ip_unprivileged_port_start=80' | sudo tee -a /etc/sysctl.conf
```

#### Container Name Conflicts

```bash
# Clean up existing containers
podman ps -a --filter "name=icao-local-pkd" --format "{{.Names}}" | xargs -r podman rm -f
```

#### LDAP Invalid credentials (49)

- **Cause**: OpenLDAP data was initialized with a different password
- **Fix**: Delete `.docker-data/openldap1`, `.docker-data/openldap2` and restart

#### Unable to access log path! -> Container exits immediately

- **Cause**: `.docker-data/*-logs` directory owner is root (created by docker)
- **Fix**: `chmod 777 .docker-data/*-logs`

#### stoi / Pydantic int_parsing error

- **Cause**: Empty environment variable like `ORACLE_PORT=` fails int conversion
- **Fix**: Fixed in v2.23.1 (C++ empty string check + Python default value return)

### 6.2 Luckfox Issues

#### OCI format error: "does not contain a manifest.json"

GitHub Actions outputs OCI format. Convert with skopeo before `docker load`:
```bash
skopeo copy --override-arch arm64 \
    oci:/tmp/oci-dir \
    docker-archive:/tmp/service-docker.tar:image-name:arm64
```

#### Service shows DOWN in monitoring

Check the health endpoint URL in `docker-compose-luckfox.yaml`. PA Service health is `/api/health` (not `/api/pa/health`).

#### 404 through API Gateway

Check `nginx/api-gateway-luckfox.conf` has the route. Reload nginx after config changes.

#### Login fails with 404

Ensure `/api/auth` route exists in API gateway config.

#### CORS errors on Luckfox

Frontend must use relative API URLs (`/api/...`) not hardcoded `http://localhost:8080/api/...`.
The Luckfox nginx config proxies `/api` to `127.0.0.1:8080`.

#### Old version deployed despite new build

1. **Delete old artifacts**: `rm -rf github-artifacts/ pkd-*-arm64/`
2. **Download fresh**: `gh run download <latest-run-id> --name <service>-arm64`
3. **Verify timestamp**: `ls -lh pkd-frontend-arm64/`
4. **Redeploy**: `scripts/deploy/from-github-artifacts.sh frontend`

### 6.3 Common Build Issues

#### Build cache prevents code changes

Update version string in `main.cpp` or use `CACHE_BUST` build arg.
See [DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md) for details.

#### Function not found in binary

**Cause**: Docker used cached build layers
**Fix**: Rebuild with `--no-cache`, verify again

#### Image is old despite rebuild

**Cause**: Docker used cached final stage
**Fix**: Check build output for "Using cache" messages, rebuild with `--no-cache`

#### Version in logs doesn't match

**Cause**: Running old container, not the newly built image
**Fix**: `docker compose up -d --force-recreate <service-name>`

---

## 7. Script Reference

### Script Structure

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

### Podman Scripts

```
scripts/podman/
+-- start.sh          # Start containers (auto nginx DNS config, --profile auto)
+-- stop.sh           # Stop containers (--profile auto, fallback: podman rm -f)
+-- restart.sh        # Restart: full (stop+start) or single service (compose restart)
+-- health.sh         # Full health check (API Gateway port 80 first)
+-- logs.sh           # View logs [service] [lines]
+-- clean-and-init.sh # Complete init (data delete + LDAP DIT + start services)
+-- backup.sh         # Data backup (Oracle Data Pump + LDAP + SSL)
+-- restore.sh        # Data restore
```

### Luckfox Scripts

| Script | Purpose |
|--------|---------|
| `scripts/luckfox/start.sh` | Start all services, create data directories |
| `scripts/luckfox/stop.sh` | Stop all services |
| `scripts/luckfox/restart.sh` | Restart all or specific service |
| `scripts/luckfox/health.sh` | Health check all services, DB stats |
| `scripts/luckfox/logs.sh` | View service logs (supports -f follow) |
| `scripts/luckfox/clean.sh` | Full data reset (--force for non-interactive) |
| `scripts/luckfox/backup.sh` | Backup configs, logs, image info |
| `scripts/luckfox/restore.sh` | Restore from backup |

### Build Scripts

| Script | Purpose |
|--------|---------|
| `scripts/build/rebuild-pkd-relay.sh` | Rebuild PKD Relay service (supports --no-cache) |
| `scripts/build/rebuild-pkd-management.sh` | Rebuild PKD Management service |
| `scripts/build/rebuild-frontend.sh` | Rebuild Frontend |

### Deployment Scripts

| Script | Purpose |
|--------|---------|
| `scripts/deploy/from-github-artifacts.sh` | Deploy ARM64 artifacts to Luckfox |

### SSL Scripts

| Script | Purpose |
|--------|---------|
| `scripts/ssl/init-cert.sh` | Generate CA + server certificates (--ip flag for SAN) |
| `scripts/ssl/renew-cert.sh` | Renew server certificate using existing CA |

### Client Scripts

| Script | Purpose |
|--------|---------|
| `scripts/client/setup-pkd-access.bat` | Windows client setup launcher |
| `scripts/client/setup-pkd-access.ps1` | Windows client HTTPS + hosts setup |

---

## References

- [DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md) - Build cache troubleshooting
- [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md) - Development guide (credentials, workflow)

---

## Version History

- v2.37.0 (2026-03-18): Merged 5 deployment documents into single comprehensive guide
- v1.2.0 (2026-02-27): Podman script stability improvements
- v1.1.0 (2026-02-27): Docker->Podman migration complete, SELinux MCS resolved
- v1.0.0 (2026-02-27): Initial Podman support
