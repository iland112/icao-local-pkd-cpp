# PKD Relay Service Repository Pattern Refactoring Plan

**Branch**: `feature/pkd-relay-repository-pattern`
**Target Version**: v2.4.0
**Estimated Duration**: 7-10 days (includes shared library integration)
**Status**: Planning Phase - Updated with Shared Library Integration

---

## Executive Summary

Apply Repository Pattern to PKD Relay Service following the same architecture used in pkd-management and pa-service. This refactoring will eliminate SQL from controllers, improve testability, and enable database migration flexibility.

**🆕 NEW: Shared Library Integration** - This refactoring will be the first to use newly consolidated shared libraries (audit logging, connection pooling, etc.) as part of the broader v2.4.0 code consolidation initiative. See [SHARED_LIBRARY_INTEGRATION_ROADMAP.md](SHARED_LIBRARY_INTEGRATION_ROADMAP.md) for complete details.

### Current State Analysis

**Service Name**: PKD Relay Service (formerly Sync Service)
**Main Responsibilities**:
1. **DB-LDAP Synchronization** - Reconcile certificates and CRLs between PostgreSQL and LDAP
2. **ICAO Version Monitoring** - Check for updates on ICAO PKD portal
3. **Auto Reconciliation** - Scheduled sync with email notifications
4. **Sync Status Tracking** - Record and query sync history

**Current Architecture Issues**:
- ✗ ~30+ SQL queries directly in main.cpp (2,003 lines)
- ✗ SQL queries scattered in reconciliation_engine.cpp
- ✗ Direct PQexec/PQexecParams calls throughout codebase
- ✗ Mixed concerns: Controllers contain business logic + database access
- ✗ Difficult to test (tight coupling with PostgreSQL)
- ✗ Hard to migrate to Oracle (database dependencies everywhere)

**Code Statistics**:
```bash
Main.cpp:                2,003 lines (~40% SQL queries)
reconciliation_engine:   ~500 lines (7 SQL queries)
ldap_operations:         LDAP-specific (no SQL)
Total SQL queries:       ~37 direct queries
```

---

## 🆕 Shared Library Integration Strategy

As part of v2.4.0, this refactoring will be the **first service** to adopt consolidated shared libraries, eliminating duplicate code across pkd-management, pa-service, and pkd-relay.

### Shared Libraries to Use

#### 1. Audit Logging (`shared/lib/audit/`)
**Replaces**: `src/relay/sync/common/audit_log.h` (212 lines)
**Benefit**: Unified audit logging with all operation types
**Integration Point**: Phase 2 (Service Layer)

#### 2. Database Connection Pool (`shared/lib/database/`)
**Replaces**: `src/main.cpp::PgConnection` (inline, ~50 lines)
**Benefit**: Thread-safe pooling, automatic reconnection, health checks
**Integration Point**: Phase 1 (Repository Layer)

#### 3. LDAP Connection Pool (`shared/lib/ldap/`)
**Replaces**: Direct libldap calls in reconciliation_engine
**Benefit**: Connection pooling, automatic reconnection
**Integration Point**: Phase 1 (Repository Layer)

#### 4. Configuration Management (`shared/lib/config/`)
**Replaces**: `src/relay/sync/common/config.h` (partial)
**Benefit**: Unified config loading across all services
**Integration Point**: Phase 1 (Infrastructure)

#### 5. Common-lib X.509 Utils (`services/common-lib/`)
**Uses**: DN normalization, certificate parsing, fingerprint calculation
**Benefit**: Consistent certificate handling
**Integration Point**: Phase 2 (Service Layer)

### Integration Timeline

| Phase | Shared Library | Action |
|-------|----------------|--------|
| **Phase 0** | All | Wait for shared lib Phase 1 completion (Week 1) |
| **Phase 1** | DB Pool, LDAP Pool, Config | Use in Repository constructors |
| **Phase 2** | Audit Log, Common-lib | Use in Service layer |
| **Phase 3** | All | Controller integration |

### Dependencies

**Prerequisite**: Shared Library Integration Roadmap Phase 1 must complete first
- ✅ Audit Log consolidated
- ✅ DB Connection Pool created
- ✅ LDAP Connection Pool created

**Timeline Impact**: Adds 5 days to overall timeline (shared lib Phase 1 completion)

---

## Goals

