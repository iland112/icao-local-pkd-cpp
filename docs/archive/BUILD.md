# ICAO Local PKD - Build & Deployment Guide

## Overview

이 문서는 ICAO Local PKD 시스템의 빌드, 배포, 운영 전 과정을 설명합니다.

---

## 1. Prerequisites

### Development Environment

| Tool | Version | Purpose |
|------|---------|---------|
| Docker | 24.0+ | Container runtime |
| Docker Compose | 2.20+ | Multi-container orchestration |
| Node.js | 20+ | Frontend build |
| Git | 2.40+ | Version control |

### Target Environments

| Environment | Architecture | IP | Docker Compose |
|-------------|--------------|-----|----------------|
| Local (Dev) | AMD64 | localhost | docker-compose.yaml |
| Luckfox (Prod) | ARM64 | 192.168.100.11 | docker-compose-luckfox.yaml |

---

## 2. Docker Image Build

### 2.1 AMD64 Build (Local Development)

```bash
# 전체 서비스 빌드
cd /path/to/icao-local-pkd
docker compose -f docker/docker-compose.yaml build

# 개별 서비스 빌드
docker compose -f docker/docker-compose.yaml build pkd-management
docker compose -f docker/docker-compose.yaml build pa-service
docker compose -f docker/docker-compose.yaml build sync-service
docker compose -f docker/docker-compose.yaml build frontend
```

### 2.2 ARM64 Cross-Platform Build (for Luckfox)

AMD64 호스트에서 ARM64 이미지를 빌드하려면 buildx를 사용합니다.

```bash
# Docker buildx 설정 (최초 1회)
docker buildx create --use --name multiarch

# PKD Management Service
cd services/pkd-management
docker buildx build --platform linux/arm64 --load -t icao-pkd-management:arm64 .

# PA Service
cd services/pa-service
docker buildx build --platform linux/arm64 --load -t icao-pa-service:arm64 .

# Sync Service
cd services/sync-service
docker buildx build --platform linux/arm64 --load -t icao-sync-service:arm64 .

# Frontend
cd frontend
docker build --platform linux/arm64 --no-cache -t icao-frontend:arm64 .
```

### 2.3 Image Export & Transfer

```bash
# 이미지를 tar.gz로 저장
docker save icao-pkd-management:arm64 | gzip > icao-pkd-management-arm64.tar.gz
docker save icao-pa-service:arm64 | gzip > icao-pa-service-arm64.tar.gz
docker save icao-sync-service:arm64 | gzip > icao-sync-service-arm64.tar.gz
docker save icao-frontend:arm64 | gzip > icao-frontend-arm64.tar.gz

# Luckfox로 전송
scp icao-*-arm64.tar.gz luckfox@192.168.100.11:/home/luckfox/
```

---

## 3. Deployment

### 3.1 Local Environment (AMD64)

```bash
# 전체 서비스 시작
./docker-start.sh

# 또는 수동으로
docker compose -f docker/docker-compose.yaml up -d

# 서비스 중지
./docker-stop.sh
```

### 3.2 Luckfox Environment (ARM64)

#### 3.2.1 Initial Setup

```bash
# SSH 접속
ssh luckfox@192.168.100.11

# 작업 디렉토리 생성
mkdir -p /home/luckfox/icao-local-pkd-cpp-v2

# 프로젝트 파일 복사 (개발 PC에서)
scp docker-compose-luckfox.yaml luckfox@192.168.100.11:/home/luckfox/icao-local-pkd-cpp-v2/
scp -r openldap luckfox@192.168.100.11:/home/luckfox/icao-local-pkd-cpp-v2/
scp -r data luckfox@192.168.100.11:/home/luckfox/icao-local-pkd-cpp-v2/
```

#### 3.2.2 Load Docker Images

```bash
# Luckfox에서 이미지 로드
cd /home/luckfox
docker load < icao-pkd-management-arm64.tar.gz
docker load < icao-pa-service-arm64.tar.gz
docker load < icao-sync-service-arm64.tar.gz
docker load < icao-frontend-arm64.tar.gz
```

#### 3.2.3 Start Services

```bash
cd /home/luckfox/icao-local-pkd-cpp-v2
docker compose -f docker-compose-luckfox.yaml up -d
```

#### 3.2.4 Database Initialization

sync_status 테이블이 없는 경우 수동 생성:

