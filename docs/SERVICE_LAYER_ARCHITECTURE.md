# Service Layer Architecture - main.cpp Refactoring Phase 1

**Version**: v2.1.3
**Date**: 2026-01-29
**Status**: Phase 1 Complete - Service Skeletons Created

---

## Overview

This document describes the Service Layer architecture created as part of the main.cpp refactoring initiative. The goal is to decompose the monolithic [main.cpp](../services/pkd-management/src/main.cpp) (9,313 lines) into a clean, maintainable architecture following **DDD** (Domain-Driven Design), **SRP** (Single Responsibility Principle), and **Front Controller Pattern**.

---

## Architecture Goals

### Current State (Before Refactoring)
- **File**: [main.cpp](../services/pkd-management/src/main.cpp) - 9,313 lines
- **Issues**:
  - Business logic mixed with HTTP routing
  - Difficult to test individual components
  - Single file contains all domain logic
  - Violates Single Responsibility Principle
  - Hard to maintain and extend

### Target State (After Refactoring)
- **File**: [main.cpp](../services/pkd-management/src/main.cpp) - <500 lines (Front Controller only)
- **Benefits**:
  - Clean separation of concerns
  - Testable business logic
  - Reusable Service components
  - Follows DDD and SRP
  - Easy to maintain and extend

---

## Service Layer Design

### Layer Responsibilities

```
┌─────────────────────────────────────────────────────────────┐
│                     main.cpp (Front Controller)              │
│  - HTTP routing only                                         │
│  - Request/Response handling                                 │
│  - Delegates to Service layer                                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     Service Layer (Business Logic)           │
│  - UploadService          - ValidationService                │
│  - AuditService           - StatisticsService                │
│  - Pure business logic    - No HTTP dependencies             │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     Repository Layer (Data Access)           │
│  - Database queries                                          │
│  - LDAP operations                                           │
│  - External system integration                               │
└─────────────────────────────────────────────────────────────┘
```

---

## Service Classes

### 1. UploadService

**File**: [upload_service.h](../services/pkd-management/src/services/upload_service.h) (431 lines)
**Implementation**: [upload_service.cpp](../services/pkd-management/src/services/upload_service.cpp) (524 lines)

**Responsibilities**:
- LDIF file upload and processing
- Master List file upload and processing
- Upload history management
- Upload validation results
- Upload statistics and issues

**Key Methods**:
```cpp
class UploadService {
public:
    // LDIF Upload
    LdifUploadResult uploadLdif(
        const std::string& fileName,
        const std::vector<uint8_t>& fileContent,
        const std::string& uploadMode,
        const std::string& uploadedBy
    );

    // Master List Upload
    MasterListUploadResult uploadMasterList(
        const std::string& fileName,
        const std::vector<uint8_t>& fileContent,
        const std::string& uploadMode,
        const std::string& uploadedBy
    );

    // Upload Management
    bool triggerParsing(const std::string& uploadId);
    bool triggerValidation(const std::string& uploadId);

    // Upload History & Detail
    Json::Value getUploadHistory(const UploadHistoryFilter& filter);
    Json::Value getUploadDetail(const std::string& uploadId);

    // Upload Validations & Issues
    Json::Value getUploadValidations(const std::string& uploadId, const ValidationFilter& filter);
    Json::Value getUploadIssues(const std::string& uploadId);

    // Statistics
    Json::Value getUploadStatistics();
    Json::Value getCountryStatistics();
    Json::Value getDetailedCountryStatistics(int limit = 0);
};
```

**Related Endpoints**:
- `POST /upload/ldif`
- `POST /upload/masterlist`
- `GET /upload/history`
- `GET /upload/{uploadId}`
- `GET /upload/{uploadId}/validations`
- `GET /upload/{uploadId}/issues`
- `POST /upload/{uploadId}/parse`
- `POST /upload/{uploadId}/validate`
- `DELETE /upload/{uploadId}`
- `GET /upload/statistics`
- `GET /upload/countries`
- `GET /upload/countries/detailed`

---

### 2. ValidationService

**File**: [validation_service.h](../services/pkd-management/src/services/validation_service.h) (376 lines)
**Implementation**: [validation_service.cpp](../services/pkd-management/src/services/validation_service.cpp) (501 lines)

