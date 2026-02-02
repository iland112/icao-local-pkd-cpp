# Shared Library Architecture Plan

**Version**: v1.0
**Date**: 2026-02-02
**Purpose**: Extract common DN parsing and X.509 utilities into shared library

---

## Executive Summary

This document outlines the plan to create a shared library (`icao-pkd-common`) containing DN parsing, X.509 certificate utilities, and other common functions used across all services (PKD Management, PA Service, PKD Relay).

**Benefits**:
- ✅ Single source of truth for DN parsing
- ✅ Consistent RFC2253 format across all services
- ✅ Better testability (test once, use everywhere)
- ✅ Improved maintainability
- ✅ Guaranteed idempotency
- ✅ ICAO guide compliance (DN as ASN.1 structure)

---

## Current State Analysis

### DN Parsing Usage

| Service | Occurrences | Method | Format | Standard Compliance |
|---------|-------------|--------|--------|---------------------|
| PKD Management | 31 | `x509NameToString()` | RFC2253 | ✅ Standard |
| PA Service | 15 | `X509_NAME_oneline()` | Slash-separated | ❌ Non-standard |
| PKD Relay | 0 | N/A | N/A | N/A |

### Identified Problems

1. **Inconsistent DN Formats**
   - PKD Management: Uses RFC2253 (standard)
   - PA Service: Uses OpenSSL oneline (non-standard)
   - Different formats cause interoperability issues

2. **Code Duplication**
   - Similar X.509 parsing logic in multiple services
   - DN normalization logic duplicated
   - Certificate validation logic duplicated

3. **Maintenance Burden**
   - Bug fixes must be applied to multiple locations
   - Inconsistent behavior across services
   - Testing overhead

---

## Proposed Architecture

### Directory Structure

```
icao-local-pkd/
├── services/
│   ├── common-lib/                    (NEW - Shared Library)
│   │   ├── include/
│   │   │   └── icao/
│   │   │       ├── x509/
│   │   │       │   ├── dn_parser.h           # DN parsing utilities
│   │   │       │   ├── certificate_parser.h  # X.509 parsing
│   │   │       │   ├── metadata_extractor.h  # Metadata extraction
│   │   │       │   └── dn_components.h       # DN component extraction
│   │   │       ├── ldap/
│   │   │       │   └── ldap_utils.h          # LDAP utilities
│   │   │       └── utils/
│   │   │           ├── string_utils.h        # String utilities
│   │   │           └── time_utils.h          # Time conversion utilities
│   │   ├── src/
│   │   │   ├── x509/
│   │   │   │   ├── dn_parser.cpp
│   │   │   │   ├── certificate_parser.cpp
│   │   │   │   ├── metadata_extractor.cpp
│   │   │   │   └── dn_components.cpp
│   │   │   ├── ldap/
│   │   │   │   └── ldap_utils.cpp
│   │   │   └── utils/
│   │   │       ├── string_utils.cpp
│   │   │       └── time_utils.cpp
│   │   ├── tests/                           # Unit tests
│   │   │   ├── x509/
│   │   │   │   ├── dn_parser_test.cpp
│   │   │   │   └── dn_components_test.cpp
│   │   │   └── CMakeLists.txt
│   │   └── CMakeLists.txt
│   ├── pkd-management/
│   ├── pa-service/
│   └── pkd-relay-service/
```

---

## Shared Library Components

### 1. DN Parser Module

**File**: `include/icao/x509/dn_parser.h`

```cpp
#pragma once

#include <string>
#include <openssl/x509.h>

namespace icao {
namespace x509 {

/**
 * @brief Convert X509_NAME to RFC2253 canonical string
 *
 * Uses OpenSSL X509_NAME_print_ex() with XN_FLAG_RFC2253.
 * This is the ICAO-recommended standard format.
 *
 * @param name X509_NAME structure
 * @return RFC2253 formatted DN string
 */
std::string x509NameToString(X509_NAME* name);

/**
 * @brief Compare two X509_NAME structures for equality
 *
 * Uses ASN.1 structure comparison (not string comparison).
 * This is the ICAO-recommended method.
 *
 * @param name1 First X509_NAME
 * @param name2 Second X509_NAME
 * @return true if names are equal
 */
bool compareX509Names(X509_NAME* name1, X509_NAME* name2);

/**
 * @brief Normalize DN for case-insensitive comparison
 *
 * Extracts RDN components, lowercases, sorts, and joins.
 * Use this only when X509_NAME structures are not available.
 *
 * @param dn DN string in any format
 * @return Normalized DN for comparison
 */
std::string normalizeDnForComparison(const std::string& dn);

} // namespace x509
} // namespace icao
```

