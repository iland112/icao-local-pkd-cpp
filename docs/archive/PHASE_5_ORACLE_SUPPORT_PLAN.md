# Phase 5: PA Service & PKD Relay Oracle Support Implementation Plan

**Created**: 2026-02-05
**Estimated Duration**: 4-6 days
**Status**: 📋 PLANNED

---

## Executive Summary

Phase 5 extends Oracle database support to the remaining two services (pa-service and pkd-relay) by migrating their Repository layers to the Query Executor Pattern. This completes the system-wide Oracle migration, enabling full database independence across all three backend services.

### Objectives

1. ✅ **PA Service Oracle Support** - Migrate 2 repositories to Query Executor Pattern
2. ✅ **PKD Relay Oracle Support** - Migrate 4 repositories to Query Executor Pattern
3. ✅ **System-Wide Database Independence** - All services support PostgreSQL and Oracle
4. ✅ **Consistent Architecture** - Uniform Query Executor Pattern across all repositories

### Rationale

While Phase 4.6 performance benchmarking showed PostgreSQL is 10-53x faster for this workload, implementing full Oracle support provides:
- **Enterprise Flexibility**: Some organizations mandate Oracle for all databases
- **Vendor Independence**: No lock-in to specific database vendor
- **Consistent Architecture**: All services follow same Repository + Query Executor pattern
- **Future-Proofing**: Easy to add MySQL, MariaDB, SQL Server support later

---

## Phase 5.1: PA Service Query Executor Migration

**Duration**: 2-3 days
**Services**: pa-service
**Repositories to Migrate**: 2

### Current State Analysis

**PaVerificationRepository** (Critical):
- Uses raw PostgreSQL API: `PGconn*`, `PGresult*`, `PQexec`
- Methods: `save()`, `findById()`, `findByUploadId()`, `getStatistics()`
- Direct SQL with manual result parsing
- ~200 lines of PostgreSQL-specific code

**DataGroupRepository** (Critical):
- Uses raw PostgreSQL API: `PGconn*`, `PGresult*`, `PQexec`
- Methods: `save()`, `findByVerificationId()`, `deleteByVerificationId()`
- Direct SQL with manual result parsing
- ~150 lines of PostgreSQL-specific code

### Implementation Plan

#### Task 5.1.1: Create Query Executor Interface Wrapper

**Duration**: 1 hour

**Files to Create**:
- `services/pa-service/src/common/query_executor_interface.h` (symlink or copy from shared)

**Approach**:
- Reuse `IQueryExecutor` interface from pkd-management
- Link shared Query Executor implementation
- Verify CMakeLists.txt dependencies

#### Task 5.1.2: Refactor PaVerificationRepository

**Duration**: 6-8 hours

**Files to Modify**:
- `services/pa-service/src/repositories/pa_verification_repository.h`
- `services/pa-service/src/repositories/pa_verification_repository.cpp`

**Changes Required**:

1. **Constructor Update**:
```cpp
// BEFORE
PaVerificationRepository(const std::string& conninfo);

// AFTER
PaVerificationRepository(IQueryExecutor* executor);
```

2. **Method Refactoring**:
- `save()`: Convert to `executor->executeCommand()` with parameterized query
- `findById()`: Convert to `executor->executeQuery()` with UUID binding
- `findByUploadId()`: Convert with pagination support
- `getStatistics()`: Convert aggregation queries

3. **Result Parsing**:
- Replace `PQgetvalue()` with `result["field_name"]` JSON access
- Replace `PQntuples()` with `result.size()`
- Update all field name references to camelCase

**Example Transformation**:
```cpp
// BEFORE: Direct PostgreSQL
PGresult* res = PQexec(conn_,
    "SELECT id, upload_id, verification_status FROM pa_verification WHERE id = $1");
std::string id = PQgetvalue(res, 0, 0);

// AFTER: Query Executor
Json::Value params;
params["id"] = verificationId;
Json::Value result = executor_->executeQuery(
    "SELECT id, upload_id, verification_status FROM pa_verification WHERE id = $1",
    params
);
std::string id = result[0]["id"].asString();
```