**Responsibilities**:
- DSC certificate re-validation
- Trust chain building and validation
- Link Certificate validation
- Validation result storage and retrieval

**Key Methods**:
```cpp
class ValidationService {
public:
    // DSC Re-validation
    RevalidateResult revalidateDscCertificates();
    RevalidateResult revalidateDscCertificatesForUpload(const std::string& uploadId);

    // Single Certificate Validation
    ValidationResult validateCertificate(X509* cert, const std::string& certType = "DSC");
    ValidationResult validateCertificateByFingerprint(const std::string& fingerprint);

    // Validation Result Retrieval
    Json::Value getValidationByFingerprint(const std::string& fingerprint);
    Json::Value getValidationStatistics(const std::string& uploadId);

    // Link Certificate Validation
    LinkCertValidationResult validateLinkCertificate(X509* cert);
    LinkCertValidationResult validateLinkCertificateById(const std::string& certId);

private:
    // Trust Chain Building
    TrustChain buildTrustChain(X509* leafCert, int maxDepth = 10);
    X509* findCscaByIssuerDn(const std::string& issuerDn);
    bool verifyCertificateSignature(X509* cert, X509* issuerCert);

    // CRL Check
    bool checkCrlRevocation(X509* cert);
};
```

**Related Endpoints**:
- `POST /certificates/revalidate`
- `POST /upload/{uploadId}/revalidate`
- `GET /certificates/validation?fingerprint={sha256}`

---

### 3. AuditService

**File**: [audit_service.h](../services/pkd-management/src/services/audit_service.h) (409 lines)
**Implementation**: [audit_service.cpp](../services/pkd-management/src/services/audit_service.cpp) (513 lines)

**Responsibilities**:
- Audit log recording (operation_audit_log table)
- Operation log retrieval with filtering
- Operation statistics and analytics
- User activity tracking
- Security event monitoring

**Key Methods**:
```cpp
class AuditService {
public:
    // Audit Log Recording
    bool recordAuditLog(const AuditLogEntry& entry);
    bool recordFileUploadAudit(const std::string& username, const std::string& ipAddress, ...);
    bool recordCertificateSearchAudit(const std::string& username, ...);
    bool recordPaVerificationAudit(const std::string& username, ...);

    // Audit Log Retrieval
    Json::Value getOperationLogs(const AuditLogFilter& filter);
    Json::Value getOperationLogById(int logId);

    // Operation Statistics
    Json::Value getOperationStatistics(const std::string& startDate, const std::string& endDate);
    Json::Value getOperationStatisticsByType(const std::string& operationType, ...);

    // User Activity Tracking
    Json::Value getUserActivity(const std::string& username, ...);
    Json::Value getTopActiveUsers(int limit = 10, ...);

    // Security Event Monitoring
    Json::Value getFailedOperations(int limit = 50, int offset = 0);
    Json::Value getSuspiciousActivities(const std::string& startDate, ...);
    Json::Value getOperationsByIp(const std::string& ipAddress, ...);

    // Data Retention & Cleanup
    int deleteOldAuditLogs(int daysToKeep);
    Json::Value getRetentionStatistics();
};
```

**Related Endpoints**:
- `POST /api/audit/log`
- `GET /api/audit/operations`
- `GET /api/audit/operations/stats`
- `GET /api/audit/operations/{logId}`
- `GET /api/audit/users/{username}/activity`
- `GET /api/audit/users/top`
- `GET /api/audit/failed-operations`
- `GET /api/audit/suspicious-activities`

---

### 4. StatisticsService

**File**: [statistics_service.h](../services/pkd-management/src/services/statistics_service.h) (367 lines)
**Implementation**: [statistics_service.cpp](../services/pkd-management/src/services/statistics_service.cpp) (417 lines)

**Responsibilities**:
- Upload statistics (total, by status, by format)
- Certificate statistics (by type, by country)
- Country statistics (certificates per country, detailed breakdown)
- System-wide statistics (total counts, trends)
- Validation statistics (success rates, trust chain metrics)

