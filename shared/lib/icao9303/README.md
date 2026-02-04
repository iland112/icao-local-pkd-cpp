# ICAO 9303 Parser Library

Thread-safe ICAO 9303 parsing library for electronic passport and travel document verification.

## Features

- **SOD (Security Object Document) Parsing**: Complete CMS SignedData structure parsing
- **DSC Certificate Extraction**: Extract Document Signer Certificate from SOD
- **Data Group Hash Extraction**: Parse all data group hashes from LDSSecurityObject
- **Signature Verification**: Verify SOD signature using DSC certificate
- **MRZ Parsing**: Machine Readable Zone parsing (TD1, TD2, TD3 formats)
- **DG1 Parsing**: MRZ data extraction from Data Group 1
- **DG2 Parsing**: Face image extraction (JPEG/JPEG2000) from Data Group 2
- **Hash Verification**: Compute and verify data group hashes (SHA-1, SHA-256, SHA-384, SHA-512)
- **Algorithm Detection**: Extract signature and hash algorithm information

## Standards Compliance

- **ICAO Doc 9303**: Machine Readable Travel Documents
  - Part 10: Logical Data Structure (LDS)
  - Part 11: Security Mechanisms
  - Part 12: Public Key Infrastructure
- **RFC 5652**: Cryptographic Message Syntax (CMS)
- **X.509**: Public Key Certificates

## Usage

### Basic SOD Parsing

```cpp
#include <sod_parser.h>
#include <vector>

// Read SOD data from passport chip
std::vector<uint8_t> sodBytes = readSodFromChip();

// Parse SOD
icao::SodParser parser;
icao::models::SodData sodData = parser.parseSod(sodBytes);

if (sodData.parsingSuccess) {
    std::cout << "Signature Algorithm: " << sodData.signatureAlgorithm << std::endl;
    std::cout << "Hash Algorithm: " << sodData.hashAlgorithm << std::endl;
    std::cout << "Data Groups: " << sodData.getDataGroupCount() << std::endl;

    // Extract DSC certificate
    if (sodData.dscCertificate) {
        // Use certificate for verification
    }
}
```

### Data Group Hash Verification

```cpp
#include <dg_parser.h>

icao::DgParser dgParser;

// Read DG1 data
std::vector<uint8_t> dg1Data = readDg1FromChip();

// Get expected hash from SOD
std::string expectedHash = sodData.getDataGroupHash("1");

// Verify hash
bool hashValid = dgParser.verifyDataGroupHash(
    dg1Data,
    expectedHash,
    sodData.hashAlgorithm
);

if (hashValid) {
    // Parse DG1 (MRZ)
    Json::Value mrzData = dgParser.parseDg1(dg1Data);
    std::string fullName = mrzData["fullName"].asString();
    std::string nationality = mrzData["nationality"].asString();
}
```

### MRZ Parsing

```cpp
// Parse MRZ from text (2 lines, 44 characters each)
std::string mrzText = "P<KORNLEE<<YOUNG<HO<<<<<<<<<<<<<<<<<<<<<<<<<<"
                      "M12345678<KOR8901011M2512312<<<<<<<<<<<<<<04";

Json::Value mrzData = dgParser.parseMrzText(mrzText);

std::cout << "Name: " << mrzData["fullName"].asString() << std::endl;
std::cout << "DOB: " << mrzData["dateOfBirth"].asString() << std::endl;
std::cout << "Expiry: " << mrzData["dateOfExpiry"].asString() << std::endl;
```

### Face Image Extraction (DG2)

```cpp
// Read DG2 data
std::vector<uint8_t> dg2Data = readDg2FromChip();

// Parse DG2 (extracts JPEG/JP2 image)
Json::Value result = dgParser.parseDg2(dg2Data);

if (result["success"].asBool()) {
    std::string imageDataUrl = result["faceImages"][0]["imageDataUrl"].asString();
    std::string imageFormat = result["imageFormat"].asString();
    int imageSize = result["faceImages"][0]["imageSize"].asInt();

    // Display or save image (imageDataUrl is base64 data URL)
}
```

## Integration with Services