---

### 2. DN Components Extractor

**File**: `include/icao/x509/dn_components.h`

```cpp
#pragma once

#include <string>
#include <optional>
#include <openssl/x509.h>

namespace icao {
namespace x509 {

/**
 * @brief Distinguished Name components
 */
struct DnComponents {
    std::string commonName;              // CN
    std::string organization;            // O
    std::string organizationalUnit;      // OU
    std::string locality;                // L
    std::string stateOrProvince;         // ST
    std::string country;                 // C
    std::string email;                   // emailAddress
    std::optional<std::string> serialNumber;  // serialNumber
    std::optional<std::string> domainComponent; // DC
};

/**
 * @brief Extract all DN components from X509_NAME
 *
 * Extracts all standard DN components using OpenSSL NID lookups.
 * Returns structured data instead of string parsing.
 *
 * @param name X509_NAME structure
 * @return DnComponents with all extracted fields
 */
DnComponents extractDnComponents(X509_NAME* name);

/**
 * @brief Extract single DN component by NID
 *
 * @param name X509_NAME structure
 * @param nid OpenSSL NID (e.g., NID_commonName)
 * @return Component value or empty string
 */
std::string extractDnComponent(X509_NAME* name, int nid);

/**
 * @brief Extract DN components from Subject
 *
 * Convenience function for extracting subject DN components.
 *
 * @param cert X509 certificate
 * @return DnComponents from subject
 */
DnComponents extractSubjectComponents(X509* cert);

/**
 * @brief Extract DN components from Issuer
 *
 * Convenience function for extracting issuer DN components.
 *
 * @param cert X509 certificate
 * @return DnComponents from issuer
 */
DnComponents extractIssuerComponents(X509* cert);

} // namespace x509
} // namespace icao
```

---

### 3. Certificate Parser Module

**File**: `include/icao/x509/certificate_parser.h`

```cpp
#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <openssl/x509.h>
#include "dn_components.h"

namespace icao {
namespace x509 {

/**
 * @brief Parse certificate from DER-encoded data
 *
 * @param derData DER-encoded certificate bytes
 * @return X509* certificate (caller must free)
 * @throws std::runtime_error on parse failure
 */
X509* parseCertificateFromDer(const std::vector<uint8_t>& derData);

/**
 * @brief Calculate SHA-256 fingerprint
 *
 * @param cert X509 certificate
 * @return Hex-encoded fingerprint
 */
std::string calculateFingerprint(X509* cert);

/**
 * @brief Calculate SHA-1 fingerprint (legacy)
 *
 * @param cert X509 certificate
 * @return Hex-encoded fingerprint
 */
std::string calculateFingerprintSha1(X509* cert);

/**
 * @brief Check if certificate is self-signed
 *
 * Compares subject and issuer using X509_NAME_cmp.
 *
 * @param cert X509 certificate
 * @return true if self-signed
 */
bool isSelfSigned(X509* cert);

/**
 * @brief Extract validity period
 *
 * @param cert X509 certificate
 * @return pair of (notBefore, notAfter)
 */
std::pair<std::chrono::system_clock::time_point,
          std::chrono::system_clock::time_point>
extractValidityPeriod(X509* cert);

} // namespace x509
} // namespace icao
```

---

### 4. Metadata Extractor Module

**File**: `include/icao/x509/metadata_extractor.h`

