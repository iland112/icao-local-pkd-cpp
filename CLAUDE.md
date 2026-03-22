# ICAO Local PKD - Development Guide

**Current Version**: v2.39.0
**Last Updated**: 2026-03-22
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

### Essential Information

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
| Frontend (SSL) | https://pkd.smartcoreinc.com |

**Technology Stack**: C++20, Drogon, Python 3.12, FastAPI, scikit-learn, PostgreSQL 15 / Oracle XE 21c, OpenLDAP (MMR), React 19, TypeScript, Tailwind CSS

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
Frontend (React 19) --> API Gateway (nginx :80/:443/:8080) --> Backend Services --> DB/LDAP
                                                        |
                                                        +-- PKD Management (:8081)
                                                        |     Upload, Certificate Search, ICAO Sync, Auth
                                                        |
                                                        +-- PA Service (:8082)
                                                        |     Passive Authentication (ICAO 9303)
                                                        |
                                                        +-- PKD Relay (:8083)
                                                        |     DB-LDAP Sync, Reconciliation
                                                        |
                                                        +-- Monitoring Service (:8084)
                                                        |     System Metrics, Service Health (DB-independent)
                                                        |
                                                        +-- AI Analysis Service (:8085)
                                                              ML Anomaly Detection, Pattern Analysis (Python/FastAPI)
```

### Design Patterns

| Pattern | Usage |
|---------|-------|
| **Repository Pattern** | 100% SQL abstraction - zero SQL in controllers |
| **Query Executor Pattern** | Database-agnostic query execution (IQueryExecutor) |
| **Query Helpers** | `common::db::` utilities — boolLiteral, paginationClause, scalarToInt, hexPrefix |
| **Service Container** | Centralized DI — `ServiceContainer` owns all repos/services/handlers (pimpl) |
| **Handler Pattern** | Route handlers extracted from main.cpp — `registerRoutes(HttpAppFramework&)` |
| **Factory Pattern** | Runtime database selection via DB_TYPE |
| **Strategy Pattern** | Processing strategies (AUTO upload mode) |
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
| `icao::validation` | ICAO 9303 certificate validation (trust chain, CRL, extensions, algorithm compliance) |

### LDAP Structure

#### Local PKD LDAP (OpenLDAP MMR)

```
dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
+-- dc=data
|   +-- c={COUNTRY}
|       +-- o=csca  (CSCA certificates — ML에서 추출, 로컬 확장)
|       +-- o=mlsc  (Master List Signer Certificates — 로컬 확장)
|       +-- o=lc    (Link Certificates — CSCA key rollover, 로컬 확장)
|       +-- o=dsc   (DSC certificates)
|       +-- o=crl   (CRLs)
|       +-- o=ml    (Master Lists)
+-- dc=nc-data
    +-- c={COUNTRY}
        +-- o=dsc   (Non-conformant DSC)
```

#### ICAO PKD LDAP (실제 / 시뮬레이션)

```
dc=download,dc=pkd,dc=icao,dc=int
+-- dc=data
|   +-- c={COUNTRY}
|       +-- o=dsc   (DSC certificates)
|       +-- o=crl   (CRLs)
|       +-- o=ml    (Master Lists — CSCA가 CMS 안에 포함)
|       +-- o=dl    (Deviation Lists)
+-- dc=nc-data
    +-- c={COUNTRY}
        +-- o=dsc   (Non-conformant DSC)

※ ICAO PKD에는 o=csca 없음 — CSCA는 ML CMS SignedData에서 추출
※ 로컬 PKD의 o=csca, o=mlsc, o=lc는 로컬 확장 (PA 검증용)
```

---

## Current Features (v2.21.0)

### Core Functionality

- **API Client Authentication**: External client API Key authentication (X-API-Key header, SHA-256 hash, per-client Rate Limiting, Permission/IP/Endpoint access control, nginx auth_request for cross-service tracking)
- **CSR Management**: ICAO PKD Certificate Signing Request generation (RSA-2048, SHA256withRSA, PKCS#10), DB storage with AES-256-GCM encrypted private key, PEM/DER export
- **Code Master**: DB-based centralized code/status/enum management (21 categories, ~150 codes, CRUD API + frontend hook)
- LDIF/Master List upload (AUTO mode, LDAP-resilient with retry)
- Individual certificate upload with preview-before-save workflow (PEM, DER, P7B, DL, CRL)
- Master List file processing (537 certificates: 1 MLSC + 536 CSCA/LC)
- Country-based LDAP storage (95+ countries)
- Certificate validation (Trust Chain, CRL, Link Certificates)
- LDAP integration (MMR cluster, Software Load Balancing)
- Passive Authentication verification (ICAO 9303 Part 10 & 11)
- Lightweight PA lookup: DSC subject DN or fingerprint → pre-computed trust chain result (no SOD/DG upload)
- **Client PA support**: Trust Materials API — 클라이언트에서 로컬 PA 수행을 위한 CSCA/CRL/LC 제공 + 암호화 MRZ 결과 보고 + 이력/통계
- DSC auto-registration from PA verification (source_type='PA_EXTRACTED')
- **Real-time notification system**: SSE-based backend event broadcasting (NotificationManager singleton, Header Bell UI with unread badge + dropdown panel)
- **DSC expiration revalidation**: Bulk trust chain re-evaluation for expired DSC certificates (Relay ValidationService + SyncDashboard result dialog)
- DB-LDAP sync monitoring with auto-reconciliation
- Certificate search & export (country/type/status/source filters, full DIT-structured ZIP)
- Certificate source tracking and dashboard statistics (bySource)
- ICAO PKD version monitoring (auto-detection of new versions, daily scheduled check)
- ICAO PKD dashboard notification banner (new version alert with dismiss)
- Trust chain visualization (frontend tree component)
- Upload issues tracking (duplicate detection with tab-based UI)
- LDIF/ASN.1 structure visualization
- Real-time upload statistics streaming (SSE) with Event Log panel
- Real-time upload processing status (PROCESSING state with periodic DB updates)
- X.509 metadata extraction (22 fields per certificate)
- ICAO 9303 compliance checking (6 validation categories) with per-certificate DB persistence
- Doc 9303 per-item compliance checklist (~28 checks, certificate type-aware: CSCA/DSC/MLSC)
- Per-certificate validation log streaming (SSE) for real-time EventLog display
- DSC_NC non-conformant certificate report (conformance code/country/year/algorithm charts, CSV export)
- CRL report page (CRL metadata, revoked certificates, revocation reasons, signature algorithms, country distribution, CSV export, CRL file download)
- DSC Trust Chain report page (validation statistics, chain distribution, sample certificate lookup, QuickLookupPanel integration)
- **AI Certificate Forensic Analysis Engine** (ML-based anomaly detection, forensic risk scoring, pattern analysis)
  - Isolation Forest + Local Outlier Factor dual-model anomaly detection (45 features, type-specific models)
  - Certificate type-specific models: CSCA/DSC/DSC_NC/MLSC with optimized contamination rates
  - Composite risk scoring (0~100: 6 categories) + Forensic risk scoring (0~100: 10 categories)
  - Forensic categories: algorithm, key_size, compliance, validity, extensions, anomaly, issuer_reputation, structural_consistency, temporal_pattern, dn_consistency
  - Extension rules engine: ICAO Doc 9303 based extension profile validation per certificate type
  - Issuer profiling: DBSCAN clustering-based behavioral analysis per issuer DN
  - Country-level PKI maturity scoring (algorithm, key size, compliance, extensions, freshness)
  - Algorithm migration trend analysis (by issuance year)
  - Key size distribution analysis (by algorithm family)
  - Daily scheduled batch analysis (APScheduler, configurable hour)
  - Per-certificate anomaly explanation (top-5 deviating features with Korean descriptions)
  - Per-certificate forensic detail (10-category breakdown, findings with severity)

### Security

- 100% parameterized SQL queries (all services)
- JWT authentication (HS256) + RBAC (admin/user) + API Key authentication (X-API-Key, SHA-256)
- Credential externalization (.env)
- File upload validation (MIME type, path sanitization)
- Dual audit logging: `auth_audit_log` (authentication events) + `operation_audit_log` (operations)
- IP tracking and User-Agent logging
- Content-Security-Policy (CSP) header via nginx (XSS defense)
- **Admin 초기화**: `ADMIN_INITIAL_PASSWORD` 환경변수 기반 (DB init 스크립트에 비밀번호 하드코딩 없음)
- **LDAP DN injection 방지**: countryCode ISO 3166-1 alpha-2/3 형식 검증
- **사이드바 RBAC**: `adminOnly` + `permission` 기반 메뉴 필터링 (비관리자에게 관리 메뉴 미표시)
- **PII encryption (개인정보보호법 제29조)**: AES-256-GCM authenticated encryption for personal information fields
  - PKD Management: `api_client_requests` 4개 PII 필드 (성명, 소속, 이메일, 전화번호)
  - PA Service: `pa_verification` 3개 PII 필드 (여권번호, IP, User-Agent)
  - 여권번호(고유식별정보) 저장 시 암호화 — 법적 의무 (법 제24조, 시행령 제21조)
  - `PII_ENCRYPTION_KEY` 환경변수 기반 키 관리, 미설정 시 암호화 비활성화
  - Public API 응답 시 PII 마스킹 (홍*동, h***@example.com)

### Multi-DBMS Support

- PostgreSQL 15 (production recommended, 10-50x faster)
- Oracle XE 21c (enterprise environments)
- Runtime switching via `DB_TYPE` environment variable
- All 4 C++ services + AI Analysis (Python) support both databases
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
- `POST /api/upload/{id}/retry` - Retry failed upload (FAILED only, resume mode)
- `DELETE /api/upload/{id}` - Delete upload
- `GET /api/upload/statistics` - Upload statistics summary
- `GET /api/upload/countries` - Country statistics (dashboard)
- `GET /api/upload/countries/detailed` - Detailed country breakdown
- `GET /api/upload/changes` - Recent upload changes
- `GET /api/upload/{id}/validations` - Trust chain validation results
- `GET /api/upload/{id}/issues` - Duplicate certificates detected
- `GET /api/upload/{id}/ldif-structure` - LDIF/ASN.1 structure
- `GET /api/progress/{id}` - Upload progress SSE stream

**Certificate Search & Validation**:
- `GET /api/certificates/countries` - Country list
- `GET /api/certificates/search` - Certificate search (filters: country, type, status)
- `GET /api/certificates/validation` - Validation result by fingerprint
- `POST /api/certificates/pa-lookup` - Lightweight PA lookup by subject DN or fingerprint
- `GET /api/certificates/dsc-nc/report` - DSC_NC non-conformant certificate report (charts + table)
- `GET /api/certificates/crl/report` - CRL report with statistics, charts, and paginated CRL list
- `GET /api/certificates/crl/{id}` - CRL detail with parsed revoked certificate list
- `GET /api/certificates/crl/{id}/download` - CRL binary file download (.crl)
- `GET /api/certificates/doc9303-checklist` - Doc 9303 compliance checklist by fingerprint
- `GET /api/certificates/pending-dsc` - Pending DSC registration list (paginated, filters)
- `GET /api/certificates/pending-dsc/stats` - Pending DSC statistics
- `POST /api/certificates/pending-dsc/{id}/approve` - Approve pending DSC (JWT required)
- `POST /api/certificates/pending-dsc/{id}/reject` - Reject pending DSC (JWT required)
- `GET /api/certificates/export/{format}` - Certificate export
- `GET /api/certificates/export/all` - Export all LDAP-stored data as DIT-structured ZIP

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

**API Client Management** (admin only, JWT required):
- `POST /api/auth/api-clients` - Create new API client + generate key
- `GET /api/auth/api-clients` - List all clients (pagination, activeOnly filter)
- `GET /api/auth/api-clients/{id}` - Get client detail
- `PUT /api/auth/api-clients/{id}` - Update client (permissions, rate limits, status)
- `DELETE /api/auth/api-clients/{id}` - Deactivate client (soft delete)
- `POST /api/auth/api-clients/{id}/regenerate` - Regenerate API key
- `GET /api/auth/api-clients/{id}/usage` - Usage statistics (days parameter)
- `GET /api/auth/internal/check` - Internal auth check (nginx auth_request only)

**Audit**:
- `GET /api/audit/operations` - Operation audit logs
- `GET /api/audit/operations/stats` - Operation statistics

**Code Master** (centralized code/status management):
- `GET /api/code-master` - List codes (category filter, pagination, activeOnly)
- `GET /api/code-master/categories` - List distinct categories
- `GET /api/code-master/{id}` - Get code by ID
- `POST /api/code-master` - Create code (JWT required)
- `PUT /api/code-master/{id}` - Update code (JWT required)
- `DELETE /api/code-master/{id}` - Deactivate code (JWT required)

**CSR Management** (ICAO PKD CSR generation, JWT required):
- `POST /api/csr/generate` - Generate RSA-2048 CSR (SHA256withRSA)
- `POST /api/csr/import` - Import external CSR + private key PEM
- `GET /api/csr` - List CSRs (pagination, status filter)
- `GET /api/csr/{id}` - CSR detail (with issued certificate metadata)
- `GET /api/csr/{id}/export/pem` - Export CSR as PEM (Base64)
- `POST /api/csr/{id}/certificate` - Register ICAO-issued certificate (public key matching)
- `DELETE /api/csr/{id}` - Delete CSR

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
- `POST /api/pa/trust-materials` - Trust materials for client-side PA (CSCA/CRL/LC DER Base64)
- `POST /api/pa/trust-materials/result` - Report client PA result + encrypted MRZ
- `GET /api/pa/trust-materials/history` - Client PA request history (paginated, country filter)
- `GET /api/pa/combined-statistics` - Server PA + Client PA combined statistics

### PKD Relay (via :8080/api)

- `GET /api/sync/status` - DB-LDAP sync status
- `GET /api/sync/stats` - Sync statistics
- `POST /api/sync/check` - Trigger manual sync check
- `POST /api/sync/reconcile` - Trigger reconciliation
- `GET /api/sync/reconcile/history` - Reconciliation history
- `GET /api/sync/reconcile/{id}` - Reconciliation detail
- `GET /api/sync/reconcile/stats` - Reconciliation statistics
- `POST /api/sync/revalidate` - Trigger DSC expiration revalidation
- `GET /api/sync/notifications/stream` - Real-time notification SSE stream
- `POST /api/sync/icao-ldap/trigger` - Trigger ICAO PKD LDAP sync
- `GET /api/sync/icao-ldap/status` - ICAO LDAP sync status
- `GET /api/sync/icao-ldap/history` - ICAO LDAP sync history (paginated, status filter)
- `GET /api/sync/icao-ldap/config` - ICAO LDAP sync config
- `PUT /api/sync/icao-ldap/config` - Update ICAO LDAP sync config
- `POST /api/sync/icao-ldap/test` - Test ICAO LDAP connection

### AI Analysis Service (via :8080/api/ai)

- `GET /api/ai/health` - Health check
- `POST /api/ai/analyze` - Trigger full analysis (background)
- `POST /api/ai/analyze/incremental` - Incremental analysis (upload_id based)
- `GET /api/ai/analyze/status` - Analysis job status
- `GET /api/ai/certificate/{fingerprint}` - Certificate analysis result
- `GET /api/ai/certificate/{fingerprint}/forensic` - Certificate forensic detail (10 categories)
- `GET /api/ai/anomalies` - Anomaly list (filters: country, type, label, risk_level, pagination)
- `GET /api/ai/statistics` - Overall analysis statistics (includes forensic scores)
- `GET /api/ai/reports/country-maturity` - Country PKI maturity ranking
- `GET /api/ai/reports/algorithm-trends` - Algorithm migration trends by year
- `GET /api/ai/reports/key-size-distribution` - Key size distribution
- `GET /api/ai/reports/risk-distribution` - Risk level distribution
- `GET /api/ai/reports/country/{code}` - Country detail analysis
- `GET /api/ai/reports/issuer-profiles` - Issuer profiling report
- `GET /api/ai/reports/forensic-summary` - Forensic analysis summary
- `GET /api/ai/reports/extension-anomalies` - Extension rule violations list

### Public vs Protected Endpoints

Public endpoints (no JWT required) are defined in [auth_middleware.cpp](services/pkd-management/src/middleware/auth_middleware.cpp) lines 10-93. Key categories:
- **Public**: Health checks, Dashboard statistics, Certificate search, Doc 9303 checklist, DSC_NC report, CRL report/download, PA lookup, ICAO monitoring, Sync status, PA verification, PA trust-materials (request/result/history/combined-statistics), Certificate preview, Code Master (GET), AI Analysis (all endpoints), Static files
- **Protected**: File uploads (LDIF/ML/Certificate save), User management, Audit logs, Upload deletion, Code Master (POST/PUT/DELETE), CSR Management (all endpoints)

---

## Frontend

### Pages (28 total)

| Page | Route | Purpose |
|------|-------|---------|
| Login | `/login` | Landing page + Authentication |
| Dashboard | `/` | Homepage with certificate statistics |
| FileUpload | `/upload` | LDIF/Master List upload |
| CertificateUpload | `/upload/certificate` | Individual certificate upload (preview-before-save) |
| UploadHistory | `/upload-history` | Upload management |
| UploadDetail | `/upload/:uploadId` | Upload detail & structure |
| UploadDashboard | `/upload-dashboard` | Certificate statistics dashboard |
| CertificateSearch | `/pkd/certificates` | Certificate search & detail |
| DscNcReport | `/pkd/dsc-nc` | DSC_NC non-conformant certificate report |
| CrlReport | `/pkd/crl` | CRL report & revoked certificate analysis |
| TrustChainValidationReport | `/pkd/trust-chain` | DSC Trust Chain report |
| PAVerify | `/pa/verify` | PA verification |
| PAHistory | `/pa/history` | PA history |
| PADetail | `/pa/:paId` | PA detail |
| PADashboard | `/pa/dashboard` | PA statistics dashboard |
| SyncDashboard | `/sync` | DB-LDAP sync monitoring |
| IcaoLdapSync | `/sync/icao-ldap` | ICAO PKD LDAP auto-sync (TLS, progress, history) |
| IcaoStatus | `/icao` | ICAO PKD version tracking |
| MonitoringDashboard | `/monitoring` | System monitoring |
| AiAnalysisDashboard | `/ai/analysis` | AI certificate forensic analysis & pattern analysis |
| UserManagement | `/admin/users` | User administration (grid card layout) |
| AuditLog | `/admin/audit-log` | Auth audit log viewer |
| OperationAuditLog | `/admin/operation-audit` | Operation audit trail |
| ApiClientManagement | `/admin/api-clients` | API Client management (CRUD, key regeneration) |
| ApiClientRequest | `/api-client-request` | External API client access request (public) |
| PendingDscApproval | `/admin/pending-dsc` | DSC registration approval workflow |
| CsrManagement | `/admin/csr` | ICAO PKD CSR generation & management |
| Profile | `/profile` | User profile & account info |

### Key Components

| Component | Purpose |
|-----------|---------|
| AdminRoute | Admin-only route guard (role-based access control) |
| ErrorBoundary | Global error boundary with recovery UI |
| TreeViewer | Reusable tree visualization (react-arborist) |
| CountryStatisticsDialog | Country-level certificate breakdown |
| TrustChainVisualization | Trust chain path display |
| DuplicateCertificatesTree | Duplicate detection with country grouping |
| LdifStructure / MasterListStructure | File structure visualization |
| EventLog | Scrollable SSE event log with auto-scroll and timestamps |
| ProcessingErrorsPanel | Upload processing error summary and details |
| ValidationSummaryPanel | Shared validation statistics card (SSE + post-upload) |
| RealTimeStatisticsPanel | Thin wrapper mapping SSE stats to ValidationSummaryPanel |
| CurrentCertificateCard | Currently processing certificate metadata |
| QuickLookupPanel | PA quick lookup by subject DN or fingerprint |
| VerificationStepsPanel | 8-step ICAO verification process display |
| VerificationResultCard | PA verification result summary card |
| CertificateDetailDialog | Certificate detail modal (4-tab: General/Details/Doc 9303/포렌식) |
| CertificateSearchFilters | Certificate search filter card with export buttons |
| Doc9303ComplianceChecklist | Doc 9303 per-item compliance checklist with collapsible categories |

### Dependencies

React 19, TypeScript, Vite, Tailwind CSS 4, React Router 7, Axios, Zustand, TanStack Query, Recharts, Lucide Icons, i18n-iso-countries

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
| Korean text | UTF-8 default | NLS_LANG required | `NLS_LANG=AMERICAN_AMERICA.AL32UTF8` env var |
| CLOB + sequential queries | TEXT (no issue) | ORA-03127 LOB session | Use VARCHAR2(4000) for short text |
| BLOB read truncation | bytea (no issue) | LOB locator reads 33-89 bytes | `RAWTOHEX(DBMS_LOB.SUBSTR(col, DBMS_LOB.GETLENGTH(col), 1))` |
| LOB/non-LOB mixed fetch | TEXT (no issue) | OCI fetch stops after 1 row | Convert all LOB columns: `TO_CHAR(clob)`, RAWTOHEX for BLOB |

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
+-- podman/          # Podman management for Production RHEL 9 (same structure as docker/)
+-- luckfox/         # ARM64 deployment (same structure as docker/)
+-- build/           # Build scripts (build, rebuild-*, check-freshness, verify-*)
+-- helpers/         # Utility functions (db-helpers.sh, ldap-helpers.sh)
+-- maintenance/     # Data management (reset-all-data, reset-ldap, dn-migration)
+-- monitoring/      # System monitoring (icao-version-check)
+-- ssl/             # SSL certificate management (init-cert, renew-cert)
+-- deploy/          # Deployment (from-github-artifacts)
+-- dev/             # Development scripts (start/stop/rebuild/logs dev services)
```

**Root convenience wrappers**:
- Docker: `./docker-start.sh`, `./docker-stop.sh`, `./docker-health.sh`, `./docker-clean-and-init.sh`
- Podman: `./podman-start.sh`, `./podman-stop.sh`, `./podman-health.sh`, `./podman-clean-and-init.sh`

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
**Solution**: Use `common::db::scalarToInt()` / `common::db::boolLiteral()` from `query_helpers.h`

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
- Reference data: code_master (21 categories, ~150 codes)
- CSR management: csr_request (CSR + encrypted private key + issued certificate metadata)

### LDAP Strategy
- Read: Software Load Balancing (openldap1:389, openldap2:389) — **현재 openldap2(192.168.100.11) 하드웨어 장애로 단일 노드(openldap1) 운영 중**
- Write: Direct to primary (openldap1:389)
- DN format: `cn={FINGERPRINT},o={TYPE},c={COUNTRY},dc=data,...`
- Object classes: pkdDownload (certs), cRLDistributionPoint (CRLs)

### Connection Pooling & Resource Configuration
- Database: configurable via `DB_POOL_MIN`/`DB_POOL_MAX` env vars (default: min=2, max=10)
- LDAP: configurable via `LDAP_POOL_MIN`/`LDAP_POOL_MAX` env vars (default: min=2, max=10, 5s timeout)
- LDAP timeout: `LDAP_NETWORK_TIMEOUT` (default: 5s), `LDAP_HEALTH_CHECK_TIMEOUT` (default: 2s), `LDAP_WRITE_TIMEOUT` (default: 10s)
- Upload limits: `MAX_CONCURRENT_UPLOADS` (default: 3), `MAX_BODY_SIZE_MB` (default: 100MB PKD Mgmt / 50MB PA)
- Drogon Thread: configurable via `THREAD_NUM` env var (default: 16)
- AI Service: configurable via `DB_POOL_SIZE`/`DB_POOL_OVERFLOW` env vars (default: 5/10)
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

