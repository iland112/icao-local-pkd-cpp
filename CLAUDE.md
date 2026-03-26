# ICAO Local PKD - Development Guide

**Current Version**: v2.41.0
**Last Updated**: 2026-03-26
**Status**: Multi-DBMS Support Complete (PostgreSQL + Oracle)

---

## Critical Project Requirement

**MULTI-DBMS SUPPORT IS MANDATORY**

This project **MUST support multiple database systems** including PostgreSQL, Oracle, and potentially other RDBMS. This is a **non-negotiable requirement**, not an optional feature.

- All code must be database-agnostic
- Runtime database switching via `DB_TYPE` environment variable
- Query Executor Pattern (`IQueryExecutor`) for database abstraction
- All services (PKD Management, PA Service, PKD Relay, AI Analysis) support both PostgreSQL and Oracle

---

## Quick Start

| Component | Address |
|-----------|---------|
| PKD Management | :8081 |
| PA Service | :8082 |
| PKD Relay | :8083 |
| Monitoring Service | :8084 |
| AI Analysis Service | :8085 |
| API Gateway | http://localhost:18080/api |
| API Gateway (SSL) | https://pkd.smartcoreinc.com/api |
| Frontend | http://localhost:13080 |

**Technology Stack**: C++20, Drogon, Python 3.12, FastAPI, scikit-learn, PostgreSQL 15 / Oracle XE 21c, OpenLDAP (MMR), React 19, TypeScript, Tailwind CSS

```bash
./docker-start.sh                                    # Start system
./scripts/build/rebuild-pkd-relay.sh [--no-cache]     # Rebuild service
source scripts/helpers/ldap-helpers.sh && ldap_count_all  # LDAP check
./docker-health.sh                                    # Health check
```

**Complete Guide**: See [docs/DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md)

---

## Architecture

```
Frontend (React 19) --> API Gateway (nginx :80/:443) --> Backend Services --> DB/LDAP
                                                   |
                                                   +-- PKD Management (:8081) — Upload, Search, Auth
                                                   +-- PA Service (:8082) — Passive Authentication
                                                   +-- PKD Relay (:8083) — DB-LDAP Sync, ICAO LDAP
                                                   +-- Monitoring (:8084) — Metrics, Health
                                                   +-- AI Analysis (:8085) — ML Anomaly Detection
```

### Design Patterns

| Pattern | Usage |
|---------|-------|
| **Repository** | 100% SQL abstraction — zero SQL in controllers |
| **Query Executor** | `IQueryExecutor` — PostgreSQL/Oracle 런타임 전환 |
| **Query Helpers** | `common::db::` — boolLiteral, paginationClause, scalarToInt, hexPrefix |
| **Service Container** | Centralized DI (pimpl), all repos/services/handlers |
| **Handler** | Route handlers extracted from main.cpp |
| **Factory** | Runtime DB selection via DB_TYPE |
| **RAII** | Connection pooling (DB and LDAP) |

### Shared Libraries (`shared/lib/`)

| Library | Purpose |
|---------|---------|
| `icao::database` | DB connection pool, Query Executor (PostgreSQL + Oracle) |
| `icao::ldap` | Thread-safe LDAP connection pool (min=2, max=10) |
| `icao::audit` | Unified audit logging (operation_audit_log) |
| `icao::config` | Configuration management |
| `icao::certificate-parser` | X.509 certificate parsing |
| `icao::icao9303` | ICAO 9303 SOD/DG parsers |
| `icao::validation` | Trust chain, CRL, extensions, algorithm compliance |

### LDAP Structure

```
dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
+-- dc=data
|   +-- c={COUNTRY}
|       +-- o=csca / o=mlsc / o=lc / o=dsc / o=crl / o=ml
+-- dc=nc-data
    +-- c={COUNTRY}
        +-- o=dsc   (Non-conformant DSC)

※ ICAO PKD에는 o=csca 없음 — CSCA는 ML CMS SignedData에서 추출
※ 로컬 PKD의 o=csca, o=mlsc, o=lc는 로컬 확장 (PA 검증용)
```

