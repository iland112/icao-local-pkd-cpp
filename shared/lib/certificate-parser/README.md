# ICAO Certificate Parser Library

**Version**: 1.0.0
**Namespace**: `icao::certificate_parser`
**Language**: C++20
**Dependencies**: OpenSSL 3.x

---

## Overview

Shared library for parsing and analyzing X.509 certificates in ICAO PKD systems. Provides automatic file format detection, certificate type identification, and parsing capabilities for multiple formats (PEM, DER, CER, BIN, DVL, Master List).

---

## Features

- ✅ **File Format Auto-Detection**: Automatically detect PEM, DER, CER, BIN, DVL, LDIF, ML formats
- ✅ **Certificate Type Detection**: Identify CSCA, DSC, MLSC, Link Certificates, DVL Signers
- ✅ **Multi-Format Parsing**: Parse PEM, DER, CER, BIN certificate files
- ✅ **ICAO 9303 Compliant**: Follows ICAO Doc 9303 Part 12 PKI specifications
- ✅ **OpenSSL Integration**: Built on OpenSSL 3.x APIs
- ✅ **Header-Only Dependency**: Easy integration with CMake

---

## Components

### 1. FileDetector

Automatic file format detection using extension and content analysis.

```cpp
#include "file_detector.h"

using namespace icao::certificate_parser;

// Read file
std::vector<uint8_t> content = readFile("cert.pem");

// Detect format
FileFormat format = FileDetector::detectFormat("cert.pem", content);

if (format == FileFormat::PEM) {
    std::cout << "PEM certificate detected" << std::endl;
}
```

**Supported Formats**:
- PEM (`.pem`, `.crt`) - Text-based, Base64 encoded
- DER (`.der`) - Binary ASN.1 encoding
- CER (`.cer`) - Windows DER convention
- BIN (`.bin`) - Generic binary
- DVL (`.dvl`) - Deviation List (CMS SignedData)
- LDIF (`.ldif`) - LDAP interchange format
- ML (`.ml`) - Master List (CMS SignedData)

### 2. CertTypeDetector

Automatic certificate type detection using X.509 extensions.

```cpp
#include "cert_type_detector.h"

using namespace icao::certificate_parser;

// Load certificate (OpenSSL)
X509* cert = d2i_X509_bio(bio, nullptr);

// Detect type
CertificateInfo info = CertTypeDetector::detectType(cert);

std::cout << "Type: " << CertTypeDetector::typeToString(info.type) << std::endl;
std::cout << "Country: " << info.country << std::endl;
std::cout << "Fingerprint: " << info.fingerprint << std::endl;
std::cout << "Is CA: " << (info.is_ca ? "Yes" : "No") << std::endl;

X509_free(cert);
```

**Certificate Types**:
- CSCA - Country Signing CA (Root CA, self-signed)
- DSC - Document Signer Certificate
- MLSC - Master List Signer Certificate
- LINK_CERT - Link Certificate (Intermediate CA)
- DVL_SIGNER - Deviation List Signer Certificate

**Detection Algorithm**:
1. Check Extended Key Usage for MLSC OID (2.23.136.1.1.9)
2. Check Extended Key Usage for DVL Signer OID (2.23.136.1.1.10)
3. Check Basic Constraints (CA=TRUE)
4. Check Key Usage (keyCertSign)
5. Check if self-signed (Issuer DN == Subject DN)
6. Default to DSC if non-CA

---

## Installation

### Option 1: CMake Subdirectory

```cmake
# In your project's CMakeLists.txt
add_subdirectory(shared/lib/certificate-parser)

target_link_libraries(your-target PRIVATE
    icao::certificate-parser
)
```

### Option 2: CMake Install

```bash
cd shared/lib/certificate-parser
cmake -B build -S .
cmake --build build
sudo cmake --install build
```

Then in your project:

```cmake
find_package(icao-certificate-parser REQUIRED)

target_link_libraries(your-target PRIVATE
    icao::certificate-parser
)
```

---

## Usage Examples

### Complete Example: Parse Certificate File

