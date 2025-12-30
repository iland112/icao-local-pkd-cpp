# Service Separation Plan

## Overview

현재 단일 `icao-local-pkd` 애플리케이션을 2개의 독립적인 서비스로 분리:

1. **Local PKD Management Service** - ICAO PKD 파일 관리
2. **Passive Authentication (PA) Service** - 전자여권 PA 검증

## Current Architecture (Monolithic)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         icao-local-pkd (Single App)                      │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │
│  │ File Upload │  │ File Parse  │  │ Cert Valid  │  │ PA Verify   │    │
│  │ Module      │  │ Module      │  │ Module      │  │ Module      │    │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                    │                                   │
                    ↓                                   ↓
            ┌───────────────┐                  ┌───────────────┐
            │  PostgreSQL   │                  │   OpenLDAP    │
            │    :5432      │                  │  (HAProxy)    │
            └───────────────┘                  └───────────────┘
```

## Target Architecture (Microservices)

```
┌─────────────────────────────────────┐  ┌─────────────────────────────────────┐
│    Local PKD Management Service     │  │     Passive Authentication Service   │
│              (Port: 8081)           │  │              (Port: 8082)            │
│  ┌─────────────┐  ┌─────────────┐  │  │  ┌─────────────┐  ┌─────────────┐  │
│  │ File Upload │  │ File Parse  │  │  │  │ PA Verify   │  │  DG Parse   │  │
│  │ (LDIF/ML)   │  │ (X.509/CRL) │  │  │  │ (SOD/Chain) │  │  (DG1/DG2)  │  │
│  ├─────────────┤  ├─────────────┤  │  │  ├─────────────┤  ├─────────────┤  │
│  │ Cert Valid  │  │ LDAP Sync   │  │  │  │ LDAP Lookup │  │  CRL Check  │  │
│  │ (CSCA/DSC)  │  │ (MMR)       │  │  │  │ (CSCA/DSC)  │  │ (Revocation)│  │
│  └─────────────┘  └─────────────┘  │  │  └─────────────┘  └─────────────┘  │
│                                     │  │                                     │
│  REST API:                          │  │  REST API:                          │
│  - POST /api/upload/ldif            │  │  - POST /api/pa/verify              │
│  - POST /api/upload/masterlist      │  │  - POST /api/pa/parse-dg1           │
│  - GET  /api/upload/history         │  │  - POST /api/pa/parse-dg2           │
│  - GET  /api/upload/statistics      │  │  - GET  /api/pa/history             │
│  - GET  /api/certificates           │  │  - GET  /api/pa/{id}                │
│  - GET  /api/crls                   │  │                                     │
└─────────────────────────────────────┘  └─────────────────────────────────────┘
            │           │                            │           │
            ↓           ↓                            ↓           ↓
    ┌───────────────────────────────────────────────────────────────────┐
    │                     Shared Infrastructure                          │
    │  ┌───────────────┐                  ┌───────────────────────────┐ │
    │  │  PostgreSQL   │                  │     OpenLDAP MMR Cluster  │ │
    │  │    :5432      │                  │  ┌─────────┐ ┌─────────┐  │ │
    │  │               │                  │  │ Primary │←→│Replica │  │ │
    │  │ - certificate │                  │  └────┬────┘ └────┬────┘  │ │
    │  │ - crl         │                  │       └────┬──────┘       │ │
    │  │ - pa_verify   │                  │           ↓               │ │
    │  │ - audit_log   │                  │      ┌──────────┐         │ │
    │  └───────────────┘                  │      │ HAProxy  │ :389    │ │
    │                                     │      └──────────┘         │ │
    └─────────────────────────────────────────────────────────────────────┘
```

## Directory Structure

```
icao-local-pkd/
├── services/
│   ├── pkd-management/           # Local PKD Management Service
│   │   ├── CMakeLists.txt
│   │   ├── vcpkg.json
│   │   ├── src/
│   │   │   ├── main.cpp
│   │   │   ├── fileupload/       # File Upload bounded context
│   │   │   ├── fileparsing/      # File Parsing bounded context
│   │   │   ├── certificatevalidation/
│   │   │   └── ldapintegration/
│   │   ├── Dockerfile
│   │   └── config/
│   │
│   └── pa-service/               # Passive Authentication Service
│       ├── CMakeLists.txt
│       ├── vcpkg.json
│       ├── src/
│       │   ├── main.cpp
│       │   └── passiveauthentication/  # PA bounded context
│       ├── Dockerfile
│       └── config/
│
├── shared/                        # Shared kernel (copied to each service)
│   ├── domain/
│   ├── exception/
│   └── util/
│
├── docker/
│   ├── docker-compose.yaml       # All services composition
│   ├── postgres/
│   └── openldap/
│
└── docs/
    └── SERVICE_SEPARATION_PLAN.md
