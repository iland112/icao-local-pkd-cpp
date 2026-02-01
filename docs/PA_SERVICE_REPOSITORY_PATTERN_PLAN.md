# PA Service Repository Pattern Refactoring Plan

**Project**: ICAO Local PKD - PA Service Refactoring
**Version**: v1.0.0
**Date**: 2026-02-01
**Status**: Planning Phase
**Branch**: feature/pa-service-repository-pattern

---

## Executive Summary

This document outlines the plan to refactor the PA (Passive Authentication) Service from a monolithic architecture to a clean, layered architecture following the Repository Pattern, DDD (Domain-Driven Design), and MSA (Microservices Architecture) principles, matching the pkd-management service refactoring completed in v2.1.5.

### Current State

**File Structure**:
- `services/pa-service/src/main.cpp` (3,706 lines) - All logic in one file
- `services/pa-service/src/common/audit_log.h` - Audit logging header

**Architecture Issues**:
- ❌ Direct PostgreSQL queries in controller handlers
- ❌ Direct LDAP queries in controller handlers
- ❌ Business logic mixed with controller code
- ❌ No separation of concerns
- ❌ Difficult to test
- ❌ Database-dependent controllers

**API Endpoints** (12 total):
1. `/api/health` - Health check
2. `/api/health/database` - Database health check
3. `/api/health/ldap` - LDAP health check
4. `/api/pa/verify` - **PA verification (core functionality)**
5. `/api/pa/history` - PA verification history
6. `/api/pa/{id}` - Get PA verification by ID
7. `/api/pa/statistics` - Statistics
8. `/api/pa/parse-dg1` - Parse Data Group 1 (MRZ)
9. `/api/pa/parse-mrz-text` - Parse MRZ text
10. `/api/pa/parse-dg2` - Parse Data Group 2 (Face)
11. `/api/pa/parse-sod` - Parse Security Object Document (SOD)
12. `/api/pa/{id}/datagroups` - Get data groups by verification ID

### Target State

**Layered Architecture**:
```
Controller (main.cpp - Front Controller)
    ↓
Service Layer (Business Logic)
    ↓
Repository Layer (Data Access)
    ↓
Database (PostgreSQL) / LDAP (OpenLDAP)
```

**Benefits**:
- ✅ Clean separation of concerns (Controller → Service → Repository)
- ✅ 100% SQL elimination from controllers
- ✅ Database-agnostic architecture
- ✅ Testable with mockable layers
- ✅ Maintainable and scalable
- ✅ Consistent with pkd-management service

---

## Architecture Design

### 1. Domain Models Layer

**Purpose**: Define core business entities as plain structs (DTOs)

**Models to Create**:

#### 1.1 PaVerification (Domain Model)
Represents a PA verification record in database.

**Fields**:
- `id` (UUID) - Primary key
- `documentNumber` (string) - MRZ document number
- `countryCode` (string) - Issuing country
- `verificationStatus` (string) - VALID/INVALID/ERROR
- `sodHash` (string) - SOD hash
- `dscSubject` (string) - DSC subject DN
- `dscSerialNumber` (string) - DSC serial number
- `cscaSubject` (string) - CSCA subject DN
- `cscaSerialNumber` (string) - CSCA serial number
- `certificateChainValid` (bool)
- `sodSignatureValid` (bool)
- `dataGroupsValid` (bool)
- `crlChecked` (bool)
- `revoked` (bool)
- `createdAt` (timestamp)
- `metadata` (JSON) - Additional verification details

#### 1.2 SodData (Domain Model)
Represents parsed SOD (CMS SignedData).

**Fields**:
- `signatureAlgorithm` (string)
- `hashAlgorithm` (string)
- `dscCertificate` (X509*) - Extracted DSC certificate
- `dataGroupHashes` (map<string, string>) - DG number → hash
- `signedAttributes` (map<string, string>)
- `ldsSecurityObjectVersion` (string)

#### 1.3 DataGroup (Domain Model)
Represents a data group with hash verification result.

