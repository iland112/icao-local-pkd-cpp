# ICAO Local PKD - Development Guide

**Current Version**: v2.7.0
**Last Updated**: 2026-02-12
**Status**: Multi-DBMS Support Complete (PostgreSQL + Oracle)

---

## Critical Project Requirement

**MULTI-DBMS SUPPORT IS MANDATORY**

This project **MUST support multiple database systems** including PostgreSQL, Oracle, and potentially other RDBMS. This is a **non-negotiable requirement**, not an optional feature.

- All code must be database-agnostic
- Runtime database switching via `DB_TYPE` environment variable
- Query Executor Pattern (`IQueryExecutor`) for database abstraction
- All services (PKD Management, PA Service, PKD Relay) support both PostgreSQL and Oracle

---

## Quick Start

### Essential Information

| Component | Address |
|-----------|---------|
| PKD Management | :8081 |
| PA Service | :8082 |
| PKD Relay | :8083 |
| API Gateway | http://localhost:8080/api |
| Frontend | http://localhost:3000 |

**Technology Stack**: C++20, Drogon, PostgreSQL 15 / Oracle XE 21c, OpenLDAP (MMR), React 19, TypeScript, Tailwind CSS

### Daily Commands

```bash
# Start system
./docker-start.sh

# Rebuild service
./scripts/build/rebuild-pkd-relay.sh [--no-cache]

# Helper functions
source scripts/helpers/ldap-helpers.sh && ldap_count_all
source scripts/helpers/db-helpers.sh && db_count_crls

# Health check
./docker-health.sh
```

**Complete Guide**: See [docs/DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md)

---

## Architecture

### System Overview

```
Frontend (React 19) --> API Gateway (nginx :8080) --> Backend Services --> DB/LDAP
                                                        |
                                                        +-- PKD Management (:8081)
                                                        |     Upload, Certificate Search, ICAO Sync, Auth
                                                        |
                                                        +-- PA Service (:8082)
                                                        |     Passive Authentication (ICAO 9303)
                                                        |
                                                        +-- PKD Relay (:8083)
                                                              DB-LDAP Sync, Reconciliation
```

### Design Patterns

| Pattern | Usage |
|---------|-------|
| **Repository Pattern** | 100% SQL abstraction - zero SQL in controllers |
| **Query Executor Pattern** | Database-agnostic query execution (IQueryExecutor) |
| **Factory Pattern** | Runtime database selection via DB_TYPE |
| **Strategy Pattern** | Processing strategies (AUTO/MANUAL upload modes) |
| **RAII** | Connection pooling (DB and LDAP) |

### Shared Libraries (`shared/lib/`)

| Library | Purpose |
|---------|---------|
| `icao::database` | DB connection pool, Query Executor (PostgreSQL + Oracle) |
| `icao::ldap` | Thread-safe LDAP connection pool (min=2, max=10) |
| `icao::audit` | Unified audit logging (operation_audit_log) |
| `icao::config` | Configuration management |
| `icao::exception` | Custom exception types |
| `icao::logging` | Structured logging (spdlog) |
| `icao::certificate-parser` | X.509 certificate parsing |
| `icao::icao9303` | ICAO 9303 SOD/DG parsers |

### LDAP Structure

```
dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
+-- dc=data
|   +-- c={COUNTRY}
|       +-- o=csca  (CSCA certificates)
|       +-- o=mlsc  (Master List Signer Certificates)
|       +-- o=dsc   (DSC certificates)
|       +-- o=crl   (CRLs)
|       +-- o=ml    (Master Lists)
+-- dc=nc-data
    +-- c={COUNTRY}
        +-- o=dsc   (Non-conformant DSC)
```

---

## Current Features (v2.7.0)

### Core Functionality

- LDIF/Master List upload (AUTO/MANUAL modes)
- Individual certificate upload with preview-before-save workflow (PEM, DER, P7B, DL, CRL)
- Master List file processing (537 certificates: 1 MLSC + 536 CSCA/LC)
- Country-based LDAP storage (95+ countries)
- Certificate validation (Trust Chain, CRL, Link Certificates)
- LDAP integration (MMR cluster, Software Load Balancing)
- Passive Authentication verification (ICAO 9303 Part 10 & 11)
- DB-LDAP sync monitoring with auto-reconciliation
- Certificate search & export (country/type/status filters)
- ICAO PKD version monitoring (auto-detection of new versions)
- Trust chain visualization (frontend tree component)
- Upload issues tracking (duplicate detection with tab-based UI)
- LDIF/ASN.1 structure visualization
- Real-time upload statistics streaming (SSE)
- X.509 metadata extraction (22 fields per certificate)
- ICAO 9303 compliance checking (6 validation categories)