**Key Methods**:
```cpp
class StatisticsService {
public:
    // Upload Statistics
    Json::Value getUploadStatistics();
    Json::Value getUploadTrend(int days = 30);

    // Certificate Statistics
    Json::Value getCertificateStatistics();
    Json::Value getCrlStatistics();

    // Country Statistics
    Json::Value getCountryStatistics();
    Json::Value getDetailedCountryStatistics(int limit = 0);
    Json::Value getCountryDetail(const std::string& countryCode);

    // Validation Statistics
    Json::Value getValidationStatistics();
    Json::Value getValidationStatisticsByUpload(const std::string& uploadId);

    // System-Wide Statistics
    Json::Value getSystemStatistics();
    Json::Value getDatabaseStatistics();

    // Trend Analysis
    Json::Value getCertificateGrowthTrend(int days = 30);
    Json::Value getValidationTrend(int days = 30);

    // Export Statistics
    std::string exportStatisticsToCSV(const std::string& statisticsType);
    std::string generateStatisticsReport(const std::string& format = "json");
};
```

**Related Endpoints**:
- `GET /upload/statistics`
- `GET /upload/countries`
- `GET /upload/countries/detailed`
- `GET /certificates/statistics`
- `GET /statistics/system`
- `GET /statistics/database`
- `GET /statistics/trends/certificates`
- `GET /statistics/trends/validation`

---

## Phase 1 Completion Summary

### ✅ What Was Accomplished

1. **Service Skeleton Creation** (All 4 Services)
   - UploadService (header + implementation)
   - ValidationService (header + implementation)
   - AuditService (header + implementation)
   - StatisticsService (header + implementation)

2. **Build Configuration**
   - Updated [CMakeLists.txt](../services/pkd-management/CMakeLists.txt) to include all Service files
   - Verified successful compilation with Docker build

3. **Interface Design**
   - Complete method signatures for all Service classes
   - Comprehensive documentation with Doxygen comments
   - Clear separation of responsibilities

4. **Helper Methods**
   - Database query execution (executeQuery, executeParamQuery)
   - JSON conversion (pgResultToJson)
   - Utility functions (timestamp parsing, number formatting)

### 📋 TODO Markers

All Service implementations contain `TODO` markers indicating where logic from main.cpp needs to be extracted:

```cpp
// Example from UploadService::uploadLdif()
// TODO: Extract this logic from main.cpp
spdlog::warn("TODO: Extract LDIF processing logic from main.cpp (lines ~6017-6267)");
```

### 📊 Code Statistics

| Service | Header Lines | Implementation Lines | Total | Methods |
|---------|-------------|---------------------|-------|---------|
| UploadService | 431 | 524 | 955 | 20 |
| ValidationService | 376 | 501 | 877 | 16 |
| AuditService | 409 | 513 | 922 | 20 |
| StatisticsService | 367 | 417 | 784 | 20 |
| **Total** | **1,583** | **1,955** | **3,538** | **76** |

---

## Next Steps (Phase 2)

### 1. Extract Business Logic from main.cpp

**Priority Order**:
1. **UploadService** - Extract LDIF and Master List upload logic
2. **ValidationService** - Extract DSC validation and trust chain building
3. **AuditService** - Extract audit log recording logic
4. **StatisticsService** - Extract statistics queries

**Example Extraction**: `UploadService::uploadLdif()`

**Before** (in main.cpp):
```cpp
app().registerHandler(
    "/upload/ldif",
    [&](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        // 250 lines of upload logic here...
    },
    {Post}
);
```

**After** (in main.cpp):
```cpp
app().registerHandler(
    "/upload/ldif",
    [&](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto uploadService = std::make_shared<services::UploadService>(dbConn, ldapConn);
        auto result = uploadService->uploadLdif(fileName, fileContent, uploadMode, uploadedBy);

        Json::Value response;
        response["success"] = result.success;
        response["uploadId"] = result.uploadId;
        response["message"] = result.message;

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);
    },
    {Post}
);
```

### 2. Create Controller Classes (Optional)

**Purpose**: Further reduce main.cpp size by grouping related endpoints

**Example**:
```cpp
class UploadController {
public:
    void registerRoutes(HttpAppFramework& app);

private:
    void handleLdifUpload(const HttpRequestPtr& req, ...);
    void handleMasterListUpload(const HttpRequestPtr& req, ...);
    void handleGetHistory(const HttpRequestPtr& req, ...);
};
```

### 3. Integration Testing

**Goals**:
- Ensure all endpoints still work correctly
- Verify no functionality regression
- Performance benchmarking

