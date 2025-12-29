# ICAO Local PKD C++ Implementation Plan

**Version**: 1.0
**Created**: 2025-12-29
**Based on**: Java Spring Boot local-pkd project analysis
**Target**: C++ REST API Backend + React.js Frontend

---

## 1. Executive Summary

### 1.1 Project Overview

Java Spring Boot 기반의 ICAO Local PKD 관리 및 Passive Authentication 시스템을 C++ REST API 백엔드와 React.js 프론트엔드로 재구현합니다.

### 1.2 Core Features (원본 Java 프로젝트 기준)

| Module | Description | Status in Java |
|--------|-------------|----------------|
| **PKD Upload** | LDIF/Master List 파일 업로드, 파싱, 검증 | Production Ready |
| **Certificate Validation** | CSCA/DSC Trust Chain, CRL 검증 | Production Ready |
| **LDAP Integration** | OpenLDAP MMR 클러스터 연동 | Production Ready |
| **Passive Authentication** | ICAO 9303 PA 검증 (SOD, DG 해시) | Production Ready |
| **Statistics Dashboard** | PKD/PA 통계 대시보드 | Production Ready |

### 1.3 Technology Stack Comparison

| Category | Java (Original) | C++ (Target) |
|----------|-----------------|--------------|
| **Web Framework** | Spring Boot 3.5.9 | **Drogon 1.9+** |
| **Language** | Java 21 | C++20 |
| **Database** | PostgreSQL + JPA/Hibernate | PostgreSQL + Drogon ORM |
| **LDAP** | UnboundID SDK | OpenLDAP C API (libldap) |
| **Crypto** | Bouncy Castle 1.78 | OpenSSL 3.x |
| **JSON** | Jackson | nlohmann/json |
| **Async** | @Async, CompletableFuture | Drogon Coroutines |
| **Build** | Maven | CMake 3.20+ |
| **Frontend** | Thymeleaf + HTMX + Alpine.js | **React.js 18 + TypeScript** |

---

## 2. System Architecture

### 2.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Client Layer                                    │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                    React.js 18 + TypeScript                            │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐   │  │
│  │  │ PKD Upload  │  │ PA Verify   │  │ History     │  │ Dashboard   │   │  │
│  │  │   Page      │  │   Page      │  │   Page      │  │   Page      │   │  │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘   │  │
│  │                         ↓ REST API / WebSocket                         │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                           C++ Backend (Drogon)                               │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                         REST API Layer                                 │  │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐        │  │
│  │  │ FileUpload      │  │ PA Controller   │  │ Statistics      │        │  │
│  │  │ Controller      │  │                 │  │ Controller      │        │  │
│  │  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘        │  │
│  └───────────┼────────────────────┼────────────────────┼────────────────┘  │
│              │                    │                    │                    │
│  ┌───────────┼────────────────────┼────────────────────┼────────────────┐  │
│  │           ↓     Application Layer (Use Cases)       ↓                 │  │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐        │  │
│  │  │ UploadFile      │  │ PerformPA       │  │ GetStatistics   │        │  │
│  │  │ UseCase         │  │ UseCase         │  │ UseCase         │        │  │
│  │  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘        │  │
│  └───────────┼────────────────────┼────────────────────┼────────────────┘  │
│              │                    │                    │                    │
│  ┌───────────┼────────────────────┼────────────────────┼────────────────┐  │
│  │           ↓        Domain Layer (Business Logic)    ↓                 │  │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐        │  │
│  │  │ Certificate     │  │ PassportData    │  │ ParsedFile      │        │  │
│  │  │ Aggregate       │  │ Aggregate       │  │ Aggregate       │        │  │
│  │  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘        │  │
│  └───────────┼────────────────────┼────────────────────┼────────────────┘  │
│              │                    │                    │                    │
│  ┌───────────┼────────────────────┼────────────────────┼────────────────┐  │
│  │           ↓       Infrastructure Layer (Adapters)   ↓                 │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐   │  │
│  │  │ PostgreSQL  │  │ OpenLDAP    │  │ OpenSSL     │  │ FileStorage │   │  │
│  │  │ Repository  │  │ Adapter     │  │ Adapter     │  │ Adapter     │   │  │
│  │  └──────┬──────┘  └──────┬──────┘  └─────────────┘  └─────────────┘   │  │
│  └─────────┼────────────────┼───────────────────────────────────────────┘  │
└────────────┼────────────────┼───────────────────────────────────────────────┘
             │                │
             ↓                ↓
┌────────────────────┐  ┌─────────────────────────────────────────────────────┐
│    PostgreSQL      │  │              OpenLDAP MMR Cluster                    │
│      :5432         │  │  ┌───────────┐      ┌───────────┐                   │
│                    │  │  │ OpenLDAP1 │◄────►│ OpenLDAP2 │                   │
│  - uploaded_file   │  │  │  :3891    │      │  :3892    │                   │
│  - certificate     │  │  └─────┬─────┘      └─────┬─────┘                   │
│  - crl             │  │        └──────┬──────────┘                          │
│  - passport_data   │  │               ↓                                      │
│  - audit_log       │  │        ┌───────────┐                                │
│                    │  │        │  HAProxy  │ :389                           │
└────────────────────┘  │        └───────────┘                                │
                        └─────────────────────────────────────────────────────┘
```

### 2.2 LDAP DIT Structure (ICAO PKD Standard)

```
dc=ldap,dc=smartcoreinc,dc=com
└── dc=download
    └── dc=pkd
        ├── dc=data
        │   ├── c=KR
        │   │   ├── o=csca    (CSCA certificates)
        │   │   ├── o=dsc     (DSC certificates)
        │   │   ├── o=crl     (CRL)
        │   │   └── o=ml      (Master Lists)
        │   ├── c=US
        │   └── ...
        └── dc=nc-data
            └── c=KR
                └── o=dsc     (DSC_NC - Non-Conformant)