### Security

- 100% parameterized SQL queries (all services)
- JWT authentication (HS256) + RBAC (admin/user)
- Credential externalization (.env)
- File upload validation (MIME type, path sanitization)
- Dual audit logging: `auth_audit_log` (authentication events) + `operation_audit_log` (operations)
- IP tracking and User-Agent logging

### Multi-DBMS Support

- PostgreSQL 15 (production recommended, 10-50x faster)
- Oracle XE 21c (enterprise environments)
- Runtime switching via `DB_TYPE` environment variable
- All 3 services fully support both databases
- Oracle-specific handling: UPPERCASE columns, NUMBER(1) booleans, OFFSET/FETCH pagination, empty-string-as-NULL

---

## API Endpoints

### PKD Management (via :8080/api)

**Upload Management**:
- `POST /api/upload/ldif` - Upload LDIF file
- `POST /api/upload/masterlist` - Upload Master List file
- `POST /api/upload/certificate` - Upload individual certificate (PEM, DER, P7B, DL, CRL)
- `POST /api/upload/certificate/preview` - Preview certificate (parse only, no save)
- `GET /api/upload/history` - Upload history (paginated)
- `GET /api/upload/detail/{id}` - Upload detail by ID
- `DELETE /api/upload/{id}` - Delete upload
- `GET /api/upload/statistics` - Upload statistics summary
- `GET /api/upload/countries` - Country statistics (dashboard)
- `GET /api/upload/countries/detailed` - Detailed country breakdown
- `GET /api/upload/changes` - Recent upload changes
- `GET /api/upload/{id}/validations` - Trust chain validation results
- `GET /api/upload/{id}/issues` - Duplicate certificates detected
- `GET /api/upload/{id}/ldif-structure` - LDIF/ASN.1 structure
- `GET /api/progress/{id}` - Upload progress SSE stream

**Certificate Search**:
- `GET /api/certificates/countries` - Country list
- `GET /api/certificates/search` - Certificate search (filters: country, type, status)
- `GET /api/certificates/validation` - Validation result by fingerprint
- `GET /api/certificates/export/{format}` - Certificate export

**ICAO Monitoring**:
- `GET /api/icao/status` - ICAO version status comparison
- `GET /api/icao/latest` - Latest ICAO version info
- `GET /api/icao/history` - Version check history
- `GET /api/icao/check-updates` - Trigger ICAO version check

**Authentication**:
- `POST /api/auth/login` - User login
- `POST /api/auth/logout` - User logout
- `POST /api/auth/refresh` - Token refresh
- `GET /api/auth/me` - Current user info
- `GET /api/auth/users` - User management (admin)
- `GET /api/auth/audit-log` - Auth audit logs (admin)

**Audit**:
- `GET /api/audit/operations` - Operation audit logs
- `GET /api/audit/operations/stats` - Operation statistics

### PA Service (via :8080/api/pa)

- `POST /api/pa/verify` - PA verification (full 8-step process)
- `POST /api/pa/parse-sod` - Parse SOD (Security Object Document)
- `POST /api/pa/parse-dg1` - Parse DG1 (MRZ data)
- `POST /api/pa/parse-dg2` - Parse DG2 (Face image extraction)
- `POST /api/pa/parse-mrz-text` - Parse MRZ text
- `GET /api/pa/history` - PA verification history
- `GET /api/pa/statistics` - PA statistics
- `GET /api/pa/{id}` - Verification detail
- `GET /api/pa/{id}/datagroups` - DataGroups detail

### PKD Relay (via :8080/api)

- `GET /api/sync/status` - DB-LDAP sync status
- `GET /api/sync/stats` - Sync statistics
- `POST /api/sync/check` - Trigger manual sync check
- `POST /api/sync/reconcile` - Trigger reconciliation
- `GET /api/sync/reconcile/history` - Reconciliation history
- `GET /api/sync/reconcile/{id}` - Reconciliation detail
- `GET /api/sync/reconcile/stats` - Reconciliation statistics