### v2.39.0 (2026-03-22) - ICAO PKD LDAP 자동 동기화 + CSR 기반 TLS 인증서 발급

- **ICAO PKD LDAP 자동 동기화**: LDAP V3 프로토콜로 ICAO PKD 서버에서 인증서/CRL 자동 다운로드
  - 모의 ICAO PKD LDAP 서버 (Docker, `icao-pkd-ldap:389/636`)
  - `IcaoLdapClient`: Simple Bind / TLS + SASL EXTERNAL 이중 모드
  - `IcaoLdapSyncService`: fingerprint 중복 체크 → X.509 메타데이터 22개 추출 → Trust Chain 검증 → DB/LDAP 저장
  - **Master List에서 CSCA 추출**: ICAO PKD DIT에 o=csca 없음 → ML CMS SignedData 파싱 → CSCA/MLSC 추출
  - 동기화 파이프라인: ML→CSCA → DSC → CRL → DSC_NC (5단계)
  - SSE 실시간 진행 상황 (타입별 진행률, 파이프라인 시각화)
  - API 6개: trigger, status, history, config, test, (config update)
- **CSR 기반 TLS 인증서 발급**: `POST /api/csr/{id}/sign`
  - Private CA로 CSR 서명 → 클라이언트 인증서 즉시 발급
  - TLS 파일 자동 저장 (client.pem, client-key.pem, ca.pem)
  - CsrManagement 페이지: "CA 인증서 발급" + "ICAO PKD 연결 적용" 버튼
- **ICAO PKD LDAP TLS 서버**: LDAPS:636, Private CA 서버 인증서
- **3가지 인증 모드**: 평문(389) / TLS+Simple Bind(636) / TLS+SASL EXTERNAL(636)
- **Oracle OCI 수정**: DML iters=1 + OCI_COMMIT_ON_SUCCESS (ORA-24333 근본 해결)
- **SSE 알림 분리**: 시작/완료만 Notification Bell, 진행 상황은 페이지 내 표시
- **사이드바 재구성**: "ICAO 연계" 섹션 신설 (ICAO 버전 상태, 파일 업로드, ICAO PKD 동기화)
- **운영 감사 로그**: PA_TRUST_MATERIALS 외 8개 OperationType 라벨 추가
- **Client PA 필터 개선**: 서버 PA와 동일한 5개 필터 (국가/상태/날짜/검색), MRZ→여권 번호 컬럼 변경
- **ICAO PKD 동기화 전용 페이지**: `/sync/icao-ldap` 별도 페이지 분리 (SyncDashboard에서 독립)
  - KPI 카드 4개, 연결 테스트, SSE 실시간 진행 상황, 타입 파이프라인 시각화
  - 동기화 이력 테이블 (시작시간/상태/트리거/전체/신규/기존/실패/소요시간/상세)
  - 이력 행 클릭 → 상세 다이얼로그 (타입별 ML→CSCA/DSC/CRL/DSC_NC 통계 테이블)
  - 페이지네이션 + 상태 필터 + i18n (한국어/영어 60+ 키)
  - 디자인 시스템 일관성 개선 (gradient header, border-l-4 KPI, shadow-xl table, badge 상태)
- **ICAO 버전 상태 버그 수정**: 비표준 파일명(.ml) 업로드 후 uploaded_version이 N/A로 표시되는 문제 → ICAO 파일명 패턴 필터 추가
- **법무부 사업계획서 적합성 분석 보고서**: 22개 요구사항 대비 12/12 Local PKD+PA 완전 대응
- DB: `icao_ldap_sync_log` 테이블 (PostgreSQL + Oracle)
- Docker: `icao-pkd-ldap` 컨테이너, PKD Relay ICAO LDAP 환경변수, TLS 볼륨
- 34 커밋, ~50 파일 변경

### v2.38.0 (2026-03-21) - Client PA 지원 (Trust Materials API) + 업로드 중복 표시 개선 + 사용자 관리 감사 연동
- **Client PA Trust Materials API**: 클라이언트(ICRM)가 로컬에서 PA 수행 가능하도록 4개 엔드포인트 추가
  - `POST /api/pa/trust-materials`: 국가별 CSCA/CRL/Link Certificate DER(Base64) 제공, requestId 발급
  - `POST /api/pa/trust-materials/result`: 클라이언트 PA 결과 + 암호화 MRZ 보고 (AES-256-GCM)
  - `GET /api/pa/trust-materials/history`: 클라이언트 PA 요청 이력 (페이지네이션, 국가 필터)
  - `GET /api/pa/combined-statistics`: 서버 PA + 클라이언트 PA 통합 통계
- **PII 보호 설계**: trust-materials 요청 시 MRZ 미전송 → 결과 보고 시에만 암호화 MRZ 전송 → 서버에서 복호화 후 재암호화 저장
- **DB 스키마**: `trust_material_request` 테이블 — 요청/결과/MRZ 통합 (PostgreSQL + Oracle)
  - 상태 흐름: REQUESTED → VALID/INVALID/ERROR (결과 보고 시 UPDATE)
- **PA 이력 페이지 이중 모드**: 서버 PA / 클라이언트 PA 탭 토글, ClientPATable 컴포넌트 분리 (필터+페이지네이션)
- **PA 대시보드 통합 통계**: 총 검증 카드에 Server N / Client M 분리 표시
- **업로드 상세 중복/신규 표시 개선**: ValidationSummaryPanel 신규 처리 수 계산 수정, LDIF 중복 계산 기반 표시
- **LDIF 중복 인증서 기록**: fingerprint 캐시 히트 시 certificate_duplicates 테이블에 기록 → 국가별 트리 표시
- **사용자 관리 감사 연동**: 카드에 인증/운영 감사 로그 바로 접근 버튼 추가, ?username= URL 파라미터 필터
- **OperationType**: `PA_TRUST_MATERIALS` 추가
- **프론트엔드 대형 페이지 컴포넌트 분리** (4개 페이지, 총 2,389줄 감소 -48%):
  - UploadHistory (1,507→913줄): UploadDetailModal (667줄) 분리
  - ApiClientManagement (1,288→377줄): ApiClientDialogs (987줄, 7개 컴포넌트) 분리
  - SyncDashboard (1,471→808줄): 7개 컴포넌트 (SyncConfigDialog, ReconciliationHistory 등) 분리
  - PAHistory (1,046→649줄): PADetailModal (486줄) + ClientPATable (194줄) 분리
- OpenAPI pa-service.yaml v2.1.9, PA_API_GUIDE v2.1.14
- ~45 files changed (17 new, ~28 modified)

### v2.37.0 (2026-03-18) - 6차 코드 보안 강화 + 권한 관리 수정 + 브랜드 리네이밍 완료
- **Admin 초기 비밀번호 환경변수 전환**: DB init 스크립트 하드코딩 admin/admin123 제거 → 서비스 기동 시 `ADMIN_INITIAL_PASSWORD` 환경변수로 admin 자동 생성 (ensureAdminUser, Phase 8)
- **XSS 수정**: `DuplicateCertificatesTree.tsx` — `dangerouslySetInnerHTML` + `escapeValue: false` 제거 → 일반 텍스트 렌더링
- **LDAP DN countryCode 검증**: `validation_repository.cpp` — ISO 3166-1 alpha-2/3 형식 검증 추가 (LDAP DN injection 방지)
- **401 응답 인터셉터 추가**: `eacApi.ts`, `pendingDscApi.ts` — JWT 만료 시 토큰 정리 + auth:expired 이벤트 (authApi 패턴 통일)
- **SQL INTERVAL 범위 제한**: `api_client_repository.cpp` — days 파라미터 1~365 범위 제한
- **사이드바 메뉴 권한 수정**: 비관리자에게 노출되던 관리 메뉴 숨김 처리
  - 시스템 모니터링: permission 없음 → `adminOnly: true`
  - API Docs: permission 없음 → `adminOnly: true`
  - EAC 인증서 (실험): permission 없음 → `adminOnly: true`
  - 인증서 통계: `upload:read` → `report:read` (보고서 섹션 권한 일치)
- **PA 권한 분리**: `pa:read`(이력) + `pa:stats`(통계) 독립 관리 — 관리자가 PA 이력/통계 접근 권한을 개별 부여 가능 (총 12개 permission)
- **브랜드 리네이밍 완료**: FASTpass® SPKD → FASTpass® SPKD (전체 79파일), SmartCore Inc. → SMARTCORE Inc. (전체 63파일)
- **OpenAPI v2.36.0 업데이트**: PKD Management (CSR 7 + pending-dsc 4 엔드포인트), PA Service (v2.1.7 dscAutoRegistration 필드), PKD Relay (v2.36.0)
- **문서**: CSR 생성 가이드 HTML, 기술제안서 PPTX 25슬라이드
- ~100 files changed

### v2.36.0 (2026-03-17~18) - 브랜드 리네이밍 + UI/UX 전면 일관성 개선 + GlossaryTerm + Link Certificate 버그 수정
- **브랜드 리네이밍**: SPKD → FASTpass® SPKD (SmartCore 네이밍: FastPass, FastFinger, FastPhoto)
- **favicon.svg 리디자인**: Shield + "S" → Shield + "F" + Speed Lines
- **로그인 페이지**: BSI TR-03110 배지 추가, 보안 관리/감사 기능 카드 추가, 히어로:로그인 68:32, 설명 문구 ICAO Local PKD 종합 솔루션으로 개선
- **UI/UX 일관성 전면 개선 (50+ 파일)**:
  - 비표준 폰트 `text-[9px]`/`text-[10px]`/`text-[11px]` → `text-xs` 통일 (23파일 167건)
  - 메인 카드 패딩 `p-5`/`p-6` → `p-4` 통일 (30+ 파일)
  - 테이블 행 `py-2.5`/`py-3` → `py-2` 통일 (14파일)
  - 다이얼로그 내부 `space-y-4` → `space-y-3` 통일 (7파일)
  - 버튼 `rounded-xl` → `rounded-lg` 통일
- **GlossaryTerm 컴포넌트**: 전문 용어(CSCA, DSC, CRL, SOD, MRZ 등 21개) 위에 `?` 아이콘 + hover 풍선 도움말 (ko/en)
  - `position: fixed` 렌더링으로 overflow-hidden 부모에서도 잘림 없이 표시
  - 14개 페이지 + 7개 컴포넌트에 적용
- **CountryFlag 컴포넌트**: 국기 아이콘 hover 시 전체 국가명 표시 (13파일 25건)
- **다이얼로그 compact화**: LDIF 구조/Master List 구조/중복 인증서 탭 TreeViewer compact 모드 적용
- **IcaoViolationDetailDialog**: 테이블 compact 디자인 (font 11→xs, table-fixed, 고정 컬럼폭)
- **ValidationSummaryPanel**: complianceViolations 합계 기반 비준수 행 표시 (effectiveNonCompliantCount)
- **Link Certificate Doc 9303 버그 수정**: CSCA Link Certificate(`o=lc`)에 대한 LDAP DN fallback 로직 추가 (certificate_handler.cpp)
- **i18n**: Master List 구조 라벨 "Prim" → "Constructed/Primitive", 대시보드 부제 "플랫폼" → "시스템"
- 59 files changed, +692/-4170 lines

### v2.35.0 (2026-03-16) - ICAO PKD CSR 관리 모듈 + 인증서 등록 + 감사 강화
- **ICAO PKD CSR 생성**: RSA 2048 bit 공개키 + SHA256withRSA 서명 + Base64(PEM) 인코딩 — ICAO PKD 요구사항 완전 준수
- **ICAO 요구사항 원문**: "The CSR must contain an RSA 2048 bit public key and be signed using SHA256withRSA and should be Base64 encoded. There are no restrictions on the subjectDN included in the CSR"
- **8 API 엔드포인트**: `POST /api/csr/generate` (생성), `POST /api/csr/import` (외부 CSR+개인키 가져오기), `GET /api/csr` (목록), `GET /api/csr/{id}` (상세), `GET /api/csr/{id}/export/pem` (PEM 내보내기), `POST /api/csr/{id}/certificate` (ICAO 발급 인증서 등록), `POST /api/csr/{id}/sign` (Private CA 서명 발급), `DELETE /api/csr/{id}` (삭제)
- **CSR Import**: 외부 생성 CSR + 개인키 PEM 가져오기 — CSR 서명 검증(`X509_REQ_verify`) + 개인키-CSR 공개키 매칭 검증(`EVP_PKEY_eq`)
- **ICAO 발급 인증서 등록**: `POST /api/csr/{id}/certificate` — X.509 파싱, 공개키 매칭 검증(CSR 핑거프린트 비교), 메타데이터 자동 추출(serial, issuer, validity, fingerprint), 중복 등록 차단
- **데이터 암호화**: CSR PEM + 개인키 모두 AES-256-GCM 암호화 저장 (`PII_ENCRYPTION_KEY`), API 응답에 개인키 미포함
- **감사 로그 강화**: `OperationType` 4개 추가(CSR_GENERATE, CSR_EXPORT, CSR_VIEW, CSR_DELETE), `createAuditEntryFromRequest()` 기반 request 컨텍스트 포함(사용자, IP, 요청경로, User-Agent)
- **Logout 감사 로그 수정**: JWT 만료 시에도 토큰 payload에서 username 추출(best-effort base64 디코딩) 후 LOGOUT 이벤트 기록
- **DB 스키마**: `csr_request` 테이블 — CSR + 개인키 + 발급 인증서 메타데이터 10개 컬럼 (PostgreSQL + Oracle)
- **Frontend**: `CsrManagement.tsx` — CSR 생성/Import/상세/PEM Export/인증서 등록/삭제 다이얼로그, 발급 인증서 정보 표시
- **사이드바 재배치**: CSR 관리 → 시스템 관리 섹션, DSC 등록 승인 → 위·변조 검사 섹션, 감사 로그 2개 → "감사" 하위 그룹
- **nginx**: 3개 설정 파일에 `/api/csr` location 블록 추가
- API 단위 테스트 11건 × 3회 반복 전체 통과
- ~30 files changed (10 new, ~20 modified)

### v2.34.0 (2026-03-15) - DB 초기화 스크립트 통합 정리
- **PostgreSQL init scripts 통합**: 18개 → 8개 파일로 축소 — 10개 마이그레이션 파일(05~09, 10-missing, 13~16)을 기본 스키마 파일(01~04, 11)에 흡수
  - `01-core-schema.sql`: X.509 메타데이터 15개 컬럼(06), duplicate_certificate 테이블(07), pending_dsc_registration 테이블(15), trust_chain_path 컬럼, ldap_dn_v2 컬럼(crl/master_list), CRL UNIQUE 인라인 제약 흡수
  - `02-services-schema.sql`: MLSC sync 컬럼(08), reconciliation 컬럼 리네임(09), revalidation_history/sync_config/crl_revocation_log 테이블(10-missing), trust chain/CRL 9개 컬럼(14) 흡수
  - `04-advanced-features.sql`: ldap_dn_v2 컬럼 + ldap_dn_migration_status 테이블(05) 흡수, ALTER TABLE trust_chain_path 제거(01로 이동)
  - `11-ai-analysis.sql`: forensic 컬럼 마이그레이션 블록 제거(이미 CREATE TABLE에 포함)
  - 삭제: 05, 06, 07, 08, 09, 10-missing, 13, 14, 15, 16
- **Oracle init scripts 통합**: 13개 → 9개 파일로 축소 — 4개 마이그레이션 파일 흡수/삭제
  - `03-core-schema.sql`: pending_dsc_registration 테이블(15) 흡수
  - `04-services-schema.sql`: revalidation_history trust chain/CRL 9개 컬럼(14) 흡수
  - 삭제: 02-schema.sql(빈 placeholder), 13(인덱스, 이미 기본 스키마에 존재), 14, 15
- **clean-and-init.sh Oracle fallback 수정**: 삭제된 `15-pending-dsc-registration.sql` → `03-core-schema.sql`로 변경 (Docker + Podman 양쪽)
- **Podman clean-and-init.sh 동기화**: Docker 버전과 동일한 Oracle init script 완료 대기 로직 추가 (PENDING_DSC_REGISTRATION + CVC_CERTIFICATE 테이블 확인)
- PostgreSQL 8개 파일, Oracle 9개 파일, Docker/Podman 스크립트 2개 수정
- local clean-and-init 검증 완료 (Oracle, 전체 서비스 healthy, pending-dsc stats 200 OK)

### v2.33.6 (2026-03-15) - UploadHistory 상세 다이얼로그 i18n 수정
- **Bug fix — Stepper 라벨 i18n 키 미번역**: UploadHistory 상세 다이얼로그의 진행 상태 Stepper에서 i18n 키가 번역되지 않고 그대로 표시되는 문제 수정 (예: "monitoring.pool.idle", "common.label.processing")
- **Root cause**: `STATUS_STEPS` labelKey에서 i18next 네임스페이스 구분자로 `.`을 사용 — 올바른 구분자는 `:` (예: `monitoring:pool.idle`)
- **Fix**: 8개 labelKey 모두 `.` → `:` 네임스페이스 구분자로 수정, `common.label.processing` → `common:status.processing` (존재하는 키로 변경)
- **Fix**: `useTranslation`에 `monitoring` 네임스페이스 추가
- 1 file changed (0 new, 1 modified: UploadHistory.tsx)

### v2.33.5 (2026-03-15) - ICAO Doc 9303 미준수 상세 다이얼로그 카테고리 필터 + DSC_NC 부적합 사유 표시 수정

- **Bug fix — validityPeriod 카테고리 인증서 목록 0건 표시**: ICAO Doc 9303 미준수 상세 다이얼로그에서 유효기간(validityPeriod) 카테고리 클릭 시 해당 인증서 목록이 비어있는 문제 수정
- **Root cause**: `validation_repository.cpp` ICAO 카테고리 필터에 `icao_compliant = FALSE` 공통 조건이 포함 — 유효기간 위반은 WARNING 레벨로 `icao_compliant`이 TRUE로 유지되므로 교집합이 공집합
- **Fix**: ICAO 카테고리 필터 블록에서 `icao_compliant = FALSE` 공통 조건 제거 — 개별 카테고리 컬럼(`icao_validity_period_compliant = FALSE`)만으로 필터링
- **Bug fix — DSC_NC 미준수 상세 다이얼로그 미표시**: collection-003 LDIF(DSC_NC) 업로드 후 ICAO Doc 9303 미준수 수치 클릭 시 상세 다이얼로그가 표시되지 않는 문제 수정
- **Root cause**: `checkIcaoCompliance()`가 DSC_NC에 대해 개별 카테고리 체크 없이 `NON_CONFORMANT` 조기 반환 → 모든 개별 카테고리 컬럼이 TRUE → `complianceViolations` 집계 결과 전체 0 → 다이얼로그 미렌더링
- **Fix (3단계)**:
  - Backend: `getStatisticsByUploadId()`에 `complianceViolations` DB 집계 쿼리 추가 (5개 카테고리 SUM)
  - Backend: `nonConformant` icaoCategory 필터 추가 — `icao_compliant = FALSE` 조건으로 DSC_NC 필터링
  - Frontend: `IcaoViolationDetailDialog`에 `nonConformant` 카테고리 추가 — violations 비어있고 `totalNonCompliantCount > 0`일 때 합성 카테고리 생성, NC Code/Description 컬럼 표시
  - Frontend: `ValidationSummaryPanel` 다이얼로그 렌더 조건 `complianceViolations` → `icaoNonCompliantCount > 0`으로 변경
  - Frontend: `UploadHistory`에서 `validation-statistics` API 호출하여 `complianceViolations` 데이터 획득
- **DSC_NC LDIF 속성 활용**: `pkdConformanceCode`/`pkdConformanceText` 속성이 `icao_violations` 필드에 파이프 구분으로 저장되며, 미준수 상세 테이블의 NC 사유 컬럼에 표시
- 4 files changed (0 new, 4 modified: validation_repository.cpp, IcaoViolationDetailDialog.tsx, ValidationSummaryPanel.tsx, UploadHistory.tsx)

### v2.33.4 (2026-03-15) - RSA-PSS 해시 알고리즘 추출 수정 (ICAO Doc 9303 준수 검사 정확도 개선)
- **Bug fix — RSA-PSS 인증서 해시 알고리즘 "unknown" 표시**: RSA-PSS(`rsassaPss`) 서명 알고리즘 사용 인증서 3,019개가 ICAO Doc 9303 미준수 상세에서 "미충족 해시 알고리즘: Unknown"으로 잘못 표시되는 문제 수정
- **Root cause**: RSA-PSS는 기존 RSA/ECDSA와 달리 해시 알고리즘이 서명 알고리즘 이름(`rsassaPss`)에 포함되지 않고 ASN.1 PSS 파라미터에 별도 지정됨. `extractHashAlgorithm()`이 문자열 매칭만 수행하여 "sha"를 찾지 못하고 `"unknown"` 반환
- **Fix (2개 파일)**: RSA-PSS 감지 시 `X509_get0_tbs_sigalg()` → `d2i_RSA_PSS_PARAMS()` → `hashAlgorithm` OID에서 실제 해시 추출 (SHA-256/384/512 등)
  - `x509_metadata_extractor.cpp`: pkd-management 로컬 extractor에 RSA-PSS PSS 파라미터 파싱 추가
  - `common-lib/src/x509/metadata_extractor.cpp`: 공유 라이브러리에 RSA-PSS + SHA-224 NID 지원 추가
- **영향 범위**: Master List 업로드 시 ICAO 준수 검사(`checkIcaoCompliance`)에서 RSA-PSS 인증서가 정확한 해시로 평가 — 대부분 SHA-256/SHA-512 사용으로 ICAO 준수로 재분류
- **알고리즘 지원 문서 업데이트**: `BSI_TR03110_ALGORITHM_SUPPORT.md`에 RSA-PSS 해시 추출 방법, 구현 파일별 알고리즘 커버리지, deprecated 알고리즘 하위 호환성 정책 추가
- 3 files changed (0 new, 3 modified: x509_metadata_extractor.cpp, common-lib metadata_extractor.cpp, BSI_TR03110_ALGORITHM_SUPPORT.md)

### v2.33.3 (2026-03-15) - ICAO Doc 9303 미준수 상세 다이얼로그 데이터 표시 수정 + 용어 변경
- **Bug fix — 미준수 상세 다이얼로그 인증서 목록 미표시**: Master List 업로드 후 ICAO Doc 9303 미준수 상세 다이얼로그에서 카테고리별 인증서 목록이 0건으로 표시되는 문제 수정
- **Root cause**: `masterlist_processor.cpp`에서 `validation_result` 레코드를 신규 인증서(`!isDuplicate`)에만 저장 → 중복 인증서(100%)로 업로드 시 해당 upload_id에 대한 `validation_result` 행이 0건이어서 다이얼로그 API 조회 결과 비어있음
- **Fix**: 4곳의 `!isDuplicate &&` 조건 제거 — MLSC/CSCA/LC 인증서에 대해 중복 여부와 무관하게 `validation_result` 레코드 저장 (LDIF 형식 2곳 + 파일 형식 2곳)
- **안전성**: `validation_result` 테이블 `ON CONFLICT (certificate_id, upload_id) DO NOTHING` (PostgreSQL) / `MERGE WHEN NOT MATCHED` (Oracle) 로 중복 삽입 방지 보장
- **용어 변경 (i18n)**: 업로드 결과 페이지 ICAO 준수 카드 라벨 수정
  - "Doc 9303 권고 준수" → "ICAO Doc 9303 권고 준수 여부 검사 결과"
  - "적합" → "준수", "부적합" → "미준수"
  - "Doc 9303 적합" → "Doc 9303 준수", "Doc 9303 부적합" → "Doc 9303 미준수"
