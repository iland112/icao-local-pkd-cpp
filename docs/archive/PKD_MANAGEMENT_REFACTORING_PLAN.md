# PKD Management Service Refactoring Plan

**Date**: 2026-01-19
**Current State**: main.cpp (6,087 lines)
**Target**: Modular architecture with clean separation of concerns
**Alongside**: ICAO Auto Sync feature implementation

---

## 1. Current Structure Analysis

### 1.1 Current main.cpp Sections

| Line Range | Section | Description | LoC |
|------------|---------|-------------|-----|
| 1-148 | Headers & Globals | Includes, using, global variables | 148 |
| 149-378 | SSE Progress | Progress tracking for uploads | 230 |
| 379-442 | Trust Anchor | CMS signature verification | 64 |
| 443-715 | CSCA Validation | Self-signature validation | 273 |
| 716-843 | Validation Storage | DB storage for validation results | 128 |
| 844-873 | Extern Functions | Functions for other .cpp files | 30 |
| 874-1152 | Duplicate Check | Certificate duplication detection | 279 |
| 1153-1372 | Parsing & DB Storage | Certificate/CRL parsing | 220 |
| 1373-2152 | LDAP Storage | LDAP insertion functions | 780 |
| 2153-3783 | Database Functions | Complex DB queries & stats | 1,631 |
| 3784-6003 | API Handlers | HTTP endpoint implementations | 2,220 |
| 6004-6087 | Main Function | App initialization & routing | 84 |

### 1.2 Problems Identified

❌ **Single Responsibility Violation**: main.cpp handles HTTP, business logic, DB, LDAP, crypto
❌ **High Coupling**: Functions directly call PG/LDAP APIs without abstraction
❌ **Low Cohesion**: Unrelated functions (upload, validation, health checks) in one file
❌ **Hard to Test**: Monolithic structure makes unit testing difficult
❌ **Poor Modularity**: No clear boundaries between domains

---

## 2. Target Architecture (Clean Code Principles)

### 2.1 Module Structure

```
services/pkd-management/
├── src/
│   ├── main.cpp                          # 500 LoC (routing only)
│   │
│   ├── handlers/                         # HTTP Request Handlers
│   │   ├── upload_handler.h/cpp          # Upload endpoints
│   │   ├── certificate_handler.h/cpp     # Certificate search/export
│   │   ├── health_handler.h/cpp          # Health check endpoints
│   │   ├── validation_handler.h/cpp      # Validation endpoints
│   │   └── icao_handler.h/cpp            # NEW: ICAO sync endpoints
│   │
│   ├── services/                         # Business Logic (Service Layer)
│   │   ├── upload_service.h/cpp          # Upload orchestration
│   │   ├── certificate_service.h/cpp     # EXISTING (already modular)
│   │   ├── validation_service.h/cpp      # Trust chain validation
│   │   └── icao_sync_service.h/cpp       # NEW: ICAO version checking
│   │
│   ├── repositories/                     # Data Access Layer
│   │   ├── certificate_repository.h/cpp  # Certificate DB operations
│   │   ├── upload_repository.h/cpp       # Upload history DB operations
│   │   ├── validation_repository.h/cpp   # Validation results DB operations
│   │   ├── ldap_certificate_repository.h/cpp  # EXISTING
│   │   └── icao_version_repository.h/cpp # NEW: ICAO version tracking
│   │
│   ├── domain/                           # Domain Models & Logic
│   │   ├── models/
│   │   │   ├── certificate.h/cpp         # EXISTING
│   │   │   ├── upload.h                  # Upload metadata
│   │   │   ├── validation_result.h       # Validation result
│   │   │   └── icao_version.h            # NEW: ICAO version metadata
│   │   └── validators/
│   │       ├── trust_chain_validator.h/cpp  # Trust chain logic
│   │       └── csca_validator.h/cpp         # CSCA self-signature
│   │
│   ├── infrastructure/                   # External Systems Integration
│   │   ├── database/
│   │   │   ├── connection_pool.h/cpp     # PG connection pooling
│   │   │   └── transaction.h/cpp         # Transaction management
│   │   ├── ldap/
│   │   │   ├── ldap_client.h/cpp         # LDAP operations wrapper
│   │   │   └── ldap_connection.h/cpp     # Connection management
│   │   ├── http/
│   │   │   └── http_client.h/cpp         # NEW: HTTP client for ICAO portal
│   │   └── notification/
│   │       ├── email_sender.h/cpp        # NEW: SMTP email sender
│   │       └── notification_service.h/cpp # NEW: Notification orchestration
│   │
│   ├── utils/                            # Utility Functions
│   │   ├── crypto_utils.h/cpp            # SHA-256, DER/PEM conversion
│   │   ├── string_utils.h/cpp            # String manipulation
│   │   ├── time_utils.h/cpp              # Timestamp formatting
│   │   └── html_parser.h/cpp             # NEW: HTML parsing for ICAO portal
│   │
│   ├── ldif/                             # EXISTING: LDIF Processing
│   │   ├── ldif_processor.h/cpp
│   │   └── ...
│   │
│   ├── reconciliation/                   # EXISTING: Auto Reconcile
│   │   ├── reconciliation_engine.h/cpp
│   │   └── ...
│   │
│   ├── processing_strategy.h/cpp         # EXISTING: Strategy Pattern
│   └── common.h                          # EXISTING: Common types
│
├── CMakeLists.txt
└── vcpkg.json
```