---

## Security

- 100% parameterized SQL queries (all services)
- JWT (HS256) + RBAC (admin/user) + API Key (X-API-Key, SHA-256, per-client rate limiting)
- AES-256-GCM PII encryption (개인정보보호법 제29조)
- Dual audit: `auth_audit_log` + `operation_audit_log`
- CSP header, LDAP DN injection 방지, HSTS, nginx rate limiting

---

## Database Configuration

```bash
# PostgreSQL (recommended)
DB_TYPE=postgres  DB_HOST=postgres  DB_PORT=5432  DB_NAME=localpkd  DB_USER=pkd

# Oracle
DB_TYPE=oracle  ORACLE_HOST=oracle  ORACLE_PORT=1521  ORACLE_SERVICE_NAME=XEPDB1
```

### Oracle Compatibility Notes

| Issue | PostgreSQL | Oracle | Solution |
|-------|-----------|--------|----------|
| Column names | lowercase | UPPERCASE | Auto-lowercase in OracleQueryExecutor |
| Booleans | TRUE/FALSE | 1/0 | `common::db::boolLiteral()` |
| Pagination | LIMIT/OFFSET | OFFSET ROWS FETCH NEXT | `common::db::paginationClause()` |
| Empty string | '' | NULL | IS NOT NULL filter |
| CLOB mixed fetch | TEXT | OCI 1행만 반환 | `TO_CHAR(clob)`, `RAWTOHEX(blob)` |

---

## Development Workflow

```bash
# Development build (cached, 2-3 min)
docker-compose -f docker/docker-compose.yaml build <service-name>

# Deployment build (clean, 20-30 min)
docker-compose -f docker/docker-compose.yaml build --no-cache <service-name>

# Testing
cd frontend && npm test                    # Vitest (98 files, 1485 tests)
cd services/ai-analysis && pytest tests/   # pytest (201 tests)
# C++ GTest: Docker 빌드 환경 필요 (icao-vcpkg-base)
```

**When to use `--no-cache`**: CMakeLists.txt 변경, 새 라이브러리 추가, Dockerfile 수정, 최종 배포

---

## Credentials (DO NOT COMMIT)

- **PostgreSQL**: postgres:5432, localpkd, pkd
- **LDAP**: openldap1:389, cn=admin,dc=ldap,dc=smartcoreinc,dc=com, Password: `ldap_test_password_123`
- **LDAP Base DN**: dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com

---

## Shell Scripts

```
scripts/
├── docker/      # Docker management (start, stop, restart, health, logs, backup)
├── podman/      # Podman for Production RHEL 9
├── luckfox/     # ARM64 deployment
├── build/       # Build scripts (rebuild-*, check-freshness)
├── helpers/     # db-helpers.sh, ldap-helpers.sh
├── maintenance/ # reset-all-data, reset-ldap
├── ssl/         # init-cert, renew-cert
├── deploy/      # from-github-artifacts
└── dev/         # Dev environment scripts
```

---

## Key Architectural Decisions

- **UUIDs** for primary keys across all services
- **Fingerprint-based LDAP DNs** (SHA-256 hex): `cn={FP},o={TYPE},c={COUNTRY},dc=data,...`
- **LDAP Strategy**: Read=Software LB (openldap1/2:389), Write=Direct primary (openldap1:389)
- **Connection Pooling**: DB/LDAP configurable via env vars (DB_POOL_MIN/MAX, LDAP_POOL_MIN/MAX)
- **Reconciliation**: stored_in_ldap=FALSE → LDAP 확인 → 추가 → TRUE (CSCA, DSC, CRL)

---

## Common Issues & Solutions