- 5 files changed (0 new, 5 modified: masterlist_processor.cpp, ko/upload.json, ko/certificate.json, en/upload.json, CLAUDE.md)

### v2.33.2 (2026-03-15) - 전체 코드 멱등성 전수 수정 (Idempotency Hardening)

#### 멱등성 수정 배경
- Luckfox 배포 환경 초기화 과정에서 재시도/재처리 시 중복 삽입, race condition, 오류 처리 누락 등 멱등성 관련 이슈들을 전수 조사 후 수정

#### 1차 수정 — 주요 이슈 (HIGH)
- **reconciliation_engine.cpp**: `findMissingInLdap` / `findMissingCrlsInLdap` — PostgreSQL `LIMIT $N` 문법이 Oracle에서 동작하지 않아 reconciliation 완전 불동작 → `common::db::limitClause()` 헬퍼로 수정
- **certificate_repository.cpp**: INSERT unique violation 처리 — PostgreSQL `23505`/`unique` 키워드 미처리(ORA-00001만 감지) → 두 DB 모두 처리, `duplicate_certificate` DO UPDATE → DO NOTHING 수정
- **validation_repository.cpp**: Oracle `copyForUpload` — INSERT...SELECT 단순 실행 → MERGE INTO WHEN NOT MATCHED 패턴으로 수정 (재시도 시 중복 삽입 방지)
- **crl_repository.cpp**: Oracle INSERT 후 ID 미반환 → unique violation 감지 후 re-query로 기존 ID 반환, `saveRevokedCertificate` ON CONFLICT DO NOTHING 추가
- **postgresql_query_executor.cpp**: `endBatch()` COMMIT 실패 시 경고 로그만 → `throw std::runtime_error` (호출자가 배치 실패 인지 가능, batchMode_ 선재 리셋)

#### 스키마 수정
- **01-core-schema.sql**: `revoked_certificate(crl_id, serial_number)` UNIQUE 제약 추가 (ON CONFLICT DO NOTHING 지원)
- **01-core-schema.sql**: `uploaded_file(file_hash)` partial UNIQUE index 추가 (`WHERE file_hash != ''`) — 동시 업로드 race condition 방지, FORCE re-upload 빈문자열 리셋과 호환
- **16-idempotency-fixes.sql** (신규): 기존 DB 안전 마이그레이션 — 중복 행 제거 후 UNIQUE 제약 추가 (revoked_certificate), partial unique index 추가 (uploaded_file)

#### 2차 수정 — race condition 방지 (MEDIUM)
- **dsc_auto_registration_service.cpp**: PA 검증 동시 요청 시 SELECT-then-INSERT race condition → INSERT unique violation 감지 후 re-query로 기존 pending 항목 반환 (ORA-00001/23505/unique constraint 키워드 감지)
- **certificate_handler.cpp**: `handlePendingDscApprove` — 동시 승인 race condition 시 certificate INSERT unique violation → 기존 인증서 ID re-query 후 정상 승인 진행, `updateStatus()` 반환값 검증 및 실패 시 warn 로그 추가

#### 스크립트 수정 (Luckfox)
- **scripts/luckfox/clean.sh**: `sudo rm -rf .docker-data/postgres/*` 권한 실패 시 조용히 넘어가는 버그 → docker alpine 방식으로 교체 (postgres 데이터 완전 삭제 보장)
- **scripts/luckfox/clean-and-init.sh** (신규): Luckfox 전용 완전 초기화 스크립트 — 컨테이너 중지 → postgres 데이터 삭제(docker alpine) → host slapd LDAP DIT 재초기화(ldapdelete -r + ldapadd) → start.sh → 헬스체크

#### 빌드 검증
- `pkd-management`, `pa-service`, `pkd-relay` 3개 서비스 Docker 빌드 성공
- 14 files changed (2 new, 12 modified)

### v2.33.1 (2026-03-14) - EAC Dashboard UX 개선 — CVC 삭제 + compact TreeViewer 전체 적용
- **EAC 인증서 삭제 기능**: CVC 인증서 목록에서 2단계 확인 후 삭제 (Trash2 아이콘 → 확인/취소 버튼)
  - `DELETE /api/eac/certificates/{id}` 엔드포인트 추가 (eac-service)
  - `CvcCertificateRepository::deleteById()` — `DELETE FROM cvc_certificate WHERE id = $1`
  - `EacCertificateHandler::handleDelete()` — 404 존재 확인 → 삭제 → 목록 자동 갱신 (`refetch`)
  - Frontend: `CertRow` 컴포넌트에 `onDeleted` 콜백 + 삭제 진행 상태(spinning) 표시
  - CORS 헤더 `DELETE` 메서드 추가
- **Algorithm/OID 공백 표시 수정**: 파서 완성 전 저장된 레코드 대응 — 빈 값은 `(없음)` fallback 표시, copyable 아이콘은 값이 있을 때만 표시
- **compact TreeViewer 전체 적용** (font `text-[11px]`, rowHeight 24px, indent 18px):
  - `EacDashboard.tsx` CVC 상세 탭 (이미 적용)
  - `CertificateDetailDialog.tsx` — Trust Chain 트리(height="200px"), 인증서 필드 트리(height="400px")
  - `CertificateUpload.tsx` — 파싱결과 카드 인증서 트리(height="320px"), DL 구조 트리(height="500px")
- **TreeViewer.tsx**: `compact` prop 추가 — 소형 데이터 카드에 최적화된 밀도 높은 레이아웃
- 10 files changed (0 new, 10 modified)

### v2.33.0 (2026-03-14) - EAC Service Phase 4 완료 — BSI TR-03110 OID 수정 + Frontend 통합 + Oracle 호환성
- **EAC Service 실험적 기능 완료 (4개 Phase)**:
  - Phase 1: 도메인 모델 + CVC 파서 공유 라이브러리 (`shared/lib/cvc-parser/`, `icao::cvc` namespace)
  - Phase 2: C++/Drogon 백엔드 — CvcService, CvcCertificateRepository, CvcChainValidator, EAC Handler (7 endpoints)
  - Phase 3: 로컬 Docker 통합 — `services/eac-service/`, Docker Compose, nginx 라우팅, Oracle 스키마
  - Phase 4: 프론트엔드 — `EacDashboard.tsx`, `eacApi.ts`, App.tsx 라우트, Sidebar 메뉴, i18n (ko/en)
- **CRITICAL FIX — BSI TR-03110-3 OID 수정** (`eac_oids.h`):
  - 기존 OID 계층 오류: RSA/ECDSA 하위 트리 누락 (e.g., `id-TA-ECDSA-SHA-512`=`.9` → 실제 `0.4.0.127.0.7.2.2.2.2.5`)
  - `id-TA-RSA` = `id-TA.1` = `0.4.0.127.0.7.2.2.2.1.{n}` (SHA-1/256/512 × v1.5/PSS 6종)
  - `id-TA-ECDSA` = `id-TA.2` = `0.4.0.127.0.7.2.2.2.2.{n}` (SHA-1/224/256/384/512 5종)
  - `isRsaAlgorithm()` / `isEcdsaAlgorithm()` 체크도 동기화, BSI Worked Example 실측 OID로 검증
- **CHAT IS 권한 디코더 수정** (`chat_decoder.cpp`):
  - Bit 6 (`0x40`) "Install Certificate", Bit 7 (`0x80`) "Install Qualified Certificate" 누락 추가
  - BSI TR-03110-3 Table C.3 전체 준수
- **Oracle NUMBER(1) 불리언 변환 수정** (`cvc_certificate_repository.cpp`):
  - `row.get("signature_valid", false).asBool()` → `common::db::getBool(row, "signature_valid", false)`
  - Oracle은 NUMBER(1)을 문자열 "0"/"1"로 반환 — jsoncpp `.asBool()` 실패 방지
- **Oracle SYS_GUID() ID 회수 수정** (`cvc_service.cpp`):
  - INSERT 후 `SYS_GUID()` 생성 ID 미반환 문제 → `findByFingerprint()` fallback으로 DB 저장 레코드 재조회
- **BSI Worked Example 실증 테스트** (`data/BSI_TR-03110_EAC-Worked-Example/`):
  - ECDH 3종(CVCA/DV/IS) + DH 3종 총 6개 CVC 인증서 Oracle에 저장
  - `GET /api/eac/certificates/{id}/chain` → IS→DV→CVCA 3-depth 체인 검증 `chainValid: true` 확인
  - 통계 API: total=6, CVCA=2, DV_DOMESTIC=2, IS=2
- **Frontend EAC Dashboard**: 보고서 & 분석 섹션 "EAC 인증서 (실험)" 메뉴 (FlaskConical 아이콘, `/eac/dashboard` 라우트)
- **Unit Tests** (`shared/lib/cvc-parser/tests/`, Google Test, 96 test cases, 전체 pass):
  - `test_tlv.cpp` (20): TlvParser 파싱/OID 디코딩/BCD 날짜, BSI 외부 태그 구조
  - `test_cvc_parser.cpp` (35): ECDH/DH 체인 전체 파싱, fingerprint 일관성, 에러 처리
  - `test_chat_decoder.cpp` (26): IS/AT/ST 권한 비트마스크 (BSI Table C.3~C.5), 통합 테스트
  - `test_cvc_signature.cpp` (15): ECDSA/RSA 서명 검증, 타입 불일치, OID 유틸리티
  - 빌드: `cmake shared/lib/cvc-parser -DBUILD_CVC_PARSER_TESTS=ON`
  - 모든 테스트 BSI TR-03110 Worked Example 실제 바이너리 데이터로 실증
- 13 files changed (7 new, 6 modified): eac_oids.h, chat_decoder.cpp, cvc_certificate_repository.cpp, cvc_service.cpp, App.tsx, Sidebar.tsx, en/nav.json, ko/nav.json, EacDashboard.tsx(new), eacApi.ts(new), test_helpers.h(new), test_tlv.cpp(new), test_cvc_parser.cpp(new), test_chat_decoder.cpp(new), test_cvc_signature.cpp(new)

### v2.32.0 (2026-03-12) - SOD 파서 ICAO Doc 9303 준수 리팩토링 + PA 검증 Step 순서 수정
- **CRITICAL FIX — DG 해시 알고리즘 오류**: SOD 파서가 LDSSecurityObject의 hashAlgorithm(DG 해시용, e.g., SHA-256) 대신 CMS SignerInfo의 digestAlgorithm(SOD 서명용, e.g., SHA-512)을 사용 → DG 해시 검증 실패. 대부분의 여권은 두 알고리즘이 동일하여 잠복 버그였으나, NL Specimen 여권(SHA-256 DG + SHA-512 CMS)에서 발견
- **SOD 파서 통합 리팩토링**: LDSSecurityObject 파싱을 `parseLdsSecurityObject()` 단일 헬퍼로 통합 (기존 `extractHashAlgorithmOid()`, `parseDataGroupHashesRaw()`에서 중복 ASN.1 파싱)
- **LDS 버전 추출**: LDSSecurityObject의 version INTEGER 필드에서 실제 버전 추출 (기존 하드코딩 "V0")
- **알고리즘 OID 확장**: 해시 4→5개 (SHA-224 추가), 서명 6→11개 (RSA-PSS, SHA1withRSA, SHA224withRSA, ECDSAwithSHA1, SHA224withECDSA 추가)
- **위험한 fallback 제거**: 미등록 OID에 대해 "SHA-256"/"SHA256withRSA" 자동 반환 → OID 문자열 그대로 반환 + 경고 로그 (잘못된 알고리즘 선택 방지)
- **CMS vs LDS 알고리즘 구분**: `SodData` 모델에 `cmsDigestAlgorithm`/`cmsDigestAlgorithmOid` 필드 추가 — CMS 서명용 다이제스트와 DG 해시용 알고리즘을 명확히 분리
- **DG 파서 SHA-224 지원**: `computeHash()`에 SHA-224 알고리즘 추가
- **PA 검증 Step 순서 수정**: Frontend Step 3 "Trust Chain 검증" ↔ Step 4 "CSCA 조회" 순서 교정 → Step 3 "CSCA 조회", Step 4 "Trust Chain 검증" (백엔드 실행 순서와 일치)
- **i18n 수정**: 한국어/영어 PA Step 3/4 라벨 교정, CSCA DN 불일치 메시지 개선 ("CSCA DN 불일치" → "DSC를 발급한 CSCA가 존재하지 않음")
- **ASN.1 파싱 안전성**: `parseAsn1Length()` 정적 헬퍼 도입 — DER 길이 파싱 로직 통합, 경계 검증 강화 (numBytes 최대 4바이트 제한)
- 8 files changed (0 new, 8 modified: sod_parser.cpp, sod_parser.h, sod_data.h, dg_parser.cpp, PAVerify.tsx, VerificationStepsPanel.tsx, ko/pa.json, en/pa.json)

### v2.31.3 (2026-03-12) - Reconciliation 후 동기화 상태 자동 갱신
- **Bug fix**: 수동 동기화(Reconciliation) 성공 후 SyncDashboard에 불일치 건수가 갱신되지 않는 문제 수정
- **Root cause**: Reconciliation이 LDAP에 인증서를 추가하고 `stored_in_ldap=TRUE`로 업데이트하지만, sync check를 다시 실행하지 않아 프론트엔드가 이전 `sync_status` 레코드(구 불일치 건수)를 표시
- **Fix**: `ReconciliationHandler`에 `SyncStatusRepository` 의존성 추가, reconciliation 성공 후(dry-run 제외, successCount > 0) `performSyncCheck()` 자동 실행하여 최신 DB/LDAP 카운트를 `sync_status` 테이블에 저장
- **안전장치**: dry-run이거나 성공 건수 0인 경우 불필요한 sync check 스킵, sync check 실패 시 경고 로그만 출력 (reconciliation 결과 응답에 영향 없음)
- 3 files changed (0 new, 3 modified: reconciliation_handler.h, reconciliation_handler.cpp, main.cpp)

### v2.31.1 (2026-03-11) - 전체 프론트엔드 반응형 디자인 개선 (13개 페이지)
- **모달/다이얼로그 모바일 대응**: 고정 `max-w-6xl`/`max-w-4xl` → `max-w-sm sm:max-w-2xl lg:max-w-Nxl` 점진적 확장 (UploadHistory, UploadDetail, PAHistory, CrlReport)
- **UserManagement 모달 스크롤**: Create/Edit 모달에 `max-h-[90vh] flex flex-col` + `overflow-y-auto` 추가 — 모바일에서 권한 그리드가 뷰포트 밖으로 넘어가는 문제 해결
- **고정 그리드 → 반응형 전환** (모바일에서 깨지는 레이아웃 수정):
  - UserManagement: 통계 `md:grid-cols-4` → `grid-cols-2 lg:grid-cols-4`, 생성 폼 `grid-cols-2` → `grid-cols-1 sm:grid-cols-2`, 수정 폼 `grid-cols-3` → `grid-cols-1 sm:grid-cols-3`
  - OperationAuditLog: 통계 `md:grid-cols-4` → `grid-cols-2 lg:grid-cols-4`, 필터 `md:grid-cols-3 lg:grid-cols-5` → `sm:grid-cols-2 lg:grid-cols-5`, 다이얼로그 3개소 `grid-cols-4` → `grid-cols-2 lg:grid-cols-4`
  - AuditLog: 통계/필터/다이얼로그 동일 패턴 적용
  - CertificateSearch: 유효/만료/미유효 카드 `grid-cols-3` → `grid-cols-1 sm:grid-cols-3`
  - ApiClientManagement: Rate limit 3개소 `grid-cols-3` → `grid-cols-1 sm:grid-cols-3`
  - ApiClientRequest: 기기 타입 `grid-cols-4` → `grid-cols-2 sm:grid-cols-4`
  - UploadDashboard: Trust Chain 카드 `grid-cols-3` → `grid-cols-1 sm:grid-cols-3`
  - UploadDetail: 인증서 타입 `grid-cols-2` → `grid-cols-1 sm:grid-cols-2`
  - PAVerify: MRZ 데이터 `grid-cols-2` → `grid-cols-1 sm:grid-cols-2`
- **SyncDashboard 레이아웃 개선**: 페이지 패딩 `p-6` → `px-4 lg:px-6 py-4` (일관된 패턴), 헤더 버튼 `flex-col sm:flex-row flex-wrap` 모바일 스택, 불일치 상세 `md:grid-cols-6` → `sm:grid-cols-3 lg:grid-cols-6`
- **MonitoringDashboard**: 연결/풀 카드 `lg:grid-cols-3` → `md:grid-cols-3` (태블릿 브레이크포인트 추가)
- **Dashboard**: 국가 목록 `gap-x-8` → `gap-x-4 lg:gap-x-8` (모바일 간격 축소)
- 13 files changed (0 new, 13 modified)

### v2.31.2 (2026-03-11) - 개인정보 암호화 (개인정보보호법 제29조 안전조치)
- **AES-256-GCM 개인정보 암호화 모듈**: `personal_info_crypto.h/.cpp` — OpenSSL EVP API 기반 인증된 암호화 (기밀성 + 무결성 동시 보장)
- **PKD Management 적용**: `api_client_requests` 테이블 4개 PII 필드 암호화 (requester_name, requester_org, requester_contact_phone, requester_contact_email)
- **PA Service 적용**: `pa_verification` 테이블 3개 PII 필드 암호화 (document_number, client_ip, user_agent)
- **여권번호 암호화**: 개인정보보호법 제24조 고유식별정보(시행령 제19조 제2호) — 저장 시 암호화 법적 의무 충족
- **암호화 형식**: `"ENC:" + hex(IV[12] + ciphertext + tag[16])` — 12바이트 랜덤 IV, 16바이트 GCM 인증 태그
- **키 관리**: `PII_ENCRYPTION_KEY` 환경변수 (64 hex chars = 256비트), 미설정 시 암호화 비활성화 (개발 환경 호환)
- **PII 마스킹**: Public API 응답 시 개인정보 마스킹 (이름: 홍*동, 이메일: h***@example.com, 전화: ***-****-5678)
- **Backward compatible**: `"ENC:"` 접두사 감지로 기존 평문 데이터와 암호화 데이터 혼재 시에도 올바르게 처리
- **DB 스키마 확장**: PII 컬럼 VARCHAR(50/255) → VARCHAR(1024) (PostgreSQL + Oracle 양쪽)
- **Fail-open 설계**: 암호화/복호화 실패 시 평문 반환 + 에러 로그 (서비스 가용성 유지)
- **법적 준수 문서**: `docs/PII_ENCRYPTION_COMPLIANCE.md` — 법적 근거, 기술 구현, 운영 가이드, 준수 체크리스트
- ~15 files changed (5 new, ~10 modified)

### v2.31.0 (2026-03-10) - DSC 등록 승인 워크플로우 (PA 자동 등록 → 관리자 승인)
- **DSC 자동 등록 → 관리자 승인 전환**: PA 검증에서 추출된 DSC 인증서를 자동 등록하지 않고 `pending_dsc_registration` 테이블에 임시 저장, 관리자가 확인 후 승인/거부
- **pending_dsc_registration 테이블**: PostgreSQL + Oracle 이중 스키마 (fingerprint UNIQUE, status PENDING/APPROVED/REJECTED, 검토자/코멘트/검토일)
- **PA Service 변경**: `DscAutoRegistrationService` — `certificate` 테이블 직접 INSERT → `pending_dsc_registration` 테이블로 변경, 3단계 중복 체크 (certificate → pending → insert)
- **PA 응답 확장**: `dscAutoRegistration` JSON에 `pendingApproval`, `alreadyRegistered`, `pendingId` 필드 추가
- **PendingDscRepository**: PKD Management 신규 리포지토리 (findAll/countAll/findById/updateStatus/getStatistics, PostgreSQL + Oracle 이중 지원)
- **4개 신규 API 엔드포인트**: `GET /api/certificates/pending-dsc` (목록), `GET /api/certificates/pending-dsc/stats` (통계), `POST /api/certificates/pending-dsc/{id}/approve` (승인), `POST /api/certificates/pending-dsc/{id}/reject` (거부)
- **승인 플로우**: pending 조회 → certificate 테이블 INSERT (PostgreSQL/Oracle 분기) → LDAP 저장 (비치명적) → pending 상태 APPROVED 업데이트 → 감사 로그
- **감사 로그**: `DSC_PENDING_SAVE`, `DSC_APPROVE`, `DSC_REJECT` OperationType 추가 — 모든 승인/거부 작업 operation_audit_log에 기록
- **Frontend**: `PendingDscApproval` 페이지 (통계 카드 4개, 필터, 테이블, 승인/거부 다이얼로그, 상세 모달)
- **Frontend**: PAVerify Step 8 — "자동 등록" → "등록 대기 (관리자 승인 필요)" 표시로 변경
- **Frontend**: `pendingDscApi.ts` API 모듈, `PAVerificationResponse.dscAutoRegistration` 타입 확장
- **사이드바**: 인증서 관리 섹션에 "DSC 등록 승인" 메뉴 추가 (adminOnly)
- **라우트**: `/admin/pending-dsc` (AdminRoute 보호)
- **인증**: 목록/통계는 public, 승인/거부는 JWT 필수
- ~15 files changed (5 new, ~10 modified)

### v2.30.0 (2026-03-09) - 로그인 페이지 모던 리디자인 + 사이드바 섹션 재구성 + 클라이언트 사이드 정렬
- **로그인 폼 모던 리디자인**: 카드 래퍼 제거(flat layout), `ring-1 ring-gray-200` 입력 필드, `group-focus-within:text-[#02385e]` 아이콘 포커스 색상, 단색 `bg-[#02385e]` 버튼 + `active:scale-[0.98]` 프레스 효과, `radial-gradient` 도트 패턴 배경
- **로그인 텍스트 변경**: "시스템 로그인" → "FASTpass® SPKD 로그인", 하단 부제 제거, 버전 번호 제거, 푸터 `© 2026 SMARTCORE Inc.` 만 표시
- **Hero 배경 가시성 향상**: `hero-bg.svg` 전 레이어 opacity ~2.5배 증가 (0.03~0.06 → 0.08~0.25), 비네팅 오버레이 감소
- **사이드바 섹션 collapsible 전환**: 인증서 관리/위·변조 검사/보고서 & 분석/시스템 관리 4개 섹션을 접기/펼치기 가능한 버튼으로 변환 (Chevron 회전 애니메이션, 활성 자식 메뉴 자동 확장)
- **사이드바 섹션 아이콘**: FolderKey(인증서 관리), Fingerprint(위·변조 검사), ClipboardList(보고서 & 분석), Settings(시스템 관리) — Home 메뉴와 동일한 스타일 통일
- **사이드바 서브메뉴 인덴트**: `ml-3 pl-3 border-l border-gray-200` 트리형 시각 계층 구조
- **사이드바 섹션 재구성** (4섹션 체계):
  - 인증서 관리: ICAO 버전 상태, 파일 업로드, 인증서 업로드, 인증서 조회, 업로드 이력, 동기화 상태
  - 위·변조 검사: PA 검증 수행, 검증 이력, DSC 등록 승인
  - 보고서 & 분석: 인증서 보고서(인증서 통계/DSC Trust Chain/CRL 보고서/표준 부적합 DSC), PA 검증 통계, AI 인증서 분석
  - 시스템 관리: 시스템 모니터링, 사용자 관리, API 클라이언트, CSR 관리, 감사(운영 감사 로그/인증 감사 로그), API Docs
