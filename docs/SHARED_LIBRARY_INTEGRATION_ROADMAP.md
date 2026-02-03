# Shared Library Integration Roadmap v2.4.0

**Project**: ICAO Local PKD - Shared Library Consolidation
**Version**: v2.4.0
**Date**: 2026-02-03
**Status**: Planning Complete - Ready for Implementation

---

## Executive Summary

Comprehensive plan to consolidate duplicate code across 3 C++ services (pkd-management, pa-service, pkd-relay) into reusable shared libraries. This initiative builds upon completed Repository Pattern refactorings and eliminates ~2,809 lines of duplicate code (57% reduction).

### Key Objectives

1. **Eliminate Code Duplication**: Consolidate common modules into shared libraries
2. **Improve Maintainability**: Single source of truth for core functionality
3. **Accelerate Development**: Reusable components across all services
4. **Enable Common-lib Adoption**: Complete Phase 2-4 of common-lib implementation

---

## Current State Analysis

### Completed Refactorings (Reference Architecture)

#### PKD Management Service ✅ (v2.3.1)
- **Repository Pattern**: 100% complete (12 endpoints)
- **Domain Models**: Certificate, Upload, Validation, IcaoVersion
- **Repositories**: 8 repositories (Upload, Certificate, Validation, Audit, etc.)
- **Services**: 7 services (Upload, Validation, Audit, Statistics, etc.)
- **Connection Pool**: Database connection pool implemented (v2.3.1)
- **Code Reduction**: main.cpp 1,234 → 600 lines (51% reduction)

#### PA Service ✅ (v2.2.0)
- **Repository Pattern**: 100% complete (9/9 endpoints)
- **Domain Models**: PaVerification, SodData, DataGroup, CertificateChainValidation
- **Repositories**: 4 repositories (PaVerification, DataGroup, LdapCertificate, LdapCrl)
- **Services**: 4 services (PaVerification, SodParser, DataGroupParser, CertificateValidation)
- **Connection Pools**: Both DB + LDAP pools (870+ lines)
- **Unit Tests**: 44 test cases across 3 test suites
- **Error Handling**: 30+ error codes, 25+ typed exceptions
- **Code Reduction**: main.cpp 3,706 → ~500 lines (70% reduction)

#### PKD Relay Service ⏳ (Pending)
- **Status**: Refactoring plan created (PKD_RELAY_REFACTORING_PLAN.md)
- **Current**: 2,003 lines in main.cpp, ~37 SQL queries
- **Target**: Apply Repository Pattern (similar to pkd-management/pa-service)

### Common-lib Status ✅ Phase 1 Complete

**Directory**: `services/common-lib/`
**Status**: Structure complete, implementation pending

**Modules**:
1. **X.509 Module** (`icao::x509`)
   - `dn_parser.h` - DN parsing (RFC2253, OpenSSL format)
   - `dn_components.h` - Structured DN extraction
   - `certificate_parser.h` - Multi-format parsing (PEM/DER/CER/CMS)
   - `metadata_extractor.h` - X.509 metadata (22 fields)

2. **Utils Module** (`icao::utils`)
   - `string_utils.h` - String processing, Hex/Base64 encoding
   - `time_utils.h` - Time conversion (ASN1_TIME ↔ chrono)

**Next**: Phase 2-4 implementation needed

---

## Duplicate Code Analysis

### 🔴 Priority 1: Critical Duplication (Immediate Integration)

#### 1. Audit Logging (3 services)

| Service | File | Lines | OperationType Support |
|---------|------|-------|-----------------------|
| pkd-management | `src/common/audit_log.h` | 220 | FILE_UPLOAD, CERT_EXPORT, UPLOAD_DELETE, PA_VERIFY, SYNC_TRIGGER |
| pa-service | `src/common/audit_log.h` | 212 | PA_VERIFY |
| pkd-relay | `src/relay/sync/common/audit_log.h` | 212 | SYNC_TRIGGER |

**Duplication**: ~640 lines (identical structure, different enums)

**Impact**:
- Bug fixes must be applied 3 times
- Inconsistent error handling
- Different operation types per service