```

---

## 3. Project Structure

### 3.1 C++ Backend Directory Structure

```
icao-local-pkd/
├── CMakeLists.txt
├── vcpkg.json                          # vcpkg dependency manifest
├── conanfile.txt                       # Alternative: Conan dependencies
│
├── src/
│   ├── main.cpp                        # Application entry point
│   │
│   ├── config/                         # Configuration
│   │   ├── AppConfig.hpp
│   │   ├── AppConfig.cpp
│   │   ├── DatabaseConfig.hpp
│   │   ├── LdapConfig.hpp
│   │   └── OpenSslConfig.hpp
│   │
│   ├── shared/                         # Shared Kernel
│   │   ├── domain/
│   │   │   ├── ValueObject.hpp         # Base class for Value Objects
│   │   │   ├── Entity.hpp              # Base class for Entities
│   │   │   ├── AggregateRoot.hpp       # Base class for Aggregates
│   │   │   └── DomainEvent.hpp         # Domain Event base
│   │   ├── exception/
│   │   │   ├── DomainException.hpp
│   │   │   ├── ApplicationException.hpp
│   │   │   └── InfrastructureException.hpp
│   │   ├── util/
│   │   │   ├── HashUtil.hpp            # SHA-256/384/512 utilities
│   │   │   ├── Base64Util.hpp
│   │   │   ├── DateTimeUtil.hpp
│   │   │   └── UuidUtil.hpp
│   │   └── progress/
│   │       ├── ProcessingProgress.hpp   # SSE progress model
│   │       └── ProgressService.hpp
│   │
│   ├── fileupload/                     # Bounded Context: File Upload
│   │   ├── domain/
│   │   │   ├── model/
│   │   │   │   ├── UploadedFile.hpp    # Aggregate Root
│   │   │   │   ├── UploadId.hpp        # Value Object
│   │   │   │   ├── FileName.hpp
│   │   │   │   ├── FileHash.hpp
│   │   │   │   ├── FileSize.hpp
│   │   │   │   ├── FileFormat.hpp      # LDIF, ML
│   │   │   │   ├── UploadStatus.hpp    # Enum
│   │   │   │   └── CollectionNumber.hpp
│   │   │   ├── repository/
│   │   │   │   └── IUploadedFileRepository.hpp
│   │   │   ├── event/
│   │   │   │   ├── FileUploadedEvent.hpp
│   │   │   │   └── FileUploadFailedEvent.hpp
│   │   │   └── port/
│   │   │       └── IFileStoragePort.hpp
│   │   ├── application/
│   │   │   ├── usecase/
│   │   │   │   ├── UploadLdifFileUseCase.hpp
│   │   │   │   ├── UploadMasterListUseCase.hpp
│   │   │   │   ├── GetUploadHistoryUseCase.hpp
│   │   │   │   └── GetUploadStatisticsUseCase.hpp
│   │   │   ├── command/
│   │   │   │   ├── UploadFileCommand.hpp
│   │   │   │   └── CheckDuplicateCommand.hpp
│   │   │   └── response/
│   │   │       ├── UploadResponse.hpp
│   │   │       └── UploadHistoryResponse.hpp
│   │   └── infrastructure/
│   │       ├── controller/
│   │       │   └── FileUploadController.hpp
│   │       ├── repository/
│   │       │   └── PostgresUploadedFileRepository.hpp
│   │       └── adapter/
│   │           └── LocalFileStorageAdapter.hpp
│   │
│   ├── fileparsing/                    # Bounded Context: File Parsing
│   │   ├── domain/
│   │   │   ├── model/
│   │   │   │   ├── ParsedFile.hpp      # Aggregate Root
│   │   │   │   ├── ParsedCertificate.hpp
│   │   │   │   ├── CertificateType.hpp # CSCA, DSC, DSC_NC
│   │   │   │   ├── CertificateData.hpp
│   │   │   │   └── CrlData.hpp
│   │   │   └── port/
│   │   │       ├── ILdifParser.hpp
│   │   │       └── IMasterListParser.hpp
│   │   ├── application/
│   │   │   └── usecase/
│   │   │       ├── ParseLdifUseCase.hpp
│   │   │       └── ParseMasterListUseCase.hpp
│   │   └── infrastructure/
│   │       └── adapter/
│   │           ├── OpenSslLdifParser.hpp
│   │           └── OpenSslMasterListParser.hpp
│   │
│   ├── certificatevalidation/          # Bounded Context: Certificate Validation
│   │   ├── domain/
│   │   │   ├── model/
│   │   │   │   ├── Certificate.hpp     # Aggregate Root
│   │   │   │   ├── CertificateId.hpp
│   │   │   │   ├── SubjectDn.hpp
│   │   │   │   ├── IssuerDn.hpp
│   │   │   │   ├── SerialNumber.hpp
│   │   │   │   ├── ValidityPeriod.hpp
│   │   │   │   ├── CertificateStatus.hpp
│   │   │   │   ├── ValidationResult.hpp
│   │   │   │   ├── ValidationError.hpp
│   │   │   │   ├── CertificateRevocationList.hpp
│   │   │   │   └── CountryCode.hpp
│   │   │   ├── repository/
│   │   │   │   ├── ICertificateRepository.hpp
│   │   │   │   └── ICrlRepository.hpp
│   │   │   ├── service/
│   │   │   │   ├── TrustChainValidator.hpp
│   │   │   │   └── CrlChecker.hpp
│   │   │   └── event/
│   │   │       └── CertificatesValidatedEvent.hpp
│   │   ├── application/
│   │   │   └── usecase/
│   │   │       └── ValidateCertificatesUseCase.hpp
│   │   └── infrastructure/
│   │       ├── repository/
│   │       │   ├── PostgresCertificateRepository.hpp
│   │       │   └── PostgresCrlRepository.hpp
│   │       └── adapter/
│   │           └── OpenSslValidationAdapter.hpp
│   │
│   ├── ldapintegration/                # Bounded Context: LDAP Integration
│   │   ├── domain/
│   │   │   ├── model/
│   │   │   │   ├── LdapEntry.hpp
│   │   │   │   ├── DistinguishedName.hpp
│   │   │   │   ├── LdapAttributes.hpp
│   │   │   │   └── LdapSearchFilter.hpp
│   │   │   └── port/
│   │   │       ├── ILdapConnectionPort.hpp
│   │   │       ├── ILdapUploadService.hpp
│   │   │       └── ILdapQueryService.hpp
│   │   ├── application/
│   │   │   └── usecase/
│   │   │       ├── UploadToLdapUseCase.hpp
│   │   │       └── LdapHealthCheckUseCase.hpp
│   │   └── infrastructure/
│   │       ├── config/
│   │       │   └── LdapProperties.hpp
│   │       └── adapter/
│   │           ├── OpenLdapAdapter.hpp        # Write operations
│   │           ├── OpenLdapCscaAdapter.hpp    # Read CSCA
│   │           └── OpenLdapCrlAdapter.hpp     # Read CRL
│   │
│   └── passiveauthentication/          # Bounded Context: Passive Authentication
│       ├── domain/
│       │   ├── model/
│       │   │   ├── PassportData.hpp          # Aggregate Root
│       │   │   ├── PassportDataId.hpp
│       │   │   ├── SecurityObjectDocument.hpp # SOD Value Object
│       │   │   ├── DataGroup.hpp
│       │   │   ├── DataGroupNumber.hpp       # DG1-DG16 Enum
│       │   │   ├── DataGroupHash.hpp
│       │   │   ├── PassiveAuthenticationResult.hpp
│       │   │   ├── PassiveAuthenticationStatus.hpp
│       │   │   ├── PassiveAuthenticationError.hpp
│       │   │   ├── CrlCheckResult.hpp
│       │   │   ├── CrlStatus.hpp             # VALID, REVOKED, UNAVAILABLE...
│       │   │   └── RequestMetadata.hpp
│       │   ├── repository/
│       │   │   └── IPassportDataRepository.hpp
│       │   ├── port/
│       │   │   ├── ISodParser.hpp
│       │   │   ├── ILdapCscaRepository.hpp
│       │   │   └── ICrlCacheService.hpp
│       │   └── service/
│       │       ├── CrlVerificationService.hpp
│       │       └── TrustChainService.hpp
│       ├── application/
│       │   ├── usecase/
│       │   │   ├── PerformPassiveAuthenticationUseCase.hpp
│       │   │   └── GetPaHistoryUseCase.hpp
│       │   ├── command/
│       │   │   └── PerformPaCommand.hpp
│       │   └── response/
│       │       ├── PassiveAuthenticationResponse.hpp
│       │       ├── CertificateChainValidationDto.hpp
│       │       ├── SodSignatureValidationDto.hpp
│       │       └── DataGroupValidationDto.hpp
│       └── infrastructure/
│           ├── controller/
│           │   ├── PassiveAuthenticationController.hpp
│           │   └── PaWebController.hpp     # SSE endpoints
│           ├── repository/
│           │   └── PostgresPassportDataRepository.hpp
│           ├── adapter/
│           │   ├── OpenSslSodParser.hpp
│           │   ├── Dg1MrzParser.hpp
│           │   └── Dg2FaceImageParser.hpp
│           └── cache/
│               └── CrlCacheService.hpp
│
├── include/                            # Public headers (if needed)
│
├── tests/
│   ├── unit/
│   │   ├── domain/
│   │   ├── application/
│   │   └── infrastructure/
│   └── integration/
│       ├── ldap/
│       ├── database/
│       └── api/
│
├── docker/
│   ├── Dockerfile                      # Multi-stage build
│   ├── Dockerfile.dev                  # Development with hot reload
│   └── docker-compose.yaml
│
├── scripts/
│   ├── build.sh
│   ├── run.sh
│   ├── test.sh
│   └── docker-start.sh
│
└── docs/
    ├── API.md
    ├── ARCHITECTURE.md
    └── DEPLOYMENT.md