**Fields**:
- `dgNumber` (string) - DG1, DG2, etc.
- `expectedHash` (string) - From SOD
- `actualHash` (string) - Computed from data
- `valid` (bool) - Hash match result
- `data` (vector<uint8_t>) - Raw data

#### 1.4 CertificateChainValidation (Domain Model)
Represents trust chain validation result.

**Fields**:
- `valid` (bool)
- `dscSubject` (string)
- `dscSerialNumber` (string)
- `dscExpired` (bool)
- `cscaSubject` (string)
- `cscaSerialNumber` (string)
- `cscaExpired` (bool)
- `validAtSigningTime` (bool)
- `crlStatus` (enum CrlStatus)
- `crlMessage` (string)
- `validationErrors` (string)

**Files to Create**:
- `services/pa-service/src/domain/models/pa_verification.h`
- `services/pa-service/src/domain/models/pa_verification.cpp`
- `services/pa-service/src/domain/models/sod_data.h`
- `services/pa-service/src/domain/models/sod_data.cpp`
- `services/pa-service/src/domain/models/data_group.h`
- `services/pa-service/src/domain/models/data_group.cpp`
- `services/pa-service/src/domain/models/certificate_chain_validation.h`
- `services/pa-service/src/domain/models/certificate_chain_validation.cpp`

---

### 2. Repository Layer

**Purpose**: Isolate all database and LDAP access logic

**Design Principles**:
- Constructor-based dependency injection (PGconn*, LDAP*)
- 100% parameterized queries (SQL injection protection)
- Exception-based error handling
- JSON responses for API compatibility

#### 2.1 PaVerificationRepository

**Responsibility**: PA verification record CRUD operations in PostgreSQL

**Methods**:
```cpp
class PaVerificationRepository {
private:
    PGconn* dbConn_;

public:
    PaVerificationRepository(PGconn* conn);

    // Create
    std::string insert(const PaVerification& verification);

    // Read
    Json::Value findById(const std::string& id);
    Json::Value findAll(int limit, int offset, const std::string& status = "");
    Json::Value getStatistics();

    // Update
    bool updateStatus(const std::string& id, const std::string& status);

    // Delete
    bool deleteById(const std::string& id);

    // Helper methods
    PGresult* executeParamQuery(const std::string& query, const std::vector<std::string>& params);
    PGresult* executeQuery(const std::string& query);
    Json::Value pgResultToJson(PGresult* res);
};
```

**SQL Queries to Migrate**:
- INSERT INTO pa_verification (...)
- SELECT * FROM pa_verification WHERE id = ?
- SELECT * FROM pa_verification ORDER BY created_at DESC LIMIT ? OFFSET ?
- SELECT COUNT(*) FROM pa_verification GROUP BY verification_status

#### 2.2 LdapCertificateRepository

**Responsibility**: CSCA/DSC certificate retrieval from LDAP

**Methods**:
```cpp
class LdapCertificateRepository {
private:
    LDAP* ldapConn_;
    std::string baseDn_;

public:
    LdapCertificateRepository(LDAP* conn, const std::string& baseDn);

    // Find CSCA certificate by subject DN
    X509* findCscaBySubjectDn(const std::string& subjectDn, const std::string& country);

    // Find all CSCAs for a country (including link certificates)
    std::vector<X509*> findAllCscasByCountry(const std::string& country);

    // Helper methods
    std::string buildLdapFilter(const std::string& type, const std::string& country);
    X509* parseCertificateFromLdap(struct berval** certData);
};
```

**LDAP Queries to Migrate**:
- Search CSCA in `o=csca,c={COUNTRY},dc=data,...`
- Search DSC in `o=dsc,c={COUNTRY},dc=data,...`

#### 2.3 LdapCrlRepository

**Responsibility**: CRL retrieval from LDAP