- **API Docs 이동**: 별도 섹션 → 시스템 관리 하위 그룹으로 이동
- **통계 대시보드 이동**: 인증서 관리 → 보고서 & 분석 섹션 "인증서 통계"로 이동 (라벨 변경)
- **검증 이력/통계 분리**: PA 검증 이력 → 위·변조 검사, PA 검증 통계 → 보고서 & 분석
- **클라이언트 사이드 테이블 정렬**: SortableHeader 컴포넌트 기반 12개 페이지 컬럼 정렬 (문자열/숫자/날짜/상태)
- **페이지네이션 z-index 수정**: 8개 다이얼로그 `z-50` → `z-[70]` (사이드바 z-[60] 뒤 숨김 방지)
- ~21 files changed (0 new, ~21 modified)

### v2.29.8 (2026-03-09) - 로그인 페이지 리브랜딩 + 업로드 중복 파일 UX 개선
- **로그인 페이지 제목 변경**: "ePassport 인증서 관리 시스템" → "전자여권 위·변조 검사 시스템" (데스크톱 + 모바일)
- **로그인 설명 문구 갱신**: PA 검증 기능 포함 — "ICAO PKD 인증서 관리 및 Passive Authentication 검증 플랫폼. 전자여권 인증서의 수집·검증·모니터링과 여권 칩 데이터의 위·변조 검사를 통합 수행합니다."
- **통계 카드 동적 데이터**: 로그인 페이지 "현재 등록 국가" / "현재 관리 인증서" 카드가 `GET /api/upload/statistics` API에서 실시간 데이터 조회 (기존 하드코딩 '95+', '31,200+' 제거)
- **업로드 버튼 disabled 확장**: 처리 중(`isProcessing`) 외에 완료(`FINALIZED`) 시에도 업로드 버튼 비활성화
- **중복 파일 경고 다이얼로그**: 동일 파일(SHA-256 해시 일치) 재업로드 시 빨간 경고 다이얼로그 표시 (파일명, 상태, 형식, 업로드 ID 정보 + "업로드 이력 보기" / "확인" 버튼)
- **409 콘솔 에러 억제**: relayApi 응답 인터셉터에서 409 Conflict(중복 파일) 시 `console.error` 로그 제외 (정상 비즈니스 로직)
- 3 files changed (0 new, 3 modified)

### v2.29.7 (2026-03-09) - SSE Heartbeat + Recharts null guard + HTML 표준 접근성 개선
- **SSE Heartbeat**: NotificationManager에 30초 간격 heartbeat 스레드 추가 — SSE 연결 유휴 시 `ERR_HTTP2_PROTOCOL_ERROR` 방지 (`: heartbeat\n\n` 코멘트 라인)
- **Heartbeat 구현**: `startHeartbeat()`/`stopHeartbeat()` lifecycle, 1초 간격 sleep으로 responsive shutdown, copy-release-execute 패턴으로 disconnected 클라이언트 자동 정리
- **SSE 응답 헤더**: `X-Accel-Buffering: no` 추가, `Cache-Control: no-cache, no-store, must-revalidate` 강화
- **Frontend SSE 안정화**: NotificationListener 재연결 최대 20회 제한 (기존 무한), 딜레이 조정 (3s→5s→10s→30s)
- **Recharts Tooltip null guard**: 6개소 `payload[0].payload` 접근 시 `!payload[0]` 가드 추가 — 차트 애니메이션 중 `payload` 배열 내 undefined 요소 접근 시 `Cannot read properties of undefined (reading 'payload')` TypeError 방지
  - AiAnalysisDashboard (Country PKI Maturity), DscNcReport (Conformance Code, Country), CrlReport (Country, Revocation Reason), IssuerProfileCard
- **HTML 표준 접근성 개선**: 전체 프론트엔드 폼 요소에 `id`/`name`/`htmlFor` 속성 추가 — 브라우저 autofill 지원, label-input 연결, 스크린 리더 접근성 (WCAG 2.1)
  - 10개 파일 수정: AuditLog, OperationAuditLog, DscNcReport, CrlReport, PAHistory, AiAnalysisDashboard, UploadHistory, ApiClientManagement, UserManagement, CertificateSearchFilters
  - InputField 컴포넌트에 동적 `id` 생성 (`label` 기반), 그룹 레이블 `<label>` → `<span>` 변경 (checkbox 그룹)
  - deprecated `onKeyPress` → `onKeyDown` 전환
- ~15 files changed (0 new, ~15 modified)

### v2.29.6 (2026-03-07) - 시스템 모니터링 고유 접속자 + 전체 대시보드 그리드 높이 정렬
- **모니터링 고유 접속자 수**: nginx access log 기반 최근 5분 고유 IP 카운트 (접속 현황 카드 주 지표), TCP 연결 수는 부수 정보로 표시
- **nginx stub_status 접근 수정**: allow `172.16.0.0/12` → Docker bridge 172.18.x.x 대역 포함 (기존 172.16.0.0/16에서 403 Forbidden)
- **모니터링 서비스 nginx 로그 볼륨**: `gateway-logs:/var/log/nginx:ro` 공유 볼륨 추가 (access log 읽기용)
- **ActiveConnectionsCard 리디자인**: 고유 접속자 수 ("N명 접속 중") 주 지표 + TCP 연결 수 보조 정보 바
- **전체 대시보드 그리드 높이 정렬** (8개 페이지):
  - MonitoringDashboard: MetricCard, ServiceCard, InfraCard, ConnectionPoolChart에 `h-full`
  - IcaoStatus: 3개 버전 카드 `h-full flex flex-col` + 내부 `flex-1` + Status Message `mt-auto`
  - PADashboard: Pie 차트 + 국가 리스트 카드 `h-full flex flex-col flex-1`, `max-h-72` → `flex-1 overflow-y-auto`
  - UploadDashboard: 인증서 유형별(2/3) + 출처별(1/3) 카드 `h-full`
  - AiAnalysisDashboard: 알고리즘 트렌드 + 키 크기 분포 차트 `h-full`, 발급자+확장 프로파일 그리드 `lg:max-h-[600px]`
  - SyncDashboard: 현재 상태 + DB↔LDAP 비교 카드 `h-full`
- **IssuerProfileCard/ExtensionComplianceChecklist**: `h-full flex flex-col overflow-hidden` + 리스트 `flex-1 overflow-y-auto`
- **auditApi JWT 인터셉터 누락 수정**: `auditApi.ts`에 JWT request interceptor 추가 (운영 감사 로그 401 오류 수정)
- ~15 files changed (0 new, ~15 modified)

### v2.29.5 (2026-03-07) - 인증서 업로드 결과 다이얼로그 + ICAO 버전 확인 다이얼로그 + 프론트엔드 UX 개선
- **인증서 업로드 결과 다이얼로그**: 업로드 완료 후 UploadDetail 페이지 이동 대신 인라인 결과 Dialog 표시 (파일 정보, 타입별 카운트, LDAP 저장 상태)
- **중복 인증서 UX 개선**: 전체 중복 시 주황 배너 "이미 등록된 인증서입니다" + LDAP 섹션 숨김, "DB + LDAP 저장" 버튼 중복 시 비활성화, "Duplicate" → "중복" 한국어화
- **ICAO 버전 확인 결과 다이얼로그**: "업데이트 확인" 버튼 클릭 후 결과를 Dialog로 표시 (신규 버전 수, 버전 목록, 최신 상태 확인)
- **ICAO 버전 스키마 확장**: `icao_pkd_versions` 테이블에 `notification_sent`, `notification_sent_at`, `certificate_count`, `error_message` 컬럼 추가
- **ICAO 버전 insert 멱등성**: `IcaoVersionRepository::insert()` — 이미 존재하는 버전은 `true` 반환 (기존 `false` → 실패로 오인)
- **DSC Trust Chain 보고서 간소화**: QuickLookupPanel(Trust Chain 조회 카드) 제거, 샘플 인증서 카드를 인증서 검색 링크로 변경
- **AI 분석 폴링 안정화**: 즉시 첫 폴링 실행 (3초 대기 제거), 연속 10회 API 실패 시 에러 토스트 + 폴링 중단 (기존 무한 대기), RUNNING 상태 페이지 로드 시 폴링 자동 재개, IDLE 상태 안전 처리, COMPLETED/FAILED 시 토스트 알림 표시
- 6 files changed (0 new, 6 modified)

### v2.29.4 (2026-03-06) - OpenSSL 메모리 누수 수정 (CRITICAL 1건 + HIGH 4건)
- **CRITICAL FIX — CMS_ContentInfo 예외 경로 누수**: `upload_handler.cpp` Master List 처리 중 예외 발생 시 `CMS_ContentInfo*` 미해제 → `cms` 선언을 try 블록 밖으로 이동 + catch 블록에서 `CMS_ContentInfo_free(cms)` 추가
- **HIGH FIX — X509 벡터 예외 경로 누수 (인증서 업로드)**: `upload_service.cpp` 인증서 처리 루프 중 예외 시 나머지 `X509*` 미해제 → 인덱스 기반 루프 + catch에서 잔여 인증서 전체 해제
- **HIGH FIX — X509 벡터 예외 경로 누수 (미리보기)**: `upload_service.cpp` 미리보기 루프 중 예외 시 나머지 `X509*` 미해제 → 동일 패턴 적용
- **HIGH FIX — X509_CRL RAII 전환**: `upload_service.cpp` `processCrlFile()` 수동 `X509_CRL_free()` → `std::unique_ptr<X509_CRL, decltype(&X509_CRL_free)>` RAII 가드 (모든 예외 경로 자동 해제)
- **HIGH FIX — PA allCscas 예외 경로 누수**: `certificate_validation_service.cpp` PA 검증 중 예외 시 CSCA 벡터 미해제 → 선언을 try 밖으로 이동 + catch에서 전체 `X509_free()` + `clear()`
- 4 files changed (0 new, 4 modified)

### v2.29.3 (2026-03-06) - 5차 코드 보안 + 안정성 강화 (CRITICAL 6건 + HIGH 8건 + MEDIUM 7건)
- **CRITICAL FIX — SQL 인젝션 방어**: `processing_strategy.cpp` PGconn 8개 쿼리 문자열 연결 → `PQexecParams()` 파라미터화 (uploadId 직접 삽입 제거)
- **CRITICAL FIX — detached thread `this` 캡처 제거**: `upload_handler.cpp` LDIF 처리 스레드에서 `this` 포인터 캡처 → `ldapStorageSvc` 값 캡처 + null 체크 (핸들러 소멸 후 dangling pointer 방지)
- **CRITICAL FIX — EVP_Digest 반환값 검증**: `main_utils.cpp` SHA-256 해시 계산 시 `EVP_DigestInit_ex`/`EVP_DigestUpdate`/`EVP_DigestFinal_ex` 반환값 미검사 → 실패 시 빈 문자열 반환 + 리소스 해제
- **CRITICAL FIX — Oracle OCI 예외 경로 메모리 누수**: `oracle_query_executor.cpp` catch 블록에서 `colBuffers`/`lobLocators` 미해제 → 예외 발생 시에도 `delete[]` + `OCIDescriptorFree` 정리
- **CRITICAL FIX — dl_parser OID 버퍼 오버플로**: `readOid()` length 필드 검증 누락 → `len <= 0 || len > 255` 가드 추가
- **HIGH FIX — PA JSON 필드 검증**: `pa_handler.cpp` DG 파싱 루프에 `isMember("number"/"data")` 존재 체크 + Base64 디코드 결과 empty 검증
- **HIGH FIX — data_group stoi 예외 안전**: `data_group_repository.cpp` `std::stoi()` try-catch 추가 (잘못된 DG 번호 문자열 시 기본값 0)
- **HIGH FIX — Frontend EditDialog/DeleteDialog 에러 표시**: `ApiClientManagement.tsx` 수정/삭제 다이얼로그에 에러 state + 빨간 배너 추가 (기존 사일런트 실패)
- **HIGH FIX — JWT 디코드 안전**: `PAVerify.tsx` `JSON.parse(atob(token.split('.')[1]))` try-catch 래핑 (malformed 토큰 방어)
- **HIGH FIX — Python AI 분석 메모리 정리**: `analysis.py` `_run_analysis()` finally 블록에 `gc.collect()` 추가
- **HIGH FIX — Python feature engineering 캐시 동시성**: `feature_engineering.py` `"loading"` 플래그 추가 — 중복 DB 로드 방지
- **HIGH FIX — HSTS 만료시간**: `api-gateway-ssl.conf` `max-age=86400`(1일) → `max-age=31536000`(1년)
- **HIGH FIX — Swagger CORS 와일드카드**: `swagger-nginx.conf` `Access-Control-Allow-Origin '*'` → `'$http_origin'`, CSP `frame-ancestors 'self'` only
- **MEDIUM FIX — ZIP export 경로 탐색 방지**: `certificate_service.cpp` country 코드 알파벳만 허용(ISO 3166-1), safeName `..` 포함 시 certType 대체, fingerprint `substr` 범위 검증
- **MEDIUM FIX — `::tolower` signed char UB**: `config_manager.cpp` `::tolower` → `[](unsigned char c) { return std::tolower(c); }` 람다 (음수 바이트 UB 방지)
- **MEDIUM FIX — SSE 알림 필드 검증**: `NotificationListener.tsx` 파싱된 JSON 필드에 `typeof` 타입 체크 + `slice()` 길이 제한 (title 200자, message 1000자)
- **MEDIUM FIX — Relay THREAD_NUM 범위 제한**: `pkd-relay/main.cpp` `std::max(1, std::min(256, ...))` 바운드 검증
- **MEDIUM FIX — Luckfox Swagger CORS**: `api-gateway-luckfox.conf` `/api/docs/` CORS `'*'` → `'$http_origin'`
- **MEDIUM FIX — AI Dockerfile CMD 인젝션 방지**: `CMD` shell form에서 `${UVICORN_WORKERS}` 환경변수 인용 + `exec` 접두사
- ~15 files changed (0 new, ~15 modified)

### v2.29.2 (2026-03-06) - 4차 코드 안정성 강화 + SSE HTTP/2 완전 수정 + 프론트엔드 UI 수정
- **SSE HTTP/2 완전 수정**: `proxy_hide_header Transfer-Encoding;` 추가 — Drogon 백엔드가 보내는 `transfer-encoding: chunked` 헤더를 nginx에서 제거 (HTTP/2에서 금지된 헤더, RFC 7540)
- **nginx HTTP/2 문법 업데이트**: `listen 443 ssl http2;` (deprecated) → `listen 443 ssl;` + `http2 on;` (nginx 1.25+)
- **다이얼로그 z-index 수정**: 8개 파일의 모달 다이얼로그 `z-50` → `z-[70]` (사이드바 `z-[60]` 뒤에 숨기는 문제 해결)
  - UploadHistory (상세/재시도/삭제 3개), FileUpload, PAHistory, CrlReport, UploadDetail, ApiClientManagement, CertificateDetailDialog, CountryStatisticsDialog
- **Recharts payload null guard**: 3개 파일의 커스텀 YAxis tick 렌더러에 `if (!payload) return null;` 가드 추가 (AiAnalysisDashboard, DscNcReport, CrlReport)
- **프론트엔드 파일 크기 검증**: FileUpload 클라이언트측 100MB 제한 추가 (서버 거부 전 사용자 피드백)
- **ASN1_TIME 버퍼 안전성**: `time_utils.cpp` — `asn1_time->data` 직접 포인터 사용 → null-terminated 버퍼 복사 + 길이 검증 (UTCTIME 12자, GENERALIZEDTIME 14자 최소)
- **DG2 JPEG2000 이미지 크기 검증**: `dg_parser.cpp` — 10000×10000 최대 치수 + `int` 오버플로 방지 (`int64_t` 안전 곱셈)
- **JWT 만료시간 검증**: `auth_middleware.cpp` — `atoi()` → `stoi()` + 최소 60초 하한 + 예외 처리
- **IP 파서 안전 강화**: `isIpAllowed()` IPv4 파서에 옥텟 범위(0-255), 점 개수(3), 유효 문자 검증 추가 + `std::optional` 반환
- **ReDoS 방어**: API 클라이언트 endpoint 패턴 200자 초과 시 스킵 + regex 컴파일 실패 로깅
- **페이지네이션 상한**: `page` 파라미터 최대 10000, `maxLines` 파라미터 최대 100000 제한 (CrlReport, CodeMaster, UploadStats)
- **catch-all 로깅**: 14개소 빈 `catch (...) {}` → `catch (...) { spdlog::warn(...) }` 또는 `/* non-critical */` 주석 (감사 로그, 파라미터 파싱 등)
- **서비스 의존성 null 체크**: `upload_handler.cpp` LDIF 처리 시 `ldapStorageService()` null 검증 추가
- **AI 스레드 시작 실패 처리**: `analysis.py` — `threading.Thread.start()` 예외 시 job_status FAILED 설정 + HTTP 500
- **AI 헬스체크 로깅**: DB 연결 실패 시 예외 메시지 포함, country report maturity 계산 실패 경고 로깅
- **Monitoring 파라미터 제한**: `minutes` 파라미터 1~1440 범위 제한
- 33 files changed (0 new, 33 modified), +145 / -62 lines

### v2.29.1 (2026-03-05) - SSE HTTP/2 프로토콜 에러 수정 + 업로드 통계 누락 수정
- **SSE HTTP/2 호환성**: nginx SSE location 블록에 `chunked_transfer_encoding off;` 추가 — HTTPS(HTTP/2) 환경에서 `ERR_HTTP2_PROTOCOL_ERROR` 해결
- **LDIF 업로드 totalCertificates 수정**: fingerprint 캐시 히트(중복) 조기 반환 시 `totalCertificates++` 누락 수정 — 모든 인증서가 중복일 때 결과 카드 상세 통계 미표시 문제 해결
- 3 files changed (nginx/api-gateway-ssl.conf, nginx/api-gateway.conf, ldif_processor.cpp)

### v2.29.0 (2026-03-05) - 실시간 알림 시스템 + DSC 재검증 + source_type 수정
- **실시간 알림 시스템**: SSE 기반 `NotificationManager` singleton (thread-safe, copy-release-execute 패턴) — Daily Sync/Revalidation/Reconciliation 완료 시 프론트엔드 실시간 알림 전송
- **NotificationHandler**: `GET /api/sync/notifications/stream` SSE 엔드포인트 (Drogon AsyncStreamResponse)
- **Header Bell UI**: Lucide Bell 아이콘 + unread 뱃지 + 드롭다운 알림 패널 (최대 50건, 읽음/전체삭제)
- **NotificationListener**: 전역 SSE 리스너 (Layout mount, 자동 재연결 최대 3회 → 30초 간격)
- **notificationStore**: Zustand 스토어 (notifications[], unreadCount, add/markRead/clear + toast.info 연동)
- **DSC 만료 상태 재검증**: `POST /api/sync/revalidate` — 만료된 DSC 인증서 trust chain 재평가 (Relay ValidationService)
- **Relay ValidationService**: `revalidateExpiredDsc()` — CSCA/CRL provider 어댑터 기반 trust chain 재구축 + validation_result DB 업데이트
- **SyncDashboard 재검증 UI**: "만료 상태 갱신" 버튼 + 결과 다이얼로그 (처리 건수, 상태 변경 건수, 처리 시간)
- **Reconciliation 버그 수정**: `reconciliation_log` INSERT 컬럼명 오류 + UUID 시퀀스 문제 수정
- **source_type 컬럼 누락 수정**: `saveCertificateWithDuplicateCheck()` INSERT에 `source_type` 컬럼 미포함 → 모든 인증서 `FILE_UPLOAD`로 저장되던 버그. LDIF→`LDIF_PARSED`, ML→`ML_PARSED`, 개별업로드→`FILE_UPLOAD` 정확 전달
- **nginx SSE 설정**: `/api/sync/notifications` location 블록 추가 (proxy_buffering off, read_timeout 3600s)
- **DB 스키마**: `revalidation_trust_chain` 테이블 신규 (PostgreSQL + Oracle)
- 48 files changed (20 new, 28 modified), +3,240 / -114 lines

### v2.28.2 (2026-03-05) - 코드 안정성 강화 (3차 코드 리뷰: CRITICAL 4건 + HIGH 4건)
- **CRITICAL FIX — BN_bn2hex null 체크**: `certificate_utils.cpp` `BN_bn2hex()` 반환값 null 검사 추가 — OOM 시 nullptr dereference 방지
- **CRITICAL FIX — LDAP 풀 race condition**: `ldap_connection_pool.cpp` `acquire()` — mutex 재획득 후 `totalConnections_ < maxSize_` 재검증, 초과 시 `ldap_unbind_ext_s()` 정리 (2개 스레드 동시 생성 시 풀 오버플로 방지)
- **CRITICAL FIX — nginx POST 재시도 차단**: `proxy_params` 기본값 `proxy_next_upstream off` — POST/PUT/DELETE 자동 재시도로 인한 데이터 중복 실행 방지, GET 전용 읽기 엔드포인트만 선택적 retry 허용
- **CRITICAL FIX — detached thread this 캡처 제거**: `upload_handler.cpp` detached thread에서 `this` + `uploadRepo` 캡처 제거 — 핸들러 소멸 후 dangling pointer 접근 방지 (값 캡처 `uploadId`, `contentBytes`만 유지)
- **nginx 구조 개선**: `api-gateway.conf` + `api-gateway-ssl.conf` — upload/PA location에서 중복 `proxy_next_upstream off` 제거 (proxy_params에서 상속), certificate export 별도 location 분리 (`export_limit` rate limiting)
- **std::stoi() 예외 안전 (14개소)**: 환경변수 파싱 전체 try-catch + 기본값 폴백 — `safeStoi` 람다 패턴 적용
  - `pkd-management/service_container.cpp` (5개: LDAP 풀/타임아웃)
  - `pkd-relay/service_container.cpp` (5개: LDAP 풀/타임아웃)
  - `pa-service/service_container.cpp` (1개: LDAP 타임아웃)
  - `ldap_storage_service.cpp` (2개: LDAP 쓰기/네트워크 타임아웃)
  - `pkd-relay/main.cpp`, `monitoring/main.cpp` (각 1개: THREAD_NUM)
  - `monitoring_handler.cpp` (3개: 서버 포트/메트릭 간격)
- **Oracle asInt() → scalarToInt() 전환**: `processing_strategy.cpp` — `.asInt()` 8개소를 `common::db::scalarToInt()`로 교체 (Oracle 문자열→정수 안전 변환)
- **Frontend AbortController 적용 (3개 페이지)**: `DscNcReport`, `CrlReport`, `MonitoringDashboard` — 필터/페이지 변경 시 이전 API 요청 취소로 race condition 방지
- **AI 데이터 캐시 TTL**: `feature_engineering.py` — `load_certificate_data()` 5분 TTL 인메모리 캐시 + `threading.Lock` (31K건 반복 로드 방지)
- **AI 결과 저장 per-row 에러 격리**: `analysis.py` — `_save_results()` 개별 행 try-except + 실패 카운트 로깅 (1건 실패 시 전체 롤백 방지)
- 25 files changed (0 new, 25 modified), +400 / -205 lines