```

### 3.2 React.js Frontend Directory Structure

```
icao-local-pkd-web/
├── package.json
├── tsconfig.json
├── vite.config.ts                      # Vite build tool
├── tailwind.config.js
│
├── public/
│   └── favicon.ico
│
├── src/
│   ├── main.tsx
│   ├── App.tsx
│   ├── index.css
│   │
│   ├── api/                            # API Client Layer
│   │   ├── client.ts                   # Axios/Fetch wrapper
│   │   ├── endpoints.ts
│   │   ├── types.ts                    # API Request/Response types
│   │   └── hooks/
│   │       ├── useUpload.ts
│   │       ├── usePassiveAuth.ts
│   │       └── useStatistics.ts
│   │
│   ├── components/                     # Reusable UI Components
│   │   ├── common/
│   │   │   ├── Button.tsx
│   │   │   ├── Card.tsx
│   │   │   ├── Modal.tsx
│   │   │   ├── Table.tsx
│   │   │   ├── Badge.tsx
│   │   │   ├── Spinner.tsx
│   │   │   └── Alert.tsx
│   │   ├── layout/
│   │   │   ├── Header.tsx
│   │   │   ├── Sidebar.tsx
│   │   │   ├── Footer.tsx
│   │   │   └── Layout.tsx
│   │   ├── upload/
│   │   │   ├── FileDropzone.tsx
│   │   │   ├── UploadProgress.tsx
│   │   │   └── UploadHistoryTable.tsx
│   │   ├── pa/
│   │   │   ├── PaVerifyForm.tsx
│   │   │   ├── PaResultCard.tsx
│   │   │   ├── MrzDisplay.tsx
│   │   │   ├── FaceImageDisplay.tsx
│   │   │   └── PaHistoryTable.tsx
│   │   └── dashboard/
│   │       ├── StatisticsCard.tsx
│   │       ├── PieChart.tsx
│   │       ├── BarChart.tsx
│   │       └── CountryStats.tsx
│   │
│   ├── pages/                          # Page Components
│   │   ├── HomePage.tsx
│   │   ├── upload/
│   │   │   ├── UploadPage.tsx
│   │   │   └── UploadHistoryPage.tsx
│   │   ├── pa/
│   │   │   ├── PaVerifyPage.tsx
│   │   │   ├── PaHistoryPage.tsx
│   │   │   └── PaDetailPage.tsx
│   │   ├── dashboard/
│   │   │   ├── PkdDashboardPage.tsx
│   │   │   └── PaDashboardPage.tsx
│   │   └── NotFoundPage.tsx
│   │
│   ├── store/                          # State Management (Zustand/Redux)
│   │   ├── uploadStore.ts
│   │   ├── paStore.ts
│   │   └── uiStore.ts
│   │
│   ├── hooks/                          # Custom Hooks
│   │   ├── useWebSocket.ts
│   │   ├── useSSE.ts                   # Server-Sent Events
│   │   └── useLocalStorage.ts
│   │
│   ├── utils/                          # Utility Functions
│   │   ├── formatters.ts
│   │   ├── validators.ts
│   │   └── constants.ts
│   │
│   └── types/                          # TypeScript Types
│       ├── upload.ts
│       ├── pa.ts
│       └── common.ts
│
└── tests/
    ├── components/
    └── pages/