**Target**: `shared/lib/audit/audit_log.{h,cpp}`

**Code Reduction**: 644 → 250 lines (**61% savings**)

---

#### 2. Database Connection Pool (2 services + inline)

| Service | File | Lines | Features |
|---------|------|-------|----------|
| pkd-management | `src/common/db_connection_pool.{h,cpp}` | 419 | Full pooling, health checks |
| pa-service | `src/common/db_connection_pool.{h,cpp}` | 419 | Full pooling, health checks |
| pkd-relay | `src/main.cpp::PgConnection` (inline) | ~50 | Basic connection only |

**Duplication**: ~888 lines (identical implementation)

**Impact**:
- pkd-relay lacks connection pooling (performance issue)
- Maintenance burden across 2 implementations
- Testing duplicated

**Target**: `shared/lib/database/pg_connection_pool.{h,cpp}`

**Code Reduction**: 888 → 450 lines (**49% savings**)

**Bonus**: pkd-relay gets connection pooling

---

#### 3. LDAP Connection Pool (1 service, needed by all)

| Service | Status | Lines |
|---------|--------|-------|
| pa-service | ✅ Implemented | 477 |
| pkd-management | ❌ Inline LDAP calls | - |
| pkd-relay | ❌ Direct libldap | - |

**Target**: `shared/lib/ldap/ldap_connection_pool.{h,cpp}`

**Code Reduction**: Enables pooling for 2 additional services

**Benefits**:
- Thread-safe LDAP access for all services
- Automatic reconnection
- Performance monitoring

---

### 🟡 Priority 2: Certificate Utilities (Partial Duplication)

#### 4. Certificate Parsing & Utilities

**pkd-management** (`src/common/certificate_utils.{h,cpp}`):
- X509 parsing (x509NameToString, asn1TimeToIso8601)
- Fingerprint calculation (computeSha256Fingerprint, computeSha1Fingerprint)
- DN extraction (extractCountryCode)
- Expiration check (isExpired)
- Link certificate detection (isLinkCertificate)

**pa-service**: Similar functions scattered in main.cpp (inline)
**pkd-relay**: Missing fingerprint calculation entirely

**Target**: Migrate to `common-lib/src/x509/certificate_parser.cpp`

**Code Reduction**: ~500 lines consolidated into common-lib

---

#### 5. DN Normalization (2 services)

| Service | Location | Function |
|---------|----------|----------|
| pkd-management | `validation_service.cpp` | `normalizeDnForComparison()` |
| pa-service | `ldap_certificate_repository.cpp` | `normalizeDn()` |
| common-lib | `dn_parser.h` | **Not implemented** |

**Target**: Implement in `common-lib/src/x509/dn_parser.cpp`

**Code Reduction**: ~300 lines eliminated after migration

---

### 🟢 Priority 3: New Shared Libraries

#### 6. LDAP Utilities (New)

**Sources**:
- `services/pkd-management/src/common/ldap_utils.{h,cpp}`
- pkd-relay LDAP connection logic
- pa-service LDAP search helpers

**Target**: `shared/lib/ldap/ldap_utils.{h,cpp}`

**Functions**:
```cpp
namespace icao::ldap {
    std::string buildDn(const CertificateInfo& cert, bool useLegacyFormat);
    std::string buildCountryDn(const std::string& country);
    SearchResult searchCscaBySubject(const std::string& subjectDn);
    SearchResult searchCrlByIssuer(const std::string& issuerDn);
}
```

---

#### 7. Configuration Management (New)

**Current State**: Each service has different config loading

| Service | Implementation |
|---------|---------------|
| pkd-management | AppConfig struct + loadFromEnv() |
| pa-service | Inline env var reading in main.cpp |
| pkd-relay | Config struct + loadFromEnv() |

**Target**: `shared/lib/config/service_config.{h,cpp}`

