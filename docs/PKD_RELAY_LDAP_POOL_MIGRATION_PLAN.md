# PKD Relay Service - LDAP Connection Pool Migration Plan

**Version**: v2.4.3
**Date**: 2026-02-04
**Author**: Claude Code
**Status**: PLANNING

---

## 🎯 Migration Objective

Migrate PKD Relay Service from direct LDAP connections to **LdapConnectionPool** (shared library) for:
- 50x performance improvement (connection reuse)
- Thread-safe connection management
- Automatic connection validation and retry
- Consistent architecture across all services

---

## ⚠️ Critical Constraints

### 1. **ZERO Frontend Changes**
- ✅ API response structures MUST remain identical
- ✅ HTTP status codes MUST remain the same
- ✅ JSON field names MUST NOT change
- ✅ Error response formats MUST remain consistent

### 2. **Backward Compatibility**
- ✅ All existing endpoints MUST continue working
- ✅ Database schema MUST NOT change
- ✅ Configuration format MUST remain compatible

### 3. **Testing Requirements**
- ✅ Build verification after each phase
- ✅ API endpoint testing before deployment
- ✅ Integration testing with frontend

---

## 📊 Current LDAP Usage Analysis

### Files Using LDAP Directly (9 files)

#### **High Priority - Core Sync Logic**
1. `src/relay/sync/reconciliation_engine.cpp` (250 lines)
   - `connectToLdapWrite()` - Returns `LDAP*` connection
   - `processCertificateType()` - Uses LDAP for adding certificates
   - `processCrls()` - Uses LDAP for adding CRLs
   - **Impact**: Core reconciliation functionality

2. `src/relay/sync/ldap_operations.cpp` (300 lines)
   - `addCertificate()` - Accepts `LDAP*` parameter
   - `deleteCertificate()` - Accepts `LDAP*` parameter
   - `addCrl()` - Accepts `LDAP*` parameter
   - `ensureParentDnExists()` - Accepts `LDAP*` parameter
   - **Impact**: All LDAP write operations

#### **Medium Priority - Monitoring**
3. `src/main.cpp` (100 lines LDAP-related)
   - `getLdapStats()` - Connects to LDAP for statistics
   - Round-robin LDAP host selection
   - **Impact**: Monitoring and health checks

#### **Lower Priority - Upload (Optional)**
4-9. Upload processing files (ldif_processor, processing_strategy)
   - **Note**: These are similar to pkd-management and can be deferred

---

## 🔄 Migration Strategy - Phased Approach

### **Phase 1: Add LDAP Connection Pool Dependency** ✅
**Estimated Time**: 10 minutes
**Risk Level**: LOW

**Tasks**:
1. Update `CMakeLists.txt`:
   - Add `icao::ldap` to `target_link_libraries`
   - Add `icao::config`, `icao::exception`, `icao::logging` (shared dependencies)
   - Similar to pkd-management migration

2. Verify build:
   ```bash
   cd docker
   docker-compose build pkd-relay-service
   ```

**Expected Output**:
- ✅ Build succeeds with new library links
- ✅ No compilation errors

**Rollback Plan**:
- Remove library links from CMakeLists.txt
- Rebuild with original configuration

---

### **Phase 2: Refactor ReconciliationEngine** 🔄
**Estimated Time**: 30 minutes
**Risk Level**: MEDIUM

**Current Pattern**:
```cpp
// reconciliation_engine.cpp (OLD)
LDAP* ld = connectToLdapWrite(errorMsg);
if (!ld) {
    // Error handling
}
processCertificateType(pgConn, ld, "CSCA", dryRun, result, reconciliationId);
ldap_unbind_ext_s(ld, nullptr, nullptr);
```

**New Pattern** (RAII):
```cpp
// reconciliation_engine.cpp (NEW)
auto conn = ldapPool_->acquire();  // RAII - auto-release on scope exit
if (!conn.isValid()) {
    // Error handling
}
processCertificateType(pgConn, conn.get(), "CSCA", dryRun, result, reconciliationId);
// Connection automatically released here
```

**Changes Required**:

#### **A. Header File (`reconciliation_engine.h`)**
```cpp
// BEFORE
#include <ldap.h>

class ReconciliationEngine {
public:
    explicit ReconciliationEngine(const Config& config);

private:
    LDAP* connectToLdapWrite(std::string& errorMsg) const;
    const Config& config_;
    std::unique_ptr<LdapOperations> ldapOps_;
};
```

```cpp
// AFTER
#include <ldap.h>
#include <ldap_connection_pool.h>  // NEW

class ReconciliationEngine {
public:
    // Constructor now accepts connection pool
    explicit ReconciliationEngine(
        const Config& config,
        common::LdapConnectionPool* ldapPool  // NEW parameter
    );

private:
    // REMOVED: LDAP* connectToLdapWrite(std::string& errorMsg) const;
    const Config& config_;
    common::LdapConnectionPool* ldapPool_;  // NEW member
    std::unique_ptr<LdapOperations> ldapOps_;
};
```