### Public vs Protected Endpoints

Public endpoints (no JWT required) are defined in [auth_middleware.cpp](services/pkd-management/src/middleware/auth_middleware.cpp) lines 10-93. Key categories:
- **Public**: Health checks, Dashboard statistics, Certificate search, ICAO monitoring, Sync status, PA verification, Certificate preview, Static files
- **Protected**: File uploads (LDIF/ML/Certificate save), User management, Audit logs, Upload deletion

---

## Frontend

### Pages (20 total)

| Page | Route | Purpose |
|------|-------|---------|
| Dashboard | `/` | Homepage with certificate statistics |
| FileUpload | `/upload` | LDIF/Master List upload |
| CertificateUpload | `/upload/certificate` | Individual certificate upload (preview-before-save) |
| CertificateSearch | `/pkd/certificates` | Certificate search & detail |
| UploadHistory | `/upload-history` | Upload management |
| UploadDetail | `/upload/:uploadId` | Upload detail & structure |
| PAVerify | `/pa/verify` | PA verification |
| PAHistory | `/pa/history` | PA history |
| PADetail | `/pa/:paId` | PA detail |
| SyncDashboard | `/sync` | DB-LDAP sync monitoring |
| IcaoStatus | `/icao` | ICAO PKD version tracking |
| MonitoringDashboard | `/monitoring` | System monitoring |
| Login | `/login` | Authentication |
| UserManagement | `/admin/users` | User administration |
| AuditLog | `/admin/audit-log` | Auth audit log viewer |
| OperationAuditLog | `/admin/operation-audit` | Operation audit trail |

### Key Components

| Component | Purpose |
|-----------|---------|
| TreeViewer | Reusable tree visualization (react-arborist) |
| CountryStatisticsDialog | Country-level certificate breakdown |
| TrustChainVisualization | Trust chain path display |
| DuplicateCertificatesTree | Duplicate detection with country grouping |
| LdifStructure / MasterListStructure | File structure visualization |

### Dependencies

React 19, TypeScript, Vite, Tailwind CSS 4, React Router 7, Axios, Zustand, TanStack Query, ECharts, Recharts, Lucide Icons, i18n-iso-countries

---

## Development Workflow

### Build Strategy

```bash
# Phase 1: Development (Cached Build - FAST, 2-3 min)
docker-compose -f docker/docker-compose.yaml build <service-name>

# Phase 2: Deployment (Clean Build - 20-30 min)
docker-compose -f docker/docker-compose.yaml build --no-cache <service-name>
```

**When to use `--no-cache`**: CMakeLists.txt changes, new library additions, dependency changes, Dockerfile modifications, final deployment.

### Rebuild Scripts

```bash
./scripts/build/rebuild-pkd-relay.sh [--no-cache]
./scripts/build/rebuild-frontend.sh
```

### Testing

```bash
# Load helpers
source scripts/helpers/ldap-helpers.sh
source scripts/helpers/db-helpers.sh

# Verify data
ldap_count_all          # Count all LDAP entries
db_count_certs          # Count DB certificates

# Test reconciliation
curl -X POST http://localhost:8080/api/sync/reconcile \
  -H "Content-Type: application/json" \
  -d '{"dryRun": false}' | jq .
```

### Development Environment (Isolated)

```bash
# Start dev environment (separate containers/ports)
docker compose -f docker/docker-compose.dev.yaml up -d

# Rebuild dev service
docker compose -f docker/docker-compose.dev.yaml build --no-cache pkd-management-dev
```

---

## Database Configuration

### PostgreSQL (Recommended)

```bash
DB_TYPE=postgres
DB_HOST=postgres
DB_PORT=5432
DB_NAME=localpkd
DB_USER=pkd
DB_PASSWORD=<from .env>
```

### Oracle

```bash
DB_TYPE=oracle
ORACLE_HOST=oracle
ORACLE_PORT=1521
ORACLE_SERVICE_NAME=XEPDB1
ORACLE_USER=pkd_user
ORACLE_PASSWORD=<from .env>
```

### Switching Databases