```

---

## 4. Core Domain Models (C++ Implementation)

### 4.1 Value Object Base Class

```cpp
// src/shared/domain/ValueObject.hpp
#pragma once
#include <string>
#include <stdexcept>

namespace shared::domain {

template<typename T>
class ValueObject {
protected:
    T value_;

    explicit ValueObject(T value) : value_(std::move(value)) {
        validate();
    }

    virtual void validate() const = 0;

public:
    virtual ~ValueObject() = default;

    const T& getValue() const { return value_; }

    bool operator==(const ValueObject& other) const {
        return value_ == other.value_;
    }

    bool operator!=(const ValueObject& other) const {
        return !(*this == other);
    }
};

} // namespace shared::domain
```

### 4.2 Security Object Document (SOD)

```cpp
// src/passiveauthentication/domain/model/SecurityObjectDocument.hpp
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "shared/exception/DomainException.hpp"

namespace pa::domain::model {

class SecurityObjectDocument {
private:
    std::vector<uint8_t> encodedData_;
    std::string hashAlgorithm_;
    std::string signatureAlgorithm_;

    void validate(const std::vector<uint8_t>& sodBytes) {
        if (sodBytes.empty()) {
            throw shared::exception::DomainException(
                "INVALID_SOD", "SOD data cannot be empty");
        }

        // Valid SOD formats:
        // 1. ICAO 9303 EF.SOD: starts with Tag 0x77 (Application[23])
        // 2. Raw CMS SignedData: starts with Tag 0x30 (SEQUENCE)
        uint8_t firstByte = sodBytes[0];
        if (firstByte != 0x30 && firstByte != 0x77) {
            throw shared::exception::DomainException(
                "INVALID_SOD_FORMAT",
                "SOD data does not appear to be valid");
        }
    }

public:
    static SecurityObjectDocument of(const std::vector<uint8_t>& sodBytes) {
        return SecurityObjectDocument(sodBytes);
    }

    static SecurityObjectDocument withAlgorithms(
        const std::vector<uint8_t>& sodBytes,
        const std::string& hashAlg,
        const std::string& sigAlg
    ) {
        SecurityObjectDocument sod(sodBytes);
        sod.hashAlgorithm_ = hashAlg;
        sod.signatureAlgorithm_ = sigAlg;
        return sod;
    }

    explicit SecurityObjectDocument(const std::vector<uint8_t>& encodedData)
        : encodedData_(encodedData) {
        validate(encodedData);
    }

    const std::vector<uint8_t>& getEncodedData() const { return encodedData_; }
    const std::string& getHashAlgorithm() const { return hashAlgorithm_; }
    const std::string& getSignatureAlgorithm() const { return signatureAlgorithm_; }

    void setHashAlgorithm(const std::string& alg) { hashAlgorithm_ = alg; }
    void setSignatureAlgorithm(const std::string& alg) { signatureAlgorithm_ = alg; }

    size_t calculateSize() const { return encodedData_.size(); }
};

} // namespace pa::domain::model
```

### 4.3 SOD Parser Port (Interface)

```cpp
// src/passiveauthentication/domain/port/ISodParser.hpp
#pragma once
#include <map>
#include <vector>
#include <string>
#include <memory>
#include <openssl/x509.h>
#include "passiveauthentication/domain/model/DataGroupNumber.hpp"
#include "passiveauthentication/domain/model/DataGroupHash.hpp"

namespace pa::domain::port {

struct DscInfo {
    std::string subjectDn;
    std::string serialNumber;
};

class ISodParser {
public:
    virtual ~ISodParser() = default;

