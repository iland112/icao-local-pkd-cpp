# ICAO Local PKD - C++ Implementation

**Version**: 1.0
**Last Updated**: 2025-12-29
**Status**: Phase 4 - LDAP Integration

---

## Project Overview

C++ REST API 기반의 ICAO Local PKD 관리 및 Passive Authentication (PA) 검증 시스템입니다.

### Core Features

| Module | Description | Status |
|--------|-------------|--------|
| **PKD Upload** | LDIF/Master List 파일 업로드, 파싱, 검증 | ✅ Complete |
| **Certificate Validation** | CSCA/DSC Trust Chain, CRL 검증 | ✅ Complete |
| **LDAP Integration** | OpenLDAP 연동 (ICAO PKD DIT) | ✅ Complete |
| **Passive Authentication** | ICAO 9303 PA 검증 (SOD, DG 해시) | Planning |
| **React.js Frontend** | CSR 기반 웹 UI | Planning |

### Technology Stack

| Category | Technology |
|----------|------------|
| **Language** | C++20 |
| **Web Framework** | Drogon 1.9+ |
| **Database** | PostgreSQL 15 + Drogon ORM |
| **LDAP** | OpenLDAP C API (libldap) |
| **Crypto** | OpenSSL 3.x |
| **JSON** | nlohmann/json |
| **Logging** | spdlog |
| **Build** | CMake 3.20+ |
| **Package Manager** | vcpkg |
| **Testing** | Catch2 |
| **Frontend** | React.js 18 + TypeScript + Vite |

---

## System Architecture

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         React.js Frontend                                │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │
│  │ PKD Upload  │  │ PA Verify   │  │ History     │  │ Dashboard   │    │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ↓ REST API
┌─────────────────────────────────────────────────────────────────────────┐
│                      C++ Backend (Drogon)                                │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                    REST API Controllers                            │  │
│  ├───────────────────────────────────────────────────────────────────┤  │
│  │                    Application Layer (Use Cases)                   │  │
│  ├───────────────────────────────────────────────────────────────────┤  │
│  │                    Domain Layer (Business Logic)                   │  │
│  ├───────────────────────────────────────────────────────────────────┤  │
│  │                    Infrastructure Layer (Adapters)                 │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
         │                              │
         ↓                              ↓
┌─────────────────┐          ┌─────────────────────────────────────────┐
│   PostgreSQL    │          │         OpenLDAP MMR Cluster            │
│     :5432       │          │  ┌───────────┐      ┌───────────┐       │
│                 │          │  │ OpenLDAP1 │◄────►│ OpenLDAP2 │       │
│ - certificate   │          │  └─────┬─────┘      └─────┬─────┘       │
│ - crl           │          │        └──────┬──────────┘              │
│ - passport_data │          │               ↓                         │
│ - audit_log     │          │        ┌───────────┐                    │
└─────────────────┘          │        │  HAProxy  │ :389               │
                             │        └───────────┘                    │
                             └─────────────────────────────────────────┘
```

### DDD Bounded Contexts (5개)

```
src/
├── shared/                    # Shared Kernel
├── fileupload/                # File Upload Context
├── fileparsing/               # File Parsing Context
├── certificatevalidation/     # Certificate Validation Context
├── ldapintegration/           # LDAP Integration Context
└── passiveauthentication/     # Passive Authentication Context
```

---

## LDAP DIT Structure (ICAO PKD)

```
dc=ldap,dc=smartcoreinc,dc=com
└── dc=download
    └── dc=pkd
        ├── dc=data
        │   └── c={COUNTRY}
        │       ├── o=csca    (CSCA certificates)
        │       ├── o=dsc     (DSC certificates)
        │       ├── o=crl     (CRL)
        │       └── o=ml      (Master Lists)
        └── dc=nc-data
            └── c={COUNTRY}
                └── o=dsc     (DSC_NC - Non-Conformant)