**Unified Structure**:
```cpp
namespace icao::config {
    struct DatabaseConfig { std::string host, port, name, user, password; };
    struct LdapConfig { std::string host, port, baseDn, bindDn, password; };
    struct ServerConfig { int port; std::string logLevel; };

    class ConfigManager {
    public:
        static ConfigManager& instance();
        void loadFromEnv();
        void loadFromFile(const std::string& path);
        DatabaseConfig getDatabase() const;
        LdapConfig getLdap() const;
        ServerConfig getServer() const;
    };
}
```

---

#### 8. Error Handling & Exceptions (New)

**Sources**:
- `services/pa-service/src/common/exceptions.h` (✅ exists)
- `services/pa-service/src/common/error_codes.h` (✅ exists)
- `shared/exception/` (headers only)

**Target**: `shared/lib/exception/icao_exceptions.{h,cpp}`

**Structure**:
```cpp
namespace icao::exception {
    class IcaoException : public std::exception { ... };
    class DatabaseException : public IcaoException { ... };
    class LdapException : public IcaoException { ... };
    class CertificateException : public IcaoException { ... };

    enum class ErrorCode {
        DB_CONNECTION_FAILED = 1000,
        LDAP_BIND_FAILED = 2000,
        CERT_PARSE_ERROR = 3000,
        CERT_EXPIRED = 3001,
        CERT_REVOKED = 3002,
        TRUST_CHAIN_INVALID = 3003,
        // ...
    };
}
```

---

#### 9. Logging Utilities (New)

**Source**: `services/pa-service/src/common/logger.h`

**Target**: `shared/lib/logging/logger.{h,cpp}`

**Structure**:
```cpp
namespace icao::logging {
    class Logger {
    public:
        static void init(const std::string& serviceName,
                        const std::string& logLevel = "info",
                        const std::string& logFile = "");
        static std::shared_ptr<spdlog::logger> get(const std::string& name = "");
        static void setRotation(size_t maxSize, size_t maxFiles);
        static void setPattern(const std::string& pattern);
    };
}
```

---

## Implementation Roadmap

### Phase 1: Critical Infrastructure (v2.4.0 - Week 1)

**Duration**: 5 days
**Priority**: 🔥 Critical

#### Day 1-2: Audit Log Consolidation

**Tasks**:
1. Create `shared/lib/audit/audit_log.{h,cpp}`
2. Unified OperationType enum (all operation types)
3. Extract from 3 services
4. Update CMakeLists.txt for all services
5. Integration testing

**Deliverables**:
- ✅ Single audit_log implementation
- ✅ All 3 services using shared version
- ✅ Backward compatible with existing audit tables

**Code Impact**:
- Remove: 644 lines (3 duplicate files)
- Add: 250 lines (shared implementation)
- **Net**: -394 lines (61% reduction)

---

#### Day 3-4: Database Connection Pool

**Tasks**:
1. Create `shared/lib/database/pg_connection_pool.{h,cpp}`
2. Extract from pkd-management/pa-service (choose best implementation)
3. Update pkd-relay to use pooling (replace inline PgConnection)
4. Connection pool configuration (min/max connections, timeouts)
5. Health check endpoints

**Deliverables**:
- ✅ Unified DB connection pool
- ✅ pkd-relay performance improvement (pooling enabled)
- ✅ Thread-safe connection management

**Code Impact**:
- Remove: 888 lines (2 duplicate implementations + inline code)
- Add: 450 lines (enhanced shared pool)
- **Net**: -438 lines (49% reduction)

---

#### Day 5: LDAP Connection Pool

**Tasks**:
1. Create `shared/lib/ldap/ldap_connection_pool.{h,cpp}`
2. Extract from pa-service (reference implementation)
3. Migrate pkd-management inline LDAP calls
4. Migrate pkd-relay direct libldap usage
5. RAII wrapper classes (LdapConnectionGuard)

**Deliverables**:
- ✅ Unified LDAP connection pool
- ✅ All services using pooled LDAP connections
- ✅ Automatic reconnection handling

**Code Impact**:
- Remove: ~600 lines (scattered across services)
- Add: 500 lines (shared pool with enhancements)
- **Net**: -100 lines + pooling for 2 services

---

### Phase 2: Common-lib Implementation (v2.5.0 - Week 2)