#### Task 5.1.3: Refactor DataGroupRepository

**Duration**: 4-6 hours

**Files to Modify**:
- `services/pa-service/src/repositories/data_group_repository.h`
- `services/pa-service/src/repositories/data_group_repository.cpp`

**Changes Required**:
- Similar pattern to PaVerificationRepository
- Constructor: Accept `IQueryExecutor*`
- Methods: Convert to `executeCommand()` / `executeQuery()`
- BLOB handling: Convert bytea data properly

#### Task 5.1.4: Update Main Application Integration

**Duration**: 2-3 hours

**Files to Modify**:
- `services/pa-service/src/main.cpp`

**Changes Required**:

1. **Query Executor Initialization**:
```cpp
// After DbConnectionPool creation
std::shared_ptr<IQueryExecutor> queryExecutor;

if (dbType == "postgres") {
    queryExecutor = std::make_shared<PostgreSQLQueryExecutor>(dbPool.get());
} else if (dbType == "oracle") {
    queryExecutor = std::make_shared<OracleQueryExecutor>(dbPool.get());
}
```

2. **Repository Instantiation**:
```cpp
// Pass Query Executor instead of connection pool
auto paVerificationRepo = std::make_shared<PaVerificationRepository>(queryExecutor.get());
auto dataGroupRepo = std::make_shared<DataGroupRepository>(queryExecutor.get());
```

#### Task 5.1.5: Build & Test

**Duration**: 2-4 hours

**Test Plan**:
1. Build with PostgreSQL (verify no regression)
2. Test all PA endpoints with PostgreSQL
3. Switch to Oracle (DB_TYPE=oracle)
4. Test all PA endpoints with Oracle
5. Performance comparison (PostgreSQL vs Oracle)

**Test Endpoints**:
- POST /api/pa/verify
- GET /api/pa/history
- GET /api/pa/{id}
- GET /api/pa/statistics
- POST /api/pa/parse-sod
- POST /api/pa/parse-dg1
- POST /api/pa/parse-dg2
- POST /api/pa/parse-mrz-text

---

## Phase 5.2: PKD Relay Query Executor Migration

**Duration**: 2-3 days
**Services**: pkd-relay
**Repositories to Migrate**: 4

### Current State Analysis

**SyncStatusRepository**:
- Uses raw PostgreSQL API
- Methods: `create()`, `findLatest()`, `findAll()`, `count()`
- ~180 lines of PostgreSQL-specific code

**CertificateRepository**:
- Uses raw PostgreSQL API
- Methods: `countByType()`, `findNotInLdap()`, `markStoredInLdap()`
- ~150 lines of PostgreSQL-specific code

**CrlRepository**:
- Uses raw PostgreSQL API
- Methods: `countTotal()`, `findNotInLdap()`, `markStoredInLdap()`
- ~120 lines of PostgreSQL-specific code

**ReconciliationRepository**:
- Uses raw PostgreSQL API
- Methods: `createSummary()`, `updateSummary()`, `createLog()`, `findLogsByReconciliationId()`
- ~200 lines of PostgreSQL-specific code

### Implementation Plan

#### Task 5.2.1: Create Query Executor Interface Wrapper

**Duration**: 1 hour

**Files to Create/Modify**:
- Link shared Query Executor from `shared/lib/database/`
- Update `services/pkd-relay-service/CMakeLists.txt`

#### Task 5.2.2: Refactor SyncStatusRepository

**Duration**: 4-5 hours

**Files to Modify**:
- `services/pkd-relay-service/src/repositories/sync_status_repository.h`
- `services/pkd-relay-service/src/repositories/sync_status_repository.cpp`

**Changes Required**:
- Constructor: `IQueryExecutor*` instead of `DbConnectionPool*`
- Convert all methods to Query Executor pattern
- Update JSONB `country_stats` handling
- Replace manual timestamp parsing with Json::Value