```

---

## Directory Structure

```
icao-local-pkd/
├── CMakeLists.txt              # Main CMake configuration
├── vcpkg.json                  # vcpkg dependencies
├── CLAUDE.md                   # This file
│
├── src/
│   ├── main.cpp                # Application entry point
│   ├── config/                 # Configuration classes
│   ├── shared/                 # Shared Kernel
│   │   ├── domain/             # Base classes (Entity, ValueObject, AggregateRoot)
│   │   ├── exception/          # Exception classes
│   │   ├── util/               # Utilities (Hash, Base64, UUID)
│   │   └── progress/           # SSE Progress service
│   ├── fileupload/             # File Upload Bounded Context
│   ├── fileparsing/            # File Parsing Bounded Context
│   ├── certificatevalidation/  # Certificate Validation Context
│   ├── ldapintegration/        # LDAP Integration Context
│   └── passiveauthentication/  # Passive Authentication Context
│
├── include/                    # Public headers
├── tests/                      # Unit & Integration tests
├── docker/                     # Docker configurations
├── scripts/                    # Build & run scripts
└── docs/                       # Documentation
```

---

## Coding Standards

### 1. Naming Conventions

```cpp
// Classes: PascalCase
class SecurityObjectDocument { };
class PassiveAuthenticationUseCase { };

// Methods: camelCase
void parseDataGroupHashes();
bool verifySignature();

// Variables: camelCase
std::string subjectDn;
int certificateCount;

// Constants: UPPER_SNAKE_CASE
const int MAX_BATCH_SIZE = 1000;
constexpr auto DEFAULT_TIMEOUT = std::chrono::seconds(30);

// Namespaces: lowercase with :: separator
namespace pa::domain::model { }
namespace shared::exception { }

// File names: PascalCase.hpp / PascalCase.cpp
// SecurityObjectDocument.hpp
// OpenSslSodParser.cpp
```

### 2. Value Object Pattern

```cpp
// Value Objects are immutable with factory methods
class SubjectDn {
private:
    std::string value_;

    explicit SubjectDn(std::string value) : value_(std::move(value)) {
        validate();
    }

    void validate() const {
        if (value_.empty()) {
            throw DomainException("INVALID_SUBJECT_DN", "Subject DN cannot be empty");
        }
    }

public:
    static SubjectDn of(const std::string& value) {
        return SubjectDn(value);
    }

    const std::string& getValue() const { return value_; }

    bool operator==(const SubjectDn& other) const {
        return value_ == other.value_;
    }
};
```

### 3. Exception Handling

```cpp
// Domain Layer
throw DomainException("INVALID_SOD", "SOD data cannot be empty");

// Application Layer
throw ApplicationException("CSCA_NOT_FOUND", "CSCA not found in LDAP");

// Infrastructure Layer
throw InfrastructureException("LDAP_CONNECTION_ERROR", "Failed to connect to LDAP");
```

### 4. Async/Coroutine Pattern (Drogon)

```cpp
// Use Drogon coroutines for async operations
drogon::Task<HttpResponsePtr> uploadFile(HttpRequestPtr req) {
    auto result = co_await processFileAsync(req);
    co_return HttpResponse::newHttpJsonResponse(result);
}
```

---

## Build & Run

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    libpq-dev \
    libldap2-dev \
    uuid-dev

# vcpkg installation
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg && ./bootstrap-vcpkg.sh
export VCPKG_ROOT=$(pwd)
```

### Build

```bash
# Configure
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build -j$(nproc)

# Run tests
cd build && ctest --output-on-failure
```

### Run

```bash
# Development
./build/icao-local-pkd

# Docker
docker-compose up -d
```

---

## API Endpoints

### File Upload

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/upload/ldif` | Upload LDIF file |
| POST | `/api/upload/masterlist` | Upload Master List file |
| GET | `/api/upload/history` | Get upload history |
| GET | `/api/upload/{id}/progress` | SSE progress stream |

### Passive Authentication

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/pa/verify` | Perform PA verification |
| GET | `/api/pa/history` | Get PA history |
| GET | `/api/pa/{id}` | Get PA result details |
| POST | `/api/pa/parse-dg1` | Parse DG1 (MRZ) |
| POST | `/api/pa/parse-dg2` | Parse DG2 (Face image) |