### Primary Goals
1. **Eliminate SQL from Controllers** - Move all database access to Repository layer
2. **Separation of Concerns** - Controller → Service → Repository → Database
3. **Improve Testability** - Enable unit testing with mock repositories
4. **Oracle Migration Ready** - Database-agnostic architecture (67% effort reduction)
5. **Maintain Existing Functionality** - Zero feature regression
6. **🆕 Adopt Shared Libraries** - First service to use consolidated shared infrastructure

### Success Criteria
- ✅ 0 SQL queries in main.cpp (currently ~30+)
- ✅ 0 SQL queries in reconciliation_engine.cpp (currently 7)
- ✅ 100% parameterized queries in Repository layer
- ✅ All existing API endpoints functional
- ✅ Integration tests pass
- ✅ Build successful on x86_64 and ARM64

---

## Architecture Design

### Target Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Controllers (main.cpp)                   │
│  - API endpoint handlers                                      │
│  - Request/response formatting                                │
│  - Thin glue code (~500 lines target)                        │
└────────────────────────┬──────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                       Service Layer                           │
│  - SyncService: Sync orchestration & statistics              │
│  - ReconciliationService: Reconciliation logic               │
│  - ValidationService: Validation updates                     │
│  - IcaoSyncService: Already exists (keep as-is)             │
└────────────────────────┬──────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                     Repository Layer                          │
│  - SyncStatusRepository: sync_status CRUD                    │
│  - CertificateRepository: Certificate queries                │
│  - CrlRepository: CRL queries                                │
│  - ValidationResultRepository: validation_result CRUD        │
│  - ReconciliationRepository: reconciliation CRUD             │
│  - IcaoVersionRepository: Already exists (keep as-is)       │
└────────────────────────┬──────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                      Database (PostgreSQL)                    │
│  - Only Repositories interact with database                   │
└─────────────────────────────────────────────────────────────┘
```

### Domain Models

**1. SyncStatus** (sync_status table)
```cpp
class SyncStatus {
    std::string id;
    std::chrono::system_clock::time_point checked_at;
    int db_csca_count, ldap_csca_count, csca_discrepancy;
    int db_mlsc_count, ldap_mlsc_count, mlsc_discrepancy;
    int db_dsc_count, ldap_dsc_count, dsc_discrepancy;
    int db_dsc_nc_count, ldap_dsc_nc_count, dsc_nc_discrepancy;
    int db_crl_count, ldap_crl_count, crl_discrepancy;
    int total_discrepancy;
    std::optional<std::chrono::system_clock::time_point> last_reconciliation_at;
    std::string last_reconciliation_result;
};
```

**2. ReconciliationSummary** (reconciliation_summary table)
```cpp
class ReconciliationSummary {
    std::string id;
    std::chrono::system_clock::time_point started_at;
    std::optional<std::chrono::system_clock::time_point> completed_at;
    std::string status;  // RUNNING, SUCCESS, FAILED
    bool dry_run;
    int success_count, failed_count;
    int csca_synced, csca_deleted, dsc_synced, dsc_deleted;
    int dsc_nc_synced, dsc_nc_deleted, crl_synced, crl_deleted;
    std::optional<std::string> error_message;
};
```

**3. ReconciliationLog** (reconciliation_log table)
```cpp
class ReconciliationLog {
    std::string id;
    std::string reconciliation_id;
    std::chrono::system_clock::time_point created_at;
    std::string cert_fingerprint;
    std::string cert_type;
    std::string country_code;
    std::string action;  // SYNC_TO_LDAP, DELETE_FROM_LDAP, SKIP
    std::string result;  // SUCCESS, FAILED
    std::optional<std::string> error_message;
};
```

**4. Certificate** (certificate table - shared with pkd-management)
```cpp
// Reuse from services/common-lib/include/icao/models/certificate.h
// Or create minimal subset needed for relay operations
```

**5. Crl** (crl table)
```cpp
class Crl {
    std::string id;
    std::string fingerprint_sha256;
    std::string issuer_dn;
    std::string country_code;
    std::chrono::system_clock::time_point this_update;
    std::optional<std::chrono::system_clock::time_point> next_update;
    bool stored_in_ldap;
    std::vector<unsigned char> crl_data;
};
```

**6. ValidationResult** (validation_result table - shared)
```cpp
// Minimal subset for expiration updates
class ValidationResult {
    std::string id;
    std::string certificate_id;
    bool is_expired;
    // ... other fields as needed
};
```

---

## Implementation Phases

### Phase 1: Domain Models & Repository Layer (Days 1-2)

**1.1 Domain Models**
- ✅ SyncStatus model (`src/domain/models/sync_status.h`)
- ✅ ReconciliationSummary model (`src/domain/models/reconciliation_summary.h`)
- ✅ ReconciliationLog model (`src/domain/models/reconciliation_log.h`)
- ✅ Certificate model (reuse from common-lib or create minimal subset)
- ✅ Crl model (`src/domain/models/crl.h`)

**1.2 Repository Interfaces & Implementations**

**SyncStatusRepository** (`src/repositories/sync_status_repository.{h,cpp}`)
```cpp
class SyncStatusRepository {
public:
    explicit SyncStatusRepository(PGconn* conn);