```

## Service Responsibilities

### 1. Local PKD Management Service

| Module | Responsibility |
|--------|----------------|
| File Upload | LDIF/Master List 파일 업로드, 저장 |
| File Parsing | X.509 인증서, CRL, Master List 파싱 |
| Certificate Validation | CSCA/DSC 검증, Trust Chain 구축 |
| LDAP Integration | OpenLDAP 동기화 (쓰기 전용) |

**API Endpoints:**
- `POST /api/upload/ldif` - LDIF 파일 업로드
- `POST /api/upload/masterlist` - Master List 업로드
- `GET /api/upload/history` - 업로드 이력
- `GET /api/upload/statistics` - 업로드 통계
- `GET /api/upload/{uploadId}` - 업로드 상세
- `GET /api/progress/stream/{uploadId}` - SSE 진행 상태
- `GET /api/certificates` - 인증서 목록
- `GET /api/crls` - CRL 목록
- `GET /api/health` - Health Check

### 2. Passive Authentication Service

| Module | Responsibility |
|--------|----------------|
| SOD Parser | Security Object Document 파싱 |
| Trust Chain | DSC → CSCA Trust Chain 검증 |
| CRL Checker | 인증서 폐기 상태 확인 |
| DG Parser | DG1 (MRZ), DG2 (Face) 파싱 |

**API Endpoints:**
- `POST /api/pa/verify` - Passive Authentication 수행
- `POST /api/pa/parse-dg1` - DG1 (MRZ) 파싱
- `POST /api/pa/parse-dg2` - DG2 (Face Image) 파싱
- `GET /api/pa/history` - PA 검증 이력
- `GET /api/pa/{id}` - PA 결과 상세
- `GET /api/pa/statistics` - PA 통계
- `GET /api/health` - Health Check

## Database Schema Sharing

### Tables by Service

| Table | PKD Management | PA Service |
|-------|----------------|------------|
| `uploaded_file` | READ/WRITE | - |
| `certificate` | READ/WRITE | READ |
| `crl` | READ/WRITE | READ |
| `master_list` | READ/WRITE | - |
| `pa_verification` | - | READ/WRITE |
| `pa_data_group` | - | READ/WRITE |
| `revoked_certificate` | READ/WRITE | READ |
| `audit_log` | READ/WRITE | READ/WRITE |

## LDAP Access Patterns

### PKD Management Service
- **Write**: CSCA, DSC, CRL 저장 (primary master로 직접 연결)
- **Read**: 중복 확인, 기존 데이터 조회 (HAProxy 경유)

### PA Service
- **Read Only**: CSCA/DSC 조회, CRL 조회 (HAProxy 경유)
- No write access needed

## Configuration

### Environment Variables

**PKD Management Service:**
```env
SERVICE_NAME=pkd-management
PORT=8081
DB_HOST=postgres
DB_PORT=5432
DB_NAME=localpkd
DB_USER=pkd
DB_PASSWORD=pkd123
LDAP_READ_HOST=haproxy
LDAP_READ_PORT=389
LDAP_WRITE_HOST=openldap1
LDAP_WRITE_PORT=389
LDAP_BIND_DN=cn=admin,dc=ldap,dc=smartcoreinc,dc=com
LDAP_BIND_PASSWORD=admin
TRUST_ANCHOR_PATH=/app/data/cert/UN_CSCA_2.pem
```

**PA Service:**
```env
SERVICE_NAME=pa-service
PORT=8082
DB_HOST=postgres
DB_PORT=5432
DB_NAME=localpkd
DB_USER=pkd
DB_PASSWORD=pkd123
LDAP_HOST=haproxy
LDAP_PORT=389
LDAP_BIND_DN=cn=admin,dc=ldap,dc=smartcoreinc,dc=com
LDAP_BIND_PASSWORD=admin
```

## Docker Compose

```yaml
version: '3.8'

services:
  pkd-management:
    build:
      context: ./services/pkd-management
    ports:
      - "8081:8081"
    environment:
      - SERVICE_NAME=pkd-management
      # ... other env vars
    depends_on:
      postgres:
        condition: service_healthy
    volumes:
      - ./data/cert:/app/data/cert:ro
      - uploads:/app/uploads

  pa-service:
    build:
      context: ./services/pa-service
    ports:
      - "8082:8082"
    environment:
      - SERVICE_NAME=pa-service
      # ... other env vars
    depends_on:
      postgres:
        condition: service_healthy

  postgres:
    image: postgres:15
    # ... config

  openldap1:
    build: ./docker/openldap
    # ... config (primary)

  openldap2:
    build: ./docker/openldap
    # ... config (replica)

  haproxy:
    image: haproxy:2.9
    # ... config

  nginx:
    image: nginx:alpine
    ports:
      - "80:80"
    # API Gateway for frontend routing
```

## Migration Steps

### Phase 1: Code Refactoring
1. Extract shared code to `shared/` directory
2. Create service-specific `main.cpp` files
3. Update CMakeLists.txt for each service
4. Create separate Dockerfiles

### Phase 2: Service Splitting
1. Create `services/pkd-management/` structure
2. Create `services/pa-service/` structure
3. Copy relevant bounded contexts to each service
4. Update imports and dependencies

### Phase 3: Integration Testing
1. Build both services independently
2. Test inter-service communication
3. Verify database access patterns
4. Test LDAP read/write separation

### Phase 4: Deployment
1. Update docker-compose.yaml
2. Add nginx as API gateway
3. Configure health checks
4. Deploy and monitor

## Benefits

1. **Independent Scaling**: PKD Management와 PA Service를 독립적으로 스케일
2. **Fault Isolation**: 하나의 서비스 장애가 다른 서비스에 영향 없음
3. **Technology Flexibility**: 각 서비스별 최적화된 기술 선택 가능
4. **Team Ownership**: 팀별 서비스 책임 분리 가능
5. **Security**: PA Service는 LDAP 읽기 전용으로 제한

## Timeline Estimate

| Phase | Tasks | Status |
|-------|-------|--------|
| Phase 1 | Code Refactoring | Pending |
| Phase 2 | Service Splitting | Pending |
| Phase 3 | Integration Testing | Pending |
| Phase 4 | Deployment | Pending |

---

**Document Version**: 1.0
**Created**: 2025-12-30
**Author**: Claude Code