### 2.2 Architectural Layers

```
┌─────────────────────────────────────────────────────────────┐
│                  HTTP Handlers (Presentation)                │
│  upload_handler, certificate_handler, icao_handler, etc.    │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                  Services (Business Logic)                   │
│  upload_service, validation_service, icao_sync_service      │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│               Repositories (Data Access)                     │
│  certificate_repo, upload_repo, icao_version_repo           │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│          Infrastructure (External Systems)                   │
│  database, ldap, http_client, email_sender                  │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. Refactoring Strategy (Incremental Approach)

### Phase 1: Extract Infrastructure Layer (Week 1)
**Goal**: Isolate external system dependencies

**Tasks**:
1. Create `infrastructure/database/connection_pool.h/cpp`
   - Extract PG connection string building
   - Add connection pooling (singleton pattern)

2. Create `infrastructure/ldap/ldap_client.h/cpp`
   - Wrap ldap_* function calls
   - Handle connection/reconnection logic
   - Abstract DN building, entry insertion

3. Create `utils/crypto_utils.h/cpp`
   - Extract SHA-256 hashing
   - Extract DER/PEM conversion
   - Extract certificate parsing helpers

**Lines Reduced**: main.cpp: 6,087 → 5,200 (~900 LoC moved)

### Phase 2: Extract Repository Layer (Week 1-2)
**Goal**: Abstract database operations

**Tasks**:
1. Create `repositories/upload_repository.h/cpp`
   - `insertUpload()`, `updateUploadStatus()`, `getUploadHistory()`
   - `getUploadStatistics()`, `deleteUpload()`

2. Create `repositories/validation_repository.h/cpp`
   - `insertValidationResult()`, `getValidationStatistics()`
   - `revalidateDscCertificates()`

3. Create `repositories/icao_version_repository.h/cpp` (NEW)
   - `insertVersion()`, `getLatestVersions()`, `getVersionHistory()`
   - `updateVersionStatus()`, `linkToUpload()`

**Lines Reduced**: main.cpp: 5,200 → 4,000 (~1,200 LoC moved)

### Phase 3: Extract Service Layer (Week 2)
**Goal**: Encapsulate business logic

**Tasks**:
1. Create `services/upload_service.h/cpp`
   - `uploadLdif()`, `uploadMasterList()`
   - `triggerParse()`, `triggerValidate()`
   - Coordinate between repositories

2. Create `services/validation_service.h/cpp`
   - `validateTrustChain()`, `validateCscaSelfSignature()`
   - Use repositories for data access

3. Create `services/icao_sync_service.h/cpp` (NEW)
   - `checkForUpdates()`, `getLatestVersions()`
   - `sendNotification()`
   - Use IcaoVersionRepository + HttpClient

**Lines Reduced**: main.cpp: 4,000 → 2,500 (~1,500 LoC moved)

### Phase 4: Extract HTTP Handlers (Week 2-3)
**Goal**: Thin request/response layer

**Tasks**:
1. Create `handlers/upload_handler.h/cpp`
   - Register: `/api/upload/ldif`, `/api/upload/masterlist`, etc.
   - Delegate to UploadService

2. Create `handlers/certificate_handler.h/cpp`
   - Register: `/api/certificates/search`, `/api/certificates/export`, etc.
   - Delegate to CertificateService (already exists)

3. Create `handlers/health_handler.h/cpp`
   - Register: `/api/health`, `/api/health/database`, etc.
   - Simple health check logic

4. Create `handlers/icao_handler.h/cpp` (NEW)
   - Register: `/api/icao/check-updates`, `/api/icao/latest`, etc.
   - Delegate to IcaoSyncService

**Lines Reduced**: main.cpp: 2,500 → 500 (~2,000 LoC moved)

### Phase 5: Implement ICAO Module (Week 3)
**Goal**: Add ICAO Auto Sync feature

**Tasks**:
1. Create `utils/html_parser.h/cpp`
   - Parse ICAO portal HTML
   - Extract version numbers with regex

2. Create `infrastructure/http/http_client.h/cpp`
   - Wrapper around libcurl or Drogon HttpClient
   - Fetch ICAO portal HTML

3. Create `infrastructure/notification/email_sender.h/cpp`
   - SMTP client integration
   - Send email notifications

4. Create `services/icao_sync_service.h/cpp`
   - Orchestrate version checking workflow
   - Call repositories, HTTP client, notification

5. Create `handlers/icao_handler.h/cpp`
   - API endpoints for version checking

**New Lines**: ~1,000 LoC (well-organized)

### Phase 6: Testing & Documentation (Week 3-4)
**Goal**: Ensure correctness and maintainability

**Tasks**:
1. Unit tests for each module
2. Integration tests for workflows
3. Update CLAUDE.md with new architecture
4. Generate Doxygen documentation

---

## 4. Detailed Module Specifications

### 4.1 Infrastructure Layer

#### `infrastructure/database/connection_pool.h`

```cpp
#pragma once
#include <libpq-fe.h>
#include <string>
#include <memory>
#include <mutex>