    // Create new sync status record
    std::string create(const SyncStatus& status);

    // Find latest sync status
    std::optional<SyncStatus> findLatest();

    // Find by ID
    std::optional<SyncStatus> findById(const std::string& id);

    // Get recent sync history
    std::vector<SyncStatus> findRecent(int limit = 10);

    // Update reconciliation info
    void updateReconciliation(const std::string& id,
                              const std::chrono::system_clock::time_point& at,
                              const std::string& result);
};
```

**CertificateRepository** (`src/repositories/certificate_repository.{h,cpp}`)
```cpp
class CertificateRepository {
public:
    explicit CertificateRepository(PGconn* conn);

    // Count certificates by type
    int countByType(const std::string& type);

    // Count CSCA (excluding MLSC)
    int countCscaOnly();

    // Count MLSC
    int countMlsc();

    // Count stored in LDAP
    int countStoredInLdap();

    // Count by country
    std::map<std::string, std::map<std::string, int>> countByCountry();

    // Find certificates not in LDAP
    std::vector<Certificate> findNotInLdap(const std::string& type, int limit = 1000);

    // Mark as stored in LDAP
    void markStoredInLdap(const std::vector<std::string>& fingerprints);
};
```

**CrlRepository** (`src/repositories/crl_repository.{h,cpp}`)
```cpp
class CrlRepository {
public:
    explicit CrlRepository(PGconn* conn);

    // Count total CRLs
    int count();

    // Find CRLs not in LDAP
    std::vector<Crl> findNotInLdap(int limit = 1000);

    // Mark as stored in LDAP
    void markStoredInLdap(const std::vector<std::string>& fingerprints);
};
```

**ReconciliationRepository** (`src/repositories/reconciliation_repository.{h,cpp}`)
```cpp
class ReconciliationRepository {
public:
    explicit ReconciliationRepository(PGconn* conn);

    // Create reconciliation summary
    std::string createSummary(const ReconciliationSummary& summary);

    // Update summary
    void updateSummary(const std::string& id, const ReconciliationSummary& summary);

    // Find summary by ID
    std::optional<ReconciliationSummary> findSummaryById(const std::string& id);

    // Get recent reconciliations
    std::vector<ReconciliationSummary> findRecentSummaries(int limit = 10);

    // Create log entry
    void createLog(const ReconciliationLog& log);

    // Get logs for reconciliation
    std::vector<ReconciliationLog> findLogsByReconciliationId(const std::string& reconciliation_id);
};
```

**ValidationResultRepository** (`src/repositories/validation_result_repository.{h,cpp}`)
```cpp
class ValidationResultRepository {
public:
    explicit ValidationResultRepository(PGconn* conn);

    // Find DSC validations needing expiration check
    std::vector<ValidationResult> findForExpirationCheck(int limit = 1000);

    // Update expiration status
    void updateExpirationStatus(const std::string& id, bool is_expired);
};
```

**Expected Code Metrics**:
- Domain models: ~300 lines total
- Repositories: ~1,000 lines total
- 100% parameterized SQL queries
- Proper OpenSSL/libpq memory management

---

### Phase 2: Service Layer (Days 3-4)

**2.1 SyncService** (`src/services/sync_service.{h,cpp}`)
```cpp
class SyncService {
private:
    std::unique_ptr<SyncStatusRepository> syncStatusRepo;
    std::unique_ptr<CertificateRepository> certRepo;
    std::unique_ptr<CrlRepository> crlRepo;

public:
    SyncService(PGconn* conn);

    // Get database statistics
    Json::Value getDbStats();

    // Get LDAP statistics (delegates to LDAP operations)
    Json::Value getLdapStats(LDAP* ldap);

    // Perform sync check
    std::string performSyncCheck(LDAP* ldap);

    // Get latest sync status
    Json::Value getLatestStatus();