```bash
# Edit .env: DB_TYPE=postgres or DB_TYPE=oracle
# Restart services
docker-compose -f docker/docker-compose.yaml restart pkd-management pa-service pkd-relay
```

### Oracle Compatibility Notes

| Issue | PostgreSQL | Oracle | Solution |
|-------|-----------|--------|----------|
| Column names | lowercase | UPPERCASE | Auto-lowercase in OracleQueryExecutor |
| Booleans | TRUE/FALSE | 1/0 | Database-aware formatting |
| Pagination | LIMIT/OFFSET | OFFSET ROWS FETCH NEXT | Database-specific SQL |
| Case search | ILIKE | UPPER() LIKE UPPER() | Conditional SQL |
| JSONB cast | $1::jsonb | $1 (no cast) | Remove cast for Oracle |
| Empty string | '' (empty) | NULL | IS NOT NULL filter |
| Timestamps | NOW() | SYSTIMESTAMP | Database-specific function |

---

## Credentials (DO NOT COMMIT)

**PostgreSQL**: Host: postgres:5432, Database: localpkd, User: pkd
**LDAP**: Host: openldap1:389, Admin: cn=admin,dc=ldap,dc=smartcoreinc,dc=com, Password: ldap_test_password_123
**LDAP Base DN**: dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com

---

## Shell Scripts

```
scripts/
+-- docker/          # Docker management (start, stop, restart, health, logs, backup)
+-- luckfox/         # ARM64 deployment (same structure as docker/)
+-- build/           # Build scripts (build, rebuild-*, check-freshness, verify-*)
+-- helpers/         # Utility functions (db-helpers.sh, ldap-helpers.sh)
+-- maintenance/     # Data management (reset-all-data, reset-ldap, dn-migration)
+-- monitoring/      # System monitoring (icao-version-check)
+-- deploy/          # Deployment (from-github-artifacts)
+-- dev/             # Development scripts (start/stop/rebuild/logs dev services)
```

**Root convenience wrappers**: `./docker-start.sh`, `./docker-stop.sh`, `./docker-health.sh`, `./docker-clean-and-init.sh`

---

## Common Issues & Solutions

### Build version mismatch
**Problem**: Binary version doesn't match source
**Solution**: `./scripts/build/rebuild-pkd-relay.sh --no-cache`

### LDAP authentication failed
**Problem**: `ldap_bind: Invalid credentials (49)`
**Solution**: Use `ldap_test_password_123` (NOT "admin")

### Oracle "Value is not convertible to Int"
**Problem**: Oracle returns all values as strings
**Solution**: Use `getInt()` helper for integer fields, check `getDatabaseType()` for boolean formatting

### Oracle empty string = NULL
**Problem**: `WHERE column != ''` fails on Oracle
**Solution**: Use `WHERE column IS NOT NULL` for Oracle, keep both conditions for PostgreSQL

### 502 Bad Gateway after container restart
**Problem**: nginx cached old container IP
**Solution**: nginx uses `resolver 127.0.0.11 valid=10s` with per-request DNS resolution

### Sync dashboard timeout
**Problem**: "timeout of 60000ms exceeded" on repeated requests
**Solution**: Thread-safe connection pool (RAII pattern) - already implemented in v2.4.2

---

## Key Architectural Decisions

### Database Schema
- UUIDs for primary keys across all services
- Fingerprint-based LDAP DNs (SHA-256 hex)
- Separate tables: certificate, crl, master_list, uploaded_file, deviation_list, deviation_entry
- Audit tables: operation_audit_log, auth_audit_log
- Sync tables: sync_status, reconciliation_summary, reconciliation_log

### LDAP Strategy
- Read: Software Load Balancing (openldap1:389, openldap2:389)
- Write: Direct to primary (openldap1:389)
- DN format: `cn={FINGERPRINT},o={TYPE},c={COUNTRY},dc=data,...`
- Object classes: pkdDownload (certs), cRLDistributionPoint (CRLs)

### Connection Pooling
- Database: min=5, max=20 connections (PostgreSQL), min=2, max=10 (Oracle OCI Session Pool)
- LDAP: min=2, max=10 connections, 5s acquire timeout
- RAII pattern: automatic connection release on scope exit