    // Parse Data Group hashes from SOD
    virtual std::map<model::DataGroupNumber, model::DataGroupHash>
        parseDataGroupHashes(const std::vector<uint8_t>& sodBytes) = 0;

    // Verify SOD signature with DSC certificate
    virtual bool verifySignature(
        const std::vector<uint8_t>& sodBytes,
        X509* dscCertificate) = 0;

    // Extract DSC certificate from SOD
    virtual X509* extractDscCertificate(
        const std::vector<uint8_t>& sodBytes) = 0;

    // Extract DSC info (Subject DN, Serial Number)
    virtual DscInfo extractDscInfo(
        const std::vector<uint8_t>& sodBytes) = 0;

    // Extract hash algorithm (SHA-256, SHA-384, SHA-512)
    virtual std::string extractHashAlgorithm(
        const std::vector<uint8_t>& sodBytes) = 0;

    // Extract signature algorithm (SHA256withRSA, etc.)
    virtual std::string extractSignatureAlgorithm(
        const std::vector<uint8_t>& sodBytes) = 0;
};

} // namespace pa::domain::port
```

### 4.4 OpenSSL SOD Parser Implementation

```cpp
// src/passiveauthentication/infrastructure/adapter/OpenSslSodParser.hpp
#pragma once
#include "passiveauthentication/domain/port/ISodParser.hpp"
#include <openssl/cms.h>
#include <openssl/bio.h>
#include <openssl/err.h>

namespace pa::infrastructure::adapter {

class OpenSslSodParser : public domain::port::ISodParser {
private:
    // OID to algorithm name mappings
    static const std::map<std::string, std::string> HASH_ALGORITHM_NAMES;
    static const std::map<std::string, std::string> SIGNATURE_ALGORITHM_NAMES;

    // Unwrap ICAO 9303 Tag 0x77 wrapper
    std::vector<uint8_t> unwrapIcaoSod(const std::vector<uint8_t>& sodBytes);

    // Parse LDSSecurityObject from CMS SignedData
    ASN1_OCTET_STRING* extractLdsSecurityObject(CMS_ContentInfo* cms);

public:
    std::map<domain::model::DataGroupNumber, domain::model::DataGroupHash>
    parseDataGroupHashes(const std::vector<uint8_t>& sodBytes) override {
        // 1. Unwrap Tag 0x77 if present
        auto cmsBytes = unwrapIcaoSod(sodBytes);

        // 2. Parse CMS SignedData
        BIO* bio = BIO_new_mem_buf(cmsBytes.data(), cmsBytes.size());
        CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);

        // 3. Extract LDSSecurityObject
        // 4. Parse DataGroupHash array
        // ... implementation

        BIO_free(bio);
        CMS_ContentInfo_free(cms);

        return hashMap;
    }

    bool verifySignature(
        const std::vector<uint8_t>& sodBytes,
        X509* dscCertificate
    ) override {
        auto cmsBytes = unwrapIcaoSod(sodBytes);

        BIO* bio = BIO_new_mem_buf(cmsBytes.data(), cmsBytes.size());
        CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);

        // Create certificate store with DSC
        X509_STORE* store = X509_STORE_new();
        X509_STORE_add_cert(store, dscCertificate);

        // Verify CMS signature
        int result = CMS_verify(cms, nullptr, store, nullptr, nullptr,
                               CMS_NO_SIGNER_CERT_VERIFY);

        X509_STORE_free(store);
        CMS_ContentInfo_free(cms);
        BIO_free(bio);