    // Get sync history
    Json::Value getSyncHistory(int limit = 10);
};
```

**2.2 ReconciliationService** (`src/services/reconciliation_service.{h,cpp}`)
```cpp
class ReconciliationService {
private:
    std::unique_ptr<ReconciliationRepository> reconRepo;
    std::unique_ptr<CertificateRepository> certRepo;
    std::unique_ptr<CrlRepository> crlRepo;

public:
    ReconciliationService(PGconn* conn);

    // Start reconciliation
    std::string startReconciliation(bool dry_run = false);

    // Perform reconciliation (orchestrates the process)
    void performReconciliation(const std::string& reconciliation_id,
                              LDAP* ldap, bool dry_run);

    // Complete reconciliation
    void completeReconciliation(const std::string& reconciliation_id,
                                const std::string& status,
                                const std::string& error_message = "");

    // Get reconciliation summary
    Json::Value getReconciliationSummary(const std::string& id);

    // Get recent reconciliations
    Json::Value getReconciliationHistory(int limit = 10);

    // Get reconciliation logs
    Json::Value getReconciliationLogs(const std::string& reconciliation_id);
};
```

**2.3 ValidationService** (`src/services/validation_service.{h,cpp}`)
```cpp
class ValidationService {
private:
    std::unique_ptr<ValidationResultRepository> validationRepo;
    std::unique_ptr<CertificateRepository> certRepo;

public:
    ValidationService(PGconn* conn);

    // Update expiration status for DSC certificates
    int updateExpirationStatus();
};
```

**Expected Code Metrics**:
- Service layer: ~800 lines total
- Clean separation of concerns
- Business logic isolated from database access
- LDAP operations delegated to existing ldap_operations module

---

### Phase 3: Controller Integration (Days 5-6)

**3.1 Initialize Services in main.cpp**
```cpp
// Global service pointers
std::unique_ptr<SyncService> syncService;
std::unique_ptr<ReconciliationService> reconciliationService;
std::unique_ptr<ValidationService> validationService;