**Methods**:
```cpp
class LdapCrlRepository {
private:
    LDAP* ldapConn_;
    std::string baseDn_;

public:
    LdapCrlRepository(LDAP* conn, const std::string& baseDn);

    // Find CRL by issuer DN and country
    X509_CRL* findCrlByIssuer(const std::string& issuerDn, const std::string& country);

    // Check if certificate is revoked
    bool isCertificateRevoked(X509* cert, X509_CRL* crl);

    // Helper methods
    X509_CRL* parseCrlFromLdap(struct berval** crlData);
};
```

**LDAP Queries to Migrate**:
- Search CRL in `c={COUNTRY},dc=data,...`

**Files to Create**:
- `services/pa-service/src/repositories/pa_verification_repository.h`
- `services/pa-service/src/repositories/pa_verification_repository.cpp`
- `services/pa-service/src/repositories/ldap_certificate_repository.h`
- `services/pa-service/src/repositories/ldap_certificate_repository.cpp`
- `services/pa-service/src/repositories/ldap_crl_repository.h`
- `services/pa-service/src/repositories/ldap_crl_repository.cpp`

---

### 3. Service Layer

**Purpose**: Implement business logic and orchestrate repositories

**Design Principles**:
- Constructor-based repository injection
- Business rule enforcement
- Validation and error handling
- Orchestration of multiple repositories

#### 3.1 PaVerificationService

**Responsibility**: Orchestrate PA verification workflow

**Methods**:
```cpp
class PaVerificationService {
private:
    PaVerificationRepository* paRepo_;
    SodParserService* sodParser_;
    CertificateValidationService* certValidator_;
    DataGroupParserService* dgParser_;

public:
    PaVerificationService(
        PaVerificationRepository* paRepo,
        SodParserService* sodParser,
        CertificateValidationService* certValidator,
        DataGroupParserService* dgParser
    );

    // Main PA verification workflow
    Json::Value verifyPassiveAuthentication(
        const std::vector<uint8_t>& sodData,
        const std::map<std::string, std::vector<uint8_t>>& dataGroups
    );

    // History and statistics
    Json::Value getVerificationHistory(int limit, int offset, const std::string& status = "");
    Json::Value getVerificationById(const std::string& id);
    Json::Value getStatistics();

    // Data group retrieval
    Json::Value getDataGroupsByVerificationId(const std::string& id);
};
```

#### 3.2 SodParserService

**Responsibility**: Parse SOD (CMS SignedData) and extract DSC

**Methods**:
```cpp
class SodParserService {
public:
    SodParserService();

    // Parse SOD from binary data
    SodData parseSod(const std::vector<uint8_t>& sodBytes);

    // Extract DSC certificate from SOD
    X509* extractDscCertificate(const std::vector<uint8_t>& sodBytes);

    // Extract data group hashes from SOD
    std::map<std::string, std::string> extractDataGroupHashes(const std::vector<uint8_t>& sodBytes);

    // Verify SOD signature
    bool verifySodSignature(const std::vector<uint8_t>& sodBytes, X509* dscCert);
};
```

#### 3.3 DataGroupParserService

**Responsibility**: Parse and verify data groups (DG1, DG2, MRZ)

**Methods**:
```cpp
class DataGroupParserService {
public:
    DataGroupParserService();

    // Parse MRZ from DG1
    Json::Value parseDg1(const std::vector<uint8_t>& dg1Data);

    // Parse MRZ from text
    Json::Value parseMrzText(const std::string& mrzText);

    // Parse face image from DG2
    Json::Value parseDg2(const std::vector<uint8_t>& dg2Data);

    // Verify data group hashes
    bool verifyDataGroupHash(
        const std::vector<uint8_t>& dgData,
        const std::string& expectedHash,
        const std::string& hashAlgorithm
    );

    // Compute hash for data group
    std::string computeHash(const std::vector<uint8_t>& data, const std::string& algorithm);
};
```

#### 3.4 CertificateValidationService

**Responsibility**: Trust chain validation with CSCA, CRL checking