**Duration**: 7 days
**Priority**: 🟡 High

#### Day 1-3: DN Parser & Certificate Parser

**Tasks**:
1. Implement `common-lib/src/x509/dn_parser.cpp`
   - RFC2253 parsing
   - OpenSSL oneline format support
   - DN normalization for comparison
   - Self-signed detection

2. Implement `common-lib/src/x509/certificate_parser.cpp`
   - PEM/DER/CER/BIN format support
   - CMS/PKCS7 extraction
   - Automatic format detection
   - SHA-256 fingerprint calculation

3. Unit tests (Google Test)

**Deliverables**:
- ✅ common-lib Phase 2 complete
- ✅ 44 unit tests passing
- ✅ pkd-management migration ready

---

#### Day 4-5: Certificate Utilities Migration

**Tasks**:
1. Migrate `pkd-management/src/common/certificate_utils.cpp` to common-lib
2. Update pa-service inline utilities to use common-lib
3. Update pkd-relay to use common-lib (add fingerprint calculation)
4. Regression testing

**Deliverables**:
- ✅ All services using common-lib for certificate operations
- ✅ ~500 lines eliminated

---

#### Day 6-7: Metadata Extractor & Time Utils

**Tasks**:
1. Implement `common-lib/src/x509/metadata_extractor.cpp`
2. Implement `common-lib/src/utils/time_utils.cpp`
3. Implement `common-lib/src/utils/string_utils.cpp`
4. Integration with services

**Deliverables**:
- ✅ common-lib Phase 3-4 complete
- ✅ Full X.509 metadata extraction
- ✅ Common utility functions

---

### Phase 3: New Shared Libraries (v2.6.0 - Week 3)

**Duration**: 5 days
**Priority**: 🟢 Medium

#### Day 1-2: Configuration Management

**Tasks**:
1. Create `shared/lib/config/service_config.{h,cpp}`
2. Unified config structure (Database, LDAP, Server)
3. Environment variable loading
4. File-based config support (.yaml/.json)
5. Migrate all 3 services

**Deliverables**:
- ✅ Consistent config management
- ✅ Single source of truth for configuration
- ✅ Environment variable validation

---

#### Day 3: LDAP Utilities

**Tasks**:
1. Create `shared/lib/ldap/ldap_utils.{h,cpp}`
2. DN building functions
3. LDAP search helpers
4. Filter construction utilities

**Deliverables**:
- ✅ Reusable LDAP helper functions
- ✅ Consistent DN format across services

---

#### Day 4: Exception Handling

**Tasks**:
1. Create `shared/lib/exception/icao_exceptions.{h,cpp}`
2. Exception hierarchy (Database, LDAP, Certificate, etc.)
3. Error code enumeration
4. Migrate from pa-service exceptions

**Deliverables**:
- ✅ Unified exception handling
- ✅ Standardized error codes
- ✅ Better error reporting

---

#### Day 5: Logging Utilities

**Tasks**:
1. Create `shared/lib/logging/logger.{h,cpp}`
2. Unified spdlog initialization
3. Service-specific logger instances
4. Log rotation configuration

**Deliverables**:
- ✅ Consistent logging across services
- ✅ Easy log configuration

---

### Phase 4: PKD Relay Integration (v2.7.0 - Week 4)

**Duration**: 5 days
**Priority**: 🔴 Critical (blocks PKD Relay Refactoring)

#### Day 1-3: Repository Pattern + Shared Libraries

**Tasks**:
1. Apply Repository Pattern to pkd-relay (as per PKD_RELAY_REFACTORING_PLAN.md)
2. Use shared libraries from Phase 1-3
3. Database connection pool integration
4. LDAP connection pool integration
5. Audit logging integration

**Deliverables**:
- ✅ pkd-relay using all shared libraries
- ✅ Repository Pattern complete
- ✅ main.cpp 2,003 → ~600 lines (70% reduction)

---

#### Day 4-5: Testing & Documentation

**Tasks**:
1. End-to-end integration tests
2. Performance benchmarking
3. Memory leak testing (valgrind)
4. Documentation updates