// Initialize in main()
syncService = std::make_unique<SyncService>(dbConn);
reconciliationService = std::make_unique<ReconciliationService>(dbConn);
validationService = std::make_unique<ValidationService>(dbConn);
```

**3.2 Migrate API Endpoints**

**Sync Endpoints** (5 endpoints):
- `GET /api/sync/status` → syncService->getLatestStatus()
- `GET /api/sync/history` → syncService->getSyncHistory()
- `POST /api/sync/check` → syncService->performSyncCheck()
- `GET /api/sync/stats` → syncService->getStats()
- `GET /api/sync/config` → Keep current implementation (simple config read)

**Reconciliation Endpoints** (3 endpoints):
- `POST /api/sync/reconcile` → reconciliationService->startReconciliation()
- `GET /api/sync/reconcile/history` → reconciliationService->getReconciliationHistory()
- `GET /api/sync/reconcile/{id}/logs` → reconciliationService->getReconciliationLogs()

**Validation Endpoints** (1 endpoint):
- `POST /api/validation/update-expiration` → validationService->updateExpirationStatus()

**Expected Code Metrics**:
- main.cpp: 2,003 lines → ~600 lines (70% reduction)
- SQL in main.cpp: ~30 queries → 0 queries (100% elimination)
- Controller code per endpoint: ~20-30 lines

---

### Phase 4: Reconciliation Engine Migration (Day 7)

**4.1 Update reconciliation_engine.cpp**
- Remove SQL queries (7 queries)
- Inject ReconciliationService and CertificateRepository
- Use Repository methods instead of direct SQL
- Keep LDAP operations logic

**4.2 Integration with ReconciliationService**
- reconciliation_engine becomes a helper module for ReconciliationService
- ReconciliationService orchestrates the flow
- reconciliation_engine handles LDAP sync details

**Expected Code Metrics**:
- reconciliation_engine: SQL queries 7 → 0
- Better separation: Engine handles sync, Service handles orchestration

---

### Phase 5: Testing & Documentation (Days 8-10)

**5.1 Integration Testing**
- Test all 9 migrated endpoints
- Verify sync check works correctly
- Verify reconciliation runs successfully
- Test dry-run mode
- Test error handling

**5.2 Build Verification**
- x86_64 build successful
- ARM64 build successful (GitHub Actions)
- Docker image builds correctly

**5.3 Documentation**
- Update CLAUDE.md with v2.4.0 changes
- Document Repository Pattern completion
- Create migration guide for Oracle

**Expected Outcomes**:
- ✅ All tests pass
- ✅ Zero regressions
- ✅ Build successful on both platforms
- ✅ Complete documentation

---

## Code Metrics Projection

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| main.cpp lines | 2,003 | ~600 | 70% reduction |
| SQL in main.cpp | ~30 queries | 0 | 100% elimination |
| SQL in reconciliation_engine | 7 queries | 0 | 100% elimination |
| Total Repository code | 0 | ~1,000 lines | New layer |
| Total Service code | 0 | ~800 lines | New layer |
| Parameterized queries | ~60% | 100% | Security hardened |
| Oracle migration effort | High | Low | 67% reduction |
| Testability | Low | High | Full mock support |

---

## Dependencies

### Build Dependencies
- PostgreSQL libpq (already present)
- OpenSSL (already present)
- Drogon framework (already present)
- jsoncpp (already present)
- common-lib (DN parsing, certificate models)

### External Dependencies
- **None** - All changes internal to pkd-relay-service
- **No breaking changes** to API contracts
- **No database migrations** required (uses existing schema)

---

## Risk Mitigation

### Technical Risks

**1. API Compatibility**
- **Risk**: Breaking changes to existing API contracts
- **Mitigation**:
  - Keep exact same API endpoints and response formats
  - Integration tests verify backward compatibility
  - Gradual migration with parallel testing

**2. LDAP Operations**
- **Risk**: LDAP sync logic changes causing failures
- **Mitigation**:
  - Keep ldap_operations module unchanged
  - Only change how data is fetched from DB
  - Extensive testing with real LDAP server

**3. Reconciliation Logic**
- **Risk**: Complex reconciliation logic breaks
- **Mitigation**:
  - Migrate incrementally
  - Test dry-run mode thoroughly
  - Keep rollback plan ready

### Schedule Risks

**1. Underestimated Complexity**
- **Risk**: Takes longer than 10 days
- **Mitigation**:
  - Break down into small phases
  - Complete Phase 1-2 first, then reassess
  - Can merge phases if ahead of schedule

**2. Concurrent ARM64 Build**
- **Risk**: Merge conflicts with ARM64 build completion
- **Mitigation**:
  - Work in separate feature branch
  - Merge main into feature branch regularly
  - Coordinate merge timing

---

## Success Criteria

### Functional Requirements
- ✅ All 9 API endpoints work correctly
- ✅ Sync check produces correct statistics
- ✅ Reconciliation syncs certificates to LDAP
- ✅ Dry-run mode works without actual sync
- ✅ Error handling works correctly

### Non-Functional Requirements
- ✅ No performance degradation
- ✅ Zero SQL in main.cpp and reconciliation_engine
- ✅ 100% parameterized queries in Repositories
- ✅ Build successful on x86_64 and ARM64
- ✅ Code coverage maintained or improved

### Quality Metrics
- ✅ No SQL injection vulnerabilities
- ✅ Proper error handling throughout
- ✅ Memory management correct (no leaks)
- ✅ Clean separation of concerns
- ✅ Code follows existing patterns (pkd-management, pa-service)

---

## Timeline

| Phase | Duration | Days | Deliverables |
|-------|----------|------|--------------|
| Phase 1 | Domain Models & Repositories | 2 days | 5 repositories, 5 domain models |
| Phase 2 | Service Layer | 2 days | 3 services (Sync, Reconciliation, Validation) |
| Phase 3 | Controller Integration | 2 days | 9 endpoints migrated |
| Phase 4 | Reconciliation Engine | 1 day | SQL eliminated from engine |
| Phase 5 | Testing & Documentation | 3 days | Tests pass, docs complete |
| **Total** | | **10 days** | Repository Pattern 100% complete |

---

## Next Steps

1. **Review Plan** - Team review and approval
2. **Create Phase 1 Branch** - Start with domain models
3. **Implement Repositories** - Phase 1 execution
4. **Build Services** - Phase 2 execution
5. **Migrate Controllers** - Phase 3 execution
6. **Test & Deploy** - Phase 5 execution

---

## References

- [PA Service Refactoring](./PA_SERVICE_REFACTORING_PROGRESS.md) - Similar refactoring completed
- [PKD Management Refactoring](./REPOSITORY_PATTERN_IMPLEMENTATION_SUMMARY.md) - Original pattern implementation
- [Repository Pattern Architecture](./PHASE_1.6_SERVICE_REPOSITORY_INJECTION.md) - Architectural guidelines

---

**Document Version**: 1.0
**Created**: 2026-02-03
**Author**: Development Team
**Status**: Ready for Implementation