```cpp
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <openssl/x509.h>

namespace icao {
namespace x509 {

/**
 * @brief X.509 certificate metadata
 */
struct CertificateMetadata {
    int version;                                    // 0=v1, 1=v2, 2=v3
    std::string signatureAlgorithm;                 // "sha256WithRSAEncryption"
    std::string signatureHashAlgorithm;             // "SHA-256"
    std::string publicKeyAlgorithm;                 // "RSA", "ECDSA"
    int publicKeySize;                              // 2048, 4096 (bits)
    std::optional<std::string> publicKeyCurve;      // "prime256v1" (ECDSA)
    std::vector<std::string> keyUsage;              // {"digitalSignature"}
    std::vector<std::string> extendedKeyUsage;      // {"serverAuth"}
    bool isCA;                                      // TRUE if CA cert
    std::optional<int> pathLenConstraint;           // Path length
    std::optional<std::string> subjectKeyIdentifier;      // SKI (hex)
    std::optional<std::string> authorityKeyIdentifier;    // AKI (hex)
    std::vector<std::string> crlDistributionPoints;       // CRL URLs
    std::optional<std::string> ocspResponderUrl;          // OCSP URL
    bool isSelfSigned;                              // Self-signed flag
};

/**
 * @brief Extract all metadata from certificate
 *
 * @param cert X509 certificate
 * @return CertificateMetadata structure
 */
CertificateMetadata extractMetadata(X509* cert);

} // namespace x509
} // namespace icao
```

---

### 5. String Utilities

**File**: `include/icao/utils/string_utils.h`

```cpp
#pragma once

#include <string>
#include <vector>

namespace icao {
namespace utils {

/**
 * @brief Convert string to lowercase
 */
std::string toLower(const std::string& str);

/**
 * @brief Convert string to uppercase
 */
std::string toUpper(const std::string& str);

/**
 * @brief Trim whitespace from both ends
 */
std::string trim(const std::string& str);

/**
 * @brief Split string by delimiter
 */
std::vector<std::string> split(const std::string& str, char delimiter);

/**
 * @brief Convert bytes to hex string
 */
std::string bytesToHex(const uint8_t* data, size_t len);

/**
 * @brief Convert hex string to bytes
 */
std::vector<uint8_t> hexToBytes(const std::string& hex);

} // namespace utils
} // namespace icao
```

---

### 6. Time Utilities

**File**: `include/icao/utils/time_utils.h`

```cpp
#pragma once

#include <string>
#include <chrono>
#include <openssl/asn1.h>

namespace icao {
namespace utils {

/**
 * @brief Convert ASN1_TIME to ISO8601 string
 *
 * @param time ASN1_TIME structure
 * @return ISO8601 formatted string
 */
std::string asn1TimeToIso8601(const ASN1_TIME* time);

/**
 * @brief Convert ASN1_TIME to time_point
 *
 * @param time ASN1_TIME structure
 * @return chrono::system_clock::time_point
 */
std::chrono::system_clock::time_point asn1TimeToTimePoint(const ASN1_TIME* time);

/**
 * @brief Convert ASN1_INTEGER to hex string
 *
 * @param integer ASN1_INTEGER structure
 * @return Hex-encoded string
 */
std::string asn1IntegerToHex(const ASN1_INTEGER* integer);

} // namespace utils
} // namespace icao
```

---

## CMake Configuration

### Shared Library CMakeLists.txt

**File**: `services/common-lib/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20)
project(icao-pkd-common VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(OpenSSL REQUIRED)

# Library sources
set(SOURCES
    src/x509/dn_parser.cpp
    src/x509/dn_components.cpp
    src/x509/certificate_parser.cpp
    src/x509/metadata_extractor.cpp
    src/ldap/ldap_utils.cpp
    src/utils/string_utils.cpp
    src/utils/time_utils.cpp
)

# Create shared library
add_library(icao-pkd-common SHARED ${SOURCES})

# Include directories
target_include_directories(icao-pkd-common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(icao-pkd-common
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Install rules
install(TARGETS icao-pkd-common
    EXPORT icao-pkd-common-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)

# Enable testing
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

### Service Integration

**Example**: `services/pkd-management/CMakeLists.txt`

```cmake
# ... existing configuration ...

# Add common library
add_subdirectory(../common-lib common-lib)