```cpp
#include "file_detector.h"
#include "cert_type_detector.h"
#include <openssl/x509.h>
#include <openssl/bio.h>
#include <fstream>
#include <vector>

using namespace icao::certificate_parser;

int main() {
    // 1. Read file
    std::ifstream file("certificate.pem", std::ios::binary);
    std::vector<uint8_t> content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    // 2. Detect format
    FileFormat format = FileDetector::detectFormat("certificate.pem", content);
    std::cout << "Format: " << FileDetector::formatToString(format) << std::endl;

    // 3. Parse certificate (OpenSSL)
    BIO* bio = BIO_new_mem_buf(content.data(), content.size());
    X509* cert = nullptr;

    if (format == FileFormat::PEM) {
        cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    } else if (format == FileFormat::DER || format == FileFormat::CER) {
        cert = d2i_X509_bio(bio, nullptr);
    }

    BIO_free(bio);

    if (!cert) {
        std::cerr << "Failed to parse certificate" << std::endl;
        return 1;
    }

    // 4. Detect certificate type
    CertificateInfo info = CertTypeDetector::detectType(cert);

    std::cout << "Certificate Type: " << CertTypeDetector::typeToString(info.type) << std::endl;
    std::cout << "Country: " << info.country << std::endl;
    std::cout << "Fingerprint: " << info.fingerprint << std::endl;
    std::cout << "Subject DN: " << info.subject_dn << std::endl;
    std::cout << "Issuer DN: " << info.issuer_dn << std::endl;
    std::cout << "Is Self-Signed: " << (info.is_self_signed ? "Yes" : "No") << std::endl;
    std::cout << "Is CA: " << (info.is_ca ? "Yes" : "No") << std::endl;

    X509_free(cert);

    return 0;
}
```

---

## API Reference

### FileDetector Class

| Method | Description | Returns |
|--------|-------------|---------|
| `detectFormat(filename, content)` | Detect file format | `FileFormat` |
| `formatToString(format)` | Convert enum to string | `std::string` |
| `stringToFormat(str)` | Convert string to enum | `FileFormat` |

### CertTypeDetector Class

| Method | Description | Returns |
|--------|-------------|---------|
| `detectType(cert)` | Detect certificate type | `CertificateInfo` |
| `typeToString(type)` | Convert enum to string | `std::string` |
| `stringToType(str)` | Convert string to enum | `CertificateType` |
| `isMasterListSigner(cert)` | Check if MLSC | `bool` |
| `isDeviationListSigner(cert)` | Check if DVL Signer | `bool` |

### CertificateInfo Struct

```cpp
struct CertificateInfo {
    CertificateType type;           // Detected type
    std::string country;            // ISO 3166-1 alpha-2
    std::string fingerprint;        // SHA-256 hex
    std::string subject_dn;         // Subject DN
    std::string issuer_dn;          // Issuer DN
    bool is_self_signed;            // Issuer == Subject
    bool is_ca;                     // Basic Constraints CA=TRUE
    bool has_key_cert_sign;         // Key Usage keyCertSign
    std::string error_message;      // Error if any
};
```

---

## Standards Compliance

- [ICAO Doc 9303 Part 12](https://www.icao.int/publications/Documents/9303_p12_cons_en.pdf) - PKI for MRTDs
- [RFC 5280](https://datatracker.ietf.org/doc/html/rfc5280) - X.509 Certificate and CRL Profile
- [RFC 3852](https://datatracker.ietf.org/doc/html/rfc3852) - CMS (for DVL/ML)

---

## Building & Testing

```bash
# Build library
cmake -B build -S .
cmake --build build

# Run tests (if enabled)
cmake -B build -S . -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build
```

---

## License

Proprietary - ICAO Local PKD System

---

## Changelog

### v1.0.0 (2026-02-04)

- ✅ Initial release
- ✅ FileDetector implementation
- ✅ CertTypeDetector implementation
- ✅ CMake build system
- ✅ OpenSSL 3.x integration

---

## Contributing

See main project [DEVELOPMENT_GUIDE.md](../../../docs/DEVELOPMENT_GUIDE.md)

---

**Maintained by**: ICAO Local PKD Development Team
**Last Updated**: 2026-02-04
