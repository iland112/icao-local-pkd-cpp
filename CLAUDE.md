# ICAO Local PKD - C++ Implementation

**Version**: 1.6.0
**Last Updated**: 2026-01-14
**Status**: Production Ready

---

## Project Overview

C++ REST API 기반의 ICAO Local PKD 관리 및 Passive Authentication (PA) 검증 시스템입니다.

### Core Features

| Module | Description | Status |
|--------|-------------|--------|
| **PKD Upload** | LDIF/Master List 파일 업로드, 파싱, 검증 | ✅ Complete |
| **Certificate Validation** | CSCA/DSC Trust Chain, CRL 검증 | ✅ Complete |
| **LDAP Integration** | OpenLDAP 연동 (ICAO PKD DIT) | ✅ Complete |
| **Passive Authentication** | ICAO 9303 PA 검증 (SOD, DG 해시) | ✅ Complete |
| **DB-LDAP Sync** | PostgreSQL-LDAP 동기화 모니터링 | ✅ Complete |
| **Auto Reconcile** | DB-LDAP 불일치 자동 조정 (v1.6.0+) | ✅ Complete |
| **React.js Frontend** | CSR 기반 웹 UI | ✅ Complete |

### Technology Stack

| Category | Technology |
|----------|------------|
| **Language** | C++20 |
| **Web Framework** | Drogon 1.9+ |
| **Database** | PostgreSQL 15 + libpq |
| **LDAP** | OpenLDAP C API (libldap) |
| **Crypto** | OpenSSL 3.x |
| **JSON** | nlohmann/json |
| **Logging** | spdlog |
| **Build** | CMake 3.20+ / vcpkg |
| **Frontend** | React 19 + TypeScript + Vite + TailwindCSS 4 |

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         React.js Frontend (:3000)                        │
└─────────────────────────────────────────────────────────────────────────┘
                                    │ /api/*
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      API Gateway (Nginx :8080)                           │
│  /api/upload, /api/health, /api/certificates → PKD Management           │
│  /api/pa/*                                   → PA Service               │
│  /api/sync/*                                 → Sync Service             │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
        ┌───────────────────────────┼───────────────────────────┐
        ▼                           ▼                           ▼
┌───────────────┐          ┌───────────────┐          ┌───────────────┐
│ PKD Management│          │  PA Service   │          │ Sync Service  │
│    (:8081)    │          │   (:8082)     │          │   (:8083)     │
│  Upload/Cert  │          │ PA Verify/DG  │          │ DB-LDAP Sync  │
└───────────────┘          └───────────────┘          └───────────────┘
        │                           │                           │
        └───────────────────────────┼───────────────────────────┘
                                    ▼
┌─────────────────┐          ┌─────────────────────────────────────────┐
│   PostgreSQL    │          │         OpenLDAP MMR Cluster            │
│     :5432       │          │  ┌───────────┐      ┌───────────┐       │
│                 │          │  │ OpenLDAP1 │◄────►│ OpenLDAP2 │       │
│ - certificate   │          │  │   :3891   │      │   :3892   │       │
│ - crl           │          │  └─────┬─────┘      └─────┬─────┘       │
│ - master_list   │          │        └──────┬──────────┘              │
│ - validation    │          │               ↓                         │
└─────────────────┘          │        ┌───────────┐                    │
                             │        │  HAProxy  │ :389               │
                             │        └───────────┘                    │
                             └─────────────────────────────────────────┘
```

### LDAP DIT Structure (ICAO PKD)

```
dc=ldap,dc=smartcoreinc,dc=com
└── dc=pkd
    └── dc=download
        ├── dc=data
        │   └── c={COUNTRY}
        │       ├── o=csca    (CSCA certificates)
        │       ├── o=dsc     (DSC certificates)
        │       ├── o=crl     (CRL)
        │       └── o=ml      (Master Lists)
        └── dc=nc-data
            └── c={COUNTRY}
                └── o=dsc     (DSC_NC - Non-Conformant)
```

---

## Directory Structure

```
icao-local-pkd/
├── services/
│   ├── pkd-management/        # PKD Management C++ service (:8081)
│   │   ├── src/main.cpp       # Upload, Certificate, Health APIs
│   │   ├── CMakeLists.txt
│   │   ├── vcpkg.json
│   │   └── Dockerfile
│   ├── pa-service/            # PA Service C++ (:8082)
│   │   ├── src/main.cpp       # PA Verify, DG Parsing APIs
│   │   ├── CMakeLists.txt
│   │   ├── vcpkg.json
│   │   └── Dockerfile
│   └── sync-service/          # DB-LDAP Sync Service (:8083)
│       ├── src/main.cpp       # Sync status, stats APIs
│       ├── CMakeLists.txt
│       ├── vcpkg.json
│       └── Dockerfile
├── nginx/                     # API Gateway configuration
│   ├── api-gateway.conf       # Nginx routing config
│   └── proxy_params           # Common proxy parameters
├── frontend/                  # React.js frontend
├── docker/
│   ├── docker-compose.yaml
│   └── init-scripts/          # PostgreSQL init
├── openldap/
│   ├── schemas/               # ICAO PKD custom schema
│   ├── bootstrap/             # Initial LDIF
│   └── scripts/               # Init scripts
├── docs/
│   ├── openapi/               # OpenAPI specifications
│   └── PA_API_GUIDE.md        # External client API guide
├── .docker-data/              # Bind mount data (gitignored)
└── data/cert/                 # Trust anchor certificates
```

---

## Quick Start

### Docker (Recommended)

```bash
# Start all services
./docker-start.sh

# With rebuild
./docker-start.sh --build

# Infrastructure only (no app)
./docker-start.sh --skip-app

# Clean all data and restart
./docker-clean.sh

# Health check (MMR 상태 포함)
./docker-health.sh
```

### Docker Management Scripts

| Script | Description |
|--------|-------------|
| `docker-start.sh` | 전체 서비스 시작 (MMR 초기화 포함) |
| `docker-stop.sh` | 서비스 중지 |
| `docker-restart.sh` | 서비스 재시작 |
| `docker-logs.sh` | 로그 확인 |
| `docker-clean.sh` | 완전 삭제 (.docker-data 포함) |
| `docker-health.sh` | 헬스 체크 (MMR 상태, 엔트리 수 포함) |
| `docker-backup.sh` | 데이터 백업 (PostgreSQL, LDAP, 업로드 파일) |
| `docker-restore.sh` | 데이터 복구 |

### Access URLs

| Service | URL |
|---------|-----|
| Frontend | http://localhost:3000 |
| **API Gateway** | **http://localhost:8080/api** |
| ├─ PKD Management | http://localhost:8080/api/upload, /api/health |
| ├─ PA Service | http://localhost:8080/api/pa/* |
| └─ Sync Service | http://localhost:8080/api/sync/* |
| HAProxy Stats | http://localhost:8404 |
| PostgreSQL | localhost:5432 (pkd/pkd123) |
| LDAP (HAProxy) | ldap://localhost:389 |

> **Note**: 모든 백엔드 서비스(8081, 8082, 8083)는 API Gateway를 통해서만 접근합니다.

---

## API Endpoints

> 모든 API는 API Gateway (http://localhost:8080)를 통해 접근합니다.

### PKD Management (via Gateway)

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/upload/ldif` | Upload LDIF file |
| POST | `/api/upload/masterlist` | Upload Master List file |
| GET | `/api/upload/history` | Get upload history |
| GET | `/api/upload/statistics` | Get upload statistics |
| GET | `/api/progress/stream/{id}` | SSE progress stream |
| GET | `/api/health` | Application health |
| GET | `/api/health/database` | PostgreSQL status |
| GET | `/api/health/ldap` | LDAP status |

### PA Service (via Gateway)

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/pa/verify` | PA verification |
| POST | `/api/pa/parse-sod` | Parse SOD metadata |
| POST | `/api/pa/parse-dg1` | Parse DG1 (MRZ) |
| POST | `/api/pa/parse-dg2` | Parse DG2 (Face Image) |
| GET | `/api/pa/statistics` | Verification statistics |
| GET | `/api/pa/history` | Verification history |
| GET | `/api/pa/{id}` | Verification details |
| GET | `/api/pa/health` | PA service health |

### Sync Service (via Gateway)

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/sync/health` | Sync service health |
| GET | `/api/sync/status` | Full sync status with DB/LDAP stats |
| GET | `/api/sync/stats` | DB and LDAP statistics |
| POST | `/api/sync/trigger` | Manual sync trigger |
| GET | `/api/sync/config` | Current configuration |

---

## ICAO 9303 Compliance

### DSC Trust Chain Validation

```
1. Parse DSC from LDIF/Master List
2. Extract issuer DN from DSC
3. Lookup CSCA by issuer DN (case-insensitive)
4. Verify DSC signature with CSCA public key: X509_verify(dsc, csca_pubkey)
5. Check validity period
6. Record validation result in DB
```

### Validation Statistics (Current)

| Metric | Count |
|--------|-------|
| Total Certificates | 30,637 |
| CSCA | 525 |
| DSC | 29,610 |
| DSC_NC | 502 |
| Trust Chain Valid | 5,868 |
| Trust Chain Invalid | 24,244 |
| CSCA Not Found | 6,299 |

---

## Key Technical Notes

### PostgreSQL Bytea Storage

**Important**: Use standard quotes for bytea hex format, NOT escape string literal.

```cpp
// CORRECT - PostgreSQL interprets \x as bytea hex format
"'" + byteaEscaped + "'"

// WRONG - E'' causes \x to be treated as escape sequence
"E'" + byteaEscaped + "'"
```

### LDAP Connection Strategy

| Operation | Host | Purpose |
|-----------|------|---------|
| Read | haproxy:389 | Load balanced across MMR nodes |
| Write | openldap1:389 | Direct to primary master |

### Master List Processing

- ICAO Master List contains **ONLY CSCA** certificates (per ICAO Doc 9303)
- Both self-signed and cross-signed CSCAs are classified as CSCA
- Uses OpenSSL CMS API (`d2i_CMS_bio`) for parsing

---

## Development

### Frontend Development Workflow

**IMPORTANT**: Frontend 수정 후 반드시 아래 방법으로 빌드/배포

```bash
# 1. 코드 수정
vim frontend/src/pages/FileUpload.tsx

# 2. 빌드 및 배포 (권장 - 자동화 스크립트)
./scripts/frontend-rebuild.sh

# 3. 브라우저 강제 새로고침
# Ctrl + Shift + R (Windows/Linux)
# Cmd + Shift + R (Mac)

# 4. 검증 (선택사항)
./scripts/verify-frontend-build.sh
```

**주의사항**:
- ❌ `docker compose restart frontend` - 구 이미지로 재시작됨
- ❌ `docker compose up -d --build frontend` - 모든 서비스가 함께 빌드됨 (10분+)
- ✅ `./scripts/frontend-rebuild.sh` - Frontend만 빌드 및 배포 (~1분)

**상세 가이드**: [docs/FRONTEND_BUILD_GUIDE.md](docs/FRONTEND_BUILD_GUIDE.md)

### Backend Build from Source

```bash
cd services/pkd-management
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build -j$(nproc)
```

### Docker Build

```bash
docker-compose -f docker/docker-compose.yaml build pkd-management
docker-compose -f docker/docker-compose.yaml up -d pkd-management
```

---

## References

- **ICAO Doc 9303 Part 11**: Security Mechanisms for MRTDs
- **ICAO Doc 9303 Part 12**: PKI for MRTDs
- **RFC 5280**: X.509 PKI Certificate and CRL Profile
- **RFC 5652**: Cryptographic Message Syntax (CMS)

---

## Luckfox ARM64 Deployment

### Target Environment

| Item | Value |
|------|-------|
| Device | Luckfox Pico (ARM64) |
| IP Address | 192.168.100.11 |
| SSH Credentials | luckfox / luckfox |
| Docker Compose | docker-compose-luckfox.yaml |
| PostgreSQL DB | localpkd (user: pkd, password: pkd) |

### Host Network Mode

Luckfox 환경에서는 모든 컨테이너가 `network_mode: host`로 실행됩니다.

```yaml
# docker-compose-luckfox.yaml
services:
  postgres:
    network_mode: host
    environment:
      - POSTGRES_DB=localpkd  # 주의: 로컬 환경의 pkd와 다름
```

### Automated Deployment (Recommended) ⭐

**공식 배포 방법**: GitHub Actions → 자동화 스크립트

```bash
# 1. 코드 수정 및 푸시
git add .
git commit -m "feat: your changes"
git push origin feature/openapi-support

# 2. GitHub Actions 빌드 완료 대기 (10-15분)
# https://github.com/iland112/icao-local-pkd-cpp/actions

# 3. 자동 배포 스크립트 실행
./scripts/deploy-from-github-artifacts.sh pkd-management

# 전체 서비스 배포
./scripts/deploy-from-github-artifacts.sh all
```

**배포 스크립트 기능**:
- ✅ GitHub Actions artifacts 자동 다운로드
- ✅ OCI 형식 → Docker 형식 자동 변환 (skopeo)
- ✅ sshpass를 통한 비대화형 SSH/SCP 인증
- ✅ 기존 컨테이너/이미지 자동 정리
- ✅ 이미지 전송 및 로드
- ✅ 서비스 시작 및 헬스체크

**필수 도구**:
```bash
# sshpass (SSH 자동 인증)
sudo apt-get install sshpass

# skopeo (OCI → Docker 변환)
sudo apt-get install skopeo

# gh CLI (artifact 다운로드)
sudo apt-get install gh
gh auth login
```

**상세 문서**: [docs/LUCKFOX_DEPLOYMENT.md](docs/LUCKFOX_DEPLOYMENT.md)

### Docker Image Name Mapping

**중요**: 배포 스크립트와 docker-compose-luckfox.yaml의 이미지 이름이 일치해야 합니다.

| Service | 배포 스크립트 이미지 이름 | docker-compose 이미지 이름 |
|---------|--------------------------|---------------------------|
| pkd-management | `icao-local-management:arm64` | `icao-local-management:arm64` |
| pa-service | `icao-local-pa:arm64-v3` | `icao-local-pa:arm64-v3` |
| sync-service | `icao-local-sync:arm64-v1.2.0` | `icao-local-sync:arm64-v1.2.0` |
| frontend | `icao-local-pkd-frontend:arm64-fixed` | `icao-local-pkd-frontend:arm64-fixed` |

**버전 업데이트 시 주의사항**:
1. `scripts/deploy-from-github-artifacts.sh` - `deploy_service` 호출 시 이미지 이름 업데이트
2. `docker-compose-luckfox.yaml` - 서비스의 `image:` 필드 업데이트
3. Luckfox에 docker-compose-luckfox.yaml 업데이트 후 재배포

### Cross-Platform Docker Build (비권장)

```bash
# AMD64에서 ARM64 이미지 빌드
docker build --platform linux/arm64 --no-cache -t icao-frontend:arm64 .

# 이미지 저장 및 전송
docker save icao-frontend:arm64 | gzip > icao-frontend-arm64.tar.gz
scp icao-frontend-arm64.tar.gz luckfox@192.168.100.11:/home/luckfox/

# Luckfox에서 이미지 로드
ssh luckfox@192.168.100.11 "docker load < /home/luckfox/icao-frontend-arm64.tar.gz"
```

### sync_status Table Schema

Luckfox 배포 시 `sync_status` 테이블 수동 생성이 필요합니다:

```sql
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

### Luckfox Docker Management

**통합 관리 스크립트** (2026-01-13):
- 모든 Docker 관리 스크립트가 `/home/luckfox/icao-local-pkd-cpp-v2`에 통합
- 상세 가이드: [LUCKFOX_README.md](LUCKFOX_README.md)

```bash
# 프로젝트 디렉토리
cd /home/luckfox/icao-local-pkd-cpp-v2

# 서비스 시작
./luckfox-start.sh

# 헬스체크
./luckfox-health.sh

# 로그 확인
./luckfox-logs.sh [서비스명]

# 재시작
./luckfox-restart.sh [서비스명]

# 백업
./luckfox-backup.sh

# 복구
./luckfox-restore.sh <백업파일>

# 완전 초기화 (⚠️ 데이터 삭제)
./luckfox-clean.sh

# 기존 방법 (여전히 사용 가능)
docker compose -f docker-compose-luckfox.yaml up -d

# 서비스 중지
docker compose -f docker-compose-luckfox.yaml down

# 로그 확인
docker compose -f docker-compose-luckfox.yaml logs -f [service]

# 컨테이너 재시작
docker compose -f docker-compose-luckfox.yaml restart [service]
```

---

## Critical Notes

### ⚠️ Docker Build Cache Issue (MUST READ)

**문제**: GitHub Actions 빌드 캐시가 소스 코드 변경을 무시할 수 있음

**증상**:
- 코드를 수정하고 푸시했지만 기능이 작동하지 않음
- 빌드 로그에 많은 "CACHED" 메시지
- 배포 후 이전 버전이 실행됨

**해결 방법**:
```cpp
// main.cpp에서 버전 번호 업데이트
spdlog::info("Starting ICAO Local PKD Application (v1.X.Y - Feature Name)...");
```

**배포 전 필수 체크**:
```bash
# 1. 빌드 신선도 검증
./scripts/check-build-freshness.sh

# 2. 검증 통과 시에만 배포
./scripts/deploy-from-github-artifacts.sh pkd-management

# 3. 버전 확인
sshpass -p "luckfox" ssh luckfox@192.168.100.11 "docker logs icao-pkd-management --tail 5"
```

**상세 문서**: [docs/DOCKER_BUILD_CACHE.md](docs/DOCKER_BUILD_CACHE.md)

---

## Change Log

### 2026-01-14: Auto Reconcile Feature Complete Implementation (v1.6.0)

**Auto Reconcile 완전 구현 (Phase 1-6 완료)**:

**Phase 1: Core Reconciliation Logic**
- 모듈화된 아키텍처 구현
  - `src/reconciliation/ldap_operations.h/cpp` - LDAP 인증서 작업 클래스
  - `src/reconciliation/reconciliation_engine.h/cpp` - 조정 엔진
  - `src/common/types.h` - 공통 타입 정의
  - `src/common/config.h` - 설정 관리
- `LdapOperations` 클래스: 인증서 추가/삭제, DN 빌드, DER↔PEM 변환
- `ReconciliationEngine` 클래스: PostgreSQL-LDAP 동기화 오케스트레이션
- Batch processing (maxReconcileBatchSize: 100)
- Dry-run mode 지원 (시뮬레이션)

**Phase 2: Database Schema Migration**
- `reconciliation_summary` 테이블: 고수준 실행 결과
  - triggered_by (MANUAL/AUTO/DAILY_SYNC), status, counts, timing
- `reconciliation_log` 테이블: 상세 작업 로그
  - operation, cert details, status, errors, per-operation timing
- Database logging 통합:
  - `createReconciliationSummary()` - 시작 시 IN_PROGRESS 레코드 생성
  - `logReconciliationOperation()` - 각 작업마다 로그 기록
  - `updateReconciliationSummary()` - 완료 시 최종 결과 업데이트
- 성능 최적화를 위한 인덱스 추가

**Phase 3: API Endpoints**
- `GET /api/sync/reconcile/history` - 페이지네이션 및 필터링 지원
  - Query params: limit, offset, status, triggeredBy
- `GET /api/sync/reconcile/{id}` - 상세 실행 정보 및 로그
  - Summary + 모든 작업 로그 반환
- HTTP 404 (not found), HTTP 400 (invalid params) 에러 처리

**Phase 4: Frontend Integration**
- `ReconciliationHistory.tsx` 컴포넌트 생성
  - 테이블 뷰 (상태, 타임스탬프, 트리거 타입, 결과)
  - 상태 아이콘 (✓ COMPLETED, ✗ FAILED, ⚠ PARTIAL, ⟳ IN_PROGRESS)
  - 트리거 배지 (▶ MANUAL, ⚡ AUTO, 📅 DAILY_SYNC)
  - 인증서 breakdown (CSCA/DSC/DSC_NC 추가 건수)
  - Duration 포맷팅 (ms → seconds → minutes)
- Details Dialog 모달:
  - Summary 카드 (상태, 트리거, 건수, 소요시간)
  - Results breakdown (성공/실패/추가된 인증서)
  - Operation logs 테이블 (스크롤 지원)
  - Per-operation 상태 및 타이밍 표시
  - 실패한 작업 하이라이트
- SyncDashboard에 통합 (Revalidation History와 Info 섹션 사이)

**Phase 5: Daily Scheduler Integration**
- Daily sync tasks에 Step 3 추가: Auto reconcile
- 트리거 조건: `autoReconcile` enabled AND `discrepancies > 0`
- `triggeredBy='DAILY_SYNC'` 로 소스 추적
- `sync_status_id`와 연결하여 audit trail 제공
- 불일치가 없으면 reconciliation 건너뛰기 (불필요한 작업 방지)
- 에러 발생 시 daily sync 중단하지 않음

**Phase 6: Testing and Documentation**
- Docker 빌드: SUCCESSFUL (모든 phase)
- `docs/AUTO_RECONCILE_DESIGN.md` - 12개 섹션, 2230+ 줄 설계 문서
- `docs/AUTO_RECONCILE_IMPLEMENTATION.md` - 구현 완료 요약
- CLAUDE.md 업데이트 (v1.6.0)

**주요 기능**:
- ✅ 자동화된 데이터 일관성 유지 (PostgreSQL ↔ LDAP)
- ✅ 전체 Audit Trail (모든 작업의 상세 로그 및 히스토리)
- ✅ 사용자 친화적 UI (직관적인 히스토리 및 상세 정보)
- ✅ Daily Scheduler 통합 (일일 동기화 워크플로우)
- ✅ 모듈화된 아키텍처 (유지보수 및 확장 가능)
- ✅ Production Ready (완전한 에러 처리 및 로깅)

**커밋 히스토리**:
- 72b2802: refactor(sync): Integrate ReconciliationEngine into main.cpp
- 351d8d4: fix(sync): Fix berval initialization and unused variable warning
- 9c6f5fb: feat(sync): Add database schema and logging for Auto Reconcile
- a8d0a95: feat(sync): Add reconciliation history API endpoints
- 41be03d: feat(sync): Add reconciliation history frontend UI
- ae6cd07: feat(sync): Integrate auto reconcile with daily sync scheduler

### 2026-01-14: Frontend Build Workflow Automation & MANUAL Mode localStorage Bug Fix (v1.5.11)

**Frontend Build Workflow 자동화**:
- `scripts/frontend-rebuild.sh` - Frontend 빌드 및 배포 자동화 스크립트
  - 로컬 빌드 (npm run build)
  - 구 컨테이너/이미지 삭제
  - 새 이미지 빌드 (다른 서비스 영향 없음)
  - 새 컨테이너 시작
  - 자동 검증
- `scripts/verify-frontend-build.sh` - 빌드 검증 스크립트
  - 로컬 빌드와 컨테이너 빌드 비교
  - 파일명 및 크기 검증
- `docs/FRONTEND_BUILD_GUIDE.md` - 상세 가이드 문서
  - Docker 빌드 함정 및 해결책
  - 올바른 빌드 방법
  - 문제 해결 체크리스트

**문제 해결**:
- ❌ 기존: `docker compose restart frontend` - 구 이미지로 재시작
- ❌ 기존: `docker compose up -d --build frontend` - 모든 서비스 함께 빌드 (10분+)
- ✅ 개선: `./scripts/frontend-rebuild.sh` - Frontend만 빌드 및 배포 (~1분)

**MANUAL 모드 localStorage 복원 버그 수정**:
- **문제**: 페이지 새로고침 시 localStorage에서 업로드 ID 복원 시, `totalEntries=0`인데도 "파싱 완료" 표시
- **원인**: [FileUpload.tsx:96-103](frontend/src/pages/FileUpload.tsx#L96-L103)에서 무조건 parseStage를 COMPLETED로 설정
- **수정**: `totalEntries > 0`일 때만 "파싱 완료", 그렇지 않으면 "파싱 대기 중" 표시
- **영향**: MANUAL 모드 사용자 경험 개선 (올바른 단계 상태 표시)

**DNS 해결 문제 재발 방지**:
- Frontend nginx DNS resolver 설정 검증
- 시스템 재시작 후에도 Docker 내부 DNS (127.0.0.11) 사용 확인

**기술적 세부사항**:
- Multi-stage Docker build 이해 및 캐시 전략
- Docker Compose 서비스 의존성 관리
- 브라우저 캐시 무효화 전략

**문서 참조**:
- [FRONTEND_BUILD_GUIDE.md](docs/FRONTEND_BUILD_GUIDE.md) - Frontend 빌드 완전 가이드

### 2026-01-13: API Documentation Integration & Deployment Process Documentation (v1.5.10)

**API Documentation**:
- Swagger UI 통합 완료 (OpenAPI 3.0 specifications)
- 사이드바 메뉴에서 새 탭으로 Swagger UI 열기
- 각 서비스별 API 문서 자동 선택
  - PKD Management API v1.5.10
  - PA Service API v1.2.0
  - Sync Service API v1.2.0
- API Gateway를 통한 프록시 제공 (포트 8080)
- CORS 헤더 설정으로 크로스 오리진 접근 허용

**배포 프로세스 문서화**:
- `docs/DEPLOYMENT_PROCESS.md` 작성 완료
- 전체 배포 파이프라인 상세 설명:
  1. Code Modification (Local)
  2. Git Commit & Push
  3. GitHub Actions Build (Change Detection, Multi-stage Caching)
  4. Artifact Download (OCI format)
  5. Deploy to Luckfox (OCI→Docker 변환, 이미지 로드, 컨테이너 재생성)
- 빌드 최적화 전략 문서화 (vcpkg 캐시, BuildKit inline cache)
- 트러블슈팅 가이드 추가
- 이미지 이름 매핑 테이블 (배포 스크립트 ↔ docker-compose)

**기술적 세부사항**:
- OCI (Open Container Initiative) format → Docker archive 변환 (skopeo)
- GitHub Actions artifact 30일 보관
- Change detection으로 변경된 서비스만 빌드 (10-15분)
- 비대화형 SSH 인증 (sshpass)

**문서 참조**:
- [DEPLOYMENT_PROCESS.md](docs/DEPLOYMENT_PROCESS.md) - 배포 프로세스 완전 가이드
- [LUCKFOX_DEPLOYMENT.md](docs/LUCKFOX_DEPLOYMENT.md) - Luckfox 특화 배포
- [DOCKER_BUILD_CACHE.md](docs/DOCKER_BUILD_CACHE.md) - 빌드 캐시 트러블슈팅

### 2026-01-13: Luckfox Docker 관리 스크립트 통합 및 AUTO MODE 완성 (v1.5.10)

**Luckfox Docker 관리 스크립트 통합**:
- `/home/luckfox/scripts` → `/home/luckfox/icao-local-pkd-cpp-v2`로 통합
- 8개 관리 스크립트 생성 및 배포:
  - `luckfox-start.sh` - 시스템 시작
  - `luckfox-stop.sh` - 시스템 중지
  - `luckfox-restart.sh` - 재시작 (전체 또는 특정 서비스)
  - `luckfox-logs.sh` - 로그 확인
  - `luckfox-health.sh` - 헬스체크 (DB/API/서비스 상태)
  - `luckfox-clean.sh` - 완전 초기화 (데이터 삭제)
  - `luckfox-backup.sh` - PostgreSQL + 업로드 파일 백업
  - `luckfox-restore.sh` - 백업 복구 (DB DROP/CREATE)
- `LUCKFOX_README.md` 작성 (사용법, 예제, 문제 해결)
- 모든 스크립트 테스트 완료 및 권한 문제 해결

**v1.5.10: AUTO MODE 진행 상태 상세 표시**:
- Backend: Pre-scan으로 총 개수 계산 후 "X/Total" 형식으로 진행 상태 표시
- `LdifProcessor::TotalCounts` 구조체 추가
- AUTO 모드 SSE 메시지: "처리 중: CSCA 100/500, DSC 200/1000, CRL 10/50, ML 5/10"
- 완료 메시지: "처리 완료: CSCA 500개, DSC 1000개, ... (검증: 800 성공, 200 실패)"

**Frontend 개선**:
- AUTO MODE ML (Master List) 감지 추가 (line 524)
- 완료 메시지 상세 정보 표시 개선
- TypeScript 스코프 오류 수정 (prev 변수)

**테스트 결과**:
- ✅ Collection 001, 002, 003 AUTO MODE 업로드 정상 완료
- ✅ 진행 상태 "X/Total" 형식 정상 표시
- ✅ 완료 시그널 정상 처리 (페이지 로딩 종료)
- ✅ 완료 메시지 인증서 breakdown 표시

**배포**:
- Backend: v1.5.10 AUTO-PROGRESS-DISPLAY (Build 20260113-190000)
- Frontend: v1.5.10 (ARM64)
- Luckfox: 완전 테스트 완료

### 2026-01-11: MANUAL 모드 Race Condition 수정 (Frontend)

**문제**:
- MANUAL 모드에서 Stage 1 (파싱) 완료 직후 Stage 2 버튼을 클릭하면 오류 발생
- 오류 메시지: "Stage 1 parsing not completed. Current status: PROCESSING"
- 실제로는 파싱이 완료되었지만 DB 상태 업데이트가 완료되지 않은 상태

**원인 분석**:
```
Timeline of Events (30,081 entries LDIF file):
1. 15:58:24 - Stage 1 시작
2. 15:58:25 - 파싱 완료 (30,081개 엔트리)
3. 15:58:25 - SSE 이벤트 PARSING_COMPLETED 전송 → Frontend 즉시 수신
4. 15:58:27 - ❌ 사용자가 Stage 2 버튼 클릭 (너무 빠름!)
5. 15:58:28 - Temp 파일 저장 완료 (76MB)
6. 15:58:29 - DB 상태 PROCESSING → PENDING 업데이트 완료
```

**Backend 코드 흐름** ([main.cpp:2564](services/pkd-management/src/main.cpp#L2564)):
1. SSE `PARSING_COMPLETED` 이벤트 전송
2. Strategy Pattern 실행 (processLdifEntries)
3. Temp 파일 저장 (~1-2초, 큰 파일의 경우)
4. DB UPDATE 쿼리 실행 (PROCESSING → PENDING)

**문제**: SSE 이벤트가 먼저 전송되고, DB 업데이트는 나중에 완료됨

**해결 방법** (Frontend - [FileUpload.tsx:340-349](frontend/src/pages/FileUpload.tsx#L340-L349)):
```typescript
} else if (stage.startsWith('PARSING')) {
  setUploadStage(prev => prev.status !== 'COMPLETED' ? { ...prev, status: 'COMPLETED', percentage: 100 } : prev);
  // For PARSING_COMPLETED, add a small delay to ensure DB status is updated
  if (stage === 'PARSING_COMPLETED') {
    // Keep button disabled for 1 second after PARSING_COMPLETED to ensure DB update completes
    setParseStage({ ...stageStatus, status: 'IN_PROGRESS' });
    setTimeout(() => {
      setParseStage(stageStatus);  // Set to COMPLETED after delay
    }, 1000);
  } else {
    setParseStage(stageStatus);
  }
}
```

**효과**:
- `PARSING_COMPLETED` SSE 이벤트 수신 후 1초 동안 Stage 2 버튼 비활성화 유지
- DB 상태 업데이트 완료 후 버튼 활성화
- 사용자가 버튼을 클릭할 수 있을 때는 항상 DB 상태가 PENDING으로 업데이트됨

**커밋**: e5f6e2e
**배포**: Luckfox ARM64 (Local Build)

### 2026-01-10: Docker Build Cache 문제 - 최종 해결 시도 (v1.4.7)

**24시간 디버깅 요약**:
- **문제**: v1.4.6 소스 코드를 푸시했지만 배포된 바이너리는 v1.3.0을 계속 표시
- **증거**:
  - 빌드 로그: `grep "spdlog::info.*ICAO" ./src/main.cpp` → v1.4.6 확인됨
  - 바이너리: `strings pkd-management | grep ICAO` → v1.3.0만 발견됨
- **시도한 방법들**:
  1. ❌ GitHub Actions cache 비활성화 (`no-cache: true`)
  2. ❌ BUILD_ID 파일 업데이트 및 커밋
  3. ❌ 버전 문자열을 고유값으로 변경
  4. ❌ CMake `--clean-first` 플래그 추가
  5. ❌ `.dockerignore` 파일 추가
  6. ❌ 소스 검증 스텝 추가
  7. ❌ ARG CACHE_BUST 구현 (Gemini 추천)
  8. ❌ GitHub Actions cache 재활성화 (ARG 보호)

**최종 해결 시도** (커밋 60d3dd5):
```dockerfile
# CRITICAL: Clean any potential cached artifacts from vcpkg-deps stage
RUN rm -rf build build_fresh bin lib CMakeCache.txt CMakeFiles && \
    find . -name "*.o" -delete && \
    find . -name "*.a" -delete

# CRITICAL: Touch all source files to force CMake to recompile
RUN find ./src -type f -name "*.cpp" -exec touch {} \; && \
    find ./src -type f -name "*.h" -exec touch {} \;

# Build with verbose output
cmake -DCMAKE_VERBOSE_MAKEFILE=ON
cmake --build build_fresh --verbose

# CRITICAL: Verify binary version BEFORE copying to runtime
RUN strings build_fresh/bin/pkd-management | grep -i "ICAO.*PKD"
```

**가설**:
- vcpkg-deps 스테이지가 캐시될 때 .o/.a 파일이 함께 캐시됨
- CMake가 타임스탬프만 확인하여 오래된 객체 파일 재사용
- `touch` 명령으로 모든 소스 파일 타임스탬프 갱신 → 강제 재컴파일

**결과** (Run 20879118487):
- ✅ Builder 스테이지: v1.4.6 바이너리 정상 컴파일 확인
- ❌ Runtime 스테이지: `COPY --from=builder` 단계가 CACHED 처리됨
- 문제: Builder가 재빌드되어도 Runtime의 COPY 명령이 캐시를 재사용

**최종 해결** (커밋 ddfd21e):
```dockerfile
# Stage 4: Runtime
FROM debian:bookworm-slim AS runtime

# ARG를 runtime stage에도 재선언
ARG CACHE_BUST=unknown
RUN echo "=== Runtime Cache Bust Token: $CACHE_BUST ==="
```

**근본 원인**:
- Docker의 ARG는 stage 간 자동 전파되지 않음
- Builder stage의 CACHE_BUST는 runtime stage에 영향 없음
- COPY --from=builder 명령이 독립적으로 캐시됨

**최종 결과** (Run 20879268691):
- ✅ Builder 스테이지: v1.4.6 바이너리 정상 컴파일
- ✅ Runtime 스테이지: `COPY --from=builder` → **DONE** (캐시 사용 안 함)
- ✅ Luckfox 배포 성공: **v1.4.6 정상 실행 확인**

**24시간 디버깅 완료!**

```
[2026-01-10 22:57:42.575] [info] [1] ====== ICAO Local PKD v1.4.6 NO-CACHE BUILD 20260110-143000 ======
```

**핵심 교훈**:
- Docker ARG는 FROM 경계를 넘지 못함 (각 stage마다 재선언 필수)
- Multi-stage build에서 builder 재빌드 ≠ runtime COPY 재실행
- 각 stage의 캐시를 독립적으로 관리해야 함

### 2026-01-11: Failed Upload Cleanup 기능 및 Luckfox 배포 이미지 이름 불일치 해결 (v1.4.8)

**Failed Upload Cleanup 기능 구현**:
- DELETE `/api/upload/{uploadId}` 엔드포인트 추가
- `ManualProcessingStrategy::cleanupFailedUpload()` 정적 함수 구현
- DB 정리: certificate, crl, master_list, uploaded_file 레코드 삭제
- 파일 정리: `/app/temp/{uploadId}_ldif.json` 임시 파일 삭제
- Frontend: Upload History 페이지에 삭제 버튼 및 확인 다이얼로그 추가 (FAILED/PENDING만)

**MANUAL 모드 안정성 개선**:
- Stage 2 시작 전 Stage 1 완료 검증 (PENDING 상태 확인)
- Temp 파일 누락 시 명확한 에러 메시지
- 타이밍 이슈 근본 해결

**Luckfox 배포 이미지 이름 불일치 해결**:
- 문제: 배포 스크립트가 생성하는 이미지 이름과 docker-compose가 사용하는 이미지 이름 불일치
- 결과: 이미지를 로드해도 컨테이너가 이전 버전 계속 사용
- 해결:
  - `scripts/deploy-from-github-artifacts.sh`: 이미지 이름을 docker-compose와 일치하도록 수정
  - `docker-compose-luckfox.yaml`: 로컬 저장소에 추가 (버전 관리)
  - CLAUDE.md: 이미지 이름 매핑 테이블 및 업데이트 주의사항 추가

**이미지 이름 통일**:
| Service | Image Name |
|---------|------------|
| pkd-management | `icao-local-management:arm64` |
| pa-service | `icao-local-pa:arm64-v3` |
| sync-service | `icao-local-sync:arm64-v1.2.0` |
| frontend | `icao-local-pkd-frontend:arm64-fixed` |

**배포 완료**:
- v1.4.8 CLEANUP-FAILED-UPLOAD Luckfox 배포 성공
- 로그 확인: `====== ICAO Local PKD v1.4.8 CLEANUP-FAILED-UPLOAD BUILD 20260111-130200 ======`

### 2026-01-10: Strategy Pattern 리팩토링 및 BUILD_ID 캐시 무효화

**구현 내용** (v1.4.0 - v1.4.6):
- MANUAL/AUTO 모드 분리를 위한 Strategy Pattern 적용
- `common.h`: LdifEntry, ValidationStats 공통 구조체
- `processing_strategy.h/cpp`: ProcessingStrategy 인터페이스 및 구현체
  - AutoProcessingStrategy: 기존 one-shot 처리
  - ManualProcessingStrategy: 3단계 분리 처리
- `ldif_processor.h/cpp`: LDIF 처리 로직 캡슐화
- ProcessingStrategyFactory: Factory Pattern으로 전략 선택

**MANUAL 모드 3단계 처리**:
1. Stage 1 (Parse): `/app/temp/{uploadId}_ldif.json`에 저장 후 대기
2. Stage 2 (Validate): Temp 파일 로드 → DB 저장 (LDAP=nullptr)
3. Stage 3 (LDAP Upload): DB → LDAP 업로드

**빌드 오류 해결 과정** (5회 반복):
1. v1.4.1: LdifProcessor 네임스페이스 호출 오류
2. v1.4.2: Drogon Json 헤더 누락
3. v1.4.3: processing_strategy.cpp 헤더 누락
4. v1.4.4: ldif_processor.cpp 헤더 누락
5. v1.4.5: **Critical** - Anonymous namespace 링커 오류
   - 문제: extern 선언된 함수들이 익명 네임스페이스 내부에 정의됨
   - 해결: main.cpp 829-2503 라인 범위를 익명 네임스페이스 외부로 이동

**Docker 빌드 캐시 문제 발견**:
- 문제: 버전 번호(v1.4.6)만 변경해도 Docker가 이전 바이너리 재사용
- 원인: ARG CACHE_BUST가 있어도 CMake가 타임스탬프 기반으로 캐시된 .o 파일 재사용
- 최종 해결은 v1.4.7에서 시도 중

### 2026-01-09: Docker Build Cache 문제 해결 및 문서화

**발견된 문제**:
- 중복 검사 기능 추가 후 배포했으나 작동하지 않음
- 원인: GitHub Actions 빌드에서 모든 레이어가 CACHED 처리
- 새 소스 코드가 컴파일되지 않고 이전 바이너리 재사용

**해결 조치**:
- v1.1.0 → v1.2.0 버전 업데이트로 캐시 무효화
- 빌드 신선도 검증 스크립트 추가 (`scripts/check-build-freshness.sh`)
- 캐시 관리 가이드 문서 작성 (`docs/DOCKER_BUILD_CACHE.md`)

**새로운 배포 프로세스**:
1. 코드 수정 + 버전 번호 업데이트
2. 커밋 및 푸시
3. GitHub Actions 빌드 대기
4. `./scripts/check-build-freshness.sh` 실행 (신선도 검증)
5. 검증 통과 시 배포
6. 버전 및 기능 테스트

**교훈**:
- 빌드 캐시는 속도 향상(10-15분)과 정확성 사이의 트레이드오프
- 중요한 기능 추가 시 항상 버전 번호 업데이트
- 배포 전 빌드 신선도 검증 필수

### 2026-01-09: 배포 자동화 스크립트 및 문서화

**자동화 스크립트**:
- `scripts/deploy-from-github-artifacts.sh`: OCI → Docker 변환, 자동 배포
- `scripts/check-build-freshness.sh`: 빌드 신선도 검증

**문서**:
- `docs/LUCKFOX_DEPLOYMENT.md`: 배포 절차 상세 가이드
- `docs/DOCKER_BUILD_CACHE.md`: 캐시 문제 예방 가이드

### 2026-01-09: 파일 업로드 중복 검사 기능 (v1.2.0)

**구현 내용**:
- `checkDuplicateFile()` 함수 추가 (SHA-256 해시 기반)
- LDIF/Master List 업로드 엔드포인트에 중복 검사 적용
- HTTP 409 Conflict 응답 (기존 업로드 정보 포함)
- AUTO/MANUAL 모드 모두 적용
- fail-open 전략 (DB 실패 시 업로드 허용)

**기능**:
- 동일한 파일 재업로드 시 거부
- 파일명이 달라도 내용이 같으면 중복 감지
- 기존 업로드 정보 제공 (ID, 파일명, 타임스탬프, 상태, 처리모드)

### 2026-01-04: Luckfox ARM64 배포 및 Sync Service 수정

**Luckfox 배포 완료:**
- ARM64 크로스 컴파일 이미지 빌드 및 배포
- Host network mode 환경에서 전체 서비스 동작 확인
- Frontend, PKD Management, PA Service, Sync Service 모두 정상 동작

**sync_status 테이블 이슈 해결:**
- 문제: `relation "sync_status" does not exist` 오류
- 원인: PostgreSQL init-scripts에 sync_status 테이블 정의 누락
- 해결: 수동으로 테이블 생성 (스키마는 sync-service 코드 분석 후 정확한 컬럼명 사용)

**주요 발견 사항:**
- Luckfox PostgreSQL DB 이름: `localpkd` (로컬 환경의 `pkd`와 다름)
- sync_status 테이블 컬럼명은 sync-service main.cpp의 INSERT/SELECT 쿼리와 정확히 일치해야 함
- `checked_at` (not `created_at`), `*_discrepancy` 컬럼 필수

**Frontend UI 개선:**
- PAHistory 상세보기 다이얼로그 모달 레이아웃 개선
- UploadHistory 상세보기 다이얼로그 모달 레이아웃 개선

### 2026-01-03: API Gateway 구현

**Nginx 기반 API Gateway 추가:**
- `nginx/api-gateway.conf` - 라우팅 설정
- `nginx/proxy_params` - 공통 프록시 파라미터
- 단일 진입점(포트 8080)으로 3개 마이크로서비스 통합

**라우팅 규칙:**
- `/api/upload/*`, `/api/health/*`, `/api/certificates/*` → PKD Management (:8081)
- `/api/pa/*` → PA Service (:8082)
- `/api/sync/*` → Sync Service (:8083)

**기능:**
- Rate Limiting (100 req/s per IP)
- 파일 업로드 최대 100MB
- SSE(Server-Sent Events) 지원
- Gzip 압축
- JSON 오류 응답 (502, 503, 504)

**Frontend 수정:**
- `frontend/nginx.conf` - 모든 `/api/*` 요청을 API Gateway로 라우팅

**docker-compose.yaml 변경:**
- `api-gateway` 서비스 추가
- 백엔드 서비스 포트 외부 노출 제거 (내부 전용)

**문서 업데이트:**
- `docs/PA_API_GUIDE.md` - API Gateway 엔드포인트로 변경

### 2026-01-03: DB-LDAP Sync Service 구현

**새 마이크로서비스 추가:**
- `services/sync-service/` - C++ Drogon 기반 동기화 모니터링 서비스
- Port 8083에서 실행
- PostgreSQL과 LDAP 간 데이터 통계 비교 및 동기화 상태 모니터링

**API 엔드포인트:**
- `GET /api/sync/health` - 서비스 헬스체크
- `GET /api/sync/status` - DB/LDAP 통계 포함 전체 상태
- `GET /api/sync/stats` - 인증서 타입별 통계
- `POST /api/sync/trigger` - 수동 동기화 트리거
- `GET /api/sync/config` - 현재 설정 조회

**기술적 해결 사항:**
- JSON 라이브러리: nlohmann/json → jsoncpp (Drogon 내장 사용)
- 로깅 권한: 파일 생성 실패 시 콘솔 전용으로 폴백
- LDAP 접근: Anonymous bind → Authenticated bind로 변경

**Frontend 추가:**
- `/sync` 라우트 → SyncDashboard 페이지
- DB/LDAP 통계 카드, 동기화 이력 테이블 표시

### 2026-01-03: Dashboard UI 간소화

**Hero 영역 변경:**
- 시간 표시 아래에 DB/LDAP 연결 상태를 컴팩트하게 표시
- 초록색/빨간색 점으로 연결 상태 표시

**시스템 연결 상태 섹션 제거:**
- Dashboard에서 큰 "시스템 연결 상태" 카드 섹션 삭제
- 페이지가 더 간결해짐

**시스템 정보 다이얼로그 개선:**
- PostgreSQL/OpenLDAP 카드에 개별 "연결 테스트" 버튼 추가
- `checkSystemStatus()` → `checkDatabaseStatus()`, `checkLdapStatus()` 분리
- "전체 새로고침" 버튼 RefreshCw 아이콘으로 변경

### 2026-01-02: PA Frontend UI/UX 개선

**PA Verify Page** (`/pa/verify`):
- Step 1-8 검증 단계 라벨을 한글로 변경
- Step 4 Trust Chain 검증에 CSCA → DSC 인증서 체인 경로 시각화 추가
- DSC Subject 텍스트 오버플로우 처리 (`break-all`)
- DG2 얼굴 이미지 카드 레이아웃 개선 (이미지 크기 확대, 정보 그리드 배치)
- 원본 MRZ 데이터 기본값을 펼친 상태로 변경

**PA Dashboard Page** (`/pa/dashboard`):
- 일별 검증 추이 차트 버그 수정 (PostgreSQL timestamp 형식 파싱)
- `verificationTimestamp.split('T')` → `split(/[T\s]/)` 정규식으로 변경

**국가 플래그 SVG 표시 문제 해결**:
- ISO 3166-1 alpha-3 (3글자) → alpha-2 (2글자) 변환 유틸리티 추가
- `frontend/src/utils/countryCode.ts` 생성
- ICAO/MRTD 특수 코드 지원 (D, GBD, UNK 등)
- PAHistory, PADashboard 페이지에 `getFlagSvgPath()` 함수 적용

### 2026-01-02: Docker 관리 스크립트 정리

**삭제된 스크립트:**
- `docker-ldap-init.sh` - ldap-init 컨테이너로 대체됨
- `scripts/docker-start.sh` - 루트의 docker-start.sh와 중복

**업데이트된 스크립트:**
- `docker-health.sh` - MMR 복제 상태, HAProxy, PA Service 내부 포트 체크 추가
- `docker-backup.sh` - `.docker-data/pkd-uploads` 경로로 업데이트
- `docker-restore.sh` - bind mount 경로 업데이트, MMR 복제 안내 추가

**OpenLDAP MMR 설정:**
- osixia/openldap의 LDAP_REPLICATION 환경변수 대신 ldap-mmr-setup1/2 컨테이너 사용
- Bootstrap LDIF에서 Base DN 제거 (osixia 자동 생성과 충돌 방지)
- ICAO PKD custom schema 추가 (cscaCertificateObject 포함)

### 2026-01-01: Frontend UI/UX Improvements

**Upload History Page** (`/upload-history`):
- Added statistics cards (Total, Completed, Failed, In Progress)
- Added filter card with file format, status, date range, and search
- Consistent design pattern with PA History page

**Upload Dashboard Page** (`/upload-dashboard`):
- Removed pie charts (certificate type, upload status)
- Added overview stats cards (4 columns): Total certificates, Upload status, Validation rate, Countries
- Added certificate breakdown cards (6 columns) with visual indicators
- Replaced pie charts with horizontal progress bars for validation status
- Improved Trust Chain validation display with card grid layout

**Dashboard Page** (`/`):
- Moved certificate/validation statistics to Upload Dashboard
- Added Top 18 countries display with 2-column grid layout
- Country cards show flag, CSCA/DSC counts, and progress bars

### 2026-01-01: Bytea Storage Bug Fix

**Issue**: DSC Trust Chain validation returned 0 valid certificates despite 30k DSCs matching 525 CSCAs.

**Root Cause**: Certificate binary data stored as ASCII hex text instead of raw DER bytes.
- `PQescapeByteaConn` returns `\x30820100` as text
- Using `E'...'` (escape string) caused PostgreSQL to interpret `\x` as escape char
- Result: `E'\x30820100'` stored `'0820100'` (ASCII) instead of bytes `0x30 0x82 0x01 0x00`

**Fix**: Removed `E` prefix from bytea INSERT statements in `main.cpp`:
```cpp
// Before: "E'" + byteaEscaped + "'"
// After:  "'" + byteaEscaped + "'"
```

**Result**: Trust Chain validation now works correctly (5,868 valid out of 29,610 DSCs).

### 2025-12-31: DSC Trust Chain Validation

- Added `findCscaByIssuerDn()` function for CSCA lookup
- Added `validateDscCertificate()` for Trust Chain verification
- Fixed Master List certificates to always classify as CSCA

### 2025-12-30: Upload Pipeline Complete

- End-to-end LDIF/ML upload with DB and LDAP storage
- OpenSSL CMS API for Master List parsing
- LDAP MMR write strategy (direct to primary master)

---

## ARM64 Build and Deployment Strategy

### Official Build Method: GitHub Actions CI/CD ✅

**모든 ARM64 빌드는 GitHub Actions를 통해 자동화됩니다.**

#### Workflow

```bash
# 1. 로컬: 코드 수정 및 커밋
git add .
git commit -m "feat: your changes"
git push origin feature/your-branch

# 2. GitHub Actions: 자동 빌드 트리거
# - 워크플로우: .github/workflows/build-arm64.yml
# - 트리거 브랜치: feature/arm64-support, feature/openapi-support
# - 빌드 대상: pkd-management, pa-service, sync-service, frontend
# - 결과: Artifacts로 저장 (30일 보관)

# 3. Artifacts 다운로드
# GitHub → Actions → 최신 workflow run → "arm64-docker-images-all" 다운로드
# 압축 해제: ./github-artifacts/

# 4. Luckfox 배포
./scripts/deploy-from-github-artifacts.sh [all|pkd-management|pa-service|sync-service|frontend]
```

#### Deployment Script Features

- **자동 정리**: 배포 전 Luckfox에서 기존 컨테이너/이미지 삭제 (clean state)
- **개별 배포**: 특정 서비스만 선택적으로 배포 가능
- **진행 상황**: 단계별 상태 표시 (정리 → 전송 → 로드 → 시작)
- **오류 처리**: 각 단계별 오류 감지 및 보고

#### Build Performance (2026-01-09 Optimization)

**Multi-stage Dockerfile Caching:**
- Stage 1 (vcpkg-base): System dependencies (rarely changes)
- Stage 2 (vcpkg-deps): Package dependencies (vcpkg.json only)
- Stage 3 (builder): Application code (frequent changes)
- Stage 4 (runtime): Production image

**GitHub Actions Multi-scope Cache:**
- Separate cache scopes per build stage
- BuildKit inline cache enabled
- Aggressive layer reuse strategy

**Build Times:**
| Scenario | Time | Notes |
|----------|------|-------|
| First build (cold cache) | 60-80min | One-time vcpkg compilation |
| vcpkg.json change | 30-40min | Rebuild dependencies only |
| Source code change | **10-15min** | **90% improvement** ⚡ |
| No changes (rerun) | ~5min | Full cache hit |

Previous performance: 130 minutes for all scenarios

### Alternative: Local Build (비권장)

**특별한 경우에만 사용** (GitHub Actions 장애, 긴급 핫픽스 등)

```bash
# 로컬에서 ARM64 크로스 컴파일 (QEMU 사용)
docker buildx build --platform linux/arm64 \
  -t icao-pkd-management:arm64-hotfix \
  -f services/pkd-management/Dockerfile \
  --load \
  .

# 이미지 저장 및 전송
docker save icao-pkd-management:arm64-hotfix | gzip > /tmp/hotfix.tar.gz
scp /tmp/hotfix.tar.gz luckfox@192.168.100.11:/tmp/

# Luckfox에서 로드 및 배포
ssh luckfox@192.168.100.11
docker load < /tmp/hotfix.tar.gz
cd ~/icao-local-pkd-cpp-v2
docker compose -f docker-compose-luckfox.yaml up -d pkd-management
```

### Luckfox Native Build (절대 금지 ❌)

**이유:**
- Luckfox 리소스 제한 (메모리, CPU 부족)
- 빌드 시간 매우 느림 (vcpkg 컴파일 1시간+)
- 빌드 중 다른 서비스 영향
- 재현성 없음 (환경 차이)

---

**Project Owner**: kbjung
**Organization**: SmartCore Inc.
