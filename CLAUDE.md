# ICAO Local PKD - Development Guide

**Current Version**: v2.1.4.3
**Last Updated**: 2026-01-30
**Status**: Production Ready - Repository Pattern Complete (Phase 1-3 ✅, Phase 4.1-4.3 ✅, 12+ APIs functional)

---

## Quick Start

### Essential Information

**Services**: PKD Management (:8081), PA Service (:8082), PKD Relay (:8083)
**API Gateway**: http://localhost:8080/api
**Frontend**: http://localhost:3000

**Technology Stack**: C++20, Drogon, PostgreSQL 15, OpenLDAP, React 19

### Daily Commands

```bash
# Start system
./docker-start.sh

# Rebuild service
./scripts/rebuild-pkd-relay.sh [--no-cache]

# Helper functions
source scripts/ldap-helpers.sh && ldap_count_all
source scripts/db-helpers.sh && db_count_crls

# Health check
./docker-health.sh
```

**Complete Guide**: See [docs/DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md)

---

## Architecture

### Service Layer

```
Frontend (React) → API Gateway (Nginx) → 3 Backend Services → DB/LDAP
```

**PKD Management**: Upload, Certificate Search, ICAO Sync
**PA Service**: Passive Authentication verification
**PKD Relay**: DB-LDAP Sync, Auto Reconciliation

### LDAP Structure

```
dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
├── dc=data
│   └── c={COUNTRY}
│       ├── o=csca (CSCA certificates)
│       ├── o=mlsc (Master List Signer Certificates - Sprint 3)
│       ├── o=dsc  (DSC certificates)
│       ├── o=crl  (CRLs)
│       └── o=ml   (Master Lists)
└── dc=nc-data
    └── c={COUNTRY}
        └── o=dsc  (Non-conformant DSC)
```

---

## Current Features (v2.1.2.6)

### Core Functionality
- ✅ LDIF/Master List upload (AUTO/MANUAL modes)
- ✅ **Master List file processing (537 certificates: 1 MLSC + 536 CSCA/LC)**
- ✅ **Country-based LDAP storage (95 countries, o=mlsc/csca/lc per country)**
- ✅ Certificate validation (Trust Chain, CRL, Link Certificates)
- ✅ LDAP integration (MMR cluster, Software LB)
- ✅ Passive Authentication (ICAO 9303)
- ✅ DB-LDAP sync monitoring
- ✅ Auto reconciliation (CSCA/DSC/CRL)
- ✅ Certificate search & export
- ✅ ICAO PKD version monitoring
- ✅ Trust chain visualization (frontend)
- ✅ Link certificate validation (Sprint 3)
- ✅ **Upload issues tracking (duplicate detection and reporting)**

### Security (v1.8.0 - v2.0.0)
- ✅ 100% Parameterized SQL queries (28 queries total)
- ✅ Credential externalization (.env)
- ✅ File upload validation (MIME, path sanitization)
- ✅ JWT authentication + RBAC
- ✅ Audit logging (IP tracking)

### Recent Changes (v2.1.4.3)

- ✅ **Repository Pattern Phase 4.3: ValidationService Core Implementation** (v2.1.4.3)
  - **CertificateRepository X509 Certificate Retrieval Methods**
    - `X509* findCscaByIssuerDn(const std::string& issuerDn)` - Find CSCA by issuer DN with normalized comparison
    - `std::vector<X509*> findAllCscasBySubjectDn(const std::string& subjectDn)` - Find all CSCAs for trust chain building
    - DN normalization helpers: `normalizeDnForComparison()`, `extractDnAttribute()` for format-independent matching
    - Supports both OpenSSL slash format (`/C=X/O=Y/CN=Z`) and RFC2253 comma format (`CN=Z,O=Y,C=X`)
    - PostgreSQL bytea hex format parsing (`\x` prefix) with OpenSSL d2i_X509()
    - Component-based SQL with LIKE + C++ post-filter to eliminate false positives
  - **ValidationService Trust Chain Building**
    - `buildTrustChain()` - Recursive chain construction with link certificate support
    - Circular reference detection using `std::set<std::string>` for visited DNs
    - Self-signed certificate detection with proper check ordering
    - Link certificate identification via basicConstraints CA=TRUE + keyCertSign usage
    - Trust chain depth limiting (maxDepth=5) to prevent infinite loops
    - Chain path generation (e.g., "DSC → Link Cert → Root CSCA")
  - **Certificate Validation with OpenSSL Integration**
    - `validateCertificate()` - Complete validation workflow (expiration + trust chain + signature)
    - `verifyCertificateSignature()` - RSA/ECDSA signature verification using X509_verify()
    - `validateTrustChainInternal()` - Chain-wide signature validation
    - Expiration check with X509_cmp_time() for notAfter field
    - Proper memory management: X509_free() for all allocated certificates
    - ValidationResult with status (VALID/INVALID/PENDING), trust chain path, detailed error messages
  - **Code Statistics**: ~250 lines in CertificateRepository, ~200 lines in ValidationService
  - **Commit**: 1d993c5 - Phase 4.3 ValidationService core implementation with OpenSSL integration

- ⏭️ **Repository Pattern Phase 4.4: Async Processing Migration (SKIPPED)** (v2.1.4.3)
  - **Decision**: Intentionally skipped - deemed unnecessary for current architecture
  - **Rationale**:
    - Core business logic already separated via Strategy Pattern (ProcessingStrategyFactory)
    - Async functions (processLdifFileAsync, processMasterListFileAsync) are now thin controller glue code
    - Moving to Service would require extensive refactoring of global dependencies (appConfig, LDAP connections, ProgressManager)
    - High complexity (750+ lines, complex threading) for minimal architectural benefit
    - Current implementation is stable and production-ready
  - **What Was Achieved Instead**:
    - ✅ Phase 4.1-4.3: Complete Repository Pattern for 12+ API endpoints
    - ✅ 500+ lines SQL eliminated, 100% parameterized queries
    - ✅ Oracle migration ready (67% effort reduction)
    - ✅ ValidationService with OpenSSL integration
  - **Future Consideration**: Phase 4.5 (complete async refactoring) only if becomes performance bottleneck

- ✅ **Repository Pattern Phase 4.2: AuditRepository & AuditService Implementation** (v2.1.4.2)
  - **Complete Audit Log System**: Migrated from direct SQL to Repository Pattern
    - AuditRepository: findAll(), countByOperationType(), getStatistics()
    - AuditService: getOperationLogs(), getOperationStatistics()
    - 2 API endpoints connected: GET /api/audit/operations, GET /api/audit/operations/stats
  - **Dynamic Filtering**: Parameterized queries with optional operationType and username filters
  - **Statistics Aggregation**: Total/successful/failed counts, operations by type, top users, average duration
  - **Pagination Support**: Limit/offset with total count for frontend pagination
  - **Verification**: 9 operations logged, 100% success rate, 41ms average duration
  - **Commit**: 4ca1951 - Phase 4.2 AuditRepository and AuditService implementation

