# ICAO Common Library

**Version**: 1.0.0
**Status**: Development (Phase 1 Complete)
**Date**: 2026-02-02

## Overview

Shared library for ICAO PKD services providing common X.509 certificate processing, DN parsing, and utility functions. Eliminates code duplication across PKD Management, PA Service, and PKD Relay.

## Features

### X.509 Module (`icao::x509`)

#### DN Parser (`dn_parser.h`)
- RFC2253 and OpenSSL oneline format support
- ASN.1 structure-based parsing (ICAO guide compliant)
- Format-independent DN normalization for comparison
- Self-signed certificate detection

#### DN Components (`dn_components.h`)
- Structured DN component extraction using OpenSSL NIDs
- Type-safe access to CN, O, OU, L, ST, C, email, serialNumber
- Eliminates error-prone regex parsing
- Human-readable display name generation

#### Certificate Parser (`certificate_parser.h`)
- Multi-format support: PEM, DER, CER, BIN, CMS/PKCS7
- Automatic format detection
- CMS SignedData extraction (for Master Lists)
- SHA-256 fingerprint computation
- RAII wrapper (CertificatePtr) for memory safety

#### Metadata Extractor (`metadata_extractor.h`)
- Complete X.509 metadata extraction (22 fields)
- Algorithm information (signature, hash, public key)
- Key usage and extended key usage
- CA flags and path length constraints
- Subject/Authority Key Identifiers
- CRL distribution points and OCSP URLs
- Validity period analysis

### Utilities Module (`icao::utils`)

#### String Utils (`string_utils.h`)
- Case conversion, trimming, splitting/joining
- Hex and Base64 encoding/decoding
- UTF-8 validation
- JSON escaping

#### Time Utils (`time_utils.h`)
- ASN1_TIME ↔ std::chrono conversion
- ISO 8601 and RFC 3339 formatting
- Date arithmetic (add days/months/years)
- Unix timestamp conversion

## Directory Structure

```
services/common-lib/
├── include/icao/
│   ├── x509/
│   │   ├── dn_parser.h
│   │   ├── dn_components.h
│   │   ├── certificate_parser.h
│   │   └── metadata_extractor.h
│   └── utils/
│       ├── string_utils.h
│       └── time_utils.h
├── src/
│   ├── x509/
│   │   ├── dn_parser.cpp
│   │   ├── dn_components.cpp
│   │   ├── certificate_parser.cpp
│   │   └── metadata_extractor.cpp
│   └── utils/
│       ├── string_utils.cpp
│       └── time_utils.cpp
├── tests/
│   ├── test_dn_parser.cpp
│   ├── test_dn_components.cpp
│   ├── test_certificate_parser.cpp
│   ├── test_metadata_extractor.cpp
│   ├── test_string_utils.cpp
│   └── test_time_utils.cpp
├── CMakeLists.txt
├── Config.cmake.in
└── README.md
```

## Building

### Standalone Build

```bash
cd services/common-lib
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run tests
make test
# or
./tests/icao_common_tests
```

### Integration with Service

Add to service's CMakeLists.txt:

```cmake
# Find or build icao-common library
find_package(icao-common QUIET)

if(NOT icao-common_FOUND)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common-lib icao-common)
endif()

# Link to your executable
target_link_libraries(your-service
    PRIVATE
        icao::icao-common
)
```

## Usage Examples

### DN Parsing