#### **B. Implementation File (`reconciliation_engine.cpp`)**

**Constructor**:
```cpp
// BEFORE
ReconciliationEngine::ReconciliationEngine(const Config& config)
    : config_(config)
    , ldapOps_(std::make_unique<LdapOperations>(config)) {
}
```

```cpp
// AFTER
ReconciliationEngine::ReconciliationEngine(
    const Config& config,
    common::LdapConnectionPool* ldapPool
)
    : config_(config)
    , ldapPool_(ldapPool)
    , ldapOps_(std::make_unique<LdapOperations>(config)) {

    if (!ldapPool_) {
        throw std::runtime_error("ReconciliationEngine: ldapPool cannot be null");
    }
}
```

**performReconciliation() method**:
```cpp
// BEFORE (lines ~50-100)
ReconciliationResult ReconciliationEngine::performReconciliation(
    PGconn* pgConn,
    bool dryRun,
    const std::string& triggeredBy,
    int syncStatusId
) {
    ReconciliationResult result;

    // Connect to LDAP
    std::string ldapError;
    LDAP* ld = connectToLdapWrite(ldapError);
    if (!ld) {
        spdlog::error("LDAP connection failed: {}", ldapError);
        result.success = false;
        result.errorMsg = ldapError;
        return result;
    }

    try {
        // ... process certificates ...
        processCertificateType(pgConn, ld, "CSCA", dryRun, result, reconciliationId);
        processCertificateType(pgConn, ld, "DSC", dryRun, result, reconciliationId);
        processCrls(pgConn, ld, dryRun, result, reconciliationId);

    } catch (...) {
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        throw;
    }

    ldap_unbind_ext_s(ld, nullptr, nullptr);
    return result;
}
```

```cpp
// AFTER (RAII pattern)
ReconciliationResult ReconciliationEngine::performReconciliation(
    PGconn* pgConn,
    bool dryRun,
    const std::string& triggeredBy,
    int syncStatusId
) {
    ReconciliationResult result;

    // Acquire LDAP connection from pool (RAII - auto-release)
    auto conn = ldapPool_->acquire();
    if (!conn.isValid()) {
        std::string errorMsg = "Failed to acquire LDAP connection from pool";
        spdlog::error(errorMsg);
        result.success = false;
        result.errorMsg = errorMsg;
        return result;
    }

    // Use connection (no try-catch needed - RAII handles cleanup)
    processCertificateType(pgConn, conn.get(), "CSCA", dryRun, result, reconciliationId);
    processCertificateType(pgConn, conn.get(), "DSC", dryRun, result, reconciliationId);
    processCrls(pgConn, conn.get(), dryRun, result, reconciliationId);

    // Connection automatically released on scope exit
    return result;
}
```

**Remove connectToLdapWrite() method**:
- Delete entire method implementation
- Remove from header file

**Testing Checkpoints**:
1. ✅ Build succeeds
2. ✅ ReconciliationEngine constructor compiles
3. ✅ performReconciliation() compiles
4. ✅ No LDAP connection leaks (verified by pool statistics)

---

### **Phase 3: Update main.cpp Initialization** 🔄
**Estimated Time**: 15 minutes
**Risk Level**: HIGH (Service startup failure if wrong order)

**Current Pattern**:
```cpp
// main.cpp (OLD)
int main() {
    // ... config loading ...

    // Initialize ReconciliationEngine
    auto reconciliationEngine = std::make_shared<icao::relay::ReconciliationEngine>(g_config);

    // ... setup endpoints ...
}
```

**New Pattern**:
```cpp
// main.cpp (NEW)
int main() {
    // ... config loading ...

    // CRITICAL: Create LDAP connection pool FIRST
    std::shared_ptr<common::LdapConnectionPool> ldapPool;
    try {
        std::string ldapWriteUri = "ldap://" + g_config.ldapWriteHost + ":" +
                                    std::to_string(g_config.ldapWritePort);

        ldapPool = std::make_shared<common::LdapConnectionPool>(
            ldapWriteUri,
            g_config.ldapBindDn,
            g_config.ldapBindPassword,
            2,   // minConnections
            10,  // maxConnections
            5    // acquireTimeoutSec
        );

        spdlog::info("LDAP connection pool initialized (min=2, max=10, host={})", ldapWriteUri);
    } catch (const std::exception& e) {
        spdlog::critical("Failed to initialize LDAP connection pool: {}", e.what());
        return 1;
    }

    // Initialize ReconciliationEngine with pool
    auto reconciliationEngine = std::make_shared<icao::relay::ReconciliationEngine>(
        g_config,
        ldapPool.get()  // Pass non-owning pointer
    );

    // ... setup endpoints ...
}
```