- ✅ **Repository Pattern Phase 4.1: UploadRepository Statistics & Schema Fixes** (v2.1.4)
  - **Database Column Mapping Fixes**: Resolved column name mismatches causing "column does not exist" errors
    - Fixed sortBy mapping: createdAt→upload_timestamp, updatedAt→completed_timestamp
    - Fixed country_code reference (was using non-existent "country" column)
    - Fixed self-signed detection: subject_dn = issuer_dn (was using non-existent is_self_signed column)
    - Updated resultToUpload() column indices from 14-22 to 17-25
  - **Statistics Methods Implementation**:
    - getStatisticsSummary(): Total certs by type (CSCA/DSC/DSC_NC/MLSC/CRL), upload count, date range
    - getCountryStatistics(): Certificate counts grouped by country with sorting
    - getDetailedCountryStatistics(): Complete breakdown including CSCA self-signed vs link cert split
  - **SQL Aggregation Queries**: COALESCE, SUM, GROUP BY, CASE expressions for comprehensive statistics
  - **Verification Results**: 31,212 total certificates across 7 uploads (872 CSCA, 29,838 DSC, 502 DSC_NC, 27 MLSC, 69 CRL)
  - **Docker Deployment Fix**: Changed from `docker-compose restart` to `docker-compose up -d --force-recreate` for image reload
  - **Commit**: 2b0e8f1 - Phase 4.1 UploadRepository statistics implementation and schema fixes

- ✅ **Repository Pattern Phase 3: API Route Integration** (v2.1.3.1)
  - **9 Endpoints Connected**: Migrated from direct SQL to Service layer calls
    - 8 Upload endpoints → UploadService (uploadLdif, uploadMasterList, getUploadHistory, getUploadDetail, getUploadStatistics, getCountryStatistics, getDetailedCountryStatistics, deleteUpload)
    - 1 Validation endpoint → ValidationService (getValidationByFingerprint)
  - **Code Reduction**: 467 lines removed from main.cpp (38% reduction in Controller code)
  - **File Deduplication**: SHA-256 hash-based duplicate detection prevents re-upload of same files
  - **Clean Architecture**: Zero SQL queries in connected endpoints, all database access through Repository layer
  - **Oracle Migration Ready**: Endpoints are database-agnostic, only Repositories need updating for Oracle
  - **Documentation**: Complete Phase 3 completion report at [docs/PHASE_3_API_ROUTE_INTEGRATION_COMPLETION.md](docs/PHASE_3_API_ROUTE_INTEGRATION_COMPLETION.md)
  - **Deferred to Phase 4**: ValidationService re-validation logic, AuditService implementations, async processing logic
  - **Commit**: Phase 3 completion with Docker build verification

- ✅ **Upload Validations API & Trust Chain Visualization** (v2.1.2.9)
  - **New Endpoint**: `GET /api/upload/{uploadId}/validations` — paginated trust chain validation results scoped to a specific upload
    - Query params: `limit`, `offset`, `status` (VALID/INVALID/PENDING), `certType` (DSC/DSC_NC)
    - Returns `trustChainPath`, `cscaSubjectDn`, `fingerprint`, signature/validity/CRL check results
    - Matches `ValidationListResponse` frontend type
  - **New Endpoint**: `GET /api/certificates/validation?fingerprint={sha256}` — single certificate validation detail by fingerprint
    - JOIN between `validation_result` and `certificate` on `fingerprint_sha256`
    - trust_chain_path JSONB parsed from `["DSC → CN=..."]` array to string
  - **Frontend: CertificateSearch Trust Chain Card** — General tab now shows trust chain summary for DSC/DSC_NC certificates
    - Compact TrustChainVisualization with color-coded status (green/yellow/red)
    - Status badges: "신뢰 체인 유효" / "검증 대기 (만료됨)" / "신뢰 체인 유효하지 않음"
  - **Fresh Data Upload Verified** (2026-01-29):
    - Master List: 536 CSCA + 1 MLSC
    - Collection-001 (DSC): 29,838 certs + 69 CRL → 16,788 VALID / 6,354 PENDING / 6,696 INVALID
    - Collection-002 (CSCA): 309 additional CSCAs
    - Collection-003 (DSC_NC): 502 certs → 80 VALID / 179 PENDING / 243 INVALID
    - Total validation_result: 30,340 records (16,868 VALID)
  - **Commits**: `41f4410`, `38f5b6a`

- ✅ **Trust Chain Validation Fix - DN Normalization & Circular Reference** (v2.1.2.8)
  - **Root Cause**: DN format mismatch between CSCAs (OpenSSL `/C=X/O=Y/CN=Z` slash format) and DSC issuer DNs (RFC2253 `CN=Z,O=Y,C=X` comma format). Direct SQL `LOWER(subject_dn) = LOWER(?)` comparison always failed → 0 validated DSCs.
  - **Fix 1 - DN Normalization**: Added `normalizeDnForComparison()` — extracts RDN components (C=, O=, CN=, OU=, serialNumber=), lowercases all, sorts alphabetically, joins with `|` for format/order-independent comparison.
  - **Fix 2 - Component-based SQL**: Updated `findCscaByIssuerDn()` and `findAllCscasBySubjectDn()` to use `LIKE '%cn=...%' AND LIKE '%c=...%'` broad candidate retrieval + C++ post-filter via normalized comparison. Eliminates LIKE false positives.
  - **Fix 3 - Circular Reference Bug**: `buildTrustChain()` reported "Circular reference detected at depth 2" for every self-signed CSCA chain. Cause: `visitedDns` check ran before `isSelfSigned()` check. For self-signed CSCAs, issuer DN == subject DN matches the already-visited set. Fixed by reordering — `isSelfSigned()` checked first.
  - **Results**: 17,869 VALID (59.3%), 6,354 PENDING (expired DSCs), 5,615 INVALID (missing link certs or expired CSCAs). Trust chain path included in validation response.
  - **Commit**: bc03f2b

- ✅ **Audit Log Recording & API Fix** (v2.1.2.7)
  - Fixed audit log INSERT failure: username NOT NULL constraint violated when no JWT auth (defaulted to "anonymous")
  - Fixed metadata JSONB security vulnerability: was string-interpolated into SQL, now uses parameterized query ($14::jsonb)
  - Aligned /api/audit/operations response format with frontend (data array, total count, operationsByType as Record)
  - Aligned /api/audit/operations/stats response (successfulOperations, failedOperations, topUsers, averageDurationMs)
  - Fixed AuditLog.tsx page: wrong endpoint URLs (/api/auth/audit-log → /api/audit/operations), interface field names
  - Verified: PA_VERIFY and FILE_UPLOAD operations now recorded with IP, duration, metadata