### v2.28.1 (2026-03-04) - 메모리 안전 + 예외 처리 + 보안 강화
- **CRITICAL FIX — CMS_ContentInfo 메모리 누수**: Master List 처리 시 CMS 성공 경로에서 `CMS_ContentInfo_free(cms)` 미호출 → `if (!cms)` 블록 밖으로 이동하여 모든 경로에서 해제
- **BIO_new_mem_buf size_t→int 오버플로 방지**: `content.size()` > `INT_MAX` 검증 추가 (upload_handler.cpp)
- **stoi() 예외 안전**: `MAX_CONCURRENT_UPLOADS` static 초기화에서 잘못된 환경변수 값 시 기본값 3 반환 (upload_handler.cpp)
- **stoi() 예외 안전**: `parseHexBinary()` hex 파싱에 try-catch 추가 — 잘못된 hex 문자열 시 파싱 중단 (reconciliation_engine.cpp)
- **LDAP message 즉시 해제**: `ldap_search_ext_s()` 반환 후 즉시 `ldap_msgfree()` 호출 — 이후 코드에서 예외 발생 시에도 메모리 누수 방지 (reconciliation_engine.cpp 2개소)
- **Audit catch 로깅**: 9개 빈 `catch (...) {}` 블록에 `spdlog::warn("Audit log failed: {}")` 추가 — 감사 로깅 실패 시 디버깅 가능 (api_client_handler 4개, auth_handler 4개, pa_handler 1개)
- **Oracle OEM 포트 제거**: `15500:5500` 포트 매핑 제거 — 관리 인터페이스 외부 노출 방지 (docker-compose.yaml + podman)
- **SSE stale closure 수정**: `isProcessingRef` (useRef) 도입 — SSE 재연결 시 최신 처리 상태 참조 (FileUpload.tsx)
- 8 files changed (0 new, 8 modified)

### v2.28.0 (2026-03-04) - 전체 코드 품질 개선 + 테스트 인프라 구축
- **코드 분석 139건 이슈 체계적 수정** (CRITICAL 13, HIGH 37, MEDIUM 59, LOW 30)
- **Phase 1 — Security & Stability (CRITICAL)**:
  - C++ ProgressManager mutex 교착 위험 해소 (콜백을 lock 밖으로 이동)
  - C++ ReconciliationEngine LDAP 풀 미사용 → `ldapPool_->acquire()` 전환
  - C++ OracleQueryExecutor 원시 메모리 → RAII (`std::vector`) + 데드 코드 587줄 제거
  - Frontend React.lazy() 코드 스플리팅 (22개 페이지 lazy load + PageLoader fallback)
  - Frontend CertificateSearch 이중 API 호출 제거
  - Python async 엔드포인트 sync I/O → `asyncio.to_thread()` 래핑 (~15개)
  - Python `_compliance_cache` 스레드 안전 (`threading.Lock`)
  - Python 분석 작업 race condition 수정
  - 인프라 하드코딩 비밀번호 제거 (Dockerfile, ARM64/Luckfox compose)
  - Oracle 과잉 권한 제거 (`SELECT/INSERT/UPDATE/DELETE ANY TABLE`)
- **Phase 2 — Thread Safety (HIGH C++)**:
  - `localtime`/`gmtime` → thread-safe `localtime_r`/`gmtime_r` 전환 (13+ 파일)
  - AuthMiddleware `addPublicEndpoint()` compiledPatterns_ 동기화
  - `s_activeProcessingCount` RAII guard 패턴 적용
  - PA 검증 TODO 스텁 필드 실제 데이터 반영
  - `g_services` null 체크 5개소 추가
- **Phase 3 — Frontend Architecture (HIGH)**:
  - `window.location.href` → 이벤트 기반 로그아웃 전환
  - PrivateRoute JWT 토큰 만료 검사 추가
  - `alert()`/`confirm()` → ConfirmDialog 컴포넌트 전환
  - AbortController 적용 (CertificateSearch, AiAnalysisDashboard)
  - pkdApi JWT 인터셉터 추가
- **Phase 4 — AI Service Reliability (HIGH Python)**:
  - SQL 문자열 보간 → 파라미터화 쿼리
  - Feature engineering 벡터화 최적화
  - DB 비밀번호 URL 노출 → `URL.create()` 전환
  - JSONB → generic JSON 타입
- **Phase 5 — Infrastructure (HIGH)**:
  - HTTP nginx PA `auth_request` 추가
  - `proxy_next_upstream` POST 재시도 방지
  - nginx 이미지 태그 고정 (`nginx:1.27-alpine`)
  - Legacy `docker/Dockerfile` 삭제
  - CORS `X-API-Key` 헤더 추가
  - `limit_req_status 429` 설정
- **Phase 6 — Code Quality (MEDIUM 59건)**:
  - C++: ApiRateLimiter 주기적 cleanup, IPv6 정규화, ProgressManager deque 전환, EmailSender 데드 코드 제거, regex 캐시 LRU 제한
  - Frontend: AiAnalysisDashboard 폴링 stale closure 수정, UploadHistory interval cleanup, ProcessingMode MANUAL 제거, console.error DEV 가드, Recharts 타입 수정, key={index} → unique key 14건
  - Python: 데드 코드 10+ 함수 제거, Pydantic v2 `ConfigDict` 전환, Country report DB-level 필터 최적화, HTTPException(503) 에러 핸들링, `engine.dispose()` shutdown
  - Infra: TIMESTAMP → TIMESTAMPTZ 8개 스키마, proxy_buffering 충돌 해결, Docker/Podman 스크립트 shared library 통합, Frontend healthcheck 추가
- **Phase 7 — Polish (LOW 30건)**:
  - C++: 에러 메시지 필드 일관성, `#pragma once`, 매직 넘버 상수화, `isExpired` TODO 구현
  - Frontend: `__APP_VERSION__` 컴파일 타임 버전, 알림 벨 제거, aria-label 접근성 추가, SHA-256 핑거프린트 라벨 수정
  - Python: health 엔드포인트 DB 연결 체크 확장, pytest 기본 인프라 구축
- **테스트 인프라 구축 (신규)**:
  - Frontend: Vitest + React Testing Library 설정, 11개 테스트 파일 (114 passed, 1 skipped)
  - AI Analysis: pytest 8개 테스트 파일 (201 passed)
  - C++ certificate-parser: GTest 5개 테스트 파일 (122 test cases)
  - 총 438 테스트 케이스 작성
- 125 files changed (14 new, 110 modified, 1 deleted), +4,063 / -2,226 lines

### v2.27.1 (2026-03-04) - API Client Rate Limit 분리 + Podman DNS Resolver 영구 수정
- **nginx Rate Limit 분리**: `/api/auth/api-clients` 별도 location 블록 추가 — API 클라이언트 관리 엔드포인트에 `api_limit`(100r/s) 적용 (기존 `login_limit` 5r/m로 인해 API Key 발급 버튼 503 오류)
- **Root cause**: `/api/auth` location이 `login_limit`(5r/m)을 사용하여 API 클라이언트 관리 포함 모든 auth 엔드포인트에 로그인 rate limit 적용 → 페이지 로드 GET이 rate limit 소모 → POST 발급 요청 차단
- **Frontend 에러 표시**: ApiClientManagement CreateDialog에 에러 state + 빨간 배너 추가, Regenerate 핸들러에 alert 추가 (기존 `console.error` 사일런트 실패 → 사용자 피드백)
- **Podman DNS Resolver 공유 함수**: `scripts/lib/common.sh`에 `generate_podman_nginx_conf()` 추가 — Podman aardvark-dns 게이트웨이 IP 자동 감지 후 nginx resolver 치환
- **Root cause**: Production Podman 환경에서 nginx `resolver 127.0.0.11`(Docker DNS) 사용으로 `auth_request` 서브리퀘스트 DNS 해석 타임아웃 → PA 서비스 500 오류
- **Podman 스크립트 통합**: `start.sh`, `restart.sh`, `clean-and-init.sh` 모두 `generate_podman_nginx_conf()` 공유 함수 사용으로 인라인 DNS 감지 코드 제거 (~50줄 중복 삭제)
- **restart.sh api-gateway 자동 적용**: api-gateway 단독 재시작 시에도 `generate_podman_nginx_conf()` 자동 호출 (DNS resolver 누락 방지)
- **docker-compose.podman.yaml 기본값 수정**: `NGINX_CONF` 기본값을 존재하지 않는 `api-gateway-podman-ssl.conf` → 생성 파일 `.docker-data/nginx/api-gateway.conf`로 변경
- **Docker clean-and-init SSL 보존**: `.docker-data` 삭제 시 SSL 인증서(`server.crt/key`) 백업 후 복원, SSL 모드 감지 시 HTTPS URL 표시
- nginx 3개 파일 수정 (api-gateway-ssl.conf, api-gateway.conf, api-gateway-luckfox.conf)
- 10 files changed (0 new, 10 modified)

### v2.27.0 (2026-03-04) - FAILED 이어하기 재처리 + COMPLETED 재업로드 차단 + SAVEPOINT 에러 격리
- **FAILED 이어하기 재처리**: FAILED 업로드 retry 시 기존 데이터 유지, fingerprint 캐시 기반으로 이미 처리된 인증서 스킵 (~15초 vs 기존 3분 35초)
- **COMPLETED 재업로드 완전 차단**: COMPLETED 파일 retry 불가(400), 동일 파일 재업로드 불가(409 DUPLICATE_FILE)
- **재처리 확인 다이얼로그**: 재처리 클릭 시 확인 팝업 표시 (파일명, 상태, 진행률, 이어하기 모드 안내)
- **SAVEPOINT 에러 격리**: PostgreSQL 배치 트랜잭션 내 개별 엔트리 실패 시 `SAVEPOINT`/`ROLLBACK TO SAVEPOINT`로 전체 트랜잭션 보호 (cascade abort 방지)
- **IQueryExecutor 인터페이스 확장**: `savepoint()`, `rollbackToSavepoint()` 가상 메서드 추가 (빈 기본 구현, backward compatible)
- **PostgreSQLQueryExecutor SAVEPOINT 구현**: 배치 모드 pinned connection에서 SAVEPOINT/ROLLBACK TO SAVEPOINT 실행
- **Early fingerprint cache check**: `parseCertificateEntry()`에서 fingerprint 계산 직후 캐시 확인 → 히트 시 X.509 파싱/검증/DB/LDAP 전체 스킵
- **DB 통계 재집계**: resume 모드 처리 완료 후 DB에서 certificate/validation_result 기반 정확한 최종 통계 재계산
- **validation_result 중복 방지**: PostgreSQL `ON CONFLICT (certificate_id, upload_id) DO NOTHING`, Oracle `MERGE INTO` 적용
- **실패 핸들러 통계 보존**: 처리 실패 시 기존 통계를 0으로 리셋하지 않고 상태만 FAILED로 변경
- Backend: upload_handler.cpp (retry FAILED-only), upload_service.cpp (COMPLETED→DUPLICATE), processing_strategy.cpp (DB 재집계)
- Shared: i_query_executor.h, postgresql_query_executor.h/.cpp (SAVEPOINT)
- Frontend: UploadHistory.tsx (재처리 다이얼로그, FAILED-only 버튼)
- 10 files changed (0 new, 10 modified)

### v2.26.1 (2026-03-03) - 3단계 성능 최적화 (X.509 이중 파싱 제거 + Statement 캐시 + 배치 커밋)
- **X.509 이중 파싱 제거**: 동일 인증서에 대해 `d2i_X509()` + `extractMetadata()`가 2회 발생하던 것을 1회로 줄임 — `ldif_processor.cpp`에서 추출한 `CertificateMetadata`를 `saveCertificateWithDuplicateCheck()`에 직접 전달
- **IQueryExecutor 배치 인터페이스**: `beginBatch()`/`endBatch()` 가상 메서드 추가 (빈 기본 구현으로 backward compatible)
- **OracleQueryExecutor 배치 모드**: 세션 고정 (acquire/release 1회), OCI Statement 캐시 (`unordered_map<SQL, OCIStmt*>`), COMMIT 지연 → `endBatch()`에서 1회 커밋
- **PostgreSQLQueryExecutor 배치 모드**: 커넥션 고정, `BEGIN`/`COMMIT` 트랜잭션 래핑
- **ldif_processor 배치 호출**: 처리 루프 전 `beginBatch()`, 500건마다 `endBatch()`/`beginBatch()` 중간 커밋, 루프 후 `endBatch()` 최종 커밋
- **실측 성능**: v2.26.0(11.1ms, 90건/초) → **v2.26.1(7.3ms, 137.5건/초)** = **1.53배 추가 개선**, 최초 대비 **21.2배 개선**
- **처리 시간**: 30,114건 기준 5분 35초 → **3분 39초** (2분 절감)
- 12 files changed (0 new, 12 modified)

### v2.26.0 (2026-03-03) - Oracle 업로드 성능 최적화 Phase 2 (Fingerprint 프리캐시)
- **Fingerprint 인메모리 프리캐시**: LDIF 처리 시작 전 전체 인증서 fingerprint (~31K건)를 1회 벌크 로드하여 `unordered_map` 캐시 → 매 인증서 중복체크 SELECT 제거 (30K 쿼리 → 1회)
- **캐시 히트 시 X.509 파싱 완전 스킵**: duplicate 인증서는 `d2i_X509` + 27개 메타데이터 파라미터 구성을 건너뜀 (기존: SELECT 10ms + parse 1.5ms → 캐시 조회 0.001ms)
- **신규 INSERT 후 캐시 동기화**: 같은 업로드 배치 내 중복 방지를 위해 INSERT 성공 시 즉시 캐시 추가
- **DB SELECT 폴백**: `fingerprintCacheLoaded_` 플래그로 캐시 미로드 시 기존 per-entry SELECT 유지 (안전한 폴백)
- **`preloadExistingFingerprints()`**: `CertificateRepository`에 벌크 SELECT + `unordered_map` 캐시 구축 메서드 추가
- **`addToFingerprintCache()`**: 신규 인증서 INSERT 후 캐시 업데이트 메서드 추가
- **실측 성능**: v2.25.9(31.5ms, 31.7건/초) → **v2.26.0(11.1ms, 90건/초)** = **2.84배 추가 개선**, 최초 대비 **13.9배 개선**
- **처리 시간**: 30,114건 기준 15분 49초 → **5분 35초** (10분 절감)
- **Fingerprint 프리로드**: 1,374건 40ms (초기 업로드, 31K건 시 ~200ms 예상)
- 3 files changed (0 new, 3 modified)

### v2.25.9 (2026-03-02) - Oracle 업로드 성능 최적화 (CSCA 캐시 + Regex 사전컴파일)
- **CSCA 인메모리 캐시**: LDIF 처리 시작 전 전체 CSCA (~845건)를 1회 벌크 로드하여 메모리 캐시 → DSC 29,838건 각각의 CSCA DB 조회 제거 (30K 쿼리 → 1~2회)
- **Oracle LOB 세션 드롭 완화**: CSCA 조회 시 `certificate_data` BLOB SELECT → OCI 세션 파괴 (`OCI_SESSRLS_DROPSESS`) 반복이 캐시로 근본 해결
- **CSCA 캐시 lazy reload**: 새 CSCA 저장 시 캐시 무효화 → 다음 DSC 조회 시 자동 재로드 (LDIF 파일 내 CSCA 추가에도 정합성 보장)
- **Regex 사전 컴파일**: `OracleQueryExecutor`의 PostgreSQL→Oracle SQL 변환 패턴 8개를 매 쿼리 재컴파일 → static 1회 컴파일 (~150K 컴파일 제거)
- **DbCscaProvider**: `preloadAllCscas()`, `invalidateCache()` 메서드 추가, normalized DN 기반 캐시 조회
- **CertificateRepository**: `findAllCscas()` 벌크 조회 메서드 추가 (캐시 로드용)
- **예상 성능**: Oracle 업로드 182ms/건 → ~32ms/건 (5.7배 개선, 5.5건/초 → ~30건/초)
- 6 files changed (0 new, 6 modified)

### v2.25.8 (2026-03-02) - SQL 인덱스 최적화 (Oracle 패리티 + 복합 인덱스)
- **Oracle 패리티 인덱스 (2개)**: PostgreSQL에만 존재하던 `certificate.subject_dn`, `certificate.issuer_dn` 인덱스를 Oracle에 추가
- **Oracle 중복 인덱스 제거**: `link_certificate.fingerprint_sha256` — UNIQUE 제약(`uq_lc_fingerprint`)이 이미 인덱스 역할, 중복 `CREATE INDEX` 제거
- **복합 인덱스 (7개, 양쪽 DB)**: 단일 컬럼 인덱스로 커버되지 않던 다중 조건 쿼리 최적화
  - `certificate(stored_in_ldap, created_at)` — Relay 동기화 쿼리 (`WHERE stored_in_ldap=FALSE ORDER BY created_at`)
  - `certificate(country_code, certificate_type)` — 국가 통계 쿼리 (`GROUP BY country_code, certificate_type`)
  - `certificate(certificate_type, created_at)` — CSCA 조회 (`WHERE type='CSCA' ORDER BY created_at`)
  - `crl(stored_in_ldap, created_at)` — CRL 동기화 쿼리
  - `validation_result(validation_status, country_code)` — 검증 통계 쿼리
  - `operation_audit_log(operation_type, created_at)` — 감사 로그 필터 쿼리
  - `ai_analysis_result(anomaly_label, anomaly_score DESC)` — 이상 목록 필터+정렬 쿼리
- **마이그레이션 스크립트**: PostgreSQL (`CREATE INDEX IF NOT EXISTS`) + Oracle (PL/SQL `ORA-955`/`ORA-1408` 예외 처리) — 기존 DB 안전 적용
- **Oracle 실측 검증**: 9개 인덱스 생성 확인 (1개는 UNIQUE 제약으로 이미 커버)
- PostgreSQL init scripts 3개 + Oracle init scripts 3개 수정, 마이그레이션 스크립트 2개 신규
- 8 files changed (2 new, 6 modified)

### v2.25.7 (2026-03-02) - 안정성 강화 + AI 벡터화 + 코드 품질 개선
- **Phase 1 — 안정성 (5개 작업)**:
  - **OpenSSL EVP_MD_CTX null 체크** (3개소): `EVP_MD_CTX_new()` 반환값 미검사 → OOM 시 segfault 방지 (upload_service.cpp, pa_verification_service.cpp, ldap_certificate_repository.cpp)
  - **optional `.value()` 안전화**: `auth_handler.cpp` — `.value()` → `.value_or("unknown")` (예외 가능성 제거)
  - **LDAP 커넥션 RAII 가드** (4개소): `upload_handler.cpp` — 수동 `ldap_unbind_ext_s()` 호출 → `LdapConnectionGuard` RAII 패턴 (예외 시 연결 누수 방지)
  - **임시 파일 RAII 가드** (2개소): `certificate_service.cpp` — `mkstemp` 파일 수동 삭제 → `TempFileGuard` RAII 패턴 (예외 시 `/tmp` 잔류 방지)
  - **Docker Compose 리소스 제한**: `docker-compose.yaml` + `docker-compose.podman.yaml` — 모든 서비스에 `deploy.resources.limits/reservations` 추가 (OOM 방지)
- **Phase 2 — AI 성능 (4 파일 벡터화)**:
  - **feature_engineering.py**: 45개 feature `iterrows()` 루프 → NumPy/Pandas 벡터 연산 전환
  - **risk_scorer.py**: 10개 위험 카테고리 벡터화, findings 생성은 고위험(>20) 인증서만 루프
  - **extension_rules_engine.py**: 결과 캐시 + `np.unique()` 집계로 중복 연산 제거
  - **issuer_profiler.py**: `DBSCAN` 미사용 import 삭제 + `map(profiles)` 벡터 연산
  - **성능 측정**: Oracle XE 환경에서 68초 (DB I/O 병목, 연산 자체는 개선됨)
- **Phase 3 — 코드 품질 (2개 작업)**:
  - **Shell 공유 라이브러리**: `scripts/lib/common.sh` (319줄, 14 함수) — Docker/Podman 스크립트 중복 85%+ 제거, start/health/backup 6개 스크립트 리팩토링
  - **API 응답 헬퍼**: `handler_utils.h` — `sendJsonSuccess()`, `notFound()` 함수 추가 (신규 엔드포인트용)
- Docker build 검증: pkd-management, pa-service, ai-analysis 3개 서비스 모두 성공
- Shell 스크립트 `bash -n` 구문 검증: 7개 모두 통과
- ~20 files changed (1 new, ~19 modified), ~1,010 lines changed

### v2.25.6 (2026-03-02) - Stepper 깜빡임 수정 + DSC_NC ICAO 준수 판정 수정
- **Bug fix**: 업로드 진행 Stepper 수평 레이아웃 — 단계 전환 시 상세 패널 깜빡임 현상 수정
- **Root cause**: `activeStep`이 단계 전환 중(예: VALIDATION → DB_SAVING) `undefined`가 되면서 상세 패널(진행률 바 + 상세 메시지)이 순간 사라졌다 다시 나타남
- **Fix**: `useRef`로 마지막 active step을 기억하여 전환 중에도 패널 유지, 모든 단계 완료 시에만 패널 제거
- **Bug fix**: DSC_NC 인증서 ICAO 9303 준수 판정 오류 — 기술 체크(알고리즘, 키 크기, KeyUsage) 통과 시 "준수"로 잘못 분류
- **Root cause**: `checkIcaoCompliance()`가 DSC_NC를 DSC와 동일하게 기술 체크 수행 → SHA256+RSA2048 DSC_NC 255건이 준수로 판정
- **Fix**: DSC_NC는 ICAO PKD에서 표준 미준수로 분류한 인증서이므로 기술 체크와 무관하게 항상 `NON_CONFORMANT` 반환
- 2 files changed (0 new, 2 modified: Stepper.tsx, progress_manager.cpp)

### v2.25.5 (2026-02-28) - 마이크로서비스 리소스 동적 확장성
- **5개 신규 환경변수**: 하드코딩된 리소스 파라미터를 환경변수로 외부화 (docker-compose environment만 수정하면 배포 환경별 튜닝 가능)
- `LDAP_NETWORK_TIMEOUT` (기본 5초): LDAP 네트워크 타임아웃 — PKD Mgmt, PA, Relay
- `LDAP_HEALTH_CHECK_TIMEOUT` (기본 2초): LDAP 헬스체크 타임아웃 — PKD Mgmt, Relay
- `LDAP_WRITE_TIMEOUT` (기본 10초): LDAP 쓰기 연결 타임아웃 — PKD Mgmt (LdapStorageService)
- `MAX_CONCURRENT_UPLOADS` (기본 3): 동시 업로드 처리 한도 — PKD Mgmt (upload_handler)
- `MAX_BODY_SIZE_MB` (기본 100/50): HTTP 업로드 크기 제한 — PKD Mgmt 100MB / PA 50MB
- **shared lib**: `LdapConnectionPool` 생성자에 `networkTimeoutSec`, `healthCheckTimeoutSec` 파라미터 추가
- **PA Service**: 자체 LDAP 풀 + 직접 LDAP 연결 모두 `LDAP_NETWORK_TIMEOUT` 적용
- **docker-compose**: `docker-compose.yaml` + `docker-compose.podman.yaml` 3개 서비스에 신규 환경변수 추가
- Backward compatible: 환경변수 미설정 시 기존 기본값으로 동작
- 16 files changed (0 new, 16 modified)