**Test Coverage**:
- Unit tests for Service methods
- Integration tests for API endpoints
- End-to-end tests for upload flows

---

## Design Principles Followed

### 1. Domain-Driven Design (DDD)

**Layers**:
- **Domain Layer**: Certificate, CRL, MasterList models
- **Application Service Layer**: UploadService, ValidationService, etc. (Business logic)
- **Infrastructure Layer**: Database, LDAP, HTTP clients
- **Presentation Layer**: HTTP handlers in main.cpp

### 2. Single Responsibility Principle (SRP)

Each Service has a **single, well-defined responsibility**:
- **UploadService**: File upload and processing **only**
- **ValidationService**: Certificate validation **only**
- **AuditService**: Audit logging **only**
- **StatisticsService**: Statistical data **only**

### 3. Dependency Injection

All Services use **constructor injection**:
```cpp
class UploadService {
public:
    explicit UploadService(PGconn* dbConn, LDAP* ldapConn);
    // Non-owning pointers - lifecycle managed by main.cpp
};
```

**Benefits**:
- Easy to test with mock dependencies
- Clear dependency graph
- Flexible instantiation

### 4. Strategy Pattern

Existing Strategy Pattern implementations are **preserved**:
- **ProcessingStrategy**: AUTO vs MANUAL upload modes
- **CertificateTypeStrategy**: DSC, CSCA, DSC_NC, CRL handling

Services **use** these strategies, not replace them.

---

## Build Verification

### Build Status

✅ **Successful Compilation** (2026-01-29)

```bash
$ cd docker && docker-compose build pkd-management
...
#26 [builder 11/11] RUN cp build_fresh/bin/pkd-management ...
#26 DONE 0.5s
...
#37 exporting to image
#37 DONE 1.1s

Image docker-pkd-management Built
```

**Version**: v2.1.2.4 (no version bump for skeleton-only changes)

### Files Modified

1. **Service Headers** (NEW):
   - [upload_service.h](../services/pkd-management/src/services/upload_service.h)
   - [validation_service.h](../services/pkd-management/src/services/validation_service.h)
   - [audit_service.h](../services/pkd-management/src/services/audit_service.h)
   - [statistics_service.h](../services/pkd-management/src/services/statistics_service.h)

2. **Service Implementations** (NEW):
   - [upload_service.cpp](../services/pkd-management/src/services/upload_service.cpp)
   - [validation_service.cpp](../services/pkd-management/src/services/validation_service.cpp)
   - [audit_service.cpp](../services/pkd-management/src/services/audit_service.cpp)
   - [statistics_service.cpp](../services/pkd-management/src/services/statistics_service.cpp)

3. **Build Configuration** (MODIFIED):
   - [CMakeLists.txt](../services/pkd-management/CMakeLists.txt) - Added all Service .cpp files

---

## Timeline

| Date | Phase | Status |
|------|-------|--------|
| 2026-01-29 | Phase 1: Service Skeleton Creation | ✅ Complete |
| TBD | Phase 2: Logic Extraction (Week 1) | 🔄 Planned |
| TBD | Phase 3: Controller Creation (Week 2) | 🔄 Planned |
| TBD | Phase 4: Integration Testing | 🔄 Planned |

**Estimated Total Duration**: 2 weeks (as per [MAIN_CPP_REFACTORING_PLAN.md](MAIN_CPP_REFACTORING_PLAN.md))

---

## Related Documentation

- [MAIN_CPP_REFACTORING_PLAN.md](MAIN_CPP_REFACTORING_PLAN.md) - Detailed refactoring plan
- [ARCHITECTURE_DESIGN_PRINCIPLES.md](ARCHITECTURE_DESIGN_PRINCIPLES.md) - Design patterns and principles
- [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md) - General development guide

---

## Conclusion

Phase 1 of the main.cpp refactoring is **complete**. All four Service class skeletons have been created, documented, and successfully compiled. The architecture is now ready for Phase 2: extracting business logic from main.cpp into the Service layer.

**Key Achievement**: Established a clean, testable Service layer foundation that adheres to DDD, SRP, and Front Controller patterns. The skeleton provides a clear roadmap for the remaining refactoring work.

**Next Action**: Begin extracting UploadService business logic from main.cpp (Phase 2, Day 1).