### Reconciliation Logic
1. Find missing entities (stored_in_ldap=FALSE)
2. Verify against LDAP (actual existence check)
3. Add to LDAP with parent DN auto-creation
4. Mark as stored (stored_in_ldap=TRUE)
5. Log operations (reconciliation_log with fingerprint)
- Scope: CSCA, DSC, CRL (DSC_NC excluded - ICAO deprecated nc-data in 2021)

---

## Production Data Summary

| Certificate Type | Count | LDAP | Coverage |
|------------------|-------|------|----------|
| CSCA | 845 | 845 | 100% |
| MLSC | 27 | 27 | 100% |
| DSC | 29,838 | 29,838 | 100% |
| DSC_NC | 502 | 502 | 100% |
| CRL | 69 | 69 | 100% |
| **Total** | **31,212** | **31,212** | **100%** |

---

## Version History

### v2.7.0 (2026-02-12) - Individual Certificate Upload + Preview-before-Save
- Separated individual certificate upload to dedicated page (`/upload/certificate`)
- Preview-before-save workflow: parse → preview → confirm → save to DB+LDAP
- Backend `POST /api/upload/certificate/preview` endpoint (parse only, no save)
- Supports PEM, DER, CER, P7B, DL (Deviation List), CRL file formats
- Certificate detail tree view (TreeViewer) in preview with General/Details tabs
- DL file support: deviation data parsing + preview (DlParser replacing DvlParser)
- CRL metadata preview (issuer, validity, revoked count)
- Duplicate file detection via SHA-256 hash before save
- FileUpload page narrowed to LDIF/Master List only
- New repositories: CrlRepository, DeviationListRepository
- Complete PGconn→QueryExecutor migration for Oracle compatibility
- Refactored main.cpp upload handlers (~500 lines reduced via UploadService)

### v2.6.3 (2026-02-11) - Oracle Audit Log Complete + ICAO Endpoint Fix
- Complete Oracle compatibility for both `auth_audit_log` and `operation_audit_log` pages
- Replaced 9 PGconn-based audit logging blocks with `icao::audit::logOperation()` using QueryExecutor
- Fixed `AuditRepository` Oracle compatibility (removed `::jsonb` cast, `NOW()`, `ILIKE`)
- Fixed `AuthAuditRepository` Oracle compatibility (pagination, boolean, ILIKE)
- Added `/api/icao/check-updates` to public endpoints (was returning 401)

### v2.6.2 (2026-02-10) - Oracle Statistics & Full Data Upload
- Resolved doubled statistics from duplicate upload records
- Fixed Oracle string-to-integer conversion in country statistics
- Fixed Oracle empty-string-as-NULL filtering in 3 locations
- Implemented OCI Session Pool (min=2, max=10) replacing single connection
- Compact country statistics table layout
- Full production data upload verified (31,212 certificates, 100% DB-LDAP consistency)

### v2.6.1 (2026-02-09) - Phase 6.1: Master List Upload Oracle Support
- Implemented comprehensive `getInt()` helper for Oracle string-to-integer conversion
- Updated 17 integer fields in `jsonToUpload()` for Oracle compatibility
- Master List upload fully functional with Oracle

### v2.6.0-alpha (2026-02-08) - Oracle Authentication Complete
- Pure OCI API implementation (replaced OTL due to SELECT timing issues)
- Oracle column name auto-lowercase conversion (CRITICAL fix)
- Database-aware boolean formatting (1/0 vs TRUE/FALSE)
- User login, JWT validation, audit logging all working with Oracle

### v2.5.0 (2026-02-06) - Phase 5 Complete: All Services Oracle Support
- Phase 5.1: PA Service Query Executor migration + Oracle support
- Phase 5.2: PKD Relay UUID migration + Oracle support
- Phase 5.3: PKD Management UUID migration (IcaoVersion model)
- All 3 services use UUID for primary keys, all support Oracle

### v2.5.0-dev (2026-02-04-05) - Oracle Migration Phase 1-4
- Phase 1: Database abstraction layer (Strategy + Factory patterns)
- Phase 2: OracleQueryExecutor implementation (OTL then OCI)
- Phase 3: Repository layer refactoring (5/5 repositories migrated to QueryExecutor)
- Phase 4: Oracle XE 21c setup, schema migration (20 tables), performance benchmarking
- Result: PostgreSQL 10-50x faster, but Oracle fully supported