- ✅ **PA Service CSCA Lookup Fix** (v2.1.1)
  - Fixed LDAP base DN construction: doubled dc=download in searchCscaInOu() and searchCrlFromLdap()
  - Fixed LDAP_HOST: PA service was connecting to non-existent haproxy container (set to openldap1 in compose)
  - Verified: Korean DSC PA verification returns VALID (CSCA found in o=csca, CRL check passed)

- ✅ **Database Schema Fixes for Sync Page** (v2.1.2.6)
  - Added MLSC columns to sync_status table (db_mlsc_count, ldap_mlsc_count, mlsc_discrepancy)
  - Updated reconciliation_summary table: added dry_run, renamed total_success/total_failed → success_count/failed_count
  - Added certificate deletion tracking: csca_deleted, dsc_deleted, dsc_nc_deleted, crl_deleted
  - Fixed sync page 500 errors: all APIs now working correctly
  - Database migrations created for reproducible deployments

- ✅ **DSC_NC Certificate Display Improvements** (v2.1.2.5)
  - Frontend: DSC_NC badge correctly displays "DSC_NC" instead of "DSC"
  - Detail dialog: Type field shows "DSC_NC" with complete description
  - Added comprehensive DSC_NC description section with non-conformance reasons and warnings
  - PKD Conformance Information section displays pkdConformanceCode, pkdConformanceText, pkdVersion
  - Backend: Extended Certificate domain model and LDAP repository to read pkdConformance attributes
  - Fixed: Certificate interface field name mismatch (certType → type to match backend API)

- ✅ **LDAP Storage Bug Fixes** (v2.1.2.1 - v2.1.2.4)
  - Fixed CN attribute duplication in v2 DN mode (MLSC/CSCA/DSC storage)
  - Fixed DSC_NC LDAP DN format (o=dsc in nc-data container)
  - Verified 100% LDAP storage success: 31,281 certificates (100% DB-LDAP match)

- ✅ **Upload Issues Tracking** (v2.1.2.2 - v2.1.2.3)
  - API endpoint for duplicate certificate detection
  - Frontend UI showing duplicates by type in Upload History
  - Accurate duplicate counting (first_upload_id exclusion logic)

- ✅ **Collection 002 Complete Analysis**
  - Verified 5,017 CSCA certificates in 26 Master Lists (11MB LDIF)
  - 94% deduplication efficiency (4,708 duplicates, 309 new unique certs)
  - Complete upload sequence validation: ML + Collections 001/002/003

---

## API Endpoints

### PKD Management (via :8080/api)

- `POST /upload/ldif` - Upload LDIF file
- `POST /upload/masterlist` - Upload Master List
- `GET /upload/history` - Upload history
- `GET /upload/{uploadId}/validations` - Validation results with trust chain
- `GET /upload/{uploadId}/issues` - Upload issues (duplicates detected) **[NEW v2.1.2.2]**
- `GET /certificates/search` - Search certificates
- `GET /certificates/validation?fingerprint={sha256}` - Certificate validation result
- `GET /certificates/export/country` - Export by country

### ICAO Auto Sync
- `POST /icao/check-updates` - Manual version check
- `GET /icao/status` - Version comparison
- `GET /icao/latest` - Latest versions
- `GET /icao/history` - Detection history

### PA Service (via :8080/api/pa)
- `POST /verify` - PA verification
- `POST /parse-sod` - Parse SOD
- `POST /parse-dg1` - Parse DG1 (MRZ)
- `POST /parse-dg2` - Parse DG2 (Face)

### PKD Relay (via :8080/api/sync)
- `GET /status` - Full sync status
- `GET /stats` - Statistics
- `POST /reconcile` - Trigger reconciliation
- `GET /reconcile/history` - Reconciliation history

---

## Development Workflow

### 1. Code Changes

```bash
# Edit source
vim services/pkd-relay-service/src/relay/sync/reconciliation_engine.cpp

# Update version (for cache busting)
vim services/pkd-relay-service/src/main.cpp
# Change: spdlog::info("... v2.0.X ...")
```

### 2. Build & Deploy

```bash
# Quick rebuild (uses cache)
./scripts/rebuild-pkd-relay.sh

# Force rebuild (no cache)
./scripts/rebuild-pkd-relay.sh --no-cache
```

### 3. Testing

```bash
# Load helpers
source scripts/ldap-helpers.sh
source scripts/db-helpers.sh

# Prepare test data
db_reset_crl_flags

# Test reconciliation
curl -X POST http://localhost:8080/api/sync/reconcile \
  -H "Content-Type: application/json" \
  -d '{"dryRun": false}' | jq .

# Verify results
ldap_count_all
db_latest_reconciliation_logs
```

---

## Credentials (DO NOT COMMIT)

**PostgreSQL**:
- Host: postgres:5432
- Database: localpkd
- User: pkd
- Password: (from .env)

**LDAP**:
- Host: openldap1:389, openldap2:389
- Admin DN: cn=admin,dc=ldap,dc=smartcoreinc,dc=com
- Password: ldap_test_password_123
- Base DN: dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com

---

## Helper Scripts

### rebuild-pkd-relay.sh
Rebuild and deploy PKD Relay service with optional --no-cache

### ldap-helpers.sh
```bash
source scripts/ldap-helpers.sh

ldap_info                  # Show connection info
ldap_count_all             # Count all certificates
ldap_count_certs CRL       # Count CRLs
ldap_search_country KR     # Search by country
ldap_delete_all_crls       # Delete all CRLs (testing)
```

### db-helpers.sh
```bash
source scripts/db-helpers.sh

db_info                          # Show connection info
db_count_certs                   # Count certificates
db_count_crls                    # Count CRLs
db_reset_crl_flags               # Reset CRL flags
db_reconciliation_summary 10     # Last 10 reconciliations
db_latest_reconciliation_logs    # Latest logs
db_sync_status 10                # Sync history
```

---

## Common Issues & Solutions

### Build version mismatch
**Problem**: Binary version doesn't match source
**Solution**: `./scripts/rebuild-pkd-relay.sh --no-cache`

### LDAP authentication failed
**Problem**: `ldap_bind: Invalid credentials (49)`
**Solution**: Use `ldap_test_password_123` (NOT "admin")

### Reconciliation logs missing
**Problem**: reconciliation_log table has no entries
**Solution**: Check table has `cert_fingerprint VARCHAR(64)` (NOT `cert_id INTEGER`)

### CRLs not syncing
**Problem**: DB shows stored_in_ldap=TRUE but LDAP has 0 CRLs
**Solution**: `db_reset_crl_flags` then trigger reconciliation

---

## Documentation

### General Guides