#### Task 5.2.3: Refactor CertificateRepository

**Duration**: 3-4 hours

**Files to Modify**:
- `services/pkd-relay-service/src/repositories/certificate_repository.h`
- `services/pkd-relay-service/src/repositories/certificate_repository.cpp`

**Changes Required**:
- Similar pattern to SyncStatusRepository
- Handle batch updates in `markStoredInLdap()`
- Convert certificate type filtering

#### Task 5.2.4: Refactor CrlRepository

**Duration**: 3-4 hours

**Files to Modify**:
- `services/pkd-relay-service/src/repositories/crl_repository.h`
- `services/pkd-relay-service/src/repositories/crl_repository.cpp`

**Changes Required**:
- Similar pattern to CertificateRepository
- Handle bytea CRL data conversion

#### Task 5.2.5: Refactor ReconciliationRepository

**Duration**: 4-5 hours

**Files to Modify**:
- `services/pkd-relay-service/src/repositories/reconciliation_repository.h`
- `services/pkd-relay-service/src/repositories/reconciliation_repository.cpp`

**Changes Required**:
- Most complex repository (4 methods)
- Transaction handling for summary + logs
- UUID generation for reconciliation_id

#### Task 5.2.6: Update Main Application Integration

**Duration**: 2-3 hours

**Files to Modify**:
- `services/pkd-relay-service/src/main.cpp`

**Changes Required**:
- Initialize Query Executor based on DB_TYPE
- Pass Query Executor to all 4 repositories
- Update global repository pointers

#### Task 5.2.7: Build & Test

**Duration**: 2-4 hours

**Test Plan**:
1. Build with PostgreSQL
2. Test sync/reconciliation with PostgreSQL
3. Switch to Oracle
4. Test sync/reconciliation with Oracle
5. Verify DB-LDAP sync works correctly

**Test Endpoints**:
- GET /api/sync/status
- POST /api/sync/check
- GET /api/sync/history
- GET /api/sync/stats
- POST /api/sync/reconcile
- GET /api/sync/reconcile/history
- GET /api/sync/reconcile/:id

---

## Phase 5.3: System-Wide Integration Testing

**Duration**: 1 day

### Test Scenarios

#### Scenario 1: Full System PostgreSQL Test
1. Set `DB_TYPE=postgres` for all services
2. Upload LDIF file via pkd-management
3. Verify PA verification via pa-service
4. Trigger reconciliation via pkd-relay
5. Verify all data consistent

#### Scenario 2: Full System Oracle Test
1. Set `DB_TYPE=oracle` for all services
2. Upload LDIF file via pkd-management
3. Verify PA verification via pa-service
4. Trigger reconciliation via pkd-relay
5. Verify all data consistent

#### Scenario 3: Mixed Database Test (if supported)
1. pkd-management: Oracle
2. pa-service: PostgreSQL
3. pkd-relay: PostgreSQL
4. Verify cross-service communication

### Performance Benchmarking

**Endpoints to Benchmark** (PostgreSQL vs Oracle):
- pkd-management: Upload History, Certificate Search, Country Statistics
- pa-service: PA Verify, PA History, PA Statistics
- pkd-relay: Sync Status, Reconciliation History

**Expected Results**:
- PostgreSQL: 10-50x faster (based on Phase 4.6 results)
- Oracle: Functional but slower due to connection overhead

---

## Code Metrics Estimation

| Service | Repositories | Lines to Refactor | Estimated Hours |
|---------|--------------|-------------------|-----------------|
| pa-service | 2 | ~350 lines | 12-16 hours |
| pkd-relay | 4 | ~650 lines | 16-20 hours |
| Integration | - | - | 8 hours |
| **Total** | **6** | **~1000 lines** | **36-44 hours** |

**Calendar Time**: 4-6 days (with breaks and testing)

---

## Files to Create/Modify

### Phase 5.1: PA Service (10 files)

**Created** (0 files):
- None (reuse shared Query Executor)