### v2.25.4 (2026-02-28) - 서버 리소스 최적화 (환경별 튜닝)
- **Production (16코어/14GB)**: DB Pool min 2→4, max 10→20, LDAP Pool min 2→4, max 10→20, shm_size 1g→2g
- **Production AI Analysis**: uvicorn workers 1→4, DB Pool 5→10, overflow 10→20
- **Local (8코어/12GB)**: THREAD_NUM 16→8 (코어 수 맞춤), Monitoring THREAD_NUM 16→4, AI workers 1→2
- **AI Dockerfile**: `UVICORN_WORKERS` 환경변수로 workers 수 런타임 설정 가능 (`--workers ${UVICORN_WORKERS:-1}`)
- **Oracle XE SGA 검증**: SGA 2GB/1.5GB는 XE 21c Docker 컨테이너에서 ORA-56752로 기동 실패 — SGA 1GB가 안정적 최대값으로 확인
- 5 files changed (0 new, 5 modified)

### v2.25.3 (2026-02-28) - Oracle XE 안정화 + XEPDB1 Healthcheck 개선
- **CRITICAL FIX**: `00-ee-tuning.sql` 삭제 — EE 파라미터(SGA 4GB, PGA 2GB, PROCESSES 1000)가 XE 이미지에 적용되어 `ORA-56752`로 Oracle 기동 실패하던 근본 원인 제거
- **00-xe-tuning.sql**: XE 컨테이너 안정 파라미터 (SGA 1GB, PGA 512MB, PROCESSES 150, OPEN_CURSORS 300)
- **Healthcheck 개선**: docker-compose.yaml + docker-compose.podman.yaml — CDB(XE) 체크 → XEPDB1(PDB) 체크로 변경, XEPDB1 미오픈 상태에서 healthy 판정 방지
- **start.sh XEPDB1 대기**: Podman/Docker 양쪽 start.sh에 Oracle XEPDB1 준비 대기 로직 추가 (최대 120초, 5초 간격 폴링)
- **health.sh XEPDB1 체크**: 컨테이너 health status 외에 XEPDB1 실제 쿼리 체크 추가
- **fix-oracle-memory.sh**: Production SPFILE 복구 스크립트 — 이미 EE 파라미터가 기록된 환경 복구용
- **주석 정정**: docker-compose 파일에서 "Oracle EE 21c (Enterprise Edition)" → "Oracle XE 21c (Express Edition)"
- 7 files changed (2 new, 5 modified, 1 deleted)

### v2.25.2 (2026-02-28) - 전체 서비스 운영 감사 로그 확장
- **OperationType enum 확장**: 15개 신규 작업 유형 추가 (API_CLIENT_CREATE/UPDATE/DELETE/KEY_REGEN, CODE_MASTER_CREATE/UPDATE/DELETE, USER_CREATE/UPDATE/DELETE, PASSWORD_CHANGE, UPLOAD_RETRY, CERT_UPLOAD, ICAO_CHECK, TRIGGER_DAILY_SYNC)
- **PKD Management 감사 로그**: ApiClientHandler (4개: CREATE/UPDATE/DELETE/KEY_REGEN), CodeMasterHandler (3개: CREATE/UPDATE/DELETE), AuthHandler (4개: USER_CREATE/UPDATE/DELETE/PASSWORD_CHANGE)
- **PKD Management DI**: ApiClientHandler, CodeMasterHandler, AuthHandler에 `IQueryExecutor*` 주입 + ServiceContainer 수정
- **PA Service 감사 로그**: PaHandler `handleVerify()` — PA_VERIFY 감사 로그 (country, documentNumber, status 메타데이터)
- **PA Service DI**: PaHandler에 `IQueryExecutor*` 주입 + main.cpp 수정
- **PKD Relay 감사 로그**: SyncHandler (4개: SYNC_CHECK/CONFIG_UPDATE/REVALIDATE/TRIGGER_DAILY_SYNC), ReconciliationHandler (1개: RECONCILE with dryRun/totalProcessed/successCount/failedCount 메타데이터)
- **감사 로그 패턴**: 성공/실패 양쪽 모두 기록, `logOperation()` 예외 삼킴으로 비즈니스 로직 영향 없음
- **변수명 충돌 수정**: CodeMasterHandler `handleCreate()` — `meta` → `auditMeta` (기존 metadata 파싱 변수와 충돌)
- 총 16개 신규 감사 로그 엔드포인트 추가 (기존 ~9개 → 총 ~25개)
- 13 files changed (0 new, 13 modified)
- Docker build 검증: pkd-management, pa-service, pkd-relay 3개 서비스 모두 성공

### v2.25.1 (2026-02-28) - API 클라이언트 사용량 추적 + 감사 로그 Oracle 수정
- **CRITICAL FIX**: `isIpAllowed()` CIDR 매칭 로직 — `/8`, `/16` 서브넷이 항상 `/24`처럼 동작하는 버그 수정
- **Root cause**: `10.0.0.0/8` → 마지막 옥텟만 제거하여 `10.0.0.` prefix 비교 → `10.89.1.42` 매칭 실패
- **Fix**: 비트 연산 기반 정확한 CIDR 매칭으로 교체 (IP→32bit 정수 변환, 서브넷 마스크 적용 비교)
- **nginx fix**: `/internal/auth-check` location에 `proxy_set_header X-API-Key $http_x_api_key;` 누락 — API Key 헤더가 auth_request 서브리퀘스트에 전달되지 않아 사용량 추적 불가
- **Oracle CLOB fix**: `operation_audit_log` — CLOB 컬럼(user_agent, request_path, error_message, metadata) `TO_CHAR()` 래핑, LOB/non-LOB 혼합 fetch로 1행만 반환 + 데이터 garbling 수정
- **Oracle CLOB fix**: `auth_audit_log` — CLOB 컬럼(user_agent, error_message) `TO_CHAR()` 래핑 (동일 패턴 선제 수정)
- 5 files changed (0 new, 5 modified: auth_middleware.cpp, audit_repository.cpp, auth_audit_repository.cpp, api-gateway-ssl.conf, api-gateway-luckfox.conf)

### v2.25.0 (2026-02-28) - MANUAL 모드 제거 + AUTO 모드 LDAP 복원력 + 실패 업로드 재시도
- **MANUAL 모드 완전 제거**: `ManualProcessingStrategy` 클래스 및 `ProcessingStrategyFactory` 제거 (~470줄 삭제)
- **Backend**: `handleValidate()` 핸들러 + `/api/upload/{uploadId}/validate` 라우트 제거
- **Backend**: `processLdifFileAsync()`, `processMasterListFileAsync()` — processingMode 분기 제거, 항상 AUTO 처리
- **Backend**: LDIF/ML 업로드 시 `processingMode` form 파라미터 파싱 제거
- **Backend**: LDAP 연결 실패 시 차단→경고로 변경 — DB-only 저장 후 Reconciliation으로 LDAP 동기화 가능
- **Backend**: SSE "LDAP 연결 불가 - DB 전용 모드" 알림 전송
- **Backend**: `POST /api/upload/{id}/retry` 엔드포인트 추가 — FAILED 업로드 부분 데이터 정리 후 재처리
- **Backend**: `cleanupPartialData()` — validation_result, certificate_duplicates, certificate, crl, master_list 레코드 정리
- **Frontend**: Processing Mode Selector UI 제거, Manual Mode Controls 패널 제거
- **Frontend**: `processingMode` state/localStorage 로직 제거 (~200줄 삭제)
- **Frontend**: `triggerParse()`, `triggerValidate()`, `triggerLdapUpload()` 함수/훅 제거
- **Frontend**: UploadHistory에 재시도 버튼 추가 (FAILED 상태, RefreshCw 아이콘 + spin 애니메이션)
- **Frontend**: `uploadApi.retryUpload()` API 함수 추가
- **SSL**: `init-cert.sh`, `renew-cert.sh` — SAN에 `DNS.4 = dev.$DOMAIN` 추가
- 11 files changed, 349 insertions, 1,297 deletions (net -948 lines)

### v2.24.1 (2026-02-27) - Podman 스크립트 안정성 + HTTPS Client 인증서 관리 개선
- **stop.sh**: `--profile oracle/postgres` 플래그 추가 — DB 컨테이너(Oracle/PostgreSQL)가 중지되지 않는 버그 수정
- **start.sh**: `mkdir -p` 에러 무시 (`2>/dev/null || true`), `chmod sudo` fallback 실패 시 스크립트 종료 방지
- **health.sh**: API Gateway 헬스 체크 port 18080→80 변경 (SSL 설정에서 port 8080 블록은 `/health`만 포함)
- **restart.sh**: `--profile` 플래그 추가, 전체 재시작 시 `stop.sh` + `start.sh` 호출 (Podman 컨테이너 의존성 순서 문제 우회)
- **setup-pkd-access.ps1**: Thumbprint 비교 → Subject CN(`ICAO Local PKD Private CA`) 기반 구 인증서 전부 삭제 후 재설치 (CA 재발급/서버 IP 변경 시 브라우저 인증서 찾기 실패 방지)
- Production 5회 반복 테스트 통과 (clean-and-init: Oracle 31 테이블 + LDAP 5 DIT 엔트리 + API 13/13 OK)
- 7개 Podman 스크립트 전수 테스트 완료 (stop, start, health, logs, restart, backup, restore)
- 6 files changed (0 new, 6 modified)

### v2.24.0 (2026-02-27) - Production Podman Migration + Oracle Schema Consolidation
- **Podman Migration**: Production RHEL 9 서버 (10.0.0.220) Docker CE → Podman 5.6.0 전환
- **Podman Compose**: `docker/docker-compose.podman.yaml` — condition 제거, image 필드 추가, init 컨테이너 제거
- **Podman scripts**: `scripts/podman/` (start, stop, restart, health, logs, clean-and-init, backup, restore) + root wrappers
- **SELinux Rootless Podman**: `:Z`/`:z` 볼륨 라벨 대신 2단계 `chcon` 사전 라벨링 (`container_file_t` + MCS `s0` 제거)
- **CNI DNS**: `podman-plugins` 패키지의 dnsname 플러그인으로 컨테이너 간 호스트명 해석 (Docker 내장 DNS 대체)
- **nginx DNS**: Podman aardvark-dns 게이트웨이 IP 자동 감지 → resolver 치환
- **LDAP init**: `clean-and-init.sh`에서 `podman exec`로 직접 MMR + DIT 초기화 (Docker init 컨테이너 불필요)
- **Oracle schema**: `docker/db-oracle/init/03-core-schema.sql` 리팩토링 — 독립 실행 가능, `CONNECT`/`SET SQLBLANKLINES`/`WHENEVER SQLERROR`/`COMMIT`/`EXIT` 추가
- **Oracle schema**: `10-code-master.sql`, `11-ai-analysis.sql`, `12-api-clients.sql` — 독립 실행 지시문 추가
- **SSL cert**: `init-cert.sh`, `renew-cert.sh` — `--ip` 플래그 추가 (SAN에 추가 IP 주입)
- **Client script**: `setup-pkd-access.ps1` — IP 10.0.0.163→10.0.0.220, CA 인증서 갱신
- Documentation: `PODMAN_DEPLOYMENT.md` (SELinux MCS, DNS, 트러블슈팅), `SERVER_SETUP_10.0.0.220.md` 업데이트
- 서버 이전: `SERVER_SETUP_10.0.0.163.md` 삭제 (구 서버)
- 23+ files changed (15 new, 8+ modified, 1 deleted)

### v2.23.0 (2026-02-26) - DoS 방어 보강 + 대시보드 통계 수정 + Admin 권한 UI
- **Security — DoS 방어**: LDIF 업로드 파일 크기 제한 (100MB), Master List 업로드 파일 크기 제한 (30MB), 크기 초과 시 HTTP 413 반환
- **Security — 동시 처리 제한**: `std::atomic<int> s_activeProcessingCount` 기반 최대 3개 동시 업로드 처리, 초과 시 HTTP 503 + Retry-After 반환
- **Security — LDAP 타임아웃**: LDAP 쓰기 연결에 `LDAP_OPT_NETWORK_TIMEOUT` (10초) 설정 (upload_handler, ldap_storage_service)
- **Security — 캐시 만료**: ProgressManager `cleanupStaleEntries()` — 30분 이상 미갱신 항목 자동 정리 (progressCache_ + sseCallbacks_)
- **Security — 분포 map 제한**: `safeIncrementMap()` 헬퍼 — validationReasons/signatureAlgorithms/complianceViolations map 최대 크기 제한 (100/50/100)
- **Security — nginx per-IP**: `limit_conn_zone` + `limit_conn conn_limit 20` (api-gateway.conf, api-gateway-ssl.conf, api-gateway-luckfox.conf)
- **Bug fix**: 업로드 대시보드 "검증 실패" 카드 — `trustChainInvalidCount`(15,005) → `invalidCount`(1)로 변경, PENDING(CSCA 미발견) 15,004건과의 중복 계수 제거
- **Bug fix**: 검증 사유 breakdown 쿼리 — `trust_chain_message IS NOT NULL` 필터 제거 + COALESCE 폴백 메시지 (`csca_found` 기반 분기)
- **Bug fix**: PA 검증 이력 상태 카드 — 현재 페이지(5건) 기반 카운트 → `/pa/statistics` API 전체 카운트로 변경 (페이지 변경 시 수량 고정)
- **Bug fix**: "만료-유효" → "만료(서명유효)" 라벨 변경 (UploadDashboard, ValidationSummaryPanel)
- **Frontend**: `AdminRoute` 컴포넌트 — admin 전용 라우트 가드 (UserManagement, ApiClient, AuditLog, OperationAuditLog)
- **Frontend**: Sidebar 권한 기반 메뉴 표시 (`adminOnly`, `permission` 필드), compact 리팩토링
- **Frontend**: Header 리디자인, Profile 페이지 확장, MonitoringDashboard 개선, UserManagement UI 개선
- **Backend**: API Client/Auth handler 리팩토링, User repository 권한 관련 변경
- Documentation: EULA.md, LICENSE_COMPLIANCE.md 추가
- 29 files changed (5 new, 24 modified)

### v2.22.1 (2026-02-25) - PA auth_request Backward Compatibility Fix + UsageDialog UX
- **CRITICAL FIX**: 미등록/유효하지 않은 API Key로 PA 엔드포인트 호출 시 401 대신 200 반환 — 기존 외부 클라이언트(Java Apache-HttpClient 등)가 미등록 X-API-Key 헤더를 전송해도 PA 서비스 정상 이용 가능
- **Root cause**: v2.22.0에서 nginx auth_request 추가 후, `handleInternalAuthCheck()`가 미등록 API Key에 대해 401을 반환하여 기존 클라이언트 차단
- **Fix**: `auth_middleware.cpp` — 미등록 API Key 시 200 OK 반환 (경고 로그만 기록), 등록된 유효한 키만 사용량 추적/Rate Limiting 적용
- **Frontend UsageDialog**: 엔드포인트 테이블 순위 번호 전체 표시 (top 3 컬러 + 나머지 회색 배지), 다이얼로그 전체 compact 레이아웃 (spacing, font size, bar size 축소)
- Documentation: PA_API_GUIDE.md v2.1.12 — PA 엔드포인트 미등록 키 허용 동작 명시, Troubleshooting 업데이트
- Documentation: API_CLIENT_USER_GUIDE.md v1.0.1 — PA 미등록 키 FAQ 추가, 에러 코드 테이블 보완
- 4 files changed (0 new, 4 modified: auth_middleware.cpp, ApiClientManagement.tsx, PA_API_GUIDE.md, API_CLIENT_USER_GUIDE.md)

### v2.22.0 (2026-02-25) - API Client Usage Tracking + PA nginx auth_request
- **Bug fix**: `insertUsageLog()` was never called — auth middleware only incremented counter (`updateUsage`), detailed log was never written to `api_client_usage_log`
- **Bug fix**: API Key regeneration didn't save hash to DB — `handleRegenerate()` used `update()` which excludes `api_key_hash`/`api_key_prefix` columns; added dedicated `updateKeyHash()` method
- **PA Service API Key tracking**: nginx `auth_request` module sends subrequest to PKD Management for `/api/pa/*` endpoints; API Key validation, rate limiting, and usage logging handled by PKD Management without modifying PA Service code
- **Internal auth endpoint**: `GET /api/auth/internal/check` — nginx-only internal endpoint; validates X-API-Key, checks rate limits, logs usage with original URI/method/IP from nginx headers (X-Original-URI, X-Original-Method, X-Real-IP)
- **Frontend UsageDialog**: API Client management page — usage history modal with period selector (7/30/90 days), summary cards, horizontal BarChart (Recharts) for top endpoints, detail table with rank badges and percentages
- nginx: `auth_request /internal/auth-check` on `/api/pa` location (api-gateway-ssl.conf, api-gateway-luckfox.conf), `@auth_denied`/`@auth_forbidden` named locations for JSON error responses
- Backend: `handleInternalAuthCheck()` in AuthMiddleware, `updateKeyHash()` in ApiClientRepository, fallback route in ApiClientHandler
- PA Service: zero code changes (service isolation maintained)
- Multi-DBMS: PostgreSQL + Oracle dual support maintained
- 9 files changed (0 new, 9 modified)