- **[DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md)** - Complete development guide (credentials, commands, troubleshooting)
- **[LUCKFOX_DEPLOYMENT.md](docs/LUCKFOX_DEPLOYMENT.md)** - ARM64 deployment guide
- **[DOCKER_BUILD_CACHE.md](docs/DOCKER_BUILD_CACHE.md)** - Build cache troubleshooting
- **[PA_API_GUIDE.md](docs/PA_API_GUIDE.md)** - PA Service API guide

### Master List Processing (v2.1.1)

- **[MASTER_LIST_PROCESSING_GUIDE.md](docs/MASTER_LIST_PROCESSING_GUIDE.md)** - **Comprehensive guide** (format, architecture, pitfalls, troubleshooting)
- **[MASTER_LIST_PROCESSING_FINAL_SUMMARY.md](docs/MASTER_LIST_PROCESSING_FINAL_SUMMARY.md)** - Executive summary & project timeline
- **[ML_FILE_PROCESSING_COMPLETION.md](docs/ML_FILE_PROCESSING_COMPLETION.md)** - Direct file processing completion
- **[COLLECTION_002_LDIF_PROCESSING_COMPLETION.md](docs/COLLECTION_002_LDIF_PROCESSING_COMPLETION.md)** - LDIF processing completion

---

## Version History

### v2.1.4 (2026-01-30) - Repository Pattern Phase 4 Progress: Statistics & Audit Implementation

#### Phase 4.1: UploadRepository Statistics Methods & Database Schema Fixes

- ✅ **Critical Schema Fixes**
  - **Column Name Mapping**: Fixed mismatches between code expectations and actual database schema
    - sortBy parameter mapping: createdAt/created_at → upload_timestamp, updatedAt/updated_at → completed_timestamp
    - Country field: country → country_code (certificate table)
    - Self-signed detection: Added DN comparison logic (subject_dn = issuer_dn) instead of non-existent is_self_signed column
  - **Result Mapping Fix**: Updated resultToUpload() column indices from 14-22 to 17-25 to match extended SELECT query
  - **Impact**: All 9 Phase 3 endpoints now functional (were failing with "column does not exist" errors)

- ✅ **Statistics Methods Implementation**
  - **getStatisticsSummary()**: Aggregate statistics across all uploads
    - Total certificate counts by type (CSCA, DSC, DSC_NC, MLSC, CRL)
    - Total upload count
    - Date range (earliest/latest upload timestamps)
    - SQL: COALESCE, SUM aggregations with JOIN to certificate table
  - **getCountryStatistics()**: Certificate distribution by country
    - Group by country_code with counts per certificate type
    - Configurable sorting (country, total, csca, dsc, etc.)
    - Pagination support (limit parameter)
    - SQL: GROUP BY with multiple SUM CASE expressions
  - **getDetailedCountryStatistics()**: Comprehensive country-level breakdown
    - CSCA split: Self-signed vs Link Certificates (subject_dn = issuer_dn logic)
    - All certificate types: MLSC, CSCA SS, CSCA LC, DSC, DSC_NC, CRL
    - Full country coverage (137+ countries)
    - SQL: Complex CASE expressions for CSCA type detection

- ✅ **Verification Results** (Production Data)
  - Total: 31,212 certificates across 7 uploads
  - Breakdown: 872 CSCA, 29,838 DSC, 502 DSC_NC, 27 MLSC, 69 CRL
  - Country distribution: 137 countries with certificates
  - API response time: ~50ms for detailed statistics

- ✅ **Docker Deployment Fix**
  - Issue: `docker-compose restart` doesn't reload updated images
  - Solution: Use `docker-compose up -d --force-recreate` to force image reload
  - Result: Consistent deployments with latest code changes

**Files Modified**:
- services/pkd-management/src/repositories/upload_repository.cpp - Schema fixes and statistics implementation
- services/pkd-management/src/repositories/upload_repository.h - Method declarations

**Commit**: 2b0e8f1 - Phase 4.1 UploadRepository statistics implementation and schema fixes

#### Phase 4.2: AuditRepository & AuditService Complete Implementation

- ✅ **AuditRepository Methods**
  - **findAll()**: Retrieve audit logs with dynamic filtering
    - Optional filters: operationType, username
    - Pagination: limit, offset
    - ORDER BY created_at DESC for recent-first ordering
    - Parameterized queries with dynamic WHERE clause construction
  - **countByOperationType()**: Count logs by specific operation type
    - Single parameterized query
    - Used for statistics and analytics
  - **getStatistics()**: Comprehensive audit statistics
    - Total operations, successful/failed counts
    - Average duration (milliseconds)
    - Operations grouped by type with counts
    - Top 10 users by activity
    - Optional date range filtering (startDate, endDate)

- ✅ **AuditService Integration**
  - **getOperationLogs()**: Service layer wrapper for findAll()
    - Accepts AuditLogFilter struct (limit, offset, operationType, username)
    - Returns JSON with success flag, data array, count
    - Error handling with exception catching
  - **getOperationStatistics()**: Service layer wrapper for getStatistics()
    - Date range support for time-bound analysis
    - Returns JSON with success flag and nested data object
    - Consistent response format with other endpoints

- ✅ **API Integration** (2 endpoints connected)
  - GET /api/audit/operations → auditService->getOperationLogs()
  - GET /api/audit/operations/stats → auditService->getOperationStatistics()
  - Zero SQL in main.cpp endpoints, all queries in Repository layer
  - Consistent error handling and response formatting

- ✅ **Verification Results** (Test Execution)
  - GET /api/audit/operations: 9 operations returned
    - Types: FILE_UPLOAD (6), CERTIFICATE_SEARCH (3)
    - Users: 3 unique users (anonymous, admin, pkd_user)
    - 100% success rate
  - GET /api/audit/operations/stats:
    - Total: 9 operations (9 successful, 0 failed)
    - Average duration: 41ms
    - Operations by type: FILE_UPLOAD (6), CERTIFICATE_SEARCH (3)
    - Top users: anonymous (5), admin (2), pkd_user (2)

**Files Modified**:
- services/pkd-management/src/repositories/audit_repository.cpp - Complete implementation
- services/pkd-management/src/repositories/audit_repository.h - Already complete from Phase 1.5
- services/pkd-management/src/services/audit_service.cpp - Service methods implementation
- services/pkd-management/src/services/audit_service.h - Already complete from Phase 1.6
- services/pkd-management/src/main.cpp - 2 endpoints connected to AuditService

**Commit**: 4ca1951 - Phase 4.2 AuditRepository and AuditService implementation

#### Phase 4 Summary