**Modified** (10 files):
- `services/pa-service/src/repositories/pa_verification_repository.h`
- `services/pa-service/src/repositories/pa_verification_repository.cpp`
- `services/pa-service/src/repositories/data_group_repository.h`
- `services/pa-service/src/repositories/data_group_repository.cpp`
- `services/pa-service/src/services/pa_verification_service.h`
- `services/pa-service/src/services/pa_verification_service.cpp`
- `services/pa-service/src/main.cpp`
- `services/pa-service/CMakeLists.txt`
- `docker/docker-compose.yaml` (add Oracle env vars to pa-service)
- `.env` (Oracle configuration for pa-service)

### Phase 5.2: PKD Relay (12 files)

**Modified** (12 files):
- `services/pkd-relay-service/src/repositories/sync_status_repository.h`
- `services/pkd-relay-service/src/repositories/sync_status_repository.cpp`
- `services/pkd-relay-service/src/repositories/certificate_repository.h`
- `services/pkd-relay-service/src/repositories/certificate_repository.cpp`
- `services/pkd-relay-service/src/repositories/crl_repository.h`
- `services/pkd-relay-service/src/repositories/crl_repository.cpp`
- `services/pkd-relay-service/src/repositories/reconciliation_repository.h`
- `services/pkd-relay-service/src/repositories/reconciliation_repository.cpp`
- `services/pkd-relay-service/src/main.cpp`
- `services/pkd-relay-service/CMakeLists.txt`
- `docker/docker-compose.yaml` (add Oracle env vars to pkd-relay)
- `.env` (Oracle configuration for pkd-relay)

### Documentation (3 files)

**Created** (3 files):
- `docs/PHASE_5_ORACLE_SUPPORT_PLAN.md` (this document)
- `docs/PHASE_5.1_PA_SERVICE_COMPLETION.md` (after Phase 5.1)
- `docs/PHASE_5.2_PKD_RELAY_COMPLETION.md` (after Phase 5.2)

**Modified** (2 files):
- `CLAUDE.md` (Phase 5 entry)
- `docs/REPOSITORY_PATTERN_IMPLEMENTATION_SUMMARY.md` (update with Phase 5 results)

---

## Dependencies & Prerequisites

### Required Components (Already Implemented)

✅ **Shared Query Executor Library** (`shared/lib/database/`)
- `IQueryExecutor` interface
- `PostgreSQLQueryExecutor` implementation
- `OracleQueryExecutor` implementation
- Factory Pattern for Query Executor creation

✅ **Oracle Environment**
- Oracle XE 21c Docker container
- Oracle Instant Client in service containers
- OTL library for Oracle connectivity

✅ **Database Schemas**
- PostgreSQL schema (production data)
- Oracle schema (test data)

### CMakeLists.txt Dependencies

**pa-service**:
```cmake
target_link_libraries(${PROJECT_NAME} PRIVATE
    icao::database       # Shared Query Executor
    Drogon::Drogon
    PostgreSQL::PostgreSQL
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
)
```

**pkd-relay-service**:
```cmake
target_link_libraries(${PROJECT_NAME} PRIVATE
    icao::database       # Shared Query Executor
    icao::ldap           # Already linked
    Drogon::Drogon
    PostgreSQL::PostgreSQL
    spdlog::spdlog
)
```

---

## Risk Assessment

### Low Risk
- ✅ Pattern already proven in pkd-management (Phase 3)
- ✅ Shared Query Executor well-tested
- ✅ Repository interfaces already exist

### Medium Risk
- ⚠️ PA Service has complex BLOB handling (DG data)
- ⚠️ PKD Relay has transaction logic (reconciliation)
- ⚠️ Testing requires full Oracle schema population

### Mitigation Strategies

1. **Incremental Testing**: Test each repository after refactoring
2. **PostgreSQL First**: Ensure no regression with PostgreSQL
3. **Comprehensive Logging**: Add detailed logs for debugging
4. **Rollback Plan**: Keep PostgreSQL-only version in git history