```bash
# PostgreSQL 접속
docker exec -it icao-pkd-postgres psql -U pkd -d localpkd

# 테이블 생성
CREATE TABLE sync_status (
    id SERIAL PRIMARY KEY,
    checked_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    db_csca_count INTEGER NOT NULL DEFAULT 0,
    db_dsc_count INTEGER NOT NULL DEFAULT 0,
    db_dsc_nc_count INTEGER NOT NULL DEFAULT 0,
    db_crl_count INTEGER NOT NULL DEFAULT 0,
    db_stored_in_ldap_count INTEGER NOT NULL DEFAULT 0,
    ldap_csca_count INTEGER NOT NULL DEFAULT 0,
    ldap_dsc_count INTEGER NOT NULL DEFAULT 0,
    ldap_dsc_nc_count INTEGER NOT NULL DEFAULT 0,
    ldap_crl_count INTEGER NOT NULL DEFAULT 0,
    ldap_total_entries INTEGER NOT NULL DEFAULT 0,
    csca_discrepancy INTEGER NOT NULL DEFAULT 0,
    dsc_discrepancy INTEGER NOT NULL DEFAULT 0,
    dsc_nc_discrepancy INTEGER NOT NULL DEFAULT 0,
    crl_discrepancy INTEGER NOT NULL DEFAULT 0,
    total_discrepancy INTEGER NOT NULL DEFAULT 0,
    db_country_stats JSONB,
    ldap_country_stats JSONB,
    status VARCHAR(20) NOT NULL DEFAULT 'UNKNOWN',
    error_message TEXT,
    check_duration_ms INTEGER NOT NULL DEFAULT 0
);
```

---

## 4. Service URLs

### Local Environment

| Service | URL |
|---------|-----|
| Frontend | http://localhost:3000 |
| API Gateway | http://localhost:8080/api |
| HAProxy Stats | http://localhost:8404 |

### Luckfox Environment

| Service | URL |
|---------|-----|
| Frontend | http://192.168.100.11:3000 |
| PKD Management API | http://192.168.100.11:8081/api |
| PA Service API | http://192.168.100.11:8082/api |
| Sync Service API | http://192.168.100.11:8083/api |

---

## 5. Health Checks

### API Health Check

```bash
# Local
curl http://localhost:8080/api/health
curl http://localhost:8080/api/sync/health

# Luckfox
curl http://192.168.100.11:8081/api/health
curl http://192.168.100.11:8083/api/sync/health
```

### Sync Status Check

```bash
# Sync 상태 확인
curl http://192.168.100.11:8083/api/sync/status

# Sync 트리거
curl -X POST http://192.168.100.11:8083/api/sync/check
```

---

## 6. Update Procedure

### 6.1 Frontend Update

```bash
# 1. 개발 PC에서 빌드
cd frontend
npm run build
docker build --platform linux/arm64 --no-cache -t icao-frontend:arm64 .

# 2. 이미지 저장 및 전송
docker save icao-frontend:arm64 | gzip > icao-frontend-arm64.tar.gz
scp icao-frontend-arm64.tar.gz luckfox@192.168.100.11:/home/luckfox/

# 3. Luckfox에서 업데이트
ssh luckfox@192.168.100.11
docker load < icao-frontend-arm64.tar.gz
cd /home/luckfox/icao-local-pkd-cpp-v2
docker compose -f docker-compose-luckfox.yaml stop frontend
docker compose -f docker-compose-luckfox.yaml up -d frontend
```

### 6.2 Backend Service Update

동일한 절차로 pkd-management, pa-service, sync-service를 업데이트합니다.

---

## 7. Troubleshooting

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| `relation "sync_status" does not exist` | 테이블 누락 | Section 3.2.4 참조하여 수동 생성 |
| `database "pkd" does not exist` | 잘못된 DB 이름 | Luckfox는 `localpkd` 사용 |
| Container network error | Host network mode | Luckfox에서는 port binding 대신 host network 사용 |

### Log Check

```bash
# 전체 로그
docker compose -f docker-compose-luckfox.yaml logs -f

# 특정 서비스 로그
docker compose -f docker-compose-luckfox.yaml logs -f sync-service
docker compose -f docker-compose-luckfox.yaml logs -f pkd-management
```

---

## 8. Backup & Restore

### Backup

```bash
# PostgreSQL 백업
docker exec icao-pkd-postgres pg_dump -U pkd localpkd > backup_$(date +%Y%m%d).sql

# LDAP 백업
docker exec icao-pkd-openldap slapcat > ldap_backup_$(date +%Y%m%d).ldif
```

### Restore

```bash
# PostgreSQL 복구
cat backup_20260104.sql | docker exec -i icao-pkd-postgres psql -U pkd -d localpkd

# LDAP 복구 (주의: 기존 데이터 삭제됨)
docker exec -i icao-pkd-openldap slapadd < ldap_backup_20260104.ldif
```

---

**Last Updated**: 2026-01-04