- ✅ **12+ API Endpoints Functional**: Phase 3 (9) + Phase 4.2 (2) + existing endpoints
- ✅ **Database Schema Alignment**: All column name mismatches resolved
- ✅ **Complete Statistics APIs**: Upload statistics, country breakdowns, audit logs all working
- ✅ **100% Parameterized Queries**: All new SQL uses prepared statements with parameter binding
- ✅ **Production Verified**: Tested with 31,212 real certificates and audit log data

**Remaining Phase 4 Work**:
- Phase 4.3: ValidationService::revalidateDscCertificates() - Complex X509 validation logic
- Phase 4.4: Move async processing (processLdifFileAsync, processMasterListFileAsync) into UploadService

---

### v2.1.3.1 (2026-01-30) - Repository Pattern Phase 3 Complete

#### Phase 3: API Route Integration to Service Layer

- ✅ **9 Endpoints Migrated from Direct SQL to Service Calls**
  - GET /api/upload/history → uploadService->getUploadHistory()
  - POST /api/upload/ldif → uploadService->uploadLdif()
  - POST /api/upload/masterlist → uploadService->uploadMasterList()
  - GET /api/upload/:id → uploadService->getUploadDetail()
  - GET /api/upload/statistics → uploadService->getUploadStatistics()
  - GET /api/upload/countries → uploadService->getCountryStatistics()
  - GET /api/upload/countries/detailed → uploadService->getDetailedCountryStatistics()
  - DELETE /api/upload/:id → uploadService->deleteUpload()
  - GET /api/certificates/validation → validationService->getValidationByFingerprint()

- ✅ **Code Quality Improvements**
  - **Code Reduction**: 467 lines removed from main.cpp (38% reduction in endpoint code)
  - **SQL Elimination**: Zero SQL queries in connected endpoints
  - **Error Handling**: Consistent exception handling in Service layer
  - **Type Safety**: Strong typing with Repository domain models
  - **100% Parameterized Queries**: All SQL in Repository layer uses prepared statements

- ✅ **File Deduplication Feature**
  - SHA-256 hash computation using OpenSSL (UploadService::computeFileHash())
  - Duplicate detection before processing (UploadRepository::findByFileHash())
  - Returns 409 Conflict with reference to existing upload ID
  - Prevents wasted processing and storage

- ✅ **Validation Statistics Integration**
  - Extended Upload struct with 9 validation fields
  - trustChainValidCount, trustChainInvalidCount, cscaNotFoundCount, expiredCount, revokedCount
  - validationValidCount, validationInvalidCount, validationPendingCount, validationErrorCount
  - Included in all upload history and detail responses

- ✅ **Architecture Benefits**
  - **Database Independence**: Endpoints have zero database knowledge
  - **Oracle Migration Ready**: Only 5 Repository files need changing (67% effort reduction)
  - **Testable**: Services can be unit tested with mock Repositories
  - **Maintainable**: Clear Controller → Service → Repository separation

**Deferred to Phase 4**:
- POST /api/validation/revalidate - ValidationService::revalidateDscCertificates() not implemented
- GET /api/audit/operations - AuditService::getOperationLogs() needs Repository support
- processLdifFileAsync() - Move async processing logic into UploadService
- processMasterListFileAsync() - Move async processing logic into UploadService

**Files Modified**:

- services/pkd-management/src/main.cpp - 9 endpoints connected to Services
- services/pkd-management/src/repositories/upload_repository.{h,cpp} - findByFileHash(), updateFileHash()
- services/pkd-management/src/services/upload_service.{h,cpp} - computeFileHash() implementation
- docs/PHASE_3_API_ROUTE_INTEGRATION_COMPLETION.md - Complete phase documentation (NEW)

**Documentation**:

- [PHASE_3_API_ROUTE_INTEGRATION_COMPLETION.md](docs/PHASE_3_API_ROUTE_INTEGRATION_COMPLETION.md) - Comprehensive completion report
- [PHASE_2_MAIN_INTEGRATION_COMPLETION.md](docs/PHASE_2_MAIN_INTEGRATION_COMPLETION.md) - Service initialization in main.cpp
- [PHASE_1.6_SERVICE_REPOSITORY_INJECTION.md](docs/PHASE_1.6_SERVICE_REPOSITORY_INJECTION.md) - Service DI implementation

### v2.1.0 (2026-01-26) - Sprint 3 Complete

**Sprint 3: Link Certificate Validation Integration**

- ✅ **Trust Chain Building** (Phase 1)
  - Recursive trust chain construction with link certificate support
  - Multi-level chain validation (DSC → Link Cert → Link Cert → Root CSCA)
  - Real-world examples: Latvia (3-level), Philippines (3-level), Luxembourg (org change)

- ✅ **Master List Link Certificate Validation** (Phase 2, Task 3.3)
  - Updated Master List processing to detect and validate link certificates
  - 536 certificates: 476 self-signed CSCAs (88.8%) + 60 link certificates (11.2%)
  - All stored as certificate_type='CSCA' with proper validation

- ✅ **CSCA Cache Performance Optimization** (Phase 2, Task 3.4)
  - In-memory cache for 536 certificates across 215 unique Subject DNs
  - 80% performance improvement (50ms → 10ms per DSC validation)
  - 5x faster bulk processing (25min → 5min for 30,000 DSCs)
  - 99.99% reduction in PostgreSQL load (30,000 queries → ~1 query)

- ✅ **Validation Result APIs** (Phase 3, Task 3.5)
  - `GET /api/upload/{uploadId}/validations` - Paginated validation results
  - `GET /api/certificates/validation?fingerprint={sha256}` - Single cert validation
  - Trust chain path included in response (e.g., "DSC → Link → Root")

- ✅ **Frontend Trust Chain Visualization** (Phase 3, Task 3.6)
  - Reusable TrustChainVisualization component (compact + full modes)
  - ValidationDemo page with 7 sample scenarios
  - Integration with Certificate Search and Upload Detail pages
  - Dark mode support and responsive design

- ✅ **MLSC Sync Support** (DB-LDAP Synchronization Update)
  - Added MLSC tracking to sync statistics and discrepancy monitoring
  - Database migration: Added mlsc columns to sync_status table
  - Backend: Updated sync statistics gathering (getDbStats, getLdapStats)
  - Critical fix: CSCA counting bug resolved (excluded MLSC to prevent false +59 discrepancy)
  - Frontend: Added MLSC row to sync comparison table with discrepancy indicators
  - Result: Complete sync tracking for all certificate types (CSCA, MLSC, DSC, DSC_NC, CRL)

**Sprint 3 Documentation**:

- `docs/archive/SPRINT3_PHASE1_COMPLETION.md` - Trust chain building
- `docs/archive/SPRINT3_TASK33_COMPLETION.md` - Master List link cert validation
- `docs/archive/SPRINT3_TASK34_COMPLETION.md` - CSCA cache optimization
- `docs/archive/SPRINT3_TASK35_COMPLETION.md` - Validation result APIs
- `docs/archive/SPRINT3_TASK36_COMPLETION.md` - Frontend visualization
- `docs/MLSC_SYNC_UPDATE.md` - DB-LDAP sync MLSC support and CSCA counting fix