---

## Success Criteria

### Phase 5.1 Success Criteria

✅ All 8 PA endpoints functional with PostgreSQL
✅ All 8 PA endpoints functional with Oracle
✅ Zero compilation errors
✅ Zero regression in PostgreSQL performance
✅ PA verification produces identical results on both databases

### Phase 5.2 Success Criteria

✅ All 7 sync/reconciliation endpoints functional with PostgreSQL
✅ All 7 sync/reconciliation endpoints functional with Oracle
✅ Zero compilation errors
✅ Zero regression in PostgreSQL performance
✅ Reconciliation produces identical results on both databases

### Overall Success Criteria

✅ **System-Wide Database Independence**: All 3 services support PostgreSQL and Oracle
✅ **Consistent Architecture**: All repositories use Query Executor Pattern
✅ **Zero Regressions**: All existing functionality works with PostgreSQL
✅ **Oracle Functionality**: All features work with Oracle (slower but functional)
✅ **Documentation Complete**: All completion reports and CLAUDE.md updated

---

## Timeline

| Phase | Task | Duration | Days |
|-------|------|----------|------|
| 5.1.1 | Query Executor Setup | 1 hour | 0.125 |
| 5.1.2 | PaVerificationRepository | 6-8 hours | 1 |
| 5.1.3 | DataGroupRepository | 4-6 hours | 0.75 |
| 5.1.4 | Main Integration | 2-3 hours | 0.375 |
| 5.1.5 | Build & Test | 2-4 hours | 0.5 |
| **Phase 5.1 Subtotal** | | **15-22 hours** | **2-3 days** |
| 5.2.1 | Query Executor Setup | 1 hour | 0.125 |
| 5.2.2 | SyncStatusRepository | 4-5 hours | 0.625 |
| 5.2.3 | CertificateRepository | 3-4 hours | 0.5 |
| 5.2.4 | CrlRepository | 3-4 hours | 0.5 |
| 5.2.5 | ReconciliationRepository | 4-5 hours | 0.625 |
| 5.2.6 | Main Integration | 2-3 hours | 0.375 |
| 5.2.7 | Build & Test | 2-4 hours | 0.5 |
| **Phase 5.2 Subtotal** | | **19-26 hours** | **2-3 days** |
| 5.3 | System Integration Testing | 8 hours | 1 |
| **Phase 5 Total** | | **42-56 hours** | **5-7 days** |

**Note**: Timeline assumes focused development time. Actual calendar time may vary with context switching and other tasks.

---

## Next Steps

After Phase 5 completion:

1. ✅ **Update CLAUDE.md** with Phase 5 completion summary
2. ✅ **Performance Comparison Report** for all 3 services (PostgreSQL vs Oracle)
3. ✅ **Production Deployment Guide** with database selection recommendations
4. ⏭️ **Optional**: MySQL/MariaDB support (if required)
5. ⏭️ **Optional**: SQL Server support (if required)

---

## References

- [PHASE_4.6_PERFORMANCE_COMPARISON_COMPLETION.md](PHASE_4.6_PERFORMANCE_COMPARISON_COMPLETION.md) - PostgreSQL vs Oracle benchmarks
- [PHASE_3.2_REPOSITORY_REFACTORING_COMPLETION.md](PHASE_3.2_REPOSITORY_REFACTORING_COMPLETION.md) - pkd-management Query Executor migration
- [REPOSITORY_PATTERN_IMPLEMENTATION_SUMMARY.md](REPOSITORY_PATTERN_IMPLEMENTATION_SUMMARY.md) - Architecture overview
- [shared/lib/database/README.md](../shared/lib/database/README.md) - Query Executor documentation

---

## Sign-off

**Status**: 📋 PLANNED
**Approval**: Pending user confirmation
**Start Date**: TBD (next session)
**Estimated Completion**: 5-7 days after start

**Prepared by**: Claude (Assistant)
**Date**: 2026-02-05