### v2.21.0 (2026-02-24) - API Client Authentication (X-API-Key)
- New feature: External client API Key authentication for server-to-server (M2M) API access
- **API Key format**: `icao_{prefix}_{random}` (46 chars), SHA-256 hash stored in DB, raw key shown only at creation
- **Auth middleware**: `X-API-Key` header validation alongside existing JWT Bearer — dual authentication support
- **Permission model**: 12 granular permissions (cert:read, cert:export, pa:verify, pa:read, pa:stats, upload:read, upload:write, report:read, ai:read, sync:read, icao:read, api-client:manage)
- **Rate Limiting**: In-memory sliding window per-client (3-tier: per-minute, per-hour, per-day), 429 response with Retry-After header
- **IP whitelist**: Per-client allowed IP/CIDR restriction (`allowed_ips` field)
- **Endpoint restriction**: Per-client allowed endpoint patterns (`allowed_endpoints` field)
- **7 management API endpoints** (admin JWT required): create, list, detail, update, deactivate, regenerate key, usage stats
- Backend: `ApiClientHandler` (7 endpoints), `ApiClientRepository` (CRUD + usage tracking, PostgreSQL + Oracle), `ApiRateLimiter` (thread-safe sliding window), `api_key_generator` (SHA-256 + Base62)
- Backend: `api_clients` + `api_client_usage_log` DB tables (PostgreSQL + Oracle schemas)
- Frontend: `ApiClientManagement.tsx` page — client CRUD, key generation with copy-to-clipboard, usage display
- Frontend: Sidebar "Admin & Security" section — "API 클라이언트" menu item (KeyRound icon)
- OpenAPI: `pkd-management.yaml` v2.21.0 — 7 paths, 6 schemas, `apiKeyAuth` security scheme
- Documentation: `API_CLIENT_ADMIN_GUIDE.md` (관리자 가이드, 7 endpoints 상세, Permission/Rate Limit/IP 설정)
- Documentation: `API_CLIENT_USER_GUIDE.md` (외부 연동 가이드, Python/Java/C#/curl 예제, FAQ)
- Multi-DBMS: PostgreSQL + Oracle dual support (IQueryExecutor pattern, conditional parameter binding)
- 15+ files changed (10 new, 5+ modified)

### v2.20.2 (2026-02-22) - Oracle CRL Report BLOB Read Fix
- **CRITICAL FIX**: Oracle CRL report "폐기 인증서" 0건 표시 — OCI LOB locator가 SQLT_LBI로 INSERT된 BLOB 데이터를 33~89 bytes로 truncate하여 읽음 (실제 280~1670 bytes)
- **Root cause 1 — BLOB truncation**: `OCILobRead`가 `SQLT_LBI` (Long Binary) binding으로 INSERT된 BLOB을 LOB locator로 읽을 때 데이터 잘림
- **Root cause 2 — LOB/non-LOB mixed fetch**: RAWTOHEX로 BLOB→VARCHAR2 변환 시, 같은 쿼리의 CLOB(`issuer_dn`) LOB locator와 혼합되어 OCI fetch가 1행 후 중단
- **Fix**: `RAWTOHEX(DBMS_LOB.SUBSTR(crl_binary, DBMS_LOB.GETLENGTH(crl_binary), 1))` — BLOB→RAW→hex VARCHAR2 (LOB locator 완전 우회)
- **Fix**: `TO_CHAR(issuer_dn)` — CLOB→VARCHAR2 변환으로 LOB/non-LOB 혼합 방지
- 4개 CRL repository 메서드 수정: `findAll()`, `findById()`, `findByCountryCode()`, `findAllForExport()`
- Oracle Compatibility Notes 테이블에 BLOB/LOB 관련 2개 항목 추가
- **Verified**: totalRevoked 0→170, countryCount 0→67, byRevocationReason 0→5, bySignatureAlgorithm 0→7
- 1 file changed (0 new, 1 modified: `crl_repository.cpp`)

### v2.20.1 (2026-02-22) - AI Analysis Multi-DBMS Compatibility Fix
- **CRITICAL FIX**: PostgreSQL batch analysis failure — `operator does not exist: character varying = uuid` on `validation_result` JOIN
- PostgreSQL JOIN: `c.fingerprint_sha256 = v.certificate_id` → `c.id = v.certificate_id` (UUID=UUID match, consistent with C++ services)
- Oracle JOIN unchanged — `v.certificate_id` stores fingerprint directly (VARCHAR2=VARCHAR2)
- **Forensic-summary unification**: Removed PostgreSQL JSONB-only branch (`->`, `->>`, `::float` operators), unified to Python-side JSON parsing for both databases
- Fixed PostgreSQL forensic-summary returning empty `severity_distribution` and `top_findings` (dead code in JSONB branch)
- **safe_json_loads()** helper: Handles both PostgreSQL JSONB (returns dict/list) and Oracle CLOB (returns JSON string) transparently
- **safe_isna()** helper: Wraps `pd.isna()` to handle non-scalar values (arrays from LEFT JOIN duplicates) without ValueError
- **Deduplication**: `drop_duplicates(subset=["fingerprint_sha256"])` after data load to handle 1:N JOIN from `validation_result` (UNIQUE on certificate_id + upload_id)
- Replaced all raw `pd.isna()` calls across 5 service modules with `safe_isna()`
- Replaced all raw `json.loads()` calls in API routers with `safe_json_loads()`
- **Verified**: PostgreSQL (luckfox ARM64) — 31,212 certificates, 277s, 17/17 endpoints 200 OK
- **Verified**: Oracle (dev) — 31,212 certificates, 17/17 endpoints 200 OK
- 8 files changed (0 new, 8 modified)

### v2.20.0 (2026-02-22) - AI Certificate Forensic Analysis Engine Enhancement
- **Feature engineering expansion**: 25 → 45 ML features with 20 new forensic features across 5 categories (issuer profile, temporal pattern, DN structure, extension profile, cross-certificate)
- **Type-specific anomaly detection**: Separate Isolation Forest + LOF models per certificate type (CSCA/DSC/DSC_NC/MLSC) with optimized contamination rates
- **MLSC rule-based fallback**: Median absolute deviation scoring for small datasets (< 30 samples)
- **Extension rules engine** (new): ICAO Doc 9303 based extension profile validation — required/recommended/forbidden rules per cert type (CSCA, DSC, MLSC, DSC_NC), structural anomaly scoring (0~1)
- **Issuer profiling** (new): Behavioral analysis per issuer DN — compliance rate, expired rate, algorithm diversity, key size diversity, anomaly deviation scoring (0~1)
- **Forensic risk scoring**: 6 → 10 risk categories (+ issuer_reputation, structural_consistency, temporal_pattern, dn_consistency), total 200pts normalized to 0-100, backward compatible original risk_score preserved
- **Forensic findings**: Per-certificate detailed findings with severity (CRITICAL/HIGH/MEDIUM), category breakdown, contributing factor analysis
- **5 new API endpoints**: `GET /certificate/{fp}/forensic`, `POST /analyze/incremental`, `GET /reports/issuer-profiles`, `GET /reports/forensic-summary`, `GET /reports/extension-anomalies`
- **DB schema**: 6 new columns on `ai_analysis_result` (forensic_risk_score, forensic_risk_level, forensic_findings JSONB, structural_anomaly_score, issuer_anomaly_score, temporal_anomaly_score) with migration support
- **Oracle schema**: Complete `ai_analysis_result` table + indexes for Oracle (`docker/db-oracle/init/11-ai-analysis.sql`)
- **Frontend**: Forensic summary card with level distribution bar + top findings on AI Dashboard
- **Frontend**: Issuer profile card with horizontal bar chart (top 15 by cert count, colored by risk)
- **Frontend**: Extension compliance checklist card with violation table + expandable details
- **Frontend**: Certificate detail dialog 4th "포렌식" tab with 10-category breakdown, score visualization, findings list
- **Frontend**: 3 new components (`ForensicAnalysisPanel`, `IssuerProfileCard`, `ExtensionComplianceChecklist`)
- All 12 existing API endpoints fully backward compatible, 5 new endpoints added (total 17)
- **Stage B verified** (Oracle): 31,212 certificates analyzed in 67s, 291.8MiB memory, 17/17 API endpoints 200 OK
- PostgreSQL + Oracle dual-DBMS support maintained
- 21 files changed (5 new, 16 modified)

### v2.19.0 (2026-02-21) - HTTPS Support (Private CA) + Frontend Proxy + AI Dashboard UX Redesign
- **HTTPS**: Private CA 기반 TLS 지원 — HTTP (:80) + HTTPS (:443) dual-listen, 내부용 HTTP (:8080) 유지
- **Private CA**: `scripts/ssl/init-cert.sh` — RSA 4096 CA (10년) + RSA 2048 서버 인증서 (1년), SAN (domain + localhost + 127.0.0.1)
- **Server cert renewal**: `scripts/ssl/renew-cert.sh` — 기존 CA로 서버 인증서 갱신 + nginx reload
- **nginx**: `api-gateway-ssl.conf` (신규) — TLS 1.2/1.3, Mozilla Intermediate cipher suite, Private CA cert paths (`/etc/ssl/private/`)
- **nginx**: 프론트엔드 프록시 추가 (`location /` → frontend upstream) — `https://pkd.smartcoreinc.com/` 에서 React SPA 접속 가능
- **nginx**: 동적 CORS origin (`map $http_origin $cors_origin`) — HTTPS/HTTP 도메인 + localhost 개발 환경 지원
- **nginx**: `proxy_params` CORS origin 하드코딩 → `$cors_origin` 변수로 전환
- **Docker**: `docker-compose.yaml` — 포트 80/443 추가, `.docker-data/ssl` 볼륨, `NGINX_CONF` 환경변수로 설정 파일 전환
- **SSL 자동 감지**: `start.sh` — `.docker-data/ssl/server.crt` 존재 시 HTTPS 모드 자동 전환, 없으면 기존 HTTP 모드
- **Frontend**: Sidebar Swagger 링크 `http://hostname:8080` → `window.location.origin` (프로토콜 자동 감지)
- **Frontend**: AI Analysis Dashboard UX 전면 리디자인 — DscNcReport 디자인 패턴 적용 (gradient header, 4-col summary cards, risk bar, flag icons, filter card, CSV export, key size pie chart)
- **Frontend**: `csvExport.ts` — `exportAiAnalysisReportToCsv()` 함수 추가 (BOM, 10 columns)
- Certificate files: `.docker-data/ssl/` (ca.key, ca.crt, server.key, server.crt) — `.gitignore`에 포함
- 13 files changed (3 new, 10 modified)

### v2.18.1 (2026-02-21) - PA History Anonymous User IP/User-Agent Display
- Frontend: PA History table — anonymous 사용자에 client IP 주소 표시 (`anonymous (192.168.1.100)` 형식)
- Frontend: PA History detail modal — anonymous 사용자에 IP 주소 + User-Agent(40자 축약, hover 전체 표시) 표시
- Frontend: `PAHistoryItem` TypeScript 인터페이스에 `clientIp`, `userAgent` 필드 추가
- Frontend: PA History 테이블 페이지 크기 10 → 5로 변경
- Backend: PA handler `getPeerAddr()` → `X-Real-IP` > `X-Forwarded-For` > `getPeerAddr()` 우선순위로 실제 클라이언트 IP 추출 (nginx 프록시 환경)
- Backend: PA history `findAll` Oracle CLOB 9개 컬럼 `DBMS_LOB.SUBSTR()` 래핑 — ORA-03127 LOB 세션 문제로 1행만 반환되던 버그 수정
- 4 files changed (0 new, 4 modified)

### v2.18.0 (2026-02-20) - AI Certificate Analysis Engine
- New service: `ai-analysis` — Python FastAPI ML-based certificate anomaly detection and pattern analysis (:8085)
- **Tech stack**: Python 3.12, FastAPI, scikit-learn, pandas, numpy, SQLAlchemy (asyncpg + oracledb), APScheduler
- **Feature engineering**: 25 ML features from certificate + validation_result tables (cryptography, validity, compliance, extensions, country-relative values)
- **Anomaly detection**: Dual-model approach — Isolation Forest (global) + Local Outlier Factor (local per country/type), combined score 0.0~1.0
- **Risk scoring**: Composite 0~100 score from 6 categories (algorithm, key_size, compliance, validity, extensions, anomaly), 4 risk levels (LOW/MEDIUM/HIGH/CRITICAL)
- **Pattern analysis**: Country PKI maturity scoring (5 weighted dimensions), algorithm migration trends by year, key size distribution by algorithm family
- **Explainability**: Per-certificate top-5 deviating features with Korean descriptions and sigma values
- **Background scheduler**: APScheduler daily batch analysis (configurable hour via `ANALYSIS_SCHEDULE_HOUR`, enable/disable via `ANALYSIS_ENABLED`)
- **Multi-DBMS**: PostgreSQL (asyncpg) + Oracle (oracledb, pure Python) via DB_TYPE environment variable, dual engine (async + sync)
- **API endpoints**: 12 endpoints — health, trigger analysis, job status, certificate result, anomaly list (filtered/paginated), statistics, country maturity, algorithm trends, key size distribution, risk distribution, country detail
- **DB schema**: `ai_analysis_result` table (anomaly_score, anomaly_label, risk_score, risk_level, risk_factors JSONB, feature_vector JSONB, anomaly_explanations JSONB, forensic_risk_score, forensic_risk_level, forensic_findings JSONB, structural_anomaly_score, issuer_anomaly_score, temporal_anomaly_score)
- **Docker**: Python 3.12-slim image, non-root user, curl healthcheck, docker-compose integration
- **nginx**: `/api/ai` location block with rate limiting, dynamic upstream resolution
- **Frontend**: `AiAnalysisDashboard.tsx` — summary cards (total/normal/suspicious/anomalous/avg risk), risk level proportional bar, country PKI maturity horizontal bar chart, algorithm migration stacked area chart, filtered anomaly table with pagination
- **Frontend**: `aiAnalysisApi.ts` — API module with 12 typed functions, TypeScript interfaces for all response types
- **Frontend**: Brain icon in sidebar under "보고서" submenu, `/ai/analysis` route in App.tsx
- **Oracle fix**: `cx_Oracle` → `oracledb` (pure Python, Python 3.12 compatible), timezone-aware datetime in risk scorer
- **Frontend fix**: Recharts unused imports cleanup, Tooltip formatter type compatibility
- 32 files changed (26 new, 6 modified)

### v2.17.0 (2026-02-20) - Doc 9303 Compliance Checklist
- New feature: Per-item Doc 9303 compliance checklist (~28 checks) for certificate upload preview and certificate detail dialog
- Backend: `doc9303_checklist.h/.cpp` — OpenSSL-based compliance engine checking version, serial number, signature algorithm OID match, issuer/subject country, unique identifiers, Key Usage (present/critical/correct), Basic Constraints (present/critical/CA/pathLen), EKU rules, AKI/SKI, Certificate Policies criticality, Netscape extensions, unknown critical extensions, key size (minimum/recommended)
- Backend: Certificate type-aware checks — CSCA (keyCertSign+cRLSign, CA=true, pathLen=0), DSC (digitalSignature, CA=false), MLSC (EKU OID 2.23.136.1.1.3)
- Backend: Preview API extension — `CertificatePreviewItem.doc9303Checklist` field added to `POST /api/upload/certificate/preview` response
- Backend: Dedicated API — `GET /api/certificates/doc9303-checklist?fingerprint={sha256}` for certificate detail dialog (lazy loading)
- Backend: Fingerprint-based DB lookup → hex→DER decode → `d2i_X509()` → `runDoc9303Checklist()` pipeline
- Backend: Public endpoint (no JWT required, consistent with certificate search)
- Frontend: `Doc9303ComplianceChecklist.tsx` — category-based collapsible groups with pass/fail/warning/NA icons, summary bar, auto-expand on failures
- Frontend: `CertificateUpload.tsx` — "Doc 9303" tab added to CertificateCard + conformance status badge in card header
- Frontend: `CertificateDetailDialog.tsx` — "Doc 9303" tab with fingerprint-based lazy loading API call (3-tab: General/Details/Doc 9303)
- Frontend: `Doc9303CheckItem`, `Doc9303ChecklistResult` TypeScript interfaces
- Frontend: `certificateApi.getDoc9303Checklist()` API function
- Reference document: `docs/DOC9303_COMPLIANCE_CHECKS.md` — Korean documentation of all Doc 9303 compliance checks
- 15 files changed (3 new, 12 modified)

### v2.16.0 (2026-02-20) - Code Master Table (Centralized Code/Status Management)
- New feature: `code_master` DB table for centralized management of all program codes, statuses, and enum values
- 21 code categories with ~150 seed values: VALIDATION_STATUS, CRL_STATUS, CRL_REVOCATION_REASON, CERTIFICATE_TYPE, UPLOAD_STATUS, PROCESSING_STAGE, OPERATION_TYPE, PA_ERROR_CODE, and 13 more
- Backend: `CodeMasterRepository` (CRUD + pagination, Oracle/PostgreSQL compatible) + `CodeMasterHandler` (6 REST endpoints)
- Backend: ServiceContainer DI integration (Phase 4 repository, Phase 7 handler)
- Backend: Auth middleware — GET endpoints public (read-only reference data), POST/PUT/DELETE require JWT
- Endpoints: `GET /api/code-master` (category filter, pagination), `GET /api/code-master/categories`, `GET /api/code-master/{id}`, `POST/PUT/DELETE` for management
- Frontend: `codeMasterApi.ts` API module + `useCodeMaster(category)` TanStack Query hook (10-min cache, `getLabel(code)` → Korean name)
- DB schema: PostgreSQL (`TEXT`, `JSONB`, `UUID`, `ON CONFLICT DO NOTHING`) + Oracle (`VARCHAR2(4000)`, `NUMBER(1)`, `SYS_GUID()`)
- Oracle: `VARCHAR2(4000)` used for `description`/`metadata` instead of `CLOB` (avoids OCI LOB session state issue with sequential queries)
- Docker: `NLS_LANG=AMERICAN_AMERICA.AL32UTF8` added to all 3 services (pkd-management, pa-service, pkd-relay) for Oracle UTF-8 Korean text support
- nginx: `/api/code-master` location block added to api-gateway.conf
- 16 files changed (9 new, 7 modified)

### v2.15.2 (2026-02-20) - Trust Chain Path Distribution + PA Structured Error Messages
- Trust Chain report: chain path distribution from DB (`chainPathDistribution` in `/api/upload/statistics`)
- Backend: `trust_chain_message` GROUP BY query in `getStatisticsSummary()` (Oracle `DBMS_LOB.SUBSTR` + PostgreSQL)
- Trust Chain 분포 bars: path-level breakdown (DSC→Root, DSC→Link→Root, etc.) with depth-based color coding
- Sample certificates updated to 6 chain path levels (KR, HU, LU, NL — all VALID from DB)
- Trust Chain report: fixed hardcoded chain pattern values + VALID double-counting (`pureValidCount`)
- PA Service: structured `errorCode` field in `CertificateChainValidation` response
- Error codes: `CSCA_NOT_FOUND`, `CSCA_DN_MISMATCH`, `CSCA_SELF_SIGNATURE_FAILED`
- Frontend: `VerificationResultCard` — structured error display with flag icon + country name (Korean) + issuer DN
- Frontend: `VerificationStepsPanel` Step 4 — structured CSCA lookup error with `errorCode`-based Korean messages
- Frontend: PA result card country display: "국기 + 국가코드" → "국기 + 국가이름(국가코드)" (i18n-iso-countries Korean locale)
- Frontend: `CertificateChainValidationDto` — added `errorCode`, `dscIssuer` fields
- Stepper: removed completed step details message box (no longer relevant after upload UX changes)
- 10 files changed (0 new, 10 modified)

### v2.15.1 (2026-02-19) - Trust Chain Demo + CRL Download + PA Conformance
- New page: Trust Chain Demo (`/validation-demo`) - validation statistics dashboard with sample certificate lookup
- Frontend: Statistics cards (total validated, VALID, EXPIRED_VALID, PENDING), Trust Chain distribution bar, status proportional bar
- Frontend: Sample certificate buttons (5 countries, auto-lookup on click), QuickLookupPanel integration
- Backend: `GET /api/certificates/crl/{id}/download` - CRL binary file download (.crl, DER format)
- Backend: hex-to-binary CRL conversion, `application/pkix-crl` content type, public endpoint (no JWT)
- PA Service: `pkdConformanceText` field added to `pa_verification` table (PostgreSQL + Oracle schemas)
- PA Service: `requestedBy`, `dscNonConformant`, `pkdConformanceCode`, `pkdConformanceText` persisted in DB
- PA Service: Country code "XX" fallback for certificates without C= field (Oracle NOT NULL fix)
- PA Service: Client metadata (IP, User-Agent, requestedBy) passed from handler to service layer
- Frontend: PA History non-conformant DSC warning banner with conformance code + text
- Frontend: PA Verify `requestedBy` field populated from logged-in user
- Frontend: CRL Report chart layout restructured (country chart full-width row, sig algo pie + revocation reason 1:1 row)
- Frontend: CRL table Korean column headers, CN-only issuer display, detail link + .crl download buttons
- Frontend: CRL detail dialog .crl download button
- Oracle: `stored_in_ldap` boolean string handling in CRL report/detail
- Oracle: Timestamp `T` separator replaced with space for TO_TIMESTAMP compatibility (4 repositories)
- Oracle: `DBMS_LOB.SUBSTR` for CLOB GROUP BY in validation reason breakdown
- Oracle: `icao_pkd_versions`, `link_certificate`, `link_certificate_issuers` tables added to init schema
- Sidebar: collapsible "보고서" submenu group (NavGroupItem type, ChevronDown/Right toggle, auto-expand on active child)
- Sidebar: "Trust Chain 데모" renamed to "DSC Trust Chain 보고서", "DSC_NC 보고서" renamed to "표준 부적합 DSC 보고서"
- Route: `/validation-demo` changed to `/pkd/trust-chain`
- Cleanup: Preline UI initializer removed (unused), `usePreline` console output suppressed, Login autocomplete attributes
- Docker: Frontend port changed from 3000 to 3080
- 27 files changed (1 new, 26 modified)

### v2.15.0 (2026-02-18) - CRL Report Page
- New page: CRL Report (`/pkd/crl`) — full analysis dashboard for Certificate Revocation Lists
- Backend: `crl_parser.h/.cpp` — standalone CRL binary parser using OpenSSL (`d2i_X509_CRL`, `X509_CRL_get_REVOKED`, revocation reason extraction)
- Backend: `CrlRepository::findAll()`, `countAll()`, `findById()` — paginated, filtered queries (PostgreSQL + Oracle)
- Backend: `CertificateHandler::handleCrlReport()` — aggregation endpoint (byCountry, bySignatureAlgorithm, byRevocationReason, summary)
- Backend: `CertificateHandler::handleCrlDetail()` — CRL detail with full revoked certificate list (serial, date, reason)
- Endpoints: `GET /api/certificates/crl/report`, `GET /api/certificates/crl/{id}` (public, no JWT required)
- Frontend: Summary cards (total CRLs, countries, valid/expired, total revoked certificates)
- Frontend: Status bar (proportional VALID/EXPIRED), country distribution bar chart, revocation reason bar chart, signature algorithm pie chart
- Frontend: Filters (country dropdown, status dropdown), paginated CRL table, row-click detail dialog
- Frontend: Detail dialog with CRL metadata card + revoked certificates table (serial number, revocation date, reason)
- Frontend: Korean translations for 11 RFC 5280 revocation reason codes
- Frontend: CSV export (`exportCrlReportToCsv`, 11 columns, BOM for Excel UTF-8)
- All CRL data parsed at runtime from binary (69 CRLs, consistent across PostgreSQL/Oracle)
- 13 files changed (5 new, 8 modified)

### v2.14.1 (2026-02-18) - Trust Chain Success Rate Fix + Upload History Duplicate Flow
- **Trust chain success rate fix**: `cscaNotFoundCount` included in denominator (was excluded, causing 100% rate when only CSCA-not-found failures)
- Formula changed from `valid / (valid + invalid)` to `valid / (valid + invalid + cscaNotFound)` in `ValidationSummaryPanel`
- **Upload history detail dialog**: duplicate flow (3-card funnel) now displayed via `duplicateCount`, `totalCertificates`, `processedCount` props
- `uploadIssues.totalDuplicates` from `certificate_duplicates` table passed to shared `ValidationSummaryPanel`
- Playwright MCP files removed (`.playwright-mcp/` directory, `.gitignore` entry)
- 3 files changed

### v2.14.0 (2026-02-18) - Per-Certificate ICAO Compliance DB Storage + SSE Validation Enhancements
- **Per-certificate ICAO 9303 compliance DB persistence**: 8 columns added to `validation_result` table (`icao_compliant`, `icao_compliance_level`, `icao_violations`, `icao_key_usage_compliant`, `icao_algorithm_compliant`, `icao_key_size_compliant`, `icao_validity_period_compliant`, `icao_extensions_compliant`)
- `checkIcaoCompliance()` results now written to `ValidationResult` domain model → `ValidationRepository::save()` → DB (previously discarded after SSE streaming)
- ICAO compliance fields returned in all validation query APIs: `findByFingerprint()`, `findBySubjectDn()`, `findByUploadId()`, `getStatisticsByUploadId()`
- **Validation statistics expansion**: `uploaded_file` table gains 4 new columns (`valid_period_count`, `icao_compliant_count`, `icao_non_compliant_count`, `icao_warning_count`)
- `ValidationStatistics` domain model: added `validPeriodCount`, `expiredValidCount`, `icaoCompliantCount`, `icaoNonCompliantCount`, `icaoWarningCount`
- Statistics source switched from basic `stats` to `enhancedStats` in `processing_strategy.cpp` (AUTO/MANUAL modes) for accurate aggregate counts
- **Per-certificate validation log streaming**: `ValidationLogEntry` struct + `addValidationLog()` for real-time EventLog display
- SSE statistics now include `recentValidationLogs` (bounded to last 200 entries) + `totalValidationLogCount`
- Master List processing: per-certificate validation logs, ICAO compliance checks, signature algorithm/key size distribution, expiration status tracking
- LDIF processing: trust chain counters, CSCA not found tracking, expiration status counters, per-category ICAO violation counts (`complianceViolations` map)
- **Frontend ValidationSummaryPanel componentization**: `RealTimeStatisticsPanel` (302→44 lines) refactored to thin wrapper → shared `ValidationSummaryPanel`
- `ValidationSummaryPanel` reused by both `FileUpload.tsx` (real-time SSE) and `UploadHistory.tsx` (post-upload detail dialog)
- UploadHistory detail dialog: wider layout (max-w-5xl→max-w-6xl), scrollable body, validation summary panel integration
- Frontend types: `ValidationLogEntry`, `ValidationSummaryData` interfaces, `validPeriodCount`/`icaoCompliantCount`/`icaoNonCompliantCount`/`icaoWarningCount` fields
- **ASN.1 parser SEGFAULT fix**: replaced `std::regex` with manual string parsing in `parseAsn1Output()` (avoids stack overflow on large ASN.1 structures)
- **Master List processing**: CSCA self-signature verification via `icao::validation::verifyCertificateSignature()`, `validationStatus`/`validationMessage` tracking
- Upload handler: ML certificate validation counts (valid/invalid/expired/ICAO compliant) tracked and saved to DB
- `EXPIRED_VALID` status added to certificate validation constraint (PostgreSQL + Oracle)
- `certificate_id` column constraint relaxed to `VARCHAR2(128)` for fingerprint storage (Oracle)
- Both PostgreSQL and Oracle schemas updated (init scripts + ALTER TABLE compatible)
- 20 files changed, +1,032 insertions, -564 deletions

### v2.13.0 (2026-02-17) - main.cpp Minimization: 9,752 → 1,261 lines (-87.1%)
- **All 4 services** main.cpp reduced to minimal orchestration layers (config → DI → routes → run)
  - PKD Management: 4,722 → 430 lines (-90.9%)
  - PA Service: 2,800 → 281 lines (-90.0%)
  - PKD Relay: 1,644 → 457 lines (-72.2%)
  - Monitoring: 586 → 93 lines (-84.1%)
- **PA Service dead code removal** (~1,085 lines): removed legacy functions wrapped in `#pragma GCC diagnostic ignored "-Wunused-function"` — SOD parsing, LDAP search, validation, DB save functions already migrated to service layer
- **PKD Management validation deduplication** (~449 lines): local `validateCscaCertificate()`, `validateDscCertificate()`, `buildTrustChain()`, `validateTrustChain()` replaced with `icao::validation` library calls
- **Handler extraction** (4 services, ~2,350 lines moved):
  - PKD Management: `MiscHandler` (health, audit, validation, PA proxy, info, ICAO endpoints)
  - PA Service: `PaHandler` (9 endpoints), `HealthHandler` (3 endpoints), `InfoHandler` (4 endpoints)
  - PKD Relay: `SyncHandler` (10 endpoints), `ReconciliationHandler` (4 endpoints), `HealthHandler` (1 endpoint)
  - Monitoring: `MonitoringHandler` (3 endpoints + SystemMetricsCollector + ServiceHealthChecker)
- **PKD Management DN Migration endpoint removed** (~500 lines): one-time PostgreSQL-only utility (PGconn, Multi-DBMS incompatible)
- **LdapStorageService** extracted (~886 lines): LDAP write operations (saveCertificateToLdap, saveCrlToLdap, ensureCountryOuExists, DN builders)
- **LDIF/ML processing extracted** (~1,500 lines): `parseCertificateEntry()` → `ldif_processor.cpp`, `processLdifFileAsync()`/`processMasterListFileAsync()` → handlers, utility functions → `main_utils.cpp`
- **ServiceContainer** extended to PA Service and PKD Relay (pImpl pattern, non-owning pointer accessors):
  - PA Service: `AppConfig` struct, `ServiceContainer` owns DB pool + LDAP conn + 4 repos + 2 parsers + 3 services
  - PKD Relay: `ServiceContainer` owns DB pool + LDAP pool + 5 repos + 3 services; `SyncScheduler` extracted with callback-based DI
  - PKD Management: `LdapStorageService` + `ProgressManager` added to existing container
- **Infrastructure modules** (PKD Relay): `relay_operations.h/.cpp` (getDbStats, getLdapStats, performSyncCheck, saveSyncStatus)
- 20 files changed, +2,223 insertions, -9,135 deletions (net -6,912 lines)
- All 4 services Docker build verified
- Zero breaking changes to public API

### v2.12.0 (2026-02-17) - Architecture Rewrite: ServiceContainer, Handler Extraction, Frontend Decomposition
- **Backend main.cpp**: 8,095 → 4,722 lines (-41.7%) through handler extraction + ServiceContainer
- **ServiceContainer** (`infrastructure/service_container.h/.cpp`): Centralized DI replacing 17 global shared_ptr variables
  - Pimpl pattern, 7-phase initialization order, `AppConfig` struct extracted from main.cpp
  - Non-owning pointer accessors: 9 repositories, 6 services, 5 handlers
  - All extern globals across 5 files (processing_strategy, ldif_processor, certificate_utils, upload_service, auth_middleware) migrated to `g_services->` pattern
- **Handler extraction**: 3 handler classes extracted from main.cpp (~3,400 lines moved)
  - `UploadHandler` (10 endpoints): LDIF/ML/Certificate upload, parse, validate, delete
  - `UploadStatsHandler` (11 endpoints): statistics, history, detail, countries, progress stream
  - `CertificateHandler` (12 endpoints): search, export, validation, pa-lookup, dsc-nc report, link-certs
- **Query Helpers** (`shared/lib/database/query_helpers.h/.cpp`): `common::db::` namespace utilities
  - `boolLiteral()`, `paginationClause()`, `scalarToInt()`, `hexPrefix()`, `currentTimestamp()`, `ilikeCond()`, `limitClause()`
  - 15 repositories across 3 services migrated (204 inline DB branches eliminated)
- **Frontend page decomposition**: PAVerify 1,927→917 (-52%), CertificateSearch 1,733→771 (-55%)
  - PA components: `QuickLookupPanel`, `VerificationStepsPanel`, `VerificationResultCard`
  - Certificate components: `CertificateDetailDialog`, `CertificateSearchFilters`
- **Chart library unified**: echarts-for-react → recharts (~500KB bundle reduction)
  - PADashboard donut chart + area chart reimplemented with recharts
- **ErrorBoundary** component added (App.tsx global wrapper, React class component)
- **API module separation**: `paApi.ts`, `monitoringApi.ts` extracted from monolithic `api.ts`
- **Type cast safety**: 6 `as unknown as` double-casts eliminated with proper generic types
- **nginx CSP headers**: Content-Security-Policy added to both api-gateway configs
- All changes verified: Docker build (pkd-management) + frontend build (`npm run build`) pass
- Zero breaking changes to public API

### v2.11.0 (2026-02-16) - Validation Library Extraction (icao::validation)
- New shared library: `icao::validation` — idempotent ICAO 9303 certificate validation functions extracted from both services
- Library modules: `cert_ops` (pure X509 ops), `trust_chain_builder`, `crl_checker`, `extension_validator`, `algorithm_compliance`
- Provider/Adapter pattern: `ICscaProvider` / `ICrlProvider` interfaces abstract DB vs LDAP backends
- PKD Management adapters: `DbCscaProvider` (CertificateRepository), `DbCrlProvider` (CrlRepository + hex→DER decode)
- PA Service adapters: `LdapCscaProvider` (LdapCertificateRepository), `LdapCrlProvider` (LdapCrlRepository)
- PKD Management `validation_service.cpp`: 1,078 → 408 lines (~62% reduction), 10+ private methods removed
- PA Service `certificate_validation_service.cpp`: 617 → 364 lines (~41% reduction), 6+ utility methods removed
- Trust chain: multi-CSCA key rollover, deep chain (DSC→Link→Root), circular reference detection, root self-signature verification
- CRL checker: RFC 5280 CRLReason extraction (11 codes), expiration check, provider-based lookup
- Extension validator: unknown critical extensions, DSC/CSCA key usage bits, warnings list
- Algorithm compliance: ICAO approved algorithms (SHA-256+, RSA-2048+, ECDSA, RSA-PSS), key size extraction
- DN utilities consolidated: `normalizeDnForComparison`, `extractDnAttribute` (slash + RFC 2253 formats)
- Public API of both services fully preserved (zero breaking changes)
- Docker build verified: both pkd-management and pa-service compile and link successfully
- Future ICAO standard changes require only `shared/lib/icao-validation/` updates

### v2.10.5 (2026-02-15) - Security Hardening (Full Audit + OWASP)
- **CRITICAL**: Upload endpoint authentication restored (removed TEMPORARY public access for LDIF/ML/Certificate)
- **CRITICAL**: Command Injection eliminated — `system()`/`popen()` replaced with native C APIs across 4 services
  - PKD Management: `system(ldapsearch)` → LDAP C API (`ldap_initialize` + `ldap_sasl_bind_s`)
  - PA Service: `popen(ldapsearch)` → LDAP C API
  - EmailSender: `system(mail)` × 3 → spdlog log-only
  - ASN.1 Parser: `popen(openssl asn1parse)` → OpenSSL `ASN1_parse_dump()` C API
- **HIGH**: SQL Injection prevention — ORDER BY whitelist validation, parameterized LIKE queries with `escapeSqlWildcards()`
- **HIGH**: SOD Parser buffer overread protection — `end` pointer boundary checks in all ASN.1 manual parsing
- **HIGH**: Null pointer checks added to 24 OpenSSL allocation sites across 8 files (BIO, EVP_MD_CTX, X509_STORE, BIGNUM, BN_bn2hex)
- **MEDIUM**: nginx security headers added (X-Content-Type-Options, X-Frame-Options, X-XSS-Protection, Referrer-Policy)
- **MEDIUM**: Auth middleware regex pre-compilation (`std::call_once` + `std::regex::optimize`)
- **MEDIUM**: JWT_SECRET minimum length validation (32 bytes) at service startup
- **MEDIUM**: LDAP DN escape utility (RFC 4514 special characters)
- **MEDIUM**: Base64 input validation (`isValidBase64()` pre-check before decode)
- Frontend OWASP: Default credentials hidden in production (`import.meta.env.DEV` guard)
- Frontend OWASP: All `console.error` in 6 API service interceptors → DEV-only
- Frontend OWASP: UserManagement.tsx / AuditLog.tsx refactored from raw `fetch()` to `createAuthenticatedClient` (centralized JWT injection + 401 handling)
- Frontend OWASP: Relay API JWT token interceptor added
- Memory leak audit: 0 leaks found, SodData RAII pattern verified
- 23 files changed, +410/-277 lines
- Action plan: `docs/SECURITY_FIX_ACTION_PLAN.md` updated with full completion status

### v2.10.4 (2026-02-15) - DSC_NC Non-Conformant Certificate Report Page
- New page: DSC_NC Report (`/pkd/dsc-nc`) — full analysis dashboard for non-conformant DSC certificates
- Backend: `GET /api/certificates/dsc-nc/report` handler with batch LDAP fetching (200/batch), server-side aggregation
- Aggregation: conformanceCodes, byCountry (valid/expired breakdown), byYear, bySignatureAlgorithm, byPublicKeyAlgorithm
- Server-side filtering (country, conformanceCode prefix match) + pagination
- Summary cards: country count, total DSC_NC, conformance code count, expiration rate
- Charts (Recharts): conformance code horizontal bar, country bar (flag icons in Y-axis ticks), year bar, signature/public key algorithm pie
- Custom chart tooltips: country tooltip with flag+name, conformance code tooltip with `?` icon + description
- Validity status bar: VALID/EXPIRED/NOT_YET_VALID/UNKNOWN proportional segments
- Table columns: country (flag icon), issued year, signature algorithm, public key (size badge, red highlight <2048bit), validity period, status, NC code (? tooltip)
- CSV export with BOM for Excel UTF-8 compatibility (14 columns)
- Filters: country dropdown, conformance code dropdown, reset button
- Public endpoint (no JWT required)
- Sidebar menu: ShieldX icon under PKD Management section
- Frontend: `certificateApi.getDscNcReport()`, `exportDscNcReportToCsv()`, route + sidebar registration

### v2.10.3 (2026-02-14) - DSC Non-Conformant (nc-data) Support + DSC_NC Documentation
- PA Service: DSC conformance check via LDAP `dc=nc-data` during PA Verify (`checkDscConformance()`)
- PA Service: `findDscBySubjectDn()` nc-data fallback search (`dc=data` → `dc=nc-data`)
- PA Service: `buildNcDataSearchBaseDn()`, `DscConformanceInfo` struct added
- PA Verify response: `dscNonConformant`, `pkdConformanceCode`, `pkdConformanceText` fields in `certificateChainValidation`
- PKD Management: PA Lookup conformance data enrichment for DSC_NC (`enrichWithConformanceData()`)
- PKD Management: LDAP pool dependency added to ValidationRepository for nc-data auxiliary lookup
- Frontend: PA Verify result NC warning banner and conformance details display
- Frontend: `CertificateChainValidationDto` conformance fields added
- Documentation: `docs/DSC_NC_HANDLING.md` - ICAO PKD DSC_NC handling guide (architecture, design decisions, operational data)
- PA API Guide updated to v2.1.4 with conformance field documentation
- OpenAPI specs updated (PA Service + PKD Management)

### v2.10.2 (2026-02-14) - Lightweight PA Lookup API + PA Trust Chain Multi-CSCA Fix
- Lightweight PA lookup: `POST /api/certificates/pa-lookup` for DSC subject DN or fingerprint-based trust chain query
- Pre-computed validation result retrieval from DB (5~20ms response, no SOD/DG file upload required)
- Case-insensitive subject DN matching (`LOWER()`) with PostgreSQL + Oracle support
- `findBySubjectDn()` in ValidationRepository, `getValidationBySubjectDn()` in ValidationService
- Public endpoint (no JWT required)
- PA Service: Multi-CSCA candidate selection with signature verification (ICAO 9303 key rollover support)
- PA Service: `findAllCscasByCountry()` replaces single-result `findCscaByIssuerDn()` for correct CSCA selection
- Frontend: PA Verify page "간편 검증" mode toggle (Subject DN / Fingerprint quick lookup)
- Frontend: `dscAutoRegistration.duplicate` → `newlyRegistered` type alignment with backend response
- OpenAPI spec updated (v2.10.2), PA Developer Guide updated (v2.1.3)

### v2.10.1 (2026-02-14) - Validation Reason Tracking + Upload UX Improvements + PA CRL Date Fix
- Validation reason tracking: `validationReasons` map in SSE statistics (reason string → count per status)
- EXPIRED_VALID status tracking fix: was missing from enhancedStats if-else chain
- Duplicate certificate tracking: `duplicateCount` in SSE ValidationStatistics
- Event Log enhancements: validation failure/pending reasons displayed in real-time during processing
- Validation result summary panel: grouped by status (VALID/EXPIRED_VALID/INVALID/PENDING) with Korean-translated sub-reasons after FINALIZED
- Reason translation: "Trust chain signature verification failed" → "서명 검증 실패", "CSCA not found" → "CSCA 미등록", etc.
- Number formatting: frontend-only locale-aware formatting for SSE progress messages (`formatMessageNumbers`)
- Horizontal Stepper layout: indicators + connector row with detail panel below (compact space usage)
- File drop zone height reduction: smaller padding (p-8→p-5), icon (w-10→w-8), container (p-4→p-3)
- PA Service: `crlThisUpdate`/`crlNextUpdate` fields populated in service layer domain model (was null due to missing fields in `CertificateChainValidation`)
- PA Service: CRL date extraction via `X509_CRL_get0_lastUpdate()`/`X509_CRL_get0_nextUpdate()` in `checkCrlStatus()`

### v2.10.0 (2026-02-14) - ICAO Auto Scheduler + Upload Processing UX + Event Log
- ICAO PKD daily auto version check scheduler (configurable hour via `ICAO_CHECK_SCHEDULE_HOUR`, enable/disable via `ICAO_SCHEDULER_ENABLED`)
- Drogon event loop timer-based scheduling (initial delay + 24h recurring)
- `last_checked_at` timestamp tracking in IcaoSyncService (thread-safe mutex)
- ICAO status API: `any_needs_update` and `last_checked_at` fields added
- Dashboard: ICAO PKD update notification banner (amber gradient, dismissible, collection version diff display)
- IcaoStatus page: last checked timestamp display in header
- Upload processing: `PROCESSING` intermediate DB status (between PENDING and COMPLETED)
- LDIF processing: periodic DB progress updates every 500 entries (`updateProgress` + `updateStatistics`)
- Master List processing: `PROCESSING` status set before certificate extraction loop
- Upload history API: `totalEntries`/`processedEntries` fields added to list response
- Frontend `UploadStatus` type: `PROCESSING` state added
- UploadHistory: auto-refresh (5s polling) when uploads are in progress, inline progress bar
- UploadDetail: auto-refresh (3s polling), live progress card with certificate counts
- EventLog component: scrollable SSE event log with auto-scroll, timestamps, status dots, clear button
- FileUpload: SSE events accumulated in EventLog panel (persistent, reviewable)
- Number formatting: all certificate/entry counts use locale-aware comma separation (toLocaleString)
- ProcessingErrorsPanel component: upload error summary with parse/DB/LDAP breakdown
- ProgressManager: centralized progress tracking for ML processing with periodic DB+SSE updates
- Oracle compatibility: all new queries support both PostgreSQL and Oracle
- Docker compose: `ICAO_CHECK_SCHEDULE_HOUR` and `ICAO_SCHEDULER_ENABLED` env vars

### v2.9.2 (2026-02-13) - Full Certificate Export + PA CRL Expiration Check
- Full certificate export: all LDAP-stored certificates, CRLs, and Master Lists as DIT-structured ZIP
- ZIP folder structure mirrors LDAP DIT: `data/{country}/{csca|dsc|mlsc|crl|ml}/`, `nc-data/{country}/dsc/`
- DB-based bulk export (stored_in_ldap=TRUE) for ~31K certificates + 69 CRLs + 27 MLs
- PEM and DER format support; Master Lists always exported as original CMS binary (.cms)
- Double-encoded BYTEA handling for PostgreSQL hex data
- New endpoint: `GET /api/certificates/export/all?format=pem|der`
- Frontend: "전체 내보내기 PEM/DER" buttons on Certificate Search page
- PA Service: CRL expiration check before revocation check (CRL_EXPIRED status)
- PA Service: `crlThisUpdate`, `crlNextUpdate` fields added to certificateChainValidation response
- Oracle compatibility: all new queries support both PostgreSQL and Oracle

### v2.9.1 (2026-02-13) - ARM64 CI/CD Pipeline + Luckfox Full Deployment
- GitHub Actions ARM64 CI/CD: vcpkg-base → GHCR → service builds → OCI artifacts
- Change detection (dorny/paths-filter) for selective builds per service
- Monitoring service added to ARM64 pipeline and luckfox deployment
- Monitoring service Dockerfile: `ARG BASE_IMAGE` for GHCR vcpkg-base support
- Luckfox management scripts updated (start/stop/restart/health/logs/clean/backup/restore)
- Script project root auto-detection (works from repo `scripts/luckfox/` and luckfox root)
- API gateway luckfox config: added /api/auth, /api/audit, /api/icao, /api/monitoring routes
- Fixed PA health endpoint in monitoring config (`/api/health` not `/api/pa/health`)
- Deploy script (`from-github-artifacts.sh`): OCI→Docker conversion via skopeo, monitoring-service support
- All 8 containers verified on luckfox: API Gateway, Frontend, PKD Management, PA Service, PKD Relay, Monitoring, Swagger UI, PostgreSQL

### v2.9.0 (2026-02-12) - DSC Auto-Registration + Certificate Source Filter & Dashboard
- DSC auto-registration from PA verification: extracts DSC from SOD and stores in certificate table (source_type='PA_EXTRACTED')
- Fingerprint-based duplicate check prevents re-registration of already-known DSCs
- Full X.509 metadata extraction on auto-registration (signature_algorithm, public_key_algorithm, public_key_size, validation_status, is_self_signed)
- SOD binary storage + SHA-256 hash computation for PA verification records
- Certificate search: source filter (LDIF_PARSED, ML_PARSED, FILE_UPLOAD, PA_EXTRACTED, DL_PARSED)
- DB-based certificate search when source filter applied (LDAP search preserved when no source filter)
- DN component parsing for DB search results (supports both `/C=KR/O=Gov/CN=Name` and `CN=Name,O=Gov,C=KR` formats)
- Validity statistics (valid/expired/notYetValid/unknown) in DB search response
- Dashboard: "인증서 출처별 현황" card with horizontal bar chart (source_type statistics)
- Upload statistics API: bySource field (GROUP BY source_type)
- New service: DscAutoRegistrationService (pa-service, Oracle + PostgreSQL)
- Oracle compatibility: all new queries support both PostgreSQL and Oracle

### v2.8.0 (2026-02-12) - PA Verification UX + DG2 JPEG2000 Face Image Support
- JPEG2000 → JPEG server-side conversion for DG2 face images (OpenJPEG + libjpeg)
- Browsers cannot render JPEG2000 natively; pa-service now auto-converts to JPEG
- Optional build-time dependency: `HAS_OPENJPEG` macro (enabled when libopenjp2-dev + libjpeg-dev present)
- PA verification step cards show failure/success reasons (Trust Chain, SOD Signature, DG Hash, CRL)
- Result summary card moved to top with failure reason breakdown for INVALID status
- Fixed "Invalid Date" in result card: added `verificationTimestamp`, `processingDurationMs`, `issuingCountry`, `documentNumber` to verify response
- PA History modal redesigned: compact layout (max-w-4xl), sticky header/footer, inline metadata
- Oracle compatibility: LIMIT→FETCH FIRST, NOW()→SYSTIMESTAMP in pa_verification_repository
- Data group repository: `\\x` prefix for BLOB binding (Oracle compatibility)
- Header bar height reduced (py-3→py-1.5, smaller icons/avatar)

### v2.7.1 (2026-02-12) - Monitoring Service DB-Free + Oracle Compatibility Fixes
- Monitoring service: removed PostgreSQL (libpq) dependency entirely, now DB-independent
- Monitoring service works in both PostgreSQL and Oracle modes (removed `profiles: [postgres]`)
- Fixed curl write callback crash in Drogon handler threads (added `discardWriteCallback`)
- Fixed country statistics Oracle compatibility: `issuing_country` → `country_code`, `LIMIT` → `FETCH FIRST`, CLOB comparison via `DBMS_LOB.COMPARE`
- Fixed CRL repository Oracle schema: added `fingerprint_sha256`, `crl_binary` columns, C++ UUID generation
- Fixed validation repository Oracle schema: `certificate_id` VARCHAR2(128) for fingerprints, expanded column mapping
- Oracle init schema: removed invalid FK constraint on `validation_result.certificate_id` (stores fingerprint, not UUID)
- Sticky table header in country statistics dialog (frontend)

### v2.7.0 (2026-02-12) - Individual Certificate Upload + Preview-before-Save
- Separated individual certificate upload to dedicated page (`/upload/certificate`)
- Preview-before-save workflow: parse → preview → confirm → save to DB+LDAP
- Backend `POST /api/upload/certificate/preview` endpoint (parse only, no save)
- Supports PEM, DER, CER, P7B, DL (Deviation List), CRL file formats
- Certificate detail tree view (TreeViewer) in preview with General/Details tabs
- DL file support: deviation data parsing + preview (DlParser replacing DvlParser)
- DL CMS metadata extraction: hashAlgorithm, digestAlgorithm, signatureAlgorithm, eContentType, signerDN
- DL ASN.1 structure tree view in preview tab (CMS ContentInfo hierarchy)
- CRL metadata preview (issuer, validity, revoked count)
- Duplicate file detection via SHA-256 hash before save
- Tab-based preview UI: Certificates | DL Structure | CRL (context-dependent tabs)
- Compact UX redesign: inline file selection, status badges, InfoRow component
- Removed unused search input from header navigation bar
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

### Guides
- [docs/DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md) - Development guide (credentials, workflow, troubleshooting)
- [docs/PA_API_GUIDE.md](docs/PA_API_GUIDE.md) - PA Service API guide (v2.1.1)
- [docs/CERTIFICATE_SEARCH_QUICKSTART.md](docs/CERTIFICATE_SEARCH_QUICKSTART.md) - Certificate search guide
- [docs/LDAP_QUERY_GUIDE.md](docs/LDAP_QUERY_GUIDE.md) - LDAP operations guide
- [docs/API_CLIENT_ADMIN_GUIDE.md](docs/API_CLIENT_ADMIN_GUIDE.md) - API Client admin guide (management API, permissions, rate limiting)
- [docs/API_CLIENT_USER_GUIDE.md](docs/API_CLIENT_USER_GUIDE.md) - API Client user guide (external integration, Python/Java/C#/curl examples)
- [docs/FRONTEND_DESIGN_SYSTEM.md](docs/FRONTEND_DESIGN_SYSTEM.md) - Frontend UI/UX design system (color theme, components, tokens)

### Architecture
- [docs/SOFTWARE_ARCHITECTURE.md](docs/SOFTWARE_ARCHITECTURE.md) - System architecture
- [docs/ARCHITECTURE_DESIGN_PRINCIPLES.md](docs/ARCHITECTURE_DESIGN_PRINCIPLES.md) - Design principles
- [docs/MASTER_LIST_PROCESSING_GUIDE.md](docs/MASTER_LIST_PROCESSING_GUIDE.md) - Master List format & processing

### Standards & Compliance
- [docs/DOC9303_COMPLIANCE_CHECKS.md](docs/DOC9303_COMPLIANCE_CHECKS.md) - Doc 9303 compliance checklist (~28 checks)
- [docs/BSI_TR03110_ALGORITHM_SUPPORT.md](docs/BSI_TR03110_ALGORITHM_SUPPORT.md) - BSI TR-03110 algorithm support guide (Brainpool, SHA-224, SHA-1 classification)
- [docs/PII_ENCRYPTION_COMPLIANCE.md](docs/PII_ENCRYPTION_COMPLIANCE.md) - PII encryption compliance (개인정보보호법, AES-256-GCM)

### Deployment & Build
- [docs/DEPLOYMENT_PROCESS.md](docs/DEPLOYMENT_PROCESS.md) - CI/CD pipeline
- [docs/LUCKFOX_DEPLOYMENT.md](docs/LUCKFOX_DEPLOYMENT.md) - ARM64 deployment guide
- [docs/BUILD_SOP.md](docs/BUILD_SOP.md) - Build verification procedures
- [docs/DOCKER_BUILD_CACHE.md](docs/DOCKER_BUILD_CACHE.md) - Build cache troubleshooting

### API Specifications
- [docs/openapi/pkd-management.yaml](docs/openapi/pkd-management.yaml) - PKD Management OpenAPI (v2.36.0, 74 paths)
- [docs/openapi/pa-service.yaml](docs/openapi/pa-service.yaml) - PA Service OpenAPI (v2.1.7)
- [docs/openapi/pkd-relay-service.yaml](docs/openapi/pkd-relay-service.yaml) - PKD Relay OpenAPI (v2.36.0)
- [docs/openapi/monitoring-service.yaml](docs/openapi/monitoring-service.yaml) - Monitoring Service OpenAPI (v1.2.0)

### Archive
- [docs/archive/](docs/archive/) - 100+ historical phase/completion/plan documents

---

## Contact

For detailed information, see [docs/DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md)