**Methods**:
```cpp
class CertificateValidationService {
private:
    LdapCertificateRepository* certRepo_;
    LdapCrlRepository* crlRepo_;

public:
    CertificateValidationService(
        LdapCertificateRepository* certRepo,
        LdapCrlRepository* crlRepo
    );

    // Validate DSC certificate and build trust chain
    CertificateChainValidation validateCertificateChain(
        X509* dscCert,
        const std::string& countryCode
    );

    // Verify certificate signature
    bool verifyCertificateSignature(X509* cert, X509* issuerCert);

    // Check certificate expiration
    bool isCertificateExpired(X509* cert);

    // Check CRL revocation status
    CrlStatus checkCrlStatus(X509* cert, const std::string& countryCode);

    // Build trust chain (DSC → Link Cert → Root CSCA)
    std::vector<X509*> buildTrustChain(X509* dscCert, const std::string& countryCode);
};
```

**Files to Create**:
- `services/pa-service/src/services/pa_verification_service.h`
- `services/pa-service/src/services/pa_verification_service.cpp`
- `services/pa-service/src/services/sod_parser_service.h`
- `services/pa-service/src/services/sod_parser_service.cpp`
- `services/pa-service/src/services/data_group_parser_service.h`
- `services/pa-service/src/services/data_group_parser_service.cpp`
- `services/pa-service/src/services/certificate_validation_service.h`
- `services/pa-service/src/services/certificate_validation_service.cpp`

---

### 4. Controller Layer (main.cpp)

**Current**: 3,706 lines with all logic embedded

**Target**: ~500 lines as thin front controller

**Responsibilities**:
- HTTP request/response handling
- Service initialization
- Dependency injection
- Global exception handling
- CORS configuration

**Code Reduction Target**:
- **Before**: 3,706 lines
- **After**: ~500 lines (controller only)
- **Reduction**: ~3,200 lines (86% reduction)

**Example Endpoint Migration**:

**Before** (Direct SQL + LDAP + OpenSSL in handler):
```cpp
app.registerHandler("/api/pa/verify",
    [](const HttpRequestPtr& req, Callback&& callback) {
        // 200+ lines of:
        // - SOD parsing
        // - LDAP CSCA search
        // - Certificate validation
        // - Data group verification
        // - PostgreSQL INSERT
        // - JSON response building
    }
);
```

**After** (Service call):
```cpp
app.registerHandler("/api/pa/verify",
    [](const HttpRequestPtr& req, Callback&& callback) {
        try {
            auto json = req->getJsonObject();
            std::vector<uint8_t> sodData = /* extract from request */;
            std::map<std::string, std::vector<uint8_t>> dataGroups = /* extract */;

            Json::Value response = paVerificationService->verifyPassiveAuthentication(
                sodData, dataGroups
            );

            callback(HttpResponse::newHttpJsonResponse(response));
        } catch (const std::exception& e) {
            Json::Value error;
            error["success"] = false;
            error["error"] = e.what();
            callback(HttpResponse::newHttpJsonResponse(error));
        }
    }
);
```

---

## Implementation Phases

### Phase 1: Repository Layer Implementation (Days 1-2)

**Objective**: Create Repository classes with database/LDAP access methods

**Tasks**:
1. Create directory structure: `repositories/`, `domain/models/`
2. Implement Domain Models (4 models: PaVerification, SodData, DataGroup, CertificateChainValidation)
3. Implement PaVerificationRepository (PostgreSQL operations)
4. Implement LdapCertificateRepository (CSCA/DSC retrieval)
5. Implement LdapCrlRepository (CRL retrieval)
6. Update CMakeLists.txt with new source files

**Deliverables**:
- 8 header files (.h)
- 8 implementation files (.cpp)
- All methods with parameterized queries
- Exception-based error handling

**Verification**:
- Compile without errors
- Basic unit tests (optional)

---

### Phase 2: Service Layer Implementation (Days 3-4)

**Objective**: Create Service classes with business logic

**Tasks**:
1. Create directory structure: `services/`
2. Implement SodParserService (SOD parsing, DSC extraction)
3. Implement DataGroupParserService (DG1, DG2, MRZ parsing)
4. Implement CertificateValidationService (Trust chain, CRL checking)
5. Implement PaVerificationService (Orchestration)
6. Update CMakeLists.txt