**⚠️ CRITICAL**: Initialization order MUST be:
1. LDAP Connection Pool
2. ReconciliationEngine (uses pool)
3. Endpoints registration

**Common Mistake** (from pkd-management experience):
- ❌ Creating ReconciliationEngine before LDAP pool
- ❌ Result: "ldapPool cannot be null" error

**Testing Checkpoints**:
1. ✅ Service starts without errors
2. ✅ Log shows "LDAP connection pool initialized"
3. ✅ Log shows "ReconciliationEngine initialized"
4. ✅ No "ldapPool cannot be null" error

---

### **Phase 4: Update getLdapStats() (Optional)** 🔄
**Estimated Time**: 10 minutes
**Risk Level**: LOW

**Current Pattern**:
```cpp
LdapStats getLdapStats() {
    // ... manual LDAP connection ...
    LDAP* ld = nullptr;
    ldap_initialize(&ld, uri.c_str());
    ldap_bind_s(ld, ...);
    // ... query stats ...
    ldap_unbind_ext_s(ld, nullptr, nullptr);
}
```

**New Pattern**:
```cpp
LdapStats getLdapStats() {
    auto conn = g_ldapPool->acquire();
    if (!conn.isValid()) {
        // Error handling
    }
    // ... query stats using conn.get() ...
    // Connection auto-released
}
```

**Note**: This is optional and can be deferred to Phase 5.

---

### **Phase 5: Testing & Verification** ✅
**Estimated Time**: 30 minutes
**Risk Level**: CRITICAL

#### **A. Build Verification**
```bash
cd /home/kbjung/projects/c/icao-local-pkd/docker
docker-compose build --no-cache pkd-relay-service
```

**Expected Output**:
- ✅ Exit code 0
- ✅ No compilation errors
- ✅ All libraries linked correctly

#### **B. Service Startup Verification**
```bash
docker-compose up -d pkd-relay-service
docker-compose logs pkd-relay-service | tail -50
```

**Expected Logs**:
```
[info] LDAP connection pool initialized (min=2, max=10, host=ldap://openldap1:389)
[info] ReconciliationEngine initialized
[info] SyncService initialized
[info] ReconciliationService initialized
[info] Server starting on http://0.0.0.0:8083
```

**Error Indicators** (MUST NOT appear):
- ❌ "ldapPool cannot be null"
- ❌ "Failed to acquire LDAP connection"
- ❌ Segmentation fault
- ❌ LDAP connection timeout

#### **C. API Endpoint Testing**

**Test 1: Sync Status Endpoint**
```bash
curl -s http://localhost:8080/api/sync/status | jq .
```

**Expected Response Structure** (MUST remain identical):
```json
{
  "success": true,
  "data": {
    "id": 1,
    "checkedAt": "2026-02-04T13:00:00Z",
    "dbCscaCount": 814,
    "ldapCscaCount": 814,
    "cscaDiscrepancy": 0,
    "dbMlscCount": 27,
    "ldapMlscCount": 27,
    "mlscDiscrepancy": 0,
    "dbDscCount": 29838,
    "ldapDscCount": 29838,
    "dscDiscrepancy": 0,
    "dbDscNcCount": 502,
    "ldapDscNcCount": 502,
    "dscNcDiscrepancy": 0,
    "dbCrlCount": 69,
    "ldapCrlCount": 69,
    "crlDiscrepancy": 0,
    "totalDbCount": 31250,
    "totalLdapCount": 31250,
    "totalDiscrepancy": 0,
    "countryStats": { ... }
  }
}
```

**Test 2: Trigger Reconciliation**
```bash
curl -X POST http://localhost:8080/api/sync/reconcile \
  -H "Content-Type: application/json" \
  -d '{"dryRun": true}' | jq .
```

**Expected Response Structure** (MUST remain identical):
```json
{
  "success": true,
  "data": {
    "reconciliationId": 1,
    "totalProcessed": 0,
    "totalSuccess": 0,
    "totalFailed": 0,
    "cscaAdded": 0,
    "dscAdded": 0,
    "dscNcAdded": 0,
    "crlAdded": 0,
    "durationMs": 50,
    "triggeredBy": "MANUAL",
    "dryRun": true
  }
}
```

**Test 3: Reconciliation History**
```bash
curl -s http://localhost:8080/api/sync/reconcile/history?limit=5 | jq .
```