```cpp
#include <icao/x509/dn_parser.h>
#include <icao/x509/dn_components.h>

using namespace icao::x509;

// Parse DN from certificate
X509* cert = loadCertificate(...);
auto subjectDn = getSubjectDn(cert, DnFormat::RFC2253);
// Returns: "CN=CSCA Latvia,O=National Security Authority,C=LV"

// Extract structured components
DnComponents components = extractSubjectComponents(cert);
if (components.commonName) {
    std::cout << "CN: " << *components.commonName << std::endl;  // "CSCA Latvia"
}
if (components.organization) {
    std::cout << "O: " << *components.organization << std::endl;  // "National Security Authority"
}
std::cout << "Display Name: " << components.getDisplayName() << std::endl;

// Compare DNs (format-independent)
auto norm1 = normalizeDnForComparison("CN=Test,O=Org,C=US");
auto norm2 = normalizeDnForComparison("/C=US/O=Org/CN=Test");
if (norm1 == norm2) {
    std::cout << "DNs match!" << std::endl;
}
```

### Certificate Parsing

```cpp
#include <icao/x509/certificate_parser.h>

using namespace icao::x509;

// Load and parse certificate
std::vector<uint8_t> data = readFile("cert.pem");
auto format = detectCertificateFormat(data);
std::cout << "Format: " << format.formatName << std::endl;

CertificatePtr cert(parseCertificate(data));
if (cert) {
    auto fingerprint = computeFingerprint(cert.get());
    std::cout << "SHA-256: " << *fingerprint << std::endl;
}
```

### Metadata Extraction

```cpp
#include <icao/x509/metadata_extractor.h>

using namespace icao::x509;

CertificateMetadata meta = extractMetadata(cert);
std::cout << "Algorithm: " << *meta.signatureAlgorithm << std::endl;
std::cout << "Key Size: " << *meta.publicKeySize << " bits" << std::endl;
std::cout << "Is CA: " << (*meta.isCA ? "Yes" : "No") << std::endl;

if (isCurrentlyValid(cert)) {
    int days = getDaysUntilExpiration(cert);
    std::cout << "Expires in " << days << " days" << std::endl;
}
```

## Dependencies

- C++20 compiler (GCC 10+, Clang 12+, MSVC 2019+)
- OpenSSL 3.x (libssl-dev, libcrypto)
- CMake 3.15+
- Google Test (automatically downloaded if not found)

## Migration Status

### Phase 1: Library Structure ✅ Complete
- [x] Directory structure
- [x] CMake configuration
- [x] Header files with API declarations
- [x] Test framework setup

### Phase 2: Core Implementation (Pending)
- [ ] dn_parser.cpp implementation
- [ ] dn_components.cpp implementation
- [ ] Unit tests for DN parsing

### Phase 3: Certificate Parsing (Pending)
- [ ] certificate_parser.cpp implementation
- [ ] metadata_extractor.cpp implementation
- [ ] Unit tests

### Phase 4: Utilities (Pending)
- [ ] string_utils.cpp implementation
- [ ] time_utils.cpp implementation
- [ ] Unit tests

### Phase 5-8: Service Migration (Pending)
- [ ] PKD Management integration
- [ ] PA Service integration
- [ ] Frontend updates
- [ ] End-to-end testing

## Architecture Benefits

### Code Reusability
- **Before**: ~500 lines of DN parsing duplicated across 3 services
- **After**: Single source of truth in shared library
- **Impact**: 67% reduction in DN-related code

### Idempotency
- Same input always produces same output
- Consistent DN formatting across all services
- Reliable certificate fingerprints

### Maintainability
- Bug fixes in one place benefit all services
- Easy to add new features (e.g., ICAO 9303-12 support)
- Clear API contracts with comprehensive documentation

### Testability
- Unit tests for all functions
- Mock-friendly interfaces
- Isolated testing without service dependencies

## Documentation

- [SHARED_LIBRARY_ARCHITECTURE_PLAN.md](../../docs/SHARED_LIBRARY_ARCHITECTURE_PLAN.md) - Complete architecture plan
- API documentation: Doxygen-style comments in header files
- Integration guide: This README

## License

Internal use for ICAO Local PKD project

## Authors

- Claude Sonnet 4.5
- Development Team

---

**Status**: Phase 1 Complete (2026-02-02)
**Next**: Phase 2 - DN Parser Implementation