### v2.1.1 (2026-01-28) - Master List Processing Refinements

**LDIF Processing MLSC Count Tracking Fix**

- ✅ **Problem Identified**: Collection 002 LDIF processing extracted MLSC certificates correctly but failed to update `uploaded_file.mlsc_count` in database
- ✅ **Root Cause**: `ProcessingCounts` structure in ldif_processor.h was missing `mlscCount` field
- ✅ **Fix Applied**:
  - Added `mlscCount` field to `ProcessingCounts` (ldif_processor.h)
  - Added `mlscCount` field to `MasterListStats` (masterlist_processor.h)
  - Fixed masterlist_processor.cpp line 248: `stats.mlCount++` → `stats.mlscCount++`
  - Updated ldif_processor.cpp to track mlscCount when processing Master Lists
  - Updated processing_strategy.cpp to write mlsc_count to database (both AUTO and MANUAL modes)
- ✅ **Result**: Collection 002 LDIF now correctly shows `mlsc_count = 26` (26 Master Lists with MLSC)
- ✅ **Verification**: End-to-end tested with Collection 002 LDIF upload + direct ML file upload

**Country-Level Detailed Statistics Dialog**

- ✅ **New Backend API**: `GET /api/upload/countries/detailed?limit={n}`
  - Returns comprehensive certificate breakdown by country
  - Includes: MLSC, CSCA Self-signed, CSCA Link Cert, DSC, DSC_NC, CRL counts
  - Supports all 137+ countries with single query
  - Response time: ~50ms
- ✅ **Frontend Enhancement**:
  - New `CountryStatisticsDialog` component with full-screen modal
  - Color-coded certificate type columns (Purple: MLSC, Blue: CSCA SS, Cyan: CSCA LC, Green: DSC, Amber: DSC_NC, Red: CRL)
  - CSV export functionality
  - Country flags display
  - Totals footer row
  - Dark mode support
- ✅ **Dashboard Integration**: "상세 통계" button opens interactive dialog (replaces link to Upload Dashboard)
- ✅ **User Impact**: Single-click access to detailed certificate statistics for all countries

**Files Modified**:

Backend:
- `services/pkd-management/src/common/masterlist_processor.h` - Added mlscCount to MasterListStats
- `services/pkd-management/src/common/masterlist_processor.cpp` - Fixed MLSC count increment
- `services/pkd-management/src/ldif_processor.h` - Added mlscCount to ProcessingCounts
- `services/pkd-management/src/ldif_processor.cpp` - Track MLSC count from Master Lists
- `services/pkd-management/src/processing_strategy.cpp` - Update mlsc_count in database
- `services/pkd-management/src/main.cpp` - New /api/upload/countries/detailed endpoint

Frontend:
- `frontend/src/services/pkdApi.ts` - Added getDetailedCountryStatistics() function
- `frontend/src/components/CountryStatisticsDialog.tsx` - New statistics dialog component (NEW FILE)
- `frontend/src/pages/Dashboard.tsx` - Integrated dialog with button trigger

**Documentation**:
- `docs/MLSC_EXTRACTION_FIX.md` - Updated with Country Statistics Dialog section

### v2.1.2.5 (2026-01-28) - DSC_NC Frontend Display Improvements

#### Frontend: DSC_NC Certificate Display Enhancements

- ✅ **Certificate Search Page Improvements**
  - **Badge Display**: DSC_NC certificates now correctly display "DSC_NC" badge (orange) instead of "DSC"
  - **Type Field Fix**: Detail dialog Type field shows "DSC_NC" matching the certificate type
  - **Field Name Fix**: Changed Certificate interface from `certType` to `type` to match backend API response

- ✅ **DSC_NC Description Section**
  - Added comprehensive description explaining Non-Conformant DSC
  - Lists common non-conformance reasons (X.509 extension issues, Key Usage violations, DN format errors, etc.)
  - Warning indicators for production use and ICAO nc-data deprecation (2021)
  - AlertTriangle icon for visual emphasis

- ✅ **PKD Conformance Information Section**
  - Displays pkdConformanceCode (e.g., "ERR:CSCA.CDP.14")
  - Displays pkdConformanceText with detailed error descriptions
  - Displays pkdVersion (ICAO PKD version number)
  - Styled with orange theme for DSC_NC context

#### Backend: PKD Conformance Fields Support

- ✅ **Domain Model Extension**
  - Updated Certificate class with optional pkdConformance fields
  - Added getters: getPkdConformanceCode(), getPkdConformanceText(), getPkdVersion()

- ✅ **LDAP Repository Enhancement**
  - Added pkdConformanceCode, pkdConformanceText, pkdVersion to LDAP attribute request
  - Reads and parses these attributes from LDAP entries
  - Passes to Certificate constructor

- ✅ **API Response Enhancement**
  - Updated certificate search API to include pkdConformance fields in JSON response
  - Conditional inclusion (only if present)

**Files Modified**:

Frontend:

- `frontend/src/pages/CertificateSearch.tsx` - Updated Certificate interface (type field), added DSC_NC description and PKD conformance sections
- `frontend/src/types/index.ts` - Already had pkdConformance fields in Certificate type

Backend:

- `services/pkd-management/src/domain/models/certificate.h` - Added pkdConformance fields to Certificate class
- `services/pkd-management/src/repositories/ldap_certificate_repository.cpp` - Added LDAP attribute reading for pkdConformance
- `services/pkd-management/src/main.cpp` - Updated API response to include pkdConformance fields

**Verification**:

- ✅ DSC_NC badge displays correctly in search results
- ✅ Detail dialog shows "DSC_NC" type with full description
- ✅ PKD Conformance section displays all three fields when available
- ✅ Backend API returns pkdConformanceCode, pkdConformanceText, pkdVersion for DSC_NC certificates

### v2.1.2.9 (2026-01-29) - Upload Validations API & Trust Chain Visualization

#### Backend: Upload Validations Endpoint

- ✅ **GET /api/upload/{uploadId}/validations** — paginated trust chain validation results
  - Filterable by `status` (VALID/INVALID/PENDING) and `certType` (DSC/DSC_NC)
  - Returns trust chain path, CSCA info, signature/validity check results, certificate fingerprint
  - Dynamic WHERE clause with parameterized queries for status/certType filters
  - Total count query for pagination metadata

- ✅ **GET /api/certificates/validation?fingerprint={sha256}** — single certificate validation by fingerprint
  - JOIN between validation_result and certificate on fingerprint_sha256
  - trust_chain_path JSONB parsed from array to string format

#### Frontend: Trust Chain Summary Card