| Problem | Solution |
|---------|----------|
| Binary version mismatch | `rebuild-*.sh --no-cache` |
| LDAP `Invalid credentials (49)` | Use `ldap_test_password_123` (NOT "admin") |
| Oracle "Value is not convertible to Int" | `common::db::scalarToInt()` / `boolLiteral()` |
| Oracle empty string = NULL | `WHERE column IS NOT NULL` |
| 502 after container restart | nginx `resolver 127.0.0.11 valid=10s` |

---

## Production Data

| Type | Count | LDAP | Coverage |
|------|-------|------|----------|
| CSCA | 845 | 845 | 100% |
| DSC | 29,838 | 29,838 | 100% |
| DSC_NC | 502 | 502 | 100% |
| CRL | 69 | 69 | 100% |
| **Total** | **31,212** | **31,212** | **100%** |

---

## Service-Specific Documentation

각 서비스의 상세 API, 코드 구조, 설정은 서비스별 CLAUDE.md 참조:

- [services/pkd-management/CLAUDE.md](services/pkd-management/CLAUDE.md) — Upload, Search, Auth, API Client
- [services/pa-service/CLAUDE.md](services/pa-service/CLAUDE.md) — Passive Authentication 8단계
- [services/pkd-relay-service/CLAUDE.md](services/pkd-relay-service/CLAUDE.md) — DB-LDAP Sync, ICAO LDAP
- [services/ai-analysis/CLAUDE.md](services/ai-analysis/CLAUDE.md) — ML Anomaly Detection
- [frontend/CLAUDE.md](frontend/CLAUDE.md) — 28개 페이지, 컴포넌트, 디자인 시스템

## Version History

최근 변경 사항은 [docs/CHANGELOG.md](docs/CHANGELOG.md) 참조.

- **v2.41.0** (2026-03-26) — 서비스 기능 재배치 (Sync↔Upload 교차 이동, -18,174줄 dead code 제거)
- **v2.40.0** (2026-03-24) — 권한 구조 5그룹 개편 + ICAO LDAP TLS Private CA 통합
- **v2.39.0** (2026-03-22) — ICAO PKD LDAP 자동 동기화 + CSR 기반 TLS 인증서 발급
- **v2.38.0** (2026-03-21) — Client PA Trust Materials API + 업로드 중복 표시 개선
- **v2.37.0** (2026-03-18) — 6차 코드 보안 강화 + 권한 관리 수정 + 브랜드 리네이밍

## Documentation Index

| Category | Document |
|----------|----------|
| **Guides** | [DEVELOPMENT_GUIDE](docs/DEVELOPMENT_GUIDE.md), [PA_API_GUIDE](docs/PA_API_GUIDE.md), [API_CLIENT_ADMIN_GUIDE](docs/API_CLIENT_ADMIN_GUIDE.md) |
| **Architecture** | [SOFTWARE_ARCHITECTURE](docs/SOFTWARE_ARCHITECTURE.md), [ARCHITECTURE_DESIGN_PRINCIPLES](docs/ARCHITECTURE_DESIGN_PRINCIPLES.md) |
| **Standards** | [DOC9303_COMPLIANCE_CHECKS](docs/DOC9303_COMPLIANCE_CHECKS.md), [PII_ENCRYPTION_COMPLIANCE](docs/PII_ENCRYPTION_COMPLIANCE.md) |
| **Build/Deploy** | [DEPLOYMENT_PROCESS](docs/DEPLOYMENT_PROCESS.md), [LUCKFOX_DEPLOYMENT](docs/LUCKFOX_DEPLOYMENT.md) |
| **OpenAPI** | [pkd-management.yaml](docs/openapi/pkd-management.yaml), [pa-service.yaml](docs/openapi/pa-service.yaml), [pkd-relay.yaml](docs/openapi/pkd-relay-service.yaml) |
| **Changelog** | [docs/CHANGELOG.md](docs/CHANGELOG.md) — 전체 버전 이력 (v1.8.0 ~ v2.39.0) |