**Deliverables**:
- 8 header files (.h)
- 8 implementation files (.cpp)
- Repository injection via constructor
- JSON response formatting

**Verification**:
- Compile without errors
- Services ready for main.cpp integration

---

### Phase 3: main.cpp Service Integration (Day 5)

**Objective**: Initialize repositories and services in main.cpp

**Tasks**:
1. Create global pointers for Repositories and Services
2. Initialize repositories after DB/LDAP connection
3. Initialize services with repository injection
4. Add cleanup before program exit

**Example**:
```cpp
// Global pointers
repositories::PaVerificationRepository* paRepo = nullptr;
repositories::LdapCertificateRepository* certRepo = nullptr;
repositories::LdapCrlRepository* crlRepo = nullptr;

services::SodParserService* sodParser = nullptr;
services::DataGroupParserService* dgParser = nullptr;
services::CertificateValidationService* certValidator = nullptr;
services::PaVerificationService* paService = nullptr;

// In main() after DB/LDAP connection
paRepo = new repositories::PaVerificationRepository(dbConn);
certRepo = new repositories::LdapCertificateRepository(ldapConn, ldapBaseDn);
crlRepo = new repositories::LdapCrlRepository(ldapConn, ldapBaseDn);

sodParser = new services::SodParserService();
dgParser = new services::DataGroupParserService();
certValidator = new services::CertificateValidationService(certRepo, crlRepo);
paService = new services::PaVerificationService(paRepo, sodParser, certValidator, dgParser);

// Cleanup before exit
delete paService;
delete certValidator;
delete dgParser;
delete sodParser;
delete crlRepo;
delete certRepo;
delete paRepo;
```

**Deliverables**:
- Services initialized in main.cpp
- Ready for endpoint migration

**Verification**:
- Application starts without errors
- Services available to handlers

---

### Phase 4: API Endpoint Migration (Days 6-8)

**Objective**: Migrate all endpoints from direct queries to Service calls

**Endpoints to Migrate** (12 total):

**Priority 1 - Core PA Functionality** (Days 6-7):
1. `/api/pa/verify` → paService->verifyPassiveAuthentication()
2. `/api/pa/parse-sod` → sodParser->parseSod()
3. `/api/pa/parse-dg1` → dgParser->parseDg1()
4. `/api/pa/parse-dg2` → dgParser->parseDg2()
5. `/api/pa/parse-mrz-text` → dgParser->parseMrzText()

**Priority 2 - History and Statistics** (Day 8):
6. `/api/pa/history` → paService->getVerificationHistory()
7. `/api/pa/{id}` → paService->getVerificationById()
8. `/api/pa/statistics` → paService->getStatistics()
9. `/api/pa/{id}/datagroups` → paService->getDataGroupsByVerificationId()

**Priority 3 - Health Checks** (Day 8):
10. `/api/health` - Keep as is (no DB/LDAP access)
11. `/api/health/database` - Keep as is
12. `/api/health/ldap` - Keep as is

**Code Reduction Metrics**:
- Target: 86% reduction in main.cpp (3,706 → ~500 lines)
- Zero SQL queries in controllers
- Zero LDAP queries in controllers
- All OpenSSL operations in Service layer

**Verification**:
- All endpoints return correct responses
- No SQL/LDAP in main.cpp handlers
- Audit logs working correctly

---

### Phase 5: Testing and Verification (Day 9)

**Objective**: End-to-end testing with real PA data

**Test Scenarios**:
1. **Valid PA Verification**: Korean passport with valid DSC/CSCA
2. **Expired Certificate**: DSC expired but CSCA valid
3. **Revoked Certificate**: DSC revoked in CRL
4. **Invalid SOD Signature**: Tampered SOD data
5. **Invalid Data Group Hash**: Modified DG1 data
6. **Link Certificate Chain**: Multi-level trust chain (DSC → Link → Root)
7. **CRL Unavailable**: Missing CRL for country
8. **History and Statistics**: Verify DB queries working correctly