### CMakeLists.txt

```cmake
# Find ICAO 9303 library
find_package(icao-icao9303 REQUIRED)

# Link to your service
target_link_libraries(your-service PRIVATE
    icao::icao9303
)
```

### Service Code

```cpp
#include <sod_parser.h>
#include <dg_parser.h>
#include <models/sod_data.h>
#include <models/data_group.h>

class PaVerificationService {
private:
    icao::SodParser sodParser_;
    icao::DgParser dgParser_;

public:
    Json::Value verifyPassport(const std::vector<uint8_t>& sodBytes,
                               const std::vector<uint8_t>& dg1Bytes,
                               const std::vector<uint8_t>& dg2Bytes) {
        // Parse SOD
        auto sodData = sodParser_.parseSod(sodBytes);

        // Verify DG1 hash
        bool dg1Valid = dgParser_.verifyDataGroupHash(
            dg1Bytes,
            sodData.getDataGroupHash("1"),
            sodData.hashAlgorithm
        );

        // Parse MRZ
        Json::Value mrzData = dgParser_.parseDg1(dg1Bytes);

        // Parse face image
        Json::Value faceData = dgParser_.parseDg2(dg2Bytes);

        // Build response
        Json::Value response;
        response["sodParsed"] = sodData.parsingSuccess;
        response["dg1Valid"] = dg1Valid;
        response["mrzData"] = mrzData;
        response["faceImage"] = faceData;

        return response;
    }
};
```

## API Reference

### SodParser

| Method | Description |
|--------|-------------|
| `parseSod()` | Parse complete SOD structure |
| `extractDscCertificate()` | Extract DSC certificate from SOD |
| `extractDataGroupHashes()` | Get all data group hashes as map |
| `verifySodSignature()` | Verify SOD signature with DSC |
| `extractSignatureAlgorithm()` | Get signature algorithm name |
| `extractHashAlgorithm()` | Get hash algorithm name |
| `parseSodForApi()` | Parse SOD with detailed metadata for API response |

### DgParser

| Method | Description |
|--------|-------------|
| `parseDg1()` | Parse DG1 (MRZ) from binary data |
| `parseMrzText()` | Parse MRZ from text string |
| `parseDg2()` | Parse DG2 (face image) from binary data |
| `verifyDataGroupHash()` | Verify data group hash matches expected |
| `computeHash()` | Compute hash of data using specified algorithm |

## Thread Safety

All classes are thread-safe for read operations. Each parser instance can be used concurrently by multiple threads.

## Dependencies

- **OpenSSL 3.x**: CMS parsing, X509 certificate handling
- **spdlog**: Logging
- **jsoncpp**: JSON serialization

## Performance

- **SOD Parsing**: ~5ms for typical passport SOD (10-15 data groups)
- **Hash Verification**: ~1ms per data group (SHA-256)
- **MRZ Parsing**: <1ms
- **DG2 Image Extraction**: ~2ms for JPEG (11KB typical)

## Error Handling

All parsing methods return structured results with error information:

```cpp
icao::models::SodData sodData = parser.parseSod(sodBytes);

if (!sodData.parsingSuccess) {
    if (sodData.parsingErrors) {
        std::cerr << "Error: " << *sodData.parsingErrors << std::endl;
    }
}
```

## Migration from Service Code

If migrating from pa-service:

```cpp
// OLD (pa-service)
#include "../services/sod_parser_service.h"
#include "../services/data_group_parser_service.h"
services::SodParserService sodParser;
services::DataGroupParserService dgParser;

// NEW (shared library)
#include <sod_parser.h>
#include <dg_parser.h>
icao::SodParser sodParser;
icao::DgParser dgParser;
```

Domain models also change namespace:

```cpp
// OLD
domain::models::SodData sodData;

// NEW
icao::models::SodData sodData;
```

## License

Copyright © 2026 SmartCore Inc. All rights reserved.

## See Also

- [Database Connection Pool](../database/README.md) - Thread-safe PostgreSQL pooling
- [ICAO Doc 9303](https://www.icao.int/publications/pages/publication.aspx?docnum=9303) - Official specification