        return result == 1;
    }

    X509* extractDscCertificate(const std::vector<uint8_t>& sodBytes) override {
        auto cmsBytes = unwrapIcaoSod(sodBytes);

        BIO* bio = BIO_new_mem_buf(cmsBytes.data(), cmsBytes.size());
        CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);

        // Extract certificates from CMS
        STACK_OF(X509)* certs = CMS_get1_certs(cms);
        if (!certs || sk_X509_num(certs) == 0) {
            throw shared::exception::InfrastructureException(
                "NO_DSC_IN_SOD", "No certificates found in SOD");
        }

        // Get first certificate (DSC)
        X509* dsc = sk_X509_value(certs, 0);
        X509* dscCopy = X509_dup(dsc);  // Return a copy

        sk_X509_pop_free(certs, X509_free);
        CMS_ContentInfo_free(cms);
        BIO_free(bio);

        return dscCopy;
    }

    // ... other implementations
};

} // namespace pa::infrastructure::adapter
```

---

## 5. REST API Endpoints

### 5.1 API Specification

| Method | Endpoint | Description |
|--------|----------|-------------|
| **File Upload** | | |
| POST | `/api/upload/ldif` | Upload LDIF file |
| POST | `/api/upload/masterlist` | Upload Master List file |
| GET | `/api/upload/history` | Get upload history (paginated) |
| GET | `/api/upload/{uploadId}` | Get upload details |
| GET | `/api/upload/{uploadId}/progress` | SSE progress stream |
| GET | `/api/upload/statistics` | Get upload statistics |
| **Passive Authentication** | | |
| POST | `/api/pa/verify` | Perform PA verification |
| GET | `/api/pa/history` | Get PA history (paginated) |
| GET | `/api/pa/{verificationId}` | Get PA result details |
| GET | `/api/pa/{verificationId}/datagroups` | Get DG1/DG2 parsed data |
| POST | `/api/pa/parse-dg1` | Parse DG1 (MRZ) |
| POST | `/api/pa/parse-dg2` | Parse DG2 (Face image) |
| POST | `/api/pa/parse-mrz-text` | Parse raw MRZ text |
| **Statistics** | | |
| GET | `/api/statistics/pkd` | PKD statistics |
| GET | `/api/statistics/pa` | PA statistics |
| GET | `/api/statistics/countries` | Country-wise statistics |
| **Health Check** | | |
| GET | `/api/health` | Application health |
| GET | `/api/health/ldap` | LDAP connection health |
| GET | `/api/health/database` | Database connection health |

### 5.2 PA Verify API Request/Response

**Request:**
```json
{
  "issuingCountry": "KOR",
  "documentNumber": "M12345678",
  "sod": "MIIGBwYJKoZIhvcNAQcCoII...",
  "dataGroups": {
    "DG1": "YV9oZWFkZXIuLi4=",
    "DG2": "iVBORw0KGgoAAAANS..."
  },
  "requestedBy": "border-control-app"
}
```

**Response:**
```json
{
  "status": "VALID",
  "verificationId": "550e8400-e29b-41d4-a716-446655440000",
  "verificationTimestamp": "2025-12-29T10:30:00+09:00",
  "issuingCountry": "KOR",
  "documentNumber": "M12345678",
  "certificateChainValidation": {
    "valid": true,
    "dscSubject": "C=KR,O=Government,OU=MOFA,CN=DS001",
    "dscSerialNumber": "1234567890ABCDEF",
    "cscaSubject": "C=KR,O=Government,OU=MOFA,CN=CSCA-KR",
    "cscaSerialNumber": "FEDCBA0987654321",
    "notBefore": "2023-01-01T00:00:00Z",
    "notAfter": "2028-01-01T00:00:00Z",
    "crlChecked": true,
    "revoked": false,
    "crlStatus": "VALID",
    "crlStatusDescription": "Certificate is valid and not revoked",
    "crlStatusDetailedDescription": "인증서가 유효하며 폐기되지 않았습니다",
    "crlStatusSeverity": "SUCCESS",
    "crlMessage": "CRL 검증 완료"
  },
  "sodSignatureValidation": {
    "valid": true,
    "signatureAlgorithm": "SHA256withRSA",
    "hashAlgorithm": "SHA-256"
  },
  "dataGroupValidation": {
    "totalGroups": 2,
    "validGroups": 2,
    "invalidGroups": 0,
    "details": {
      "DG1": {"valid": true, "expectedHash": "a1b2...", "actualHash": "a1b2..."},
      "DG2": {"valid": true, "expectedHash": "c3d4...", "actualHash": "c3d4..."}
    }
  },
  "processingDurationMs": 245,
  "errors": []
}
```

---

## 6. Database Schema

### 6.1 Tables (PostgreSQL)

```sql
-- File Upload
CREATE TABLE uploaded_file (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    file_name VARCHAR(255) NOT NULL,
    original_file_name VARCHAR(255),
    file_path VARCHAR(500),
    file_hash VARCHAR(64) NOT NULL,  -- SHA-256
    file_size BIGINT NOT NULL,
    file_format VARCHAR(20) NOT NULL,  -- LDIF, ML
    collection_number VARCHAR(50),
    status VARCHAR(30) NOT NULL,  -- PENDING, PROCESSING, COMPLETED, FAILED
    upload_timestamp TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    completed_timestamp TIMESTAMP WITH TIME ZONE,
    error_message TEXT,
    uploaded_by VARCHAR(100)
);