**Deliverables**:
- ✅ All services tested with shared libraries
- ✅ Performance metrics documented
- ✅ Migration guide complete

---

## Expected Benefits

### Code Reduction Summary

| Category | Before | After | Savings |
|----------|--------|-------|---------|
| Audit Log | 644 lines | 250 lines | **61%** |
| DB Connection Pool | 888 lines | 450 lines | **49%** |
| LDAP Connection Pool | 477 lines | 500 lines | 3x reuse |
| Certificate Utils | ~500 lines | common-lib | **67%** |
| DN Normalization | ~300 lines | common-lib | **80%** |
| **Total** | **~2,809 lines** | **~1,200 lines** | **57%** ✅ |

### Maintenance Benefits

1. **Single Source of Truth**
   - Bug fixes in one place
   - Consistent behavior across services
   - Easier to add new features

2. **Testing Efficiency**
   - Unit tests for shared libraries
   - Reduce service-level test duplication
   - Better coverage with less effort

3. **Onboarding Speed**
   - New developers learn shared libraries once
   - Consistent patterns across codebase
   - Clear separation of concerns

4. **Oracle Migration**
   - Only update connection pool library
   - No changes to service code
   - 90% effort reduction for DB migration

---

## Risk Mitigation

### Build System Complexity

**Risk**: CMake dependency management for shared libraries

**Mitigation**:
- Use `add_subdirectory()` for in-tree builds
- Support both static and shared library builds
- Provide CMake find_package() scripts

### Backward Compatibility

**Risk**: Breaking changes to existing services

**Mitigation**:
- Maintain backward-compatible APIs
- Deprecation warnings before removal
- Comprehensive integration tests

### Performance Overhead

**Risk**: Shared library call overhead

**Mitigation**:
- Benchmark before/after
- Inline hot path functions
- Use LTO (Link-Time Optimization)

---

## Success Criteria

### Phase 1 (Week 1)
- ✅ 3 shared libraries created (audit, db_pool, ldap_pool)
- ✅ All services building with shared libraries
- ✅ No regression in functionality
- ✅ 1,432 lines eliminated (61% of target)

### Phase 2 (Week 2)
- ✅ common-lib Phase 2-4 complete
- ✅ Certificate utilities migrated
- ✅ All unit tests passing
- ✅ Additional 500 lines eliminated

### Phase 3 (Week 3)
- ✅ 4 additional shared libraries (config, ldap_utils, exceptions, logging)
- ✅ Complete shared library ecosystem
- ✅ Documentation complete

### Phase 4 (Week 4)
- ✅ PKD Relay refactoring complete
- ✅ All 3 services using shared libraries
- ✅ End-to-end tests passing
- ✅ Performance validated

---

## Next Steps

1. **Immediate**: Start Phase 1 implementation (Audit Log consolidation)
2. **Week 1**: Complete Phase 1 (Critical infrastructure)
3. **Week 2**: Complete common-lib implementation
4. **Week 3**: New shared libraries
5. **Week 4**: PKD Relay integration + final testing

**Review Checkpoint**: End of Week 2 (evaluate progress, adjust timeline if needed)

---

## Related Documentation

- [PKD_RELAY_REFACTORING_PLAN.md](PKD_RELAY_REFACTORING_PLAN.md) - PKD Relay Repository Pattern plan
- [PKD_MANAGEMENT_REFACTORING_COMPLETE_SUMMARY.md](PKD_MANAGEMENT_REFACTORING_COMPLETE_SUMMARY.md) - Reference architecture
- [PA_SERVICE_REFACTORING_COMPLETE.md](PA_SERVICE_REFACTORING_COMPLETE.md) - Reference architecture
- [services/common-lib/README.md](../services/common-lib/README.md) - Common library structure
- [SHARED_LIBRARY_ARCHITECTURE_PLAN.md](SHARED_LIBRARY_ARCHITECTURE_PLAN.md) - Original architecture plan

---

**Status**: ✅ Planning Complete - Ready for Implementation
**Next**: Begin Phase 1 - Audit Log Consolidation
