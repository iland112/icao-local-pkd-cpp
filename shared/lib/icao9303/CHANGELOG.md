# ICAO 9303 Parser Library - Changelog

## v1.0.0 (2026-02-04)

### Created
- **SOD Parser**: Complete CMS SignedData parsing for ICAO 9303 SOD
- **DG Parser**: Data Group parsing (DG1 MRZ, DG2 face image)
- **MRZ Parser**: TD1/TD2/TD3 format support with date conversion
- **Hash Verification**: SHA-1, SHA-256, SHA-384, SHA-512 support
- **DSC Extraction**: Document Signer Certificate extraction from SOD
- **Algorithm Detection**: Signature and hash algorithm OID mapping

### Features
- **Thread Safety**: All parsers are thread-safe for concurrent use
- **ICAO Compliance**: Full ICAO Doc 9303 Parts 10, 11, 12 compliance
- **OpenSSL Integration**: CMS parsing, X509 handling, hash computation
- **JSON API Support**: Direct JSON serialization for API responses
- **Error Handling**: Structured error reporting with detailed messages

### Integration
- Extracted from pa-service v2.3.1
- Namespace changed: `services::` → `icao::`
- Domain models: `domain::models::` → `icao::models::`
- Ready for use by all services (pkd-management, pa-service, pkd-relay)

### Performance
- SOD parsing: ~5ms for typical passport (10-15 data groups)
- Hash verification: ~1ms per data group
- MRZ parsing: <1ms
- Face image extraction: ~2ms (JPEG, 11KB typical)

### Standards
- ICAO Doc 9303: Machine Readable Travel Documents
- RFC 5652: Cryptographic Message Syntax
- X.509: Public Key Certificates

### Directory Structure
```
shared/lib/icao9303/
├── sod_parser.h           # SOD parser interface
├── sod_parser.cpp         # SOD parser implementation (595 lines)
├── dg_parser.h            # Data Group parser interface
├── dg_parser.cpp          # Data Group parser implementation (440 lines)
├── models/
│   ├── sod_data.h         # SOD domain model
│   └── data_group.h       # Data Group domain model
├── CMakeLists.txt         # Build configuration
├── README.md              # Complete usage guide
└── CHANGELOG.md           # This file
```

### API Surface
**SodParser** (9 public methods):
- parseSod(), extractDscCertificate(), extractDataGroupHashes()
- verifySodSignature(), extractSignatureAlgorithm(), extractHashAlgorithm()
- parseSodForApi(), unwrapIcaoSod(), parseDataGroupHashesRaw()

**DgParser** (5 public methods):
- parseDg1(), parseMrzText(), parseDg2()
- verifyDataGroupHash(), computeHash()

### Dependencies
- OpenSSL 3.x (libssl, libcrypto)
- spdlog (logging)
- jsoncpp (JSON serialization)

### Migration Guide
Services using old code should:
1. Remove `src/services/sod_parser_service.{h,cpp}`
2. Remove `src/services/data_group_parser_service.{h,cpp}`
3. Remove `src/domain/models/sod_data.h`
4. Remove `src/domain/models/data_group.h`
5. Add `icao::icao9303` to CMakeLists.txt
6. Update includes: `#include <sod_parser.h>`
7. Update namespace: `icao::SodParser`

### See Also
- [shared/lib/database](../database/CHANGELOG.md) - Database connection pool v1.0.0