-- Parsed Certificate
CREATE TABLE certificate (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    upload_id UUID REFERENCES uploaded_file(id),
    certificate_type VARCHAR(20) NOT NULL,  -- CSCA, DSC, DSC_NC
    country_code VARCHAR(3) NOT NULL,
    subject_dn TEXT NOT NULL,
    issuer_dn TEXT NOT NULL,
    serial_number VARCHAR(100) NOT NULL,
    fingerprint_sha256 VARCHAR(64) NOT NULL UNIQUE,
    not_before TIMESTAMP WITH TIME ZONE,
    not_after TIMESTAMP WITH TIME ZONE,
    certificate_binary BYTEA NOT NULL,
    validation_status VARCHAR(20),  -- VALID, INVALID, PENDING
    validation_timestamp TIMESTAMP WITH TIME ZONE,
    ldap_upload_status VARCHAR(20),  -- PENDING, UPLOADED, SKIPPED, FAILED
    ldap_upload_timestamp TIMESTAMP WITH TIME ZONE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- CRL
CREATE TABLE certificate_revocation_list (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    upload_id UUID REFERENCES uploaded_file(id),
    issuer_dn TEXT NOT NULL,
    country_code VARCHAR(3) NOT NULL,
    this_update TIMESTAMP WITH TIME ZONE NOT NULL,
    next_update TIMESTAMP WITH TIME ZONE,
    crl_number BIGINT,
    revoked_count INTEGER DEFAULT 0,
    crl_binary BYTEA NOT NULL,
    ldap_upload_status VARCHAR(20),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Passport Data (PA Results)
CREATE TABLE passport_data (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    issuing_country VARCHAR(3),
    document_number VARCHAR(50),
    status VARCHAR(20) NOT NULL,  -- VALID, INVALID, ERROR
    verification_timestamp TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    sod_encoded BYTEA,
    hash_algorithm VARCHAR(20),
    signature_algorithm VARCHAR(50),
    chain_valid BOOLEAN,
    sod_signature_valid BOOLEAN,
    dg_total INTEGER,
    dg_valid INTEGER,
    dg_invalid INTEGER,
    crl_status VARCHAR(30),
    processing_duration_ms BIGINT,
    client_ip VARCHAR(45),
    user_agent TEXT,
    requested_by VARCHAR(100),
    error_details JSONB
);

-- Data Groups
CREATE TABLE data_group (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    passport_data_id UUID REFERENCES passport_data(id),
    dg_number INTEGER NOT NULL,  -- 1-16
    content BYTEA,
    expected_hash VARCHAR(128),
    actual_hash VARCHAR(128),
    hash_valid BOOLEAN
);

-- Audit Log
CREATE TABLE passive_authentication_audit_log (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    passport_data_id UUID REFERENCES passport_data(id),
    step VARCHAR(50) NOT NULL,
    status VARCHAR(20) NOT NULL,
    message TEXT,
    timestamp TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Indexes
CREATE INDEX idx_certificate_country ON certificate(country_code);
CREATE INDEX idx_certificate_type ON certificate(certificate_type);
CREATE INDEX idx_certificate_fingerprint ON certificate(fingerprint_sha256);
CREATE INDEX idx_passport_data_country ON passport_data(issuing_country);
CREATE INDEX idx_passport_data_status ON passport_data(status);
CREATE INDEX idx_passport_data_timestamp ON passport_data(verification_timestamp DESC);
```

---

## 7. Implementation Phases

### Phase 1: Project Foundation (Week 1-2)

| Task | Description | Priority |
|------|-------------|----------|
| Project Setup | CMake configuration, vcpkg dependencies | HIGH |
| Drogon Integration | Web framework setup, routing | HIGH |
| PostgreSQL Connection | Database connection pool, migrations | HIGH |
| Basic DDD Structure | Shared kernel, base classes | HIGH |
| Docker Environment | Development containers | MEDIUM |
| Logging Setup | spdlog configuration | MEDIUM |

**Deliverables:**
- [ ] Working CMake build
- [ ] Drogon hello world API
- [ ] PostgreSQL connection test
- [ ] Docker development environment

### Phase 2: File Upload Module (Week 3-4)

| Task | Description | Priority |
|------|-------------|----------|
| Upload API | POST /api/upload/ldif, /api/upload/masterlist | HIGH |
| File Storage | Local filesystem adapter | HIGH |
| LDIF Parser | OpenSSL-based LDIF parsing | HIGH |
| Master List Parser | CMS SignedData parsing | HIGH |
| X.509 Certificate Parsing | Certificate extraction | HIGH |
| Upload History | GET /api/upload/history | MEDIUM |
| Progress SSE | Real-time progress updates | MEDIUM |

**Deliverables:**
- [ ] File upload API working
- [ ] LDIF/ML parsing complete
- [ ] Certificates stored in DB
- [ ] SSE progress stream

### Phase 3: Certificate Validation Module (Week 5-6)

| Task | Description | Priority |
|------|-------------|----------|
| Trust Chain Validation | CSCA → DSC verification | HIGH |
| Two-Pass Processing | CSCA first, then DSC | HIGH |
| CRL Validation | CRL signature, freshness | HIGH |
| Validity Period Check | NotBefore/NotAfter | HIGH |
| Batch Processing | 1000 certificate batches | MEDIUM |
| Validation Statistics | Valid/Invalid counts | MEDIUM |

**Deliverables:**
- [ ] Trust chain validation working
- [ ] CRL checking implemented
- [ ] Two-pass validation complete

### Phase 4: LDAP Integration Module (Week 7)

| Task | Description | Priority |
|------|-------------|----------|
| OpenLDAP Connection | libldap adapter | HIGH |
| ICAO PKD DIT | Create PKD structure | HIGH |
| Certificate Upload | Add CSCA/DSC/CRL to LDAP | HIGH |
| RFC 5280 Compliance | Add/Modify/Skip logic | HIGH |
| Read/Write Separation | Separate connection pools | MEDIUM |
| Connection Pooling | Efficient LDAP connections | MEDIUM |

**Deliverables:**
- [ ] LDAP connection working
- [ ] PKD DIT structure created
- [ ] Certificates uploaded to LDAP

### Phase 5: Passive Authentication Module (Week 8-9)

| Task | Description | Priority |
|------|-------------|----------|
| SOD Parser | Tag 0x77 unwrapping, CMS parsing | HIGH |
| DSC Extraction | Extract DSC from SOD | HIGH |
| CSCA Lookup | LDAP CSCA retrieval | HIGH |
| Trust Chain Verify | DSC.verify(CSCA.publicKey) | HIGH |
| SOD Signature Verify | CMS_verify() | HIGH |
| Data Group Hash | Hash comparison | HIGH |
| CRL Check | DSC revocation check | HIGH |
| DG1 MRZ Parser | TD3 format parsing | MEDIUM |
| DG2 Face Parser | JPEG/JPEG2000 extraction | MEDIUM |
| PA History | History storage and retrieval | MEDIUM |

**Deliverables:**
- [ ] Full PA verification working
- [ ] DG1/DG2 parsing complete
- [ ] PA history and details

### Phase 6: React.js Frontend (Week 10-11)

| Task | Description | Priority |
|------|-------------|----------|
| Project Setup | Vite + React + TypeScript | HIGH |
| UI Framework | Tailwind CSS + shadcn/ui | HIGH |
| API Client | Axios with TypeScript types | HIGH |
| Upload Page | Drag-drop, progress bar | HIGH |
| PA Verify Page | Form, result display | HIGH |
| History Pages | Tables with pagination | HIGH |
| Dashboard Pages | Charts (Chart.js/Recharts) | MEDIUM |
| SSE Integration | Real-time progress updates | MEDIUM |
| Responsive Design | Mobile-friendly | MEDIUM |

**Deliverables:**
- [ ] All pages implemented
- [ ] API integration complete
- [ ] Responsive UI

### Phase 7: Integration & Testing (Week 12)

| Task | Description | Priority |
|------|-------------|----------|
| Unit Tests | Catch2/GoogleTest | HIGH |
| Integration Tests | API tests | HIGH |
| LDAP Tests | LDAP integration tests | HIGH |
| Performance Tests | Load testing | MEDIUM |
| Security Review | OWASP guidelines | MEDIUM |
| Documentation | API docs, deployment guide | MEDIUM |

**Deliverables:**
- [ ] Test coverage > 80%
- [ ] All integration tests passing
- [ ] Documentation complete

---

## 8. Dependencies

### 8.1 C++ Libraries (vcpkg)

```json
{
  "name": "icao-local-pkd",
  "version": "1.0.0",
  "dependencies": [
    "drogon",
    "openssl",
    "libpq",
    "nlohmann-json",
    "spdlog",
    "catch2",
    "fmt",
    "date"
  ]
}
```

### 8.2 React.js Dependencies (package.json)

```json
{
  "dependencies": {
    "react": "^18.2.0",
    "react-dom": "^18.2.0",
    "react-router-dom": "^6.20.0",
    "axios": "^1.6.0",
    "@tanstack/react-query": "^5.0.0",
    "zustand": "^4.4.0",
    "tailwindcss": "^3.4.0",
    "recharts": "^2.10.0",
    "react-dropzone": "^14.2.0",
    "date-fns": "^3.0.0",
    "clsx": "^2.0.0",
    "lucide-react": "^0.300.0"
  },
  "devDependencies": {
    "typescript": "^5.3.0",
    "vite": "^5.0.0",
    "@types/react": "^18.2.0",
    "@vitejs/plugin-react": "^4.2.0",
    "vitest": "^1.0.0",
    "@testing-library/react": "^14.1.0"
  }
}
```

---

## 9. Deployment Architecture

### 9.1 Docker Compose (Development)

```yaml
version: '3.8'

services:
  app:
    build:
      context: .
      dockerfile: docker/Dockerfile
    ports:
      - "8081:8081"
    environment:
      - DATABASE_URL=postgresql://pkd:pkd@postgres:5432/localpkd
      - LDAP_WRITE_URL=ldap://openldap1:389
      - LDAP_READ_URL=ldap://haproxy:389
    depends_on:
      - postgres
      - haproxy
    networks:
      - pkd-network

  frontend:
    build:
      context: ./icao-local-pkd-web
      dockerfile: Dockerfile
    ports:
      - "3000:80"
    depends_on:
      - app
    networks:
      - pkd-network

  postgres:
    image: postgres:15
    environment:
      POSTGRES_USER: pkd
      POSTGRES_PASSWORD: pkd
      POSTGRES_DB: localpkd
      TZ: Asia/Seoul
    volumes:
      - postgres-data:/var/lib/postgresql/data
    networks:
      - pkd-network

  haproxy:
    image: haproxy:2.8
    ports:
      - "389:389"
      - "8404:8404"
    volumes:
      - ./haproxy/haproxy.cfg:/usr/local/etc/haproxy/haproxy.cfg
    depends_on:
      - openldap1
      - openldap2
    networks:
      - pkd-network

  openldap1:
    image: osixia/openldap:1.5.0
    ports:
      - "3891:389"
    environment:
      LDAP_ORGANISATION: "SmartCore Inc"
      LDAP_DOMAIN: "ldap.smartcoreinc.com"
      LDAP_ADMIN_PASSWORD: "admin"
    volumes:
      - ldap1-data:/var/lib/ldap
    networks:
      - pkd-network

  openldap2:
    image: osixia/openldap:1.5.0
    ports:
      - "3892:389"
    environment:
      LDAP_ORGANISATION: "SmartCore Inc"
      LDAP_DOMAIN: "ldap.smartcoreinc.com"
      LDAP_ADMIN_PASSWORD: "admin"
    volumes:
      - ldap2-data:/var/lib/ldap
    networks:
      - pkd-network

volumes:
  postgres-data:
  ldap1-data:
  ldap2-data:

networks:
  pkd-network:
    driver: bridge
```

---

## 10. Risk Assessment

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| OpenSSL CMS API complexity | HIGH | MEDIUM | Prototype early, reference sod_example |
| libldap API differences from UnboundID | MEDIUM | MEDIUM | Create abstraction layer |
| Drogon ORM limitations | MEDIUM | LOW | Use raw SQL when needed |
| C++ async complexity | MEDIUM | MEDIUM | Use Drogon coroutines, test thoroughly |
| Cross-platform build issues | LOW | MEDIUM | Use vcpkg, Docker for consistency |

---

## 11. Success Criteria

1. **Functional Parity**: All Java features implemented in C++
2. **Performance**: Response time < 500ms for PA verification
3. **Reliability**: 99.9% uptime target
4. **Test Coverage**: > 80% code coverage
5. **Documentation**: Complete API and deployment docs
6. **Security**: OWASP Top 10 compliance

---

## 12. References

- [ICAO Doc 9303 Part 11 - Security Mechanisms for MRTDs](https://www.icao.int/publications/Documents/9303_p11_cons_en.pdf)
- [ICAO Doc 9303 Part 12 - PKI for MRTDs](https://www.icao.int/publications/Documents/9303_p12_cons_en.pdf)
- [RFC 5280 - X.509 PKI Certificate and CRL Profile](https://tools.ietf.org/html/rfc5280)
- [RFC 5652 - Cryptographic Message Syntax (CMS)](https://tools.ietf.org/html/rfc5652)
- [Drogon Framework Documentation](https://drogon.docsforge.com/)
- [OpenSSL CMS API](https://www.openssl.org/docs/man3.0/man3/CMS_verify.html)
- [OpenLDAP C SDK](https://www.openldap.org/software/man.cgi?query=ldap)

---

**Document Owner**: kbjung
**Review Date**: 2025-12-29