### v2.4.3 (2026-02-04) - LDAP Connection Pool Migration Complete
- All 3 services migrated to shared `icao::ldap` connection pool
- 50x performance improvement through connection reuse
- Thread-safe RAII pattern, min=2/max=10 connections

### v2.4.2 (2026-02-04) - Shared Database Connection Pool Library
- Created `icao::database` shared library for thread-safe DB connection pooling
- Resolved sync dashboard timeout errors (single PGconn race condition)
- 11 repositories migrated across all 3 services

### v2.4.0 (2026-02-03) - PKD Relay Repository Pattern Complete
- 5 domain models, 4 repositories, 2 services
- 6 API endpoints migrated, 80% code reduction
- 100% SQL elimination from controller code

### v2.3.x (2026-02-02-04) - UI/UX Enhancements & System Stabilization
- TreeViewer refactoring (-303 lines duplicated code)
- Certificate search UI optimization (i18n-iso-countries, table columns)
- Audit log system with JWT integration
- Comprehensive public endpoint configuration (49 patterns)
- PA Service integration and frontend fixes
- Sync dashboard stability and memory safety improvements

### v2.2.x (2026-01-30-31) - Enhanced Metadata & Critical Fixes
- X.509 metadata extraction (22 fields), ICAO compliance checking
- Master List upload 502 fix (missing file_hash column)
- LDIF structure visualization with DN tree hierarchy
- ASN.1 parser for Master List structure

### v2.1.x (2026-01-26-30) - Repository Pattern & Trust Chain
- Repository Pattern complete (12/12 endpoints, 100% SQL elimination)
- Trust chain validation with link certificate support
- CSCA cache optimization (80% performance improvement)
- DN normalization for format-independent comparison
- PKD Management connection pool implementation

### v2.0.x (2026-01-21-25) - Service Separation & Security
- PKD Relay Service separation from monolith
- 100% parameterized queries, credential externalization
- CRL reconciliation, auto parent DN creation

### v1.8.0-v1.9.0 - Security Hardening
- JWT authentication + RBAC
- File upload security, audit logging

---

## Documentation Index

### Architecture & Design
- [DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md) - Complete development guide
- [REPOSITORY_PATTERN_IMPLEMENTATION_SUMMARY.md](docs/REPOSITORY_PATTERN_IMPLEMENTATION_SUMMARY.md) - Architecture overview
- [MASTER_LIST_PROCESSING_GUIDE.md](docs/MASTER_LIST_PROCESSING_GUIDE.md) - Master List format & processing

### Oracle Migration
- [ORACLE_AUTHENTICATION_COMPLETION.md](docs/ORACLE_AUTHENTICATION_COMPLETION.md) - Oracle auth implementation
- [PHASE_4.6_PERFORMANCE_COMPARISON_COMPLETION.md](docs/PHASE_4.6_PERFORMANCE_COMPARISON_COMPLETION.md) - PostgreSQL vs Oracle benchmarks

### Service Refactoring
- [PA_SERVICE_REFACTORING_PROGRESS.md](docs/PA_SERVICE_REFACTORING_PROGRESS.md) - PA Service architecture
- [PKD_RELAY_REPOSITORY_PATTERN_COMPLETION.md](docs/PKD_RELAY_REPOSITORY_PATTERN_COMPLETION.md) - PKD Relay refactoring

### Phase Reports
- [PHASE_3_API_ROUTE_INTEGRATION_COMPLETION.md](docs/PHASE_3_API_ROUTE_INTEGRATION_COMPLETION.md) - API migration
- [PHASE_5.2_PKD_RELAY_UUID_MIGRATION_COMPLETION.md](docs/PHASE_5.2_PKD_RELAY_UUID_MIGRATION_COMPLETION.md) - UUID migration
- [PHASE_5.2_PKD_RELAY_ORACLE_COMPLETION.md](docs/PHASE_5.2_PKD_RELAY_ORACLE_COMPLETION.md) - Oracle support

### Deployment
- [LUCKFOX_DEPLOYMENT.md](docs/LUCKFOX_DEPLOYMENT.md) - ARM64 deployment guide
- [DOCKER_BUILD_CACHE.md](docs/DOCKER_BUILD_CACHE.md) - Build cache troubleshooting

---

## Contact

For detailed information, see [docs/DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md)
