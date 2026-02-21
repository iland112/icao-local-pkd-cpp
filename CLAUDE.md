# ICAO Local PKD - Development Guide

**Current Version**: v2.18.0
**Last Updated**: 2026-02-21
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
| API Gateway | http://localhost:8080/api |
| Frontend | http://localhost:3000 |

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
Frontend (React 19) --> API Gateway (nginx :8080) --> Backend Services --> DB/LDAP
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
| `icao::validation` | ICAO 9303 certificate validation (trust chain, CRL, extensions, algorithm compliance) |

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

## Current Features (v2.18.0)

### Core Functionality

- **Code Master**: DB-based centralized code/status/enum management (21 categories, ~150 codes, CRUD API + frontend hook)
- LDIF/Master List upload (AUTO/MANUAL modes)
- Individual certificate upload with preview-before-save workflow (PEM, DER, P7B, DL, CRL)
- Master List file processing (537 certificates: 1 MLSC + 536 CSCA/LC)
- Country-based LDAP storage (95+ countries)
- Certificate validation (Trust Chain, CRL, Link Certificates)
- LDAP integration (MMR cluster, Software Load Balancing)
- Passive Authentication verification (ICAO 9303 Part 10 & 11)
- Lightweight PA lookup: DSC subject DN or fingerprint → pre-computed trust chain result (no SOD/DG upload)
- DSC auto-registration from PA verification (source_type='PA_EXTRACTED')
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
- **AI Certificate Analysis Engine** (ML-based anomaly detection, risk scoring, pattern analysis)
  - Isolation Forest + Local Outlier Factor dual-model anomaly detection (25 features)
  - Composite risk scoring (0~100: algorithm, key size, compliance, validity, extensions, anomaly)
  - Country-level PKI maturity scoring (algorithm, key size, compliance, extensions, freshness)
  - Algorithm migration trend analysis (by issuance year)
  - Key size distribution analysis (by algorithm family)
  - Daily scheduled batch analysis (APScheduler, configurable hour)
  - Per-certificate anomaly explanation (top-5 deviating features with Korean descriptions)

### Security

- 100% parameterized SQL queries (all services)
- JWT authentication (HS256) + RBAC (admin/user)
- Credential externalization (.env)
- File upload validation (MIME type, path sanitization)
- Dual audit logging: `auth_audit_log` (authentication events) + `operation_audit_log` (operations)
- IP tracking and User-Agent logging
- Content-Security-Policy (CSP) header via nginx (XSS defense)

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

### AI Analysis Service (via :8080/api/ai)

- `GET /api/ai/health` - Health check
- `POST /api/ai/analyze` - Trigger full analysis (background)
- `GET /api/ai/analyze/status` - Analysis job status
- `GET /api/ai/certificate/{fingerprint}` - Certificate analysis result
- `GET /api/ai/anomalies` - Anomaly list (filters: country, type, label, risk_level, pagination)
- `GET /api/ai/statistics` - Overall analysis statistics
- `GET /api/ai/reports/country-maturity` - Country PKI maturity ranking
- `GET /api/ai/reports/algorithm-trends` - Algorithm migration trends by year
- `GET /api/ai/reports/key-size-distribution` - Key size distribution
- `GET /api/ai/reports/risk-distribution` - Risk level distribution
- `GET /api/ai/reports/country/{code}` - Country detail analysis

### Public vs Protected Endpoints

Public endpoints (no JWT required) are defined in [auth_middleware.cpp](services/pkd-management/src/middleware/auth_middleware.cpp) lines 10-93. Key categories:
- **Public**: Health checks, Dashboard statistics, Certificate search, Doc 9303 checklist, DSC_NC report, CRL report/download, PA lookup, ICAO monitoring, Sync status, PA verification, Certificate preview, Code Master (GET), AI Analysis (all endpoints), Static files
- **Protected**: File uploads (LDIF/ML/Certificate save), User management, Audit logs, Upload deletion, Code Master (POST/PUT/DELETE)

---

## Frontend

### Pages (24 total)

| Page | Route | Purpose |
|------|-------|---------|
| Dashboard | `/` | Homepage with certificate statistics |
| FileUpload | `/upload` | LDIF/Master List upload |
| CertificateUpload | `/upload/certificate` | Individual certificate upload (preview-before-save) |
| CertificateSearch | `/pkd/certificates` | Certificate search & detail |
| DscNcReport | `/pkd/dsc-nc` | DSC_NC non-conformant certificate report |
| CrlReport | `/pkd/crl` | CRL report & revoked certificate analysis |
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
| ValidationDemo | `/pkd/trust-chain` | DSC Trust Chain report |
| AiAnalysisDashboard | `/ai/analysis` | AI certificate anomaly detection & pattern analysis |

### Key Components

| Component | Purpose |
|-----------|---------|
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
| CertificateDetailDialog | Certificate detail modal (3-tab: General/Details/Doc 9303) |
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
- **DB schema**: `ai_analysis_result` table (anomaly_score, anomaly_label, risk_score, risk_level, risk_factors JSONB, feature_vector JSONB, anomaly_explanations JSONB)
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

### Architecture
- [docs/SOFTWARE_ARCHITECTURE.md](docs/SOFTWARE_ARCHITECTURE.md) - System architecture
- [docs/ARCHITECTURE_DESIGN_PRINCIPLES.md](docs/ARCHITECTURE_DESIGN_PRINCIPLES.md) - Design principles
- [docs/MASTER_LIST_PROCESSING_GUIDE.md](docs/MASTER_LIST_PROCESSING_GUIDE.md) - Master List format & processing

### Deployment & Build
- [docs/DEPLOYMENT_PROCESS.md](docs/DEPLOYMENT_PROCESS.md) - CI/CD pipeline
- [docs/LUCKFOX_DEPLOYMENT.md](docs/LUCKFOX_DEPLOYMENT.md) - ARM64 deployment guide
- [docs/BUILD_SOP.md](docs/BUILD_SOP.md) - Build verification procedures
- [docs/DOCKER_BUILD_CACHE.md](docs/DOCKER_BUILD_CACHE.md) - Build cache troubleshooting

### API Specifications
- [docs/openapi/pkd-management.yaml](docs/openapi/pkd-management.yaml) - PKD Management OpenAPI
- [docs/openapi/pa-service.yaml](docs/openapi/pa-service.yaml) - PA Service OpenAPI
- [docs/openapi/pkd-relay.yaml](docs/openapi/pkd-relay.yaml) - PKD Relay OpenAPI

### Archive
- [docs/archive/](docs/archive/) - 100+ historical phase/completion/plan documents

---

## Contact

For detailed information, see [docs/DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md)