### Health Check

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/health` | Application health |
| GET | `/api/health/ldap` | LDAP connection status |
| GET | `/api/health/database` | Database connection status |

---

## ICAO 9303 Standards Reference

### Passive Authentication Workflow

```
1. Extract SOD from ePassport chip
2. Unwrap ICAO Tag 0x77 (if present)
3. Extract DSC from SOD certificates[0]
4. Lookup CSCA from LDAP using DSC Issuer DN
5. Verify Trust Chain: DSC.verify(CSCA.publicKey)
6. Verify SOD Signature: CMS_verify(SOD, DSC)
7. Verify Data Group Hashes: compare SOD hashes with actual DG hashes
8. Check CRL for DSC revocation status
9. Return verification result
```

### SOD Structure

```
Tag 0x77 (Application 23) - EF.SOD wrapper
  └─ CMS SignedData (Tag 0x30)
       ├─ encapContentInfo (LDSSecurityObject)
       │   └─ dataGroupHashValues (DG1, DG2, ... hashes)
       ├─ certificates [0]
       │   └─ DSC certificate (X.509)
       └─ signerInfos
           └─ signature
```

### CRL Status Values

| Status | Description |
|--------|-------------|
| VALID | Certificate is valid and not revoked |
| REVOKED | Certificate has been revoked |
| CRL_UNAVAILABLE | CRL not available in LDAP |
| CRL_EXPIRED | CRL has expired |
| CRL_INVALID | CRL signature verification failed |

---

## Development Phases

| Phase | Duration | Status | Description |
|-------|----------|--------|-------------|
| 1 | Week 1-2 | ✅ Complete | Project Foundation |
| 2 | Week 3-4 | ✅ Complete | File Upload Module |
| 3 | Week 5-6 | ✅ Complete | Certificate Validation |
| 4 | Week 7 | ✅ Complete | LDAP Integration |
| 5 | Week 8-9 | Pending | Passive Authentication |
| 6 | Week 10-11 | Pending | React.js Frontend |
| 7 | Week 12 | Pending | Integration & Testing |

### Phase 1 Tasks

- [x] Project directory structure
- [x] CLAUDE.md creation
- [x] Git repository initialization (commit: 612f6c2)
- [x] CMakeLists.txt configuration
- [x] vcpkg.json dependencies setup
- [x] Drogon hello world API (main.cpp with /api/health endpoint)
- [x] Docker development environment (docker-compose.yaml)
- [x] HAProxy config for LDAP load balancing
- [x] Shared kernel classes (Entity, ValueObject, AggregateRoot)
- [x] Exception classes (Domain, Application, Infrastructure)
- [x] Build/Run scripts (build.sh, run.sh, docker-start.sh)
- [x] vcpkg installation and build test
- [x] PostgreSQL connection test
- [x] LDAP connection test
- [x] OpenLDAP MMR (Multi-Master Replication) setup
- [x] ICAO PKD DIT structure initialization

---

## Key Documents

| Document | Location | Description |
|----------|----------|-------------|
| Implementation Plan | `docs/ICAO_LOCAL_PKD_CPP_IMPLEMENTATION_PLAN.md` | Detailed implementation plan |
| API Documentation | `docs/API.md` | REST API specification |
| Architecture | `docs/ARCHITECTURE.md` | System architecture details |
| Deployment | `docs/DEPLOYMENT.md` | Deployment guide |

---

## Reference Projects

- **Java Spring Boot Original**: `/home/kbjung/projects/java/smartcore/local-pkd`
- **ICAO Doc 9303 Part 11**: Security Mechanisms for MRTDs
- **ICAO Doc 9303 Part 12**: PKI for MRTDs
- **RFC 5280**: X.509 PKI Certificate and CRL Profile
- **RFC 5652**: Cryptographic Message Syntax (CMS)

---

**Project Owner**: kbjung
**Organization**: SmartCore Inc.

---

## Work History

### 2025-12-29: Initial Project Setup (Session 1)

**Objective**: Create C++ REST API based ICAO Local PKD by analyzing Java Spring Boot project

**Completed Tasks**:
1. Analyzed Java project at `/home/kbjung/projects/java/smartcore/local-pkd`
   - Read CLAUDE.md (37,224 chars) for system architecture reference
   - Analyzed key source files: BouncyCastleSodParser, PerformPassiveAuthenticationUseCase
   - Understood DDD bounded contexts and PA verification workflow

2. Created implementation plan document
   - Location: `docs/ICAO_LOCAL_PKD_CPP_IMPLEMENTATION_PLAN.md`
   - Covers 7 phases over 12 weeks

3. Project initialization
   - Created directory structure with 5 DDD bounded contexts
   - Created CLAUDE.md documentation
   - Initialized git repository (main branch)
   - Initial commit: 612f6c2

4. Build configuration
   - CMakeLists.txt with vcpkg integration
   - vcpkg.json with dependencies (drogon, openssl, libpq, nlohmann-json, spdlog, catch2)

5. Core source files
   - `src/main.cpp`: Drogon app with /api/health, /, /api endpoints
   - Shared kernel: ValueObject.hpp, Entity.hpp, AggregateRoot.hpp
   - Exception classes: DomainException, ApplicationException, InfrastructureException

6. Docker environment
   - docker-compose.yaml with all services (app, postgres, pgadmin, haproxy, openldap1, openldap2, phpldapadmin)
   - HAProxy config for LDAP load balancing

7. Utility scripts
   - build.sh: CMake build with options (--debug, --release, --clean, --no-tests)
   - run.sh: Execute built binary
   - docker-start.sh: Start Docker services (--build, --skip-app options)

**Next Steps**:
- Phase 2: File Upload Module implementation

### 2025-12-29: Phase 1 Completion (Session 2)

**Objective**: Complete Phase 1 - Project Foundation with infrastructure verification

**Completed Tasks**:
1. OpenLDAP MMR (Multi-Master Replication) Configuration
   - Created `docker-ldap-init.sh` script for MMR setup
   - Configured syncprov module on both OpenLDAP nodes
   - Set up bidirectional syncrepl replication
   - Verified MMR with entry count comparison test

2. ICAO PKD DIT Structure Creation
   - dc=pkd,dc=ldap,dc=smartcoreinc,dc=com (root)
   - dc=download,dc=pkd (container for PKD data)
   - dc=data,dc=download,dc=pkd (compliant certificates)
   - dc=nc-data,dc=download,dc=pkd (non-compliant certificates)

3. Build and Test Verification
   - CMake configuration with vcpkg toolchain
   - Successful build: icao-local-pkd executable + unit_tests
   - All 9 unit tests passed (100%)

4. Infrastructure Connection Tests
   - PostgreSQL: Connected to localpkd database (v15.15)
   - LDAP: Connected via HAProxy (port 389)
   - ICAO PKD custom schema verified: cn={9}icao-pkd

**Git Commits**:
- `ccedf23`: feat: Add OpenLDAP MMR configuration and initialization script

**Phase 1 Status**: ✅ COMPLETE

### 2025-12-29: Phase 2 - File Upload Module (Session 3)

**Objective**: Implement File Upload and File Parsing bounded contexts

**Completed Tasks**:
1. Database Schema (`docker/init-scripts/01-schema.sql`)
   - 8 tables: uploaded_file, certificate, crl, master_list, pa_verification, pa_data_group, revoked_certificate, audit_log
   - 3 views: v_upload_statistics, v_certificate_by_country, v_pa_statistics
   - Applied to PostgreSQL successfully

2. File Upload Bounded Context (`src/fileupload/`)
   - Domain Models:
     - UploadedFile (Aggregate Root) with UploadStatistics
     - Value Objects: UploadId, FileName, FileHash, FileSize
     - Enums: FileFormat (LDIF, ML), UploadStatus
     - Events: FileUploadedEvent, FileProcessingCompletedEvent
   - Application Layer:
     - UseCases: UploadLdifFileUseCase, UploadMasterListUseCase
     - UseCases: GetUploadHistoryUseCase, GetUploadDetailUseCase, GetUploadStatisticsUseCase
     - Commands: UploadFileCommand
     - Responses: UploadResponse, UploadDetailResponse, UploadHistoryResponse
   - Infrastructure Layer:
     - PostgresUploadedFileRepository (IUploadedFileRepository implementation)
     - LocalFileStorageAdapter (IFileStoragePort implementation)
     - UploadController (REST API with Drogon)

3. File Parsing Bounded Context (`src/fileparsing/`)
   - Domain Models:
     - CertificateType: CSCA, DSC, DSC_NC
     - CertificateData: Builder pattern with all certificate fields
     - CrlData: CRL with revoked certificates list
     - ParsedFile: Aggregate root for parsing results
   - Infrastructure Adapters:
     - OpenSslCertificateParser: X.509 parsing with OpenSSL 3.x
     - LdifParser: LDIF file parsing with base64 decoding
     - MasterListParser: CMS SignedData parsing for Master Lists

4. API Endpoints Implemented:
   - POST /api/upload/ldif - Upload LDIF file
   - POST /api/upload/masterlist - Upload Master List
   - GET /api/upload/history - Paginated upload history
   - GET /api/upload/{uploadId} - Upload details
   - GET /api/upload/statistics - Upload statistics

**Git Commits**:
- `7077f5f`: feat: Add File Upload module - Phase 2 implementation
- `56523cd`: feat: Add File Parsing module with LDIF and Master List parsers

**Phase 2 Status**: ✅ COMPLETE (Core Implementation)

### 2025-12-29: Phase 3 - Certificate Validation Module (Session 4)

**Objective**: Implement Certificate Validation bounded context with DDD architecture

**Completed Tasks**:
1. Domain Layer (`src/certificatevalidation/domain/`)
   - Aggregate Roots:
     - Certificate: Full certificate lifecycle management
     - CertificateRevocationList: CRL management with revoked serial tracking
   - Value Objects:
     - CertificateId, CrlId: UUID-based identifiers
     - CountryCode: ISO 3166-1 alpha-2 validation
     - CertificateType: CSCA, DSC, DSC_NC, DS, ML_SIGNER
     - CertificateStatus: PENDING, VALID, INVALID, EXPIRED, REVOKED
     - ValidationResult: Aggregated validation status
     - ValidationError: Error details with severity levels
     - X509Data, SubjectInfo, IssuerInfo: Certificate metadata
     - ValidityPeriod: NotBefore/NotAfter management
     - RevokedCertificates: Set of revoked serial numbers
     - X509CrlData, IssuerName: CRL metadata
   - Repository Interfaces:
     - ICertificateRepository: Certificate CRUD operations
     - ICrlRepository: CRL CRUD and lookup operations
   - Port Interface:
     - ICertificateValidationPort: OpenSSL validation operations
   - Domain Services:
     - TrustChainValidator: DSC -> CSCA trust chain validation
     - CrlChecker: Certificate revocation checking

2. Application Layer (`src/certificatevalidation/application/`)
   - Commands:
     - ValidateCertificateCommand: Single certificate validation
     - ValidateBatchCommand: Batch validation request
     - CheckRevocationCommand: Revocation check request
     - VerifyTrustChainCommand: Trust chain verification
   - Responses:
     - ValidateCertificateResponse: Validation result DTO
     - BatchValidationResponse: Batch results aggregation
     - ValidationSummaryResponse: Statistics summary
   - Use Cases:
     - ValidateCertificateUseCase: Full certificate validation
     - CheckRevocationUseCase: CRL-based revocation checking
     - VerifyTrustChainUseCase: Trust chain verification

3. Infrastructure Layer (`src/certificatevalidation/infrastructure/`)
   - Adapters:
     - OpenSslValidationAdapter: OpenSSL 3.x based validation
       - Signature verification (RSA, ECDSA)
       - Validity period checking
       - Basic Constraints validation
       - Key Usage validation
       - CRL-based revocation checking
       - Trust chain building
   - Repositories:
     - PostgresCertificateRepository: Drogon ORM implementation
     - PostgresCrlRepository: CRL persistence

4. Build Configuration:
   - Added UUID library dependency to CMakeLists.txt
   - Created .cpp stubs for compilation unit requirements
   - Build successful: 100% tests passed (9/9)

**Git Commits**:
- `e870314`: feat: Add Certificate Validation module - Phase 3 implementation

**Phase 3 Status**: ✅ COMPLETE

### 2025-12-29: Phase 4 - LDAP Integration Module (Session 5)

**Objective**: Implement LDAP Integration bounded context with OpenLDAP C API

**Completed Tasks**:
1. Domain Layer (`src/ldapintegration/domain/`)
   - Value Objects:
     - DistinguishedName: RFC 2253 format DN validation
       - RDN parsing and extraction
       - Attribute extraction (CN, OU, O, DC, C)
       - Parent DN navigation
       - Case-insensitive comparison
     - LdapEntryType: CSCA, DSC, DSC_NC, CRL, MASTER_LIST
       - OU path generation for each type
   - Entities:
     - LdapCertificateEntry: Certificate LDAP entry
       - DN building from subject DN
       - Validity checking
       - Base64 encoding
       - Sync status tracking
     - LdapCrlEntry: CRL LDAP entry
       - Revoked serial numbers tracking
       - Expiration checking
       - Update detection
     - LdapMasterListEntry: Master List LDAP entry
       - Version comparison for updates
       - Certificate count tracking
   - Port Interface:
     - ILdapConnectionPort: Comprehensive LDAP operations
       - Connection management (pool stats, test)
       - Certificate CRUD operations
       - CRL CRUD operations
       - Master List CRUD operations
       - Generic search capabilities
       - Progress callback support

2. Infrastructure Layer (`src/ldapintegration/infrastructure/`)
   - Adapters:
     - OpenLdapAdapter: OpenLDAP C API implementation
       - Connection pooling (configurable size)
       - Thread-safe operations with mutex
       - Automatic reconnection
       - Binary attribute handling
       - LDAP filter escaping
       - Batch operations with progress
       - Entry existence checking
       - Search with scope (base, onelevel, subtree)
   - Controller:
     - LdapController: REST API endpoints
       - GET /api/ldap/health - Health check
       - GET /api/ldap/statistics - Statistics
       - GET /api/ldap/certificates - Search certificates
       - GET /api/ldap/certificates/{fingerprint} - Get by fingerprint
       - GET /api/ldap/crls - Search CRLs
       - GET /api/ldap/crls/issuer - Get CRL by issuer
       - GET /api/ldap/revocation/check - Check revocation

3. Application Layer (`src/ldapintegration/application/`)
   - Use Cases:
     - UploadToLdapUseCase: Batch upload operations
       - Certificate upload with skip/update options
       - CRL upload with version comparison
       - Master List upload with version checking
       - Country structure initialization
       - Progress callback support
     - LdapHealthCheckUseCase: Health monitoring
       - Connection availability
       - Response time measurement
       - Pool statistics
       - Entry count by type
       - Country statistics
     - SearchLdapUseCase: Search capabilities
       - Certificate search by country/fingerprint/issuer
       - CRL search by country/issuer
       - CSCA lookup for DSC verification
       - Revocation status checking

4. Build Configuration:
   - Updated CMakeLists.txt with new LDAP source files
   - Build successful: 100% tests passed (9/9)

**API Endpoints Implemented**:
- GET /api/ldap/health - LDAP health check
- GET /api/ldap/statistics - LDAP statistics (counts by type)
- GET /api/ldap/certificates - Search certificates
- GET /api/ldap/certificates/{fingerprint} - Get certificate by fingerprint
- GET /api/ldap/crls - Search CRLs
- GET /api/ldap/crls/issuer - Get CRL by issuer DN
- GET /api/ldap/revocation/check - Check certificate revocation

**Phase 4 Status**: ✅ COMPLETE

**Next Steps**:
- Phase 5: Passive Authentication Module