namespace infrastructure {
namespace database {

class ConnectionPool {
public:
    static ConnectionPool& getInstance();

    PGconn* acquire();
    void release(PGconn* conn);

    void initialize(const std::string& host, int port,
                   const std::string& dbname,
                   const std::string& user,
                   const std::string& password);

private:
    ConnectionPool() = default;
    ~ConnectionPool();

    std::string connInfo_;
    std::mutex mutex_;

    // Disable copy/move
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
};

// RAII wrapper for automatic connection release
class Connection {
public:
    explicit Connection(ConnectionPool& pool);
    ~Connection();

    PGconn* get() { return conn_; }
    operator PGconn*() { return conn_; }

private:
    ConnectionPool& pool_;
    PGconn* conn_;
};

} // namespace database
} // namespace infrastructure
```

#### `infrastructure/ldap/ldap_client.h`

```cpp
#pragma once
#include <ldap.h>
#include <string>
#include <vector>
#include <optional>

namespace infrastructure {
namespace ldap {

class LdapClient {
public:
    LdapClient(const std::string& host, int port,
               const std::string& bindDn, const std::string& bindPassword);
    ~LdapClient();

    bool connect();
    void disconnect();
    bool ensureConnected();

    // High-level operations
    bool addEntry(const std::string& dn, const std::map<std::string, std::vector<std::string>>& attrs);
    bool deleteEntry(const std::string& dn);
    std::optional<std::vector<std::string>> search(const std::string& baseDn,
                                                    const std::string& filter,
                                                    const std::vector<std::string>& attrs);

    // DN builders
    std::string buildCertificateDn(const std::string& country, const std::string& certType,
                                   const std::string& serialNumber);

private:
    std::string host_;
    int port_;
    std::string bindDn_;
    std::string bindPassword_;
    LDAP* ldap_;
};

} // namespace ldap
} // namespace infrastructure
```

### 4.2 Repository Layer

#### `repositories/upload_repository.h`

```cpp
#pragma once
#include <string>
#include <vector>
#include <optional>
#include "../domain/models/upload.h"

namespace repositories {

class UploadRepository {
public:
    explicit UploadRepository(PGconn* conn);

    int insertUpload(const std::string& filename, const std::string& fileType,
                     const std::string& fileHash, size_t fileSize,
                     const std::string& processingMode);

    bool updateStatus(int uploadId, const std::string& status);
    bool updateCounts(int uploadId, int cscaCount, int dscCount, int dscNcCount, int crlCount);
    bool deleteUpload(int uploadId);

    std::optional<domain::models::Upload> getById(int uploadId);
    std::vector<domain::models::Upload> getHistory(int limit, int offset,
                                                     const std::string& fileType,
                                                     const std::string& status);

    struct Statistics {
        int total;
        int completed;
        int failed;
        int inProgress;
    };
    Statistics getStatistics();

private:
    PGconn* conn_;
};

} // namespace repositories
```

#### `repositories/icao_version_repository.h` (NEW)

```cpp
#pragma once
#include <string>
#include <vector>
#include <optional>
#include "../domain/models/icao_version.h"

namespace repositories {

class IcaoVersionRepository {
public:
    explicit IcaoVersionRepository(PGconn* conn);

    bool insert(const domain::models::IcaoVersion& version);
    bool updateStatus(const std::string& fileName, const std::string& status);
    bool linkToUpload(const std::string& fileName, int uploadId);

    std::optional<domain::models::IcaoVersion> getByFileName(const std::string& fileName);
    std::vector<domain::models::IcaoVersion> getLatest();
    std::vector<domain::models::IcaoVersion> getHistory(int limit);