- ✅ **CertificateSearch General Tab** — Trust chain summary for DSC/DSC_NC certificates
  - Compact TrustChainVisualization component with color-coded status
  - Status badges: VALID (green) / PENDING/expired (yellow) / INVALID (red)
  - Links to Details tab for full trust chain visualization

#### Verification (Fresh Data Upload 2026-01-29)

| Certificate Type | Count | VALID | PENDING | INVALID |
|------------------|-------|-------|---------|---------|
| DSC | 29,838 | 16,788 | 6,354 | 6,696 |
| DSC_NC | 502 | 80 | 179 | 243 |
| **Total** | **30,340** | **16,868** | **6,533** | **6,939** |

**Files Modified**:
- `services/pkd-management/src/main.cpp` — Upload validations endpoint + certificates validation endpoint
- `frontend/src/pages/CertificateSearch.tsx` — Trust chain summary card in General tab

### v2.1.2.8 (2026-01-28) - Trust Chain Validation Fix

#### Trust Chain: DN Normalization & Circular Reference Fix

- ✅ **DN Format Normalization** — `normalizeDnForComparison()` extracts RDN components, lowercases, sorts, joins with `|`
- ✅ **Component-based SQL** — `findCscaByIssuerDn()` uses LIKE per component + C++ post-filter
- ✅ **Circular Reference Fix** — `isSelfSigned()` check moved before `visitedDns` in `buildTrustChain()`
- ✅ **Results**: 17,869 VALID DSCs validated (59.3%)

**Commit**: `bc03f2b`

### v2.1.2.7 (2026-01-28) - Audit Log Recording & PA Service Fix

#### Audit Log: Recording and API Alignment

- ✅ **audit_log.h Fix (both pkd-management & pa-service)**
  - `username` defaults to `"anonymous"` — fixes NOT NULL constraint violation for unauthenticated requests
  - Metadata JSONB now passed as parameterized query (`$14::jsonb`) instead of string interpolation (security fix)
  - Parameter count: 14 → 15

- ✅ **Audit API Response Alignment (main.cpp)**
  - `/api/audit/operations`: returns `{ data: [...], total, count, limit, offset }` with camelCase fields
  - `/api/audit/operations/stats`: returns `{ data: { totalOperations, successfulOperations, failedOperations, operationsByType, topUsers, averageDurationMs } }`

- ✅ **AuditLog.tsx Frontend Fix**
  - Corrected endpoint URLs (`/api/auth/audit-log` → `/api/audit/operations`)
  - Updated interface types to match backend response (camelCase, operationsByType as Record)
  - Filter options aligned to actual operation types (UPLOAD, PA_VERIFY, CERTIFICATE_SEARCH, etc.)

#### PA Service: CSCA Lookup via LDAP

- ✅ **Base DN Fix (pa-service main.cpp)**
  - `searchCscaInOu()`: removed redundant `dc=download,` prefix (ldapBaseDn already contains it)
  - `searchCrlFromLdap()`: same fix applied
  - Root cause: DN was constructed as `o=csca,c=KR,dc=data,dc=download,dc=download,dc=pkd,...`