**Expected Response Structure** (MUST remain identical):
```json
{
  "success": true,
  "data": [
    {
      "id": 1,
      "startedAt": "2026-02-04T13:00:00Z",
      "completedAt": "2026-02-04T13:00:05Z",
      "totalProcessed": 100,
      "totalSuccess": 98,
      "totalFailed": 2,
      "cscaAdded": 10,
      "dscAdded": 80,
      "crlAdded": 8,
      "triggeredBy": "MANUAL",
      "dryRun": false
    }
  ],
  "count": 5,
  "total": 50
}
```

#### **D. Performance Verification**

**Before Migration** (baseline):
```bash
# Measure reconciliation time
time curl -X POST http://localhost:8080/api/sync/reconcile -d '{"dryRun": true}'
```

**After Migration** (should be faster or same):
```bash
# Same test
time curl -X POST http://localhost:8080/api/sync/reconcile -d '{"dryRun": true}'
```

**Expected**: No performance degradation (ideally 10-50% faster)

#### **E. Connection Pool Statistics**

Check LDAP connection pool metrics in logs:
```bash
docker-compose logs pkd-relay-service | grep -i "pool"
```

**Expected Patterns**:
- ✅ "Connection pool initialized"
- ✅ "Acquired connection from pool"
- ✅ "Released connection to pool"
- ✅ No "Pool exhausted" messages

---

## 🚨 Rollback Plan

If ANY test fails in Phase 5:

### **Immediate Rollback Steps**:
1. Stop service:
   ```bash
   docker-compose stop pkd-relay-service
   ```

2. Restore original code:
   ```bash
   git checkout services/pkd-relay-service/
   ```

3. Rebuild original version:
   ```bash
   docker-compose build pkd-relay-service
   docker-compose up -d pkd-relay-service
   ```

4. Verify rollback:
   ```bash
   curl -s http://localhost:8080/api/sync/status
   ```

### **Root Cause Analysis Required For**:
- ❌ Build failures
- ❌ Service startup failures
- ❌ API response structure changes
- ❌ Performance degradation > 20%
- ❌ Connection pool exhaustion

---

## 📝 Implementation Checklist

### Phase 1: Dependencies ✅
- [ ] Update CMakeLists.txt with icao::ldap
- [ ] Add icao::config, icao::exception, icao::logging
- [ ] Build verification (docker-compose build)
- [ ] No compilation errors

### Phase 2: ReconciliationEngine Refactor ✅
- [ ] Update reconciliation_engine.h (add ldapPool parameter)
- [ ] Update reconciliation_engine.cpp constructor
- [ ] Refactor performReconciliation() to use pool (RAII pattern)
- [ ] Remove connectToLdapWrite() method
- [ ] Build verification
- [ ] No compilation errors

### Phase 3: main.cpp Initialization ✅
- [ ] Create LDAP connection pool (BEFORE ReconciliationEngine)
- [ ] Update ReconciliationEngine construction
- [ ] Verify initialization order
- [ ] Build verification
- [ ] Service startup test

### Phase 4: getLdapStats() (Optional) ⏭️
- [ ] Refactor to use connection pool
- [ ] Build verification

### Phase 5: Testing & Verification ✅
- [ ] Build with --no-cache
- [ ] Service startup logs check
- [ ] API endpoint testing (3 endpoints)
- [ ] Response structure verification (JSON field matching)
- [ ] Performance comparison
- [ ] Connection pool statistics
- [ ] Frontend integration test (Sync Dashboard page)

---

## 🎓 Lessons Learned (from pkd-management)

### ✅ What Worked Well:
1. **RAII Pattern**: Automatic connection release prevents leaks
2. **Phased Approach**: Incremental changes reduce risk
3. **Thorough Testing**: Catch issues early

### ❌ Common Pitfalls to Avoid:
1. **Wrong Initialization Order**: MUST create pool before engine
2. **Null Pointer Issues**: Always validate pool in constructor
3. **API Response Changes**: NEVER change JSON structure
4. **Cache Issues**: Use --no-cache for final build

---

## 📊 Expected Benefits

### Performance:
- 🚀 **50x faster**: LDAP queries (50ms → 1ms)
- 🚀 **10x faster**: Bulk reconciliation (30min → 3min for 30k certs)

### Reliability:
- ✅ Thread-safe connection management
- ✅ Automatic connection validation
- ✅ Connection retry on failure

### Maintainability:
- ✅ Consistent architecture across services
- ✅ Easier testing (mockable connection pool)
- ✅ Cleaner code (RAII pattern)

---

## 🎯 Next Steps

1. **Review this plan with user** ✅
2. **Get approval to proceed** ⏳
3. **Execute Phase 1** (Dependencies)
4. **Execute Phase 2** (ReconciliationEngine)
5. **Execute Phase 3** (main.cpp)
6. **Execute Phase 5** (Testing)
7. **Document completion** ✅

---

**END OF MIGRATION PLAN**