    // Check if version already exists
    bool exists(const std::string& collectionType, int fileVersion);

private:
    PGconn* conn_;
};

} // namespace repositories
```

### 4.3 Service Layer

#### `services/icao_sync_service.h` (NEW)

```cpp
#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../domain/models/icao_version.h"
#include "../repositories/icao_version_repository.h"
#include "../infrastructure/http/http_client.h"
#include "../infrastructure/notification/notification_service.h"

namespace services {

class IcaoSyncService {
public:
    IcaoSyncService(std::shared_ptr<repositories::IcaoVersionRepository> repo,
                    std::shared_ptr<infrastructure::http::HttpClient> httpClient,
                    std::shared_ptr<infrastructure::notification::NotificationService> notifier);

    struct CheckResult {
        bool success;
        std::string message;
        int newVersionCount;
        std::vector<domain::models::IcaoVersion> newVersions;
    };

    CheckResult checkForUpdates();
    std::vector<domain::models::IcaoVersion> getLatestVersions();
    std::vector<domain::models::IcaoVersion> getVersionHistory(int limit);

private:
    std::shared_ptr<repositories::IcaoVersionRepository> repo_;
    std::shared_ptr<infrastructure::http::HttpClient> httpClient_;
    std::shared_ptr<infrastructure::notification::NotificationService> notifier_;

    std::string fetchIcaoPortalHtml();
    std::vector<domain::models::IcaoVersion> parseVersionNumbers(const std::string& html);
    std::vector<domain::models::IcaoVersion> compareVersions(
        const std::vector<domain::models::IcaoVersion>& remote,
        const std::vector<domain::models::IcaoVersion>& local);
};

} // namespace services
```

### 4.4 Handler Layer

#### `handlers/icao_handler.h` (NEW)

```cpp
#pragma once
#include <drogon/drogon.h>
#include <memory>
#include "../services/icao_sync_service.h"

namespace handlers {

class IcaoHandler {
public:
    explicit IcaoHandler(std::shared_ptr<services::IcaoSyncService> service);

    void registerRoutes(drogon::HttpAppFramework& app);

private:
    std::shared_ptr<services::IcaoSyncService> service_;

    // Route handlers
    void handleCheckUpdates(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void handleGetLatest(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void handleGetHistory(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         int limit);
};

} // namespace handlers
```

---

## 5. Migration Plan (Incremental & Safe)

### Strategy: Strangler Fig Pattern

**Principle**: Gradually replace monolithic code with modular code, keeping system functional at all times.

### Week 1: Infrastructure + Repositories
1. Create new modules **without modifying main.cpp**
2. Test new modules independently (unit tests)
3. Start using new modules in **new endpoints only** (ICAO)
4. Old endpoints continue using legacy code

### Week 2: Service Layer + ICAO Implementation
1. Implement ICAO module using new architecture
2. Verify ICAO endpoints work correctly
3. Gradually migrate old endpoints one by one

### Week 3: Handler Extraction
1. Extract handlers for old endpoints
2. Update main.cpp to use handlers
3. Remove legacy code from main.cpp

### Week 4: Testing & Polish
1. Comprehensive testing
2. Performance benchmarking
3. Documentation updates

---

## 6. Success Metrics

| Metric | Before | Target | Measure |
|--------|--------|--------|---------|
| **main.cpp LoC** | 6,087 | <500 | Lines of code |
| **Cyclomatic Complexity** | High | Low | cppcheck |
| **Test Coverage** | ~0% | >80% | gcov/lcov |
| **Build Time** | 10-15 min | <12 min | CI/CD |
| **Code Duplication** | High | <5% | SonarQube |

---

## 7. Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Breaking existing functionality | Incremental migration, comprehensive tests |
| Build time increase | Precompiled headers, parallel compilation |
| Complex dependencies | Dependency injection, interface segregation |
| Team confusion | Clear documentation, code reviews |

---

## 8. Next Steps (Immediate Actions)

1. ✅ **Approve this refactoring plan**
2. **Phase 1 (Database Migration)**: Create `icao_pkd_versions` table
3. **Phase 2 (Infrastructure)**: Create `infrastructure/` directory structure
4. **Phase 3 (ICAO Module)**: Implement ICAO sync using new architecture
5. **Phase 4 (Gradual Migration)**: Extract existing code to new modules

---

**Estimated Total Effort**: 3-4 weeks
**Immediate Benefit**: ICAO feature + cleaner codebase
**Long-term Benefit**: Maintainable, testable, scalable architecture

---

**Document Status**: Ready for Implementation
**Approved By**: _______________
**Date**: _______________