- ✅ **LDAP_HOST Configuration (docker-compose.yaml)**
  - Added `LDAP_HOST=openldap1` and `LDAP_PORT=389` to PA service environment
  - Previously used Dockerfile default `LDAP_HOST=haproxy` (container doesn't exist)

**Verification**:

- ✅ PA verify with Korean DSC returns VALID (CSCA003, Serial 0101, CRL checked)
- ✅ Audit log table records PA_VERIFY (36ms duration) and FILE_UPLOAD (LDIF) operations
- ✅ `/api/audit/operations` and `/api/audit/operations/stats` return correct format

**Files Modified**:

- `services/pkd-management/src/common/audit_log.h` - username default, parameterized metadata
- `services/pa-service/src/common/audit_log.h` - same fixes (shared code, separate copy)
- `services/pkd-management/src/main.cpp` - audit API response format alignment
- `services/pa-service/src/main.cpp` - CSCA/CRL base DN fix
- `docker/docker-compose.yaml` - PA service LDAP_HOST env
- `docker/init-scripts/03-security-schema.sql` - operation_audit_log schema update
- `docker/db/migrations/fix_operation_audit_log_schema.sql` - migration for existing deployments
- `frontend/src/pages/AuditLog.tsx` - endpoint URLs and interface types

### v2.1.2.6 (2026-01-28) - Database Schema Fixes for Sync Page

#### Database: PKD Relay Service Schema Compatibility

- ✅ **sync_status Table Updates**
  - Added `db_mlsc_count INTEGER NOT NULL DEFAULT 0` - Master List Signer Certificate count in PostgreSQL
  - Added `ldap_mlsc_count INTEGER NOT NULL DEFAULT 0` - Master List Signer Certificate count in LDAP
  - Added `mlsc_discrepancy INTEGER NOT NULL DEFAULT 0` - Discrepancy count between DB and LDAP for MLSC
  - **Purpose**: Track MLSC certificates in synchronization monitoring (completes Sprint 3 MLSC support)

- ✅ **reconciliation_summary Table Updates**
  - Added `dry_run BOOLEAN NOT NULL DEFAULT FALSE` - Whether this was a dry run or actual reconciliation
  - Renamed `total_success` → `success_count` - Number of certificates successfully reconciled
  - Renamed `total_failed` → `failed_count` - Number of certificates that failed reconciliation
  - Added `csca_deleted INTEGER NOT NULL DEFAULT 0` - Number of CSCA certificates deleted during reconciliation
  - Added `dsc_deleted INTEGER NOT NULL DEFAULT 0` - Number of DSC certificates deleted during reconciliation
  - Added `dsc_nc_deleted INTEGER NOT NULL DEFAULT 0` - Number of DSC_NC certificates deleted during reconciliation
  - Added `crl_deleted INTEGER NOT NULL DEFAULT 0` - Number of CRLs deleted during reconciliation
  - **Purpose**: Match PKD Relay Service v2.1.0 expected schema for reconciliation history and statistics

**Database Migrations**:

- `docker/db/migrations/add_mlsc_sync_columns.sql` - Adds MLSC tracking to sync_status table
- `docker/db/migrations/add_dry_run_to_reconciliation.sql` - Comprehensive reconciliation_summary schema update

**API Fixes**:

- ✅ `GET /api/sync/status` - Now returns complete sync status with MLSC counts
- ✅ `GET /api/sync/reconcile/history` - Fixed 500 error, now returns reconciliation history correctly
- ✅ Manual sync check button on Sync page now works correctly

**Verification**:

- ✅ Sync page loads without errors
- ✅ Manual sync check button triggers synchronization and updates statistics
- ✅ Reconciliation history displays correctly with all columns
- ✅ MLSC counts tracked in sync monitoring
- ✅ All sync APIs return expected data structure

**Documentation**:

- Updated database schema with migration scripts for future deployments
- Added comments to all new columns for clarity

### v2.1.2 - v2.1.2.4 (2026-01-28) - Critical Bug Fixes & Upload Verification

**Bug Fixes: LDAP Storage Issues**

- ✅ **v2.1.2.1 - CN Attribute Duplication Fix**
  - **Problem**: When `useLegacyDn=false` (v2 DN mode), cn attribute was set to `[fingerprint, fingerprint]` causing LDAP to reject entries
  - **Impact**: MLSC, CSCA, DSC certificates failed to save to LDAP with "LDAP operation failed" error
  - **Root Cause**: Lines 2528-2550 in main.cpp - duplicate value in cn attribute array
  - **Fix**: Conditional logic - Legacy DN: cn = `[standardDn, fingerprint]`, v2 DN: cn = `[fingerprint]` only
  - **Result**: All certificate types now save to LDAP correctly with v2 DN format

- ✅ **v2.1.2.4 - DSC_NC LDAP DN Fix**
  - **Problem**: 502 DSC_NC saved to DB but 0 to LDAP, error "No such object (32)" for `o=dsc_nc`
  - **Root Cause**: Line 2317 in main.cpp used `o=dsc_nc` but LDAP structure only has `o=dsc` under nc-data
  - **Fix**: Changed organizational unit from "dsc_nc" to "dsc" for DSC_NC certificates
  - **DN Format**: `cn={fingerprint},o=dsc,c={COUNTRY},dc=nc-data,dc=download,dc=pkd,...`
  - **Result**: All 502 DSC_NC certificates successfully saved to LDAP

**Feature: Upload Issues Tracking**

- ✅ **v2.1.2.2 - Upload Issues API**
  - New endpoint: `GET /api/upload/{uploadId}/issues`
  - Returns duplicate certificates detected during upload
  - Breakdown by certificate type (CSCA, DSC, DSC_NC, MLSC, CRL)
  - Frontend UI integration in Upload History page

- ✅ **v2.1.2.3 - Duplicate Count Accuracy Fix**
  - **Problem**: API returned 872 duplicates instead of 537 for Collection 002
  - **Root Cause**: Query returned all tracking records, not just actual duplicates
  - **Fix**: Added condition `first_upload_id != uploadId` to exclude first appearances
  - **Result**: Accurate duplicate count showing only certificates that failed registration

**Collection 002 Complete Analysis**

- ✅ **File Structure Verification**
  - Analyzed LDIF with OpenSSL asn1parse
  - Confirmed: 26 Master Lists containing 5,017 CSCA certificates
  - File size: 11,534,336 bytes (avg 2,299 bytes/cert including LDIF overhead)
  - Deduplication: 4,708 duplicates (94%), 309 new unique certificates

- ✅ **Upload Sequence Validation** (Complete System Data Upload)
  - Master List file: 537 certs (1 MLSC + 536 CSCA/LC) - 5 seconds
  - Collection 002 LDIF: 5,017 extracted → 309 new (4,708 duplicates) - 10 seconds
  - Collection 003 LDIF: 502 DSC_NC - 8 seconds
  - Collection 001 LDIF: 29,838 DSC + 69 CRL - 6 minutes 40 seconds
  - **Total: 31,281 certificates uploaded and verified**

**Final Verification Matrix**

| Certificate Type | DB Count | LDAP Count | Location | Status |
|------------------|----------|------------|----------|--------|
| CSCA (self-signed) | 735 | 735 | o=csca, dc=data | ✅ 100% Match |
| Link Certificates | 110 | 110 | o=lc, dc=data | ✅ 100% Match |
| MLSC | 27 | 27 | o=mlsc, dc=data | ✅ 100% Match |
| DSC | 29,838 | 29,838 | o=dsc, dc=data | ✅ 100% Match |
| DSC_NC | 502 | 502 | o=dsc, dc=nc-data | ✅ 100% Match |
| CRL | 69 | 69 | dc=data | ✅ 100% Match |
| **Total** | **31,281** | **31,281** | | ✅ **100% Match** |

**Files Modified**:

Backend:
- `services/pkd-management/src/main.cpp` - Lines 2317, 2528-2550, 6588-6686, 8992 (version)

Frontend:
- `frontend/src/types/index.ts` - Added UploadDuplicate, UploadIssues interfaces
- `frontend/src/services/pkdApi.ts` - Added getIssues() API call
- `frontend/src/pages/UploadHistory.tsx` - Added upload issues UI section

**Documentation**:
- `docs/COLLECTION_002_ANALYSIS.md` - Complete file structure analysis and upload verification results

### v2.0.6 (2026-01-25)

- **DSC_NC excluded from reconciliation** - ICAO deprecated nc-data in 2021
- ICAO standards compliance: nc-data is legacy only (pre-2021 uploads)
- PA Service verification: Does not use DSC_NC (DSC extracted from SOD)
- Reconciliation scope: CSCA, DSC, CRL only

### v2.0.5 (2026-01-25)
- CRL reconciliation support (findMissingCrlsInLdap, processCrls, addCrl)
- reconciliation_log UUID fix (cert_id INTEGER → cert_fingerprint VARCHAR)
- Development helper scripts (rebuild-pkd-relay.sh, ldap-helpers.sh, db-helpers.sh)

### v2.0.4 (2026-01-25)
- Auto parent DN creation in LDAP

### v2.0.3 (2026-01-24)
- Fingerprint-based DN format

### v2.0.0 (2026-01-21)
- Service separation (PKD Relay Service)
- Frontend sidebar reorganization

### v1.8.0 - v1.9.0 (Security Hardening)
- 100% Parameterized queries
- Credential externalization
- File upload security

---

## Key Architectural Decisions

### Database Schema
- UUIDs for primary keys (certificate.id, crl.id, uploaded_file.id)
- Fingerprint-based LDAP DNs (SHA-256 hex)
- Separate tables: certificate, crl, master_list
- Audit tables: reconciliation_summary, reconciliation_log, sync_status

### LDAP Strategy
- Read: Software Load Balancing (openldap1:389, openldap2:389)
- Write: Direct to primary (openldap1:389)
- DN format: `cn={FINGERPRINT},o={TYPE},c={COUNTRY},dc=data,...`
- Object classes: pkdDownload (certs), cRLDistributionPoint (CRLs)

### Reconciliation Logic
1. Find missing entities (stored_in_ldap=FALSE)
2. Verify against LDAP (actual existence check)
3. Add to LDAP with parent DN auto-creation
4. Mark as stored (stored_in_ldap=TRUE)
5. Log operations (reconciliation_log with fingerprint)

---

## Contact

For detailed information, see [docs/DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md)
