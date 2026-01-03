# ICAO Local PKD - C++ Implementation

**Version**: 1.3
**Last Updated**: 2026-01-03
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

### Build from Source

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

## Change Log

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

**Project Owner**: kbjung
**Organization**: SmartCore Inc.