**Verification Checklist**:
- ✅ All 12 endpoints functional
- ✅ PA verification returns correct results
- ✅ Trust chain validation working
- ✅ CRL checking working
- ✅ Data group hash verification working
- ✅ Audit logs recording correctly
- ✅ Statistics API returning correct data
- ✅ No SQL/LDAP in main.cpp

---

### Phase 6: Documentation (Day 10)

**Documents to Create/Update**:

1. **PA_SERVICE_REPOSITORY_PATTERN_COMPLETION.md**
   - Implementation summary
   - Code metrics (before/after)
   - Architecture diagrams
   - Verification results

2. **CLAUDE.md Update**
   - Add PA Service refactoring to version history
   - Update architecture section
   - Update file structure

3. **Code Documentation**
   - Doxygen comments for all classes
   - Method documentation
   - Usage examples

---

## Success Metrics

### Code Quality

| Metric | Before | After | Target |
|--------|--------|-------|--------|
| SQL in Controllers | ~200 lines | 0 lines | 100% reduction |
| LDAP in Controllers | ~150 lines | 0 lines | 100% reduction |
| Controller Code | 3,706 lines | ~500 lines | 86% reduction |
| Parameterized Queries | 60% | 100% | 100% coverage |
| OpenSSL in Controllers | ~800 lines | 0 lines | 100% reduction |

### Architecture

| Metric | Before | After |
|--------|--------|-------|
| Separation of Concerns | ❌ None | ✅ 3 layers |
| Database Independence | ❌ PostgreSQL-dependent | ✅ Repository only |
| Testability | ❌ Low | ✅ High (mockable) |
| Maintainability | ❌ Monolithic | ✅ Modular |

### Performance

- No performance degradation (same queries, different location)
- Response time within 5% of current implementation
- Memory usage stable

---

## Risk Mitigation

### Technical Risks

1. **OpenSSL Memory Management**
   - Risk: Memory leaks with X509*, X509_CRL*
   - Mitigation: Strict RAII pattern, X509_free() in destructors

2. **LDAP Connection Stability**
   - Risk: Connection timeouts during long operations
   - Mitigation: Connection health checks, retry logic

3. **Backward Compatibility**
   - Risk: API response format changes
   - Mitigation: Match existing JSON structure exactly

### Process Risks

1. **Scope Creep**
   - Risk: Adding new features during refactoring
   - Mitigation: Strict "refactor only, no new features" policy

2. **Testing Complexity**
   - Risk: Difficult to test trust chain scenarios
   - Mitigation: Use existing PKD test data (31,212 certificates)

---

## Timeline

| Phase | Days | Deliverable |
|-------|------|-------------|
| Phase 1: Repository Layer | 2 | 3 Repositories, 4 Domain Models |
| Phase 2: Service Layer | 2 | 4 Services |
| Phase 3: main.cpp Integration | 1 | Service initialization |
| Phase 4: Endpoint Migration | 3 | 12 endpoints migrated |
| Phase 5: Testing | 1 | All scenarios verified |
| Phase 6: Documentation | 1 | Complete documentation |
| **Total** | **10 days** | Production-ready refactoring |

---

## Conclusion

This refactoring will transform the PA Service from a monolithic 3,706-line main.cpp to a clean, layered architecture matching the pkd-management service design. The result will be:

- ✅ **86% code reduction** in main.cpp (3,706 → ~500 lines)
- ✅ **100% SQL/LDAP elimination** from controllers
- ✅ **Database-agnostic architecture** ready for Oracle migration
- ✅ **Testable** with mockable Service/Repository layers
- ✅ **Maintainable** with clear separation of concerns
- ✅ **Consistent** with pkd-management service architecture

**Next Steps**:
1. Review and approve this plan
2. Create feature branch: `feature/pa-service-repository-pattern`
3. Begin Phase 1: Repository Layer Implementation

---

**Document Version**: 1.0
**Last Updated**: 2026-02-01
**Author**: Claude Code (Anthropic)
**Project**: ICAO Local PKD - PA Service Repository Pattern Refactoring