# Link against common library
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        icao-pkd-common  # NEW
        # ... existing dependencies ...
)
```

---

## Implementation Plan

### Phase 1: Create Shared Library Structure (2 hours)

**Tasks**:
1. Create `services/common-lib/` directory structure
2. Set up CMake configuration
3. Create header files with function declarations
4. Set up unit test framework (Catch2 or Google Test)

**Files Created**:
- `services/common-lib/CMakeLists.txt`
- `services/common-lib/include/icao/x509/*.h` (5 files)
- `services/common-lib/include/icao/utils/*.h` (2 files)
- `services/common-lib/tests/CMakeLists.txt`

---

### Phase 2: Implement Core DN Parsing (3 hours)

**Tasks**:
1. Implement `dn_parser.cpp`
   - `x509NameToString()` - Extract from PKD Management main.cpp:1834
   - `compareX509Names()` - New implementation
   - `normalizeDnForComparison()` - Extract from validation_service.cpp:783

2. Implement `dn_components.cpp`
   - `extractDnComponents()` - Extract all DN components
   - `extractDnComponent()` - Extract single component by NID
   - `extractSubjectComponents()` - Convenience wrapper
   - `extractIssuerComponents()` - Convenience wrapper

3. Write unit tests
   - Test with real certificate data
   - Test with LDAP DN formats
   - Test edge cases (empty, special characters)

**Files Created**:
- `services/common-lib/src/x509/dn_parser.cpp`
- `services/common-lib/src/x509/dn_components.cpp`
- `services/common-lib/tests/x509/dn_parser_test.cpp`
- `services/common-lib/tests/x509/dn_components_test.cpp`

---

### Phase 3: Implement Certificate Parsing (2 hours)

**Tasks**:
1. Implement `certificate_parser.cpp`
   - Extract from ldap_certificate_repository.cpp
   - `parseCertificateFromDer()`
   - `calculateFingerprint()`
   - `isSelfSigned()`
   - `extractValidityPeriod()`

2. Implement `metadata_extractor.cpp`
   - Move from x509_metadata_extractor.cpp
   - Refactor for shared use

3. Write unit tests

**Files Created**:
- `services/common-lib/src/x509/certificate_parser.cpp`
- `services/common-lib/src/x509/metadata_extractor.cpp`
- `services/common-lib/tests/x509/certificate_parser_test.cpp`

---

### Phase 4: Implement Utility Modules (1 hour)

**Tasks**:
1. Implement `string_utils.cpp` - Common string operations
2. Implement `time_utils.cpp` - Extract from main.cpp
3. Write unit tests

**Files Created**:
- `services/common-lib/src/utils/string_utils.cpp`
- `services/common-lib/src/utils/time_utils.cpp`
- `services/common-lib/tests/utils/string_utils_test.cpp`

---

### Phase 5: Migrate PKD Management Service (3 hours)

**Tasks**:
1. Update CMakeLists.txt to link common library
2. Replace local DN parsing with shared library
3. Update Certificate model to use `DnComponents`
4. Update LDAP repository to use shared functions
5. Remove duplicate code
6. Build and test

**Files Modified**:
- `services/pkd-management/CMakeLists.txt`
- `services/pkd-management/src/domain/models/certificate.h`
- `services/pkd-management/src/repositories/ldap_certificate_repository.cpp`
- `services/pkd-management/src/main.cpp` (remove duplicate functions)

---

### Phase 6: Migrate PA Service (2 hours)

**Tasks**:
1. Update CMakeLists.txt
2. Replace `X509_NAME_oneline()` with `x509NameToString()`
3. Update DN handling to use RFC2253 format
4. Build and test

**Files Modified**:
- `services/pa-service/CMakeLists.txt`
- `services/pa-service/src/main.cpp`
- `services/pa-service/src/services/*.cpp` (if applicable)

---

### Phase 7: Update Frontend (1 hour)

**Tasks**:
1. Update Certificate TypeScript interface
2. Replace regex DN parsing with structured fields
3. Update CertificateSearch.tsx display logic
4. Test all certificate detail views

**Files Modified**:
- `frontend/src/types/index.ts`
- `frontend/src/pages/CertificateSearch.tsx`

---

### Phase 8: Integration Testing (2 hours)

**Tasks**:
1. Test certificate upload with new DN parsing
2. Test certificate search with structured data
3. Test PA verification with RFC2253 format
4. Verify consistency across all services
5. Performance testing

**Verification**:
- ✅ All services use same DN format
- ✅ DN components display correctly in frontend
- ✅ No regression in functionality
- ✅ All unit tests pass
- ✅ Integration tests pass

---

## Benefits Summary

### 1. Code Quality

**Before**:
- 31 DN parsing calls in PKD Management
- 15 DN parsing calls in PA Service
- Inconsistent formats (RFC2253 vs oneline)
- Duplicated logic

**After**:
- 1 shared implementation
- Consistent RFC2253 format everywhere
- Single source of truth
- ~500 lines of duplicate code eliminated

### 2. Maintainability

**Before**:
- Bug fixes require changes in 2+ services
- Inconsistent behavior
- Difficult to test

**After**:
- Fix once, benefit everywhere
- Consistent behavior guaranteed
- Easy to test (dedicated test suite)

### 3. ICAO Compliance

**Before**:
- PA Service: Non-standard format
- Mixed DN parsing approaches

**After**:
- 100% RFC2253 standard
- ASN.1 structure-based comparison
- Follows ICAO guide recommendations

---

## Timeline Estimate

| Phase | Duration | Cumulative |
|-------|----------|------------|
| Phase 1: Structure | 2 hours | 2 hours |
| Phase 2: DN Parsing | 3 hours | 5 hours |
| Phase 3: Certificate Parsing | 2 hours | 7 hours |
| Phase 4: Utilities | 1 hour | 8 hours |
| Phase 5: PKD Management | 3 hours | 11 hours |
| Phase 6: PA Service | 2 hours | 13 hours |
| Phase 7: Frontend | 1 hour | 14 hours |
| Phase 8: Testing | 2 hours | 16 hours |
| **Total** | **16 hours** | **2 work days** |

---

## Success Criteria

- ✅ Shared library builds successfully
- ✅ All unit tests pass (>95% coverage)
- ✅ All services link against shared library
- ✅ No duplicate DN parsing code
- ✅ Consistent RFC2253 format everywhere
- ✅ Frontend displays structured DN components
- ✅ No regression in functionality
- ✅ Performance maintained or improved
- ✅ Documentation complete

---

## Risks and Mitigation

### Risk 1: Build System Complexity

**Risk**: Shared library adds CMake complexity
**Mitigation**: Use standard CMake practices, document clearly

### Risk 2: ABI Compatibility

**Risk**: Changes to shared library break services
**Mitigation**: Use semantic versioning, maintain API stability

### Risk 3: Testing Coverage

**Risk**: Insufficient testing of shared library
**Mitigation**: Require >95% test coverage, use real certificate data

### Risk 4: Performance Regression

**Risk**: Shared library adds overhead
**Mitigation**: Benchmark before/after, optimize hot paths

---

## Future Enhancements

### Phase 9: LDAP Utilities (Optional)

**Tasks**:
- Extract LDAP connection management
- DN construction utilities
- LDAP search helpers

### Phase 10: Validation Library (Optional)

**Tasks**:
- Trust chain validation
- CRL checking
- OCSP validation
- Link certificate validation

---

## Conclusion

Creating a shared library for DN parsing and X.509 utilities provides:
- ✅ **Single Source of Truth**: One implementation, consistent behavior
- ✅ **ICAO Compliance**: RFC2253 standard everywhere
- ✅ **Better Testing**: Dedicated test suite with high coverage
- ✅ **Easier Maintenance**: Fix bugs once, benefit everywhere
- ✅ **Code Quality**: Eliminate ~500 lines of duplicate code
- ✅ **Idempotency**: Guaranteed consistent results

**Recommended**: Proceed with this approach. Start with Phase 1-4 (shared library core), then incrementally migrate services.

---

**Author**: Claude Sonnet 4.5
**Reviewed**: Architecture and implementation feasibility verified
**Status**: Ready for implementation
