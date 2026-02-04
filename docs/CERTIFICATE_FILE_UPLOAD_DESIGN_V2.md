# Certificate File Upload Feature - Design Document V2

**Feature Branch**: `feature/certificate-file-upload`
**Target Version**: v2.5.0
**Date**: 2026-02-04
**Status**: 🎯 Architecture Design Phase

---

## 📋 Executive Summary

본 기능은 ICAO PKD 시스템에 개별 인증서 파일 업로드 기능을 추가합니다. 기존 LDIF/Master List 업로드와 함께, PEM, DER, CER, BIN 인증서 파일 및 DVL (Deviation List) 파일을 지원합니다.

**Key Innovations** (V2):
- ✨ **Shared Library Architecture** - 재사용 가능한 파서 라이브러리
- 🔄 **Multi-File Upload** - 여러 파일 동시 업로드 (배치 처리)
- 🏷️ **Source Tracking** - 데이터 출처 기록 (ICAO PKD, 외교부, BSI 등)
- 🤖 **Auto-Detection** - 파일 타입 및 인증서 유형 자동 감지
- 📊 **DVL Support** - Deviation List 파일 파싱 및 저장

---

## 🏗️ Architecture Overview

### Shared Library Structure

```
icao-local-pkd/
├── shared/
│   └── lib/
│       ├── certificate-parser/          # NEW - Certificate Parser Library ⭐
│       │   ├── include/
│       │   │   ├── file_detector.h      # File format detection
│       │   │   ├── cert_type_detector.h # Certificate type detection
│       │   │   ├── pem_parser.h         # PEM format parser
│       │   │   ├── der_parser.h         # DER/CER/BIN parser
│       │   │   ├── dvl_parser.h         # Deviation List parser
│       │   │   └── cert_validator.h     # X.509 validation
│       │   ├── src/
│       │   │   ├── file_detector.cpp
│       │   │   ├── cert_type_detector.cpp
│       │   │   ├── pem_parser.cpp
│       │   │   ├── der_parser.cpp
│       │   │   ├── dvl_parser.cpp
│       │   │   └── cert_validator.cpp
│       │   ├── CMakeLists.txt
│       │   ├── icao-certificate-parser-config.cmake.in
│       │   ├── README.md
│       │   └── CHANGELOG.md
│       ├── icao9303/                    # Existing - SOD, DG parsers
│       ├── database/                    # Existing - DB connection pool
│       ├── ldap/                        # Existing - LDAP connection pool
│       ├── audit/                       # Existing - Audit logging
│       └── ...
├── services/
│   └── pkd-management/
│       └── src/
│           ├── processing_strategy.h    # Extended with new strategies
│           └── main.cpp                 # New endpoint integration
└── docs/
    ├── CERTIFICATE_FILE_UPLOAD_DESIGN_V2.md (this file)
    └── DVL_ANALYSIS.md                  # DVL file format analysis
```

### Library Interface Design

**Namespace**: `icao::certificate_parser`

**Key Components**:
1. **FileDetector** - File format auto-detection
2. **CertTypeDetector** - Certificate type auto-detection
3. **PemParser** - PEM format parsing
4. **DerParser** - DER/CER/BIN format parsing
5. **DvlParser** - Deviation List parsing
6. **CertValidator** - X.509 validation

---

## 🔍 Component Design

### 1. File Detector

**File**: `shared/lib/certificate-parser/include/file_detector.h`

```cpp
namespace icao::certificate_parser {

enum class FileFormat {
    UNKNOWN,
    PEM,
    DER,
    CER,      // Alias for DER (Windows convention)
    BIN,      // Generic binary
    DVL,      // Deviation List
    LDIF,     // For future integration
    ML        // Master List
};

class FileDetector {
public:
    /**
     * @brief Detect file format from filename and content
     * @param filename Original filename
     * @param content File content (first 512 bytes sufficient)
     * @return Detected file format
     */
    static FileFormat detectFormat(
        const std::string& filename,
        const std::vector<uint8_t>& content
    );

    /**
     * @brief Get file format string for database
     */
    static std::string formatToString(FileFormat format);

private:
    static FileFormat detectByExtension(const std::string& filename);
    static FileFormat detectByContent(const std::vector<uint8_t>& content);
    static bool isPEM(const std::vector<uint8_t>& content);
    static bool isDER(const std::vector<uint8_t>& content);
    static bool isDVL(const std::vector<uint8_t>& content);
};

} // namespace icao::certificate_parser
```

**Detection Algorithm**:

1. **Extension-based** (priority):
   - `.pem`, `.crt` → PEM
   - `.der` → DER
   - `.cer` → CER (Windows DER)
   - `.bin` → BIN
   - `.dvl` → DVL

2. **Content-based** (fallback):
   - PEM: `-----BEGIN CERTIFICATE-----` header
   - DER: ASN.1 DER encoding (starts with `0x30 0x82` or `0x30 0x81`)
   - DVL: PKCS#7 SignedData with OID 2.23.136.1.1.7

---

### 2. Certificate Type Detector

**File**: `shared/lib/certificate-parser/include/cert_type_detector.h`

```cpp
namespace icao::certificate_parser {

enum class CertificateType {
    UNKNOWN,
    CSCA,         // Country Signing CA
    DSC,          // Document Signer Certificate
    DSC_NC,       // Non-Conformant DSC
    MLSC,         // Master List Signer Certificate
    LINK_CERT,    // Link Certificate (intermediate CSCA)
    DVL_SIGNER    // Deviation List Signer
};

struct CertificateInfo {
    CertificateType type;
    std::string country;           // ISO 3166-1 alpha-2
    std::string fingerprint;       // SHA-256 hex
    std::string subject_dn;
    std::string issuer_dn;
    bool is_self_signed;
    std::string error_message;
};

class CertTypeDetector {
public:
    /**
     * @brief Detect certificate type from X.509 attributes
     * @param cert OpenSSL X509 certificate
     * @return Certificate information
     */
    static CertificateInfo detectType(X509* cert);

    /**
     * @brief Check if certificate is MLSC
     * @details Checks for OID 2.23.136.1.1.9 (id-icao-mrtd-security-masterListSigner)
     */
    static bool isMasterListSigner(X509* cert);

    /**
     * @brief Check if certificate is DVL Signer
     * @details Checks for OID 2.23.136.1.1.10 (id-icao-mrtd-security-deviationListSigner)
     */
    static bool isDeviationListSigner(X509* cert);

private:
    static bool isCA(X509* cert);
    static bool isSelfSigned(X509* cert);
    static std::string extractCountry(X509* cert);
    static bool hasKeyCertSign(X509* cert);
};

} // namespace icao::certificate_parser
```

**Detection Logic**:

```
1. Check Extended Key Usage for MLSC OID (2.23.136.1.1.9)
   → MLSC

2. Check Extended Key Usage for DVL Signer OID (2.23.136.1.1.10)
   → DVL_SIGNER

3. Check Basic Constraints: CA=TRUE
   └─ Yes:
      ├─ Self-signed (Issuer DN == Subject DN)
      │  └─ CSCA (Root)
      └─ Not self-signed
         └─ LINK_CERT (Intermediate CSCA)

4. Default:
   └─ DSC (Document Signer)
```

---

### 3. PEM Parser

**File**: `shared/lib/certificate-parser/include/pem_parser.h`

```cpp
namespace icao::certificate_parser {

struct ParseResult {
    bool success;
    X509* certificate;          // OpenSSL X509 (caller must free)
    CertificateInfo cert_info;
    std::string error_message;
};

class PemParser {
public:
    /**
     * @brief Parse PEM-encoded certificate
     * @param content File content
     * @return Parse result with X509 certificate
     */
    static ParseResult parse(const std::vector<uint8_t>& content);

    /**
     * @brief Parse multiple PEM certificates from single file
     * @param content File content (may contain multiple certs)
     * @return Vector of parse results
     */
    static std::vector<ParseResult> parseMultiple(const std::vector<uint8_t>& content);

private:
    static X509* loadFromBIO(BIO* bio);
};

} // namespace icao::certificate_parser
```

---

### 4. DER Parser

**File**: `shared/lib/certificate-parser/include/der_parser.h`

```cpp
namespace icao::certificate_parser {

class DerParser {
public:
    /**
     * @brief Parse DER/CER/BIN-encoded certificate
     * @param content File content
     * @param format File format (DER, CER, BIN)
     * @return Parse result with X509 certificate
     */
    static ParseResult parse(
        const std::vector<uint8_t>& content,
        FileFormat format = FileFormat::DER
    );

private:
    static X509* loadFromDER(const uint8_t* data, size_t length);
};

} // namespace icao::certificate_parser
```

---

### 5. DVL Parser

**File**: `shared/lib/certificate-parser/include/dvl_parser.h`

```cpp
namespace icao::certificate_parser {

struct DeviationEntry {
    std::string csca_subject_dn;
    std::string csca_serial_number;
    std::string deviation_description;
    std::string deviation_code_oid;
};

struct DvlParseResult {
    bool success;
    std::string country;
    X509* signer_certificate;           // DVL Signer Certificate
    std::vector<DeviationEntry> deviations;
    std::string issued_date;
    std::string valid_until;
    std::string error_message;
};

class DvlParser {
public:
    /**
     * @brief Parse Deviation List (DVL) file
     * @param content DVL file content (CMS SignedData)
     * @return DVL parse result
     */
    static DvlParseResult parse(const std::vector<uint8_t>& content);

    /**
     * @brief Verify DVL signature
     * @param content DVL file content
     * @param csca_cert CSCA certificate for verification
     * @return true if signature is valid
     */
    static bool verifySignature(
        const std::vector<uint8_t>& content,
        X509* csca_cert
    );

private:
    static DvlParseResult parseCMS(const uint8_t* data, size_t length);
    static std::vector<DeviationEntry> parseDeviationList(const uint8_t* data, size_t length);
};

} // namespace icao::certificate_parser
```

**DVL Structure** (based on analysis):
```
CMS SignedData {
  contentType: 1.2.840.113549.1.7.2 (pkcs7-signedData)
  content: {
    encapContentInfo: {
      eContentType: 2.23.136.1.1.7 (ICAO deviationList)
      eContent: {
        version: 0
        digestAlgorithm: SHA-1
        deviations: SEQUENCE {
          deviation: {
            cscaSubjectDN: DN
            cscaSerialNumber: INTEGER
            deviationDescription: UTF8String
            deviationCodeOID: OID
          }
        }
      }
    }
    certificates: [ DVL Signer Certificate ]
    signerInfos: [ Signature ]
  }
}
```

---

### 6. Certificate Validator

**File**: `shared/lib/certificate-parser/include/cert_validator.h`

```cpp
namespace icao::certificate_parser {

struct ValidationResult {
    bool is_valid;
    bool is_expired;
    bool signature_valid;
    bool trust_chain_valid;
    std::string error_message;
};

class CertValidator {
public:
    /**
     * @brief Validate X.509 certificate
     * @param cert Certificate to validate
     * @param issuer_cert Issuer certificate (optional)
     * @return Validation result
     */
    static ValidationResult validate(X509* cert, X509* issuer_cert = nullptr);

    /**
     * @brief Check if certificate is expired
     */
    static bool isExpired(X509* cert);

    /**
     * @brief Verify certificate signature
     */
    static bool verifySignature(X509* cert, X509* issuer_cert);

private:
    static bool checkValidity(X509* cert);
};

} // namespace icao::certificate_parser
```

---

## 🗄️ Database Schema Extensions

### 1. Source Tracking

**Add to `uploaded_file` table**:

```sql
-- Add source tracking columns
ALTER TABLE uploaded_file
ADD COLUMN source_type VARCHAR(50) DEFAULT 'UNKNOWN',
ADD COLUMN source_description TEXT,
ADD COLUMN source_url VARCHAR(500),
ADD COLUMN source_organization VARCHAR(200);

-- Add check constraint
ALTER TABLE uploaded_file
ADD CONSTRAINT chk_source_type CHECK (
    source_type IN (
        'ICAO_PKD',          -- ICAO Public Key Directory
        'NATIONAL_CA',       -- National Certificate Authority
        'DIPLOMATIC',        -- Diplomatic channel
        'BSI_GERMANY',       -- Germany BSI
        'FOREIGN_AFFAIRS',   -- Ministry of Foreign Affairs
        'EMBASSY',           -- Embassy/Consulate
        'MANUAL_UPLOAD',     -- Manual upload by admin
        'UNKNOWN'            -- Unknown source
    )
);

-- Update file format constraint
ALTER TABLE uploaded_file
DROP CONSTRAINT IF EXISTS chk_file_format;

ALTER TABLE uploaded_file
ADD CONSTRAINT chk_file_format CHECK (
    file_format IN ('LDIF', 'ML', 'PEM', 'DER', 'CER', 'BIN', 'DVL')
);
```

**Example values**:
```sql
INSERT INTO uploaded_file (source_type, source_description, source_url, source_organization)
VALUES (
    'BSI_GERMANY',
    'Germany Federal Office for Information Security',
    'https://www.bsi.bund.de/csca',
    'Bundesamt für Sicherheit in der Informationstechnik'
);
```

### 2. Deviation List Table

```sql
CREATE TABLE deviation_list (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    upload_id UUID REFERENCES uploaded_file(id) ON DELETE CASCADE,

    -- Affected Certificate
    country_code VARCHAR(2) NOT NULL,
    csca_subject_dn VARCHAR(500) NOT NULL,
    csca_serial_number VARCHAR(100) NOT NULL,

    -- Deviation Details
    deviation_description TEXT NOT NULL,
    deviation_code_oid VARCHAR(100),

    -- DVL Metadata
    signer_certificate_fingerprint VARCHAR(64),
    signer_subject_dn VARCHAR(500),
    issued_date TIMESTAMP,
    valid_until TIMESTAMP,

    -- Audit
    uploaded_at TIMESTAMP DEFAULT NOW(),
    uploaded_by VARCHAR(100),

    -- Indexes
    CONSTRAINT uk_deviation UNIQUE (csca_subject_dn, csca_serial_number, deviation_code_oid)
);

CREATE INDEX idx_deviation_country ON deviation_list(country_code);
CREATE INDEX idx_deviation_upload ON deviation_list(upload_id);
CREATE INDEX idx_deviation_csca ON deviation_list(csca_subject_dn);
```

### 3. Multi-File Upload Tracking

**Add to `uploaded_file` table**:

```sql
ALTER TABLE uploaded_file
ADD COLUMN batch_id UUID,
ADD COLUMN batch_index INTEGER,
ADD COLUMN batch_total INTEGER;

CREATE INDEX idx_uploaded_file_batch ON uploaded_file(batch_id);
```

**Usage**:
```sql
-- Upload 5 certificates in one batch
INSERT INTO uploaded_file (batch_id, batch_index, batch_total, ...) VALUES
  ('uuid-1', 1, 5, ...),
  ('uuid-1', 2, 5, ...),
  ('uuid-1', 3, 5, ...),
  ('uuid-1', 4, 5, ...),
  ('uuid-1', 5, 5, ...);
```

---

## 🏷️ LDAP Schema Extensions

### Source Attribute

**Add to all certificate objectClasses**:

```ldif
# Update schema
dn: cn=schema
changetype: modify
add: attributeTypes
attributeTypes: ( 1.3.6.1.4.1.999999.1.1.20
  NAME 'certificateSource'
  DESC 'Source of certificate (ICAO PKD, BSI, etc.)'
  EQUALITY caseIgnoreMatch
  SUBSTR caseIgnoreSubstringsMatch
  SYNTAX 1.3.6.1.4.1.1466.115.121.1.15
  SINGLE-VALUE )

attributeTypes: ( 1.3.6.1.4.1.999999.1.1.21
  NAME 'sourceOrganization'
  DESC 'Organization that provided the certificate'
  EQUALITY caseIgnoreMatch
  SUBSTR caseIgnoreSubstringsMatch
  SYNTAX 1.3.6.1.4.1.1466.115.121.1.15
  SINGLE-VALUE )

attributeTypes: ( 1.3.6.1.4.1.999999.1.1.22
  NAME 'sourceUrl'
  DESC 'URL of certificate source'
  EQUALITY caseIgnoreMatch
  SYNTAX 1.3.6.1.4.1.1466.115.121.1.15
  SINGLE-VALUE )

# Update objectClasses
modify: objectClasses
objectClasses: ( 1.3.6.1.4.1.999999.1.2.1
  NAME 'pkdCertificate'
  SUP top STRUCTURAL
  MUST ( cn $ userCertificate;binary )
  MAY ( certificateSource $ sourceOrganization $ sourceUrl $ ... ) )
```

### Deviation List ObjectClass

```ldif
# New objectClass for Deviation List entries
dn: cn=schema
changetype: modify
add: objectClasses
objectClasses: ( 1.3.6.1.4.1.999999.1.2.10
  NAME 'pkdDeviation'
  DESC 'ICAO Deviation List Entry'
  SUP top STRUCTURAL
  MUST ( cn $ cscaSubjectDN $ cscaSerialNumber $ deviationDescription )
  MAY ( deviationCodeOID $ signerCertificate;binary $ issuedDate $ validUntil $ certificateSource ) )

# Sample entry
dn: cn=abc123...,o=dvl,c=DE,dc=data,dc=download,dc=pkd,...
objectClass: pkdDeviation
objectClass: top
cn: abc123...
cscaSubjectDN: C=DE,O=bund,OU=bsi,serialNumber=013,CN=csca-germany
cscaSerialNumber: 0142
deviationDescription: Country name is encoded as UTF-8 instead of Printable String.
deviationCodeOID: 2.23.136.1.1.7.1.2
certificateSource: BSI_GERMANY
sourceOrganization: Bundesamt für Sicherheit in der Informationstechnik
sourceUrl: https://www.bsi.bund.de/csca
issuedDate: 20181106000000Z
```

---

## 🎯 Strategy Pattern Integration

### Extended Processing Strategy

**File**: `services/pkd-management/src/processing_strategy.h`

```cpp
class ProcessingStrategy {
public:
    virtual ~ProcessingStrategy() = default;

    // Existing methods
    virtual void processLdifEntries(...) = 0;
    virtual void processMasterListContent(...) = 0;

    // NEW: Certificate file processing
    virtual void processCertificateFile(
        const std::string& uploadId,
        const std::string& fileFormat,
        const std::vector<uint8_t>& content,
        PGconn* conn,
        LDAP* ld
    ) = 0;

    // NEW: Deviation List processing
    virtual void processDeviationList(
        const std::string& uploadId,
        const std::vector<uint8_t>& content,
        PGconn* conn,
        LDAP* ld
    ) = 0;

    // NEW: Multi-file batch processing
    virtual void processCertificateBatch(
        const std::string& batchId,
        const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files,
        PGconn* conn,
        LDAP* ld
    ) = 0;
};
```

### Strategy Implementation Example

```cpp
void AutoProcessingStrategy::processCertificateFile(
    const std::string& uploadId,
    const std::string& fileFormat,
    const std::vector<uint8_t>& content,
    PGconn* conn,
    LDAP* ld
) {
    using namespace icao::certificate_parser;

    // 1. Detect file format (if not provided)
    FileFormat format = FileDetector::detectFormat("", content);

    // 2. Parse certificate
    ParseResult result;
    if (format == FileFormat::PEM) {
        result = PemParser::parse(content);
    } else if (format == FileFormat::DER || format == FileFormat::CER || format == FileFormat::BIN) {
        result = DerParser::parse(content, format);
    } else {
        throw std::runtime_error("Unsupported file format");
    }

    if (!result.success) {
        throw std::runtime_error(result.error_message);
    }

    // 3. Detect certificate type
    CertificateInfo certInfo = CertTypeDetector::detectType(result.certificate);

    // 4. Validate certificate
    ValidationResult validation = CertValidator::validate(result.certificate);

    // 5. Save to database (using existing certificate_utils.cpp)
    saveCertificateWithDuplicateCheck(
        result.certificate,
        certInfo.type,
        certInfo.country,
        uploadId,
        conn
    );

    // 6. Save to LDAP (if connection available)
    if (ld) {
        saveCertificateToLdap(
            result.certificate,
            certInfo.type,
            certInfo.country,
            ld
        );
    }

    X509_free(result.certificate);
}
```

---

## 🌐 API Endpoints

### 1. Single Certificate Upload

```http
POST /api/upload/certificate
Content-Type: multipart/form-data

Parameters:
- file: Certificate file (required)
- mode: "AUTO" | "MANUAL" (optional, default: AUTO)
- source_type: Source type enum (optional)
- source_description: Description (optional)
- country: ISO 3166-1 alpha-2 (optional, auto-detect if not provided)
```

**Response**:
```json
{
  "success": true,
  "uploadId": "uuid",
  "fileFormat": "PEM",
  "detectedCertType": "CSCA",
  "country": "KR",
  "fingerprint": "abc123...",
  "source": {
    "type": "BSI_GERMANY",
    "organization": "Bundesamt für Sicherheit..."
  }
}
```

### 2. Multi-File Upload (Batch)

```http
POST /api/upload/certificates/batch
Content-Type: multipart/form-data

Parameters:
- files[]: Multiple certificate files (required)
- mode: "AUTO" | "MANUAL" (optional)
- source_type: Source type (optional)
```

**Response**:
```json
{
  "success": true,
  "batchId": "uuid",
  "totalFiles": 5,
  "results": [
    {
      "uploadId": "uuid1",
      "filename": "csca-kr.pem",
      "fileFormat": "PEM",
      "certType": "CSCA",
      "status": "SUCCESS"
    },
    {
      "uploadId": "uuid2",
      "filename": "dsc-kr.der",
      "fileFormat": "DER",
      "certType": "DSC",
      "status": "SUCCESS"
    },
    ...
  ],
  "successCount": 4,
  "failureCount": 1
}
```

### 3. Deviation List Upload

```http
POST /api/upload/deviation-list
Content-Type: multipart/form-data

Parameters:
- file: DVL file (required)
- source_type: Source type (optional)
```

**Response**:
```json
{
  "success": true,
  "uploadId": "uuid",
  "country": "DE",
  "deviationCount": 1,
  "deviations": [
    {
      "cscaSubjectDN": "C=DE,O=bund,OU=bsi,serialNumber=013,CN=csca-germany",
      "cscaSerialNumber": "0142",
      "description": "Country name is encoded as UTF-8 instead of Printable String.",
      "deviationCodeOID": "2.23.136.1.1.7.1.2"
    }
  ],
  "signerCertificate": {
    "subject": "CN=CSCA Deviation List Signer,serialNumber=0001,OU=bsi,O=bund,C=DE",
    "issuer": "CN=csca-germany,serialNumber=103,OU=bsi,O=bund,C=DE"
  }
}
```

---

## 🎨 Frontend Design

### 1. File Upload UI Enhancement

**File**: `frontend/src/pages/FileUpload.tsx`

**New Features**:
- Multi-file drag & drop
- Source type selector
- Source organization input
- Batch upload progress tracking

**UI Components**:

```tsx
// Source Type Selector
<select name="sourceType">
  <option value="ICAO_PKD">ICAO PKD</option>
  <option value="BSI_GERMANY">Germany BSI</option>
  <option value="NATIONAL_CA">National CA</option>
  <option value="DIPLOMATIC">Diplomatic Channel</option>
  <option value="FOREIGN_AFFAIRS">Ministry of Foreign Affairs</option>
  <option value="EMBASSY">Embassy/Consulate</option>
  <option value="MANUAL_UPLOAD">Manual Upload</option>
  <option value="UNKNOWN">Unknown</option>
</select>

// Source Organization Input
<input
  type="text"
  name="sourceOrganization"
  placeholder="Organization name (optional)"
  maxLength={200}
/>

// Multi-file Upload Area
<div className="dropzone">
  <p>Drop certificate files here or click to browse</p>
  <p className="text-sm">Supports: PEM, DER, CER, BIN, DVL</p>
  <p className="text-sm">Multiple files allowed</p>
</div>

// Batch Progress
<div className="batch-progress">
  <h3>Uploading {totalFiles} files...</h3>
  {files.map((file, index) => (
    <div key={index} className="file-progress">
      <span>{file.name}</span>
      <span>{file.status}</span>
      <ProgressBar value={file.progress} />
    </div>
  ))}
</div>
```

### 2. Deviation List Viewer

**New Component**: `frontend/src/components/DeviationListViewer.tsx`

```tsx
interface DeviationListViewerProps {
  uploadId: string;
}

export function DeviationListViewer({ uploadId }: DeviationListViewerProps) {
  const [deviations, setDeviations] = useState<Deviation[]>([]);

  return (
    <div className="deviation-list">
      <h3>Deviation List Details</h3>
      <table>
        <thead>
          <tr>
            <th>CSCA Subject DN</th>
            <th>Serial Number</th>
            <th>Deviation Description</th>
            <th>Code OID</th>
          </tr>
        </thead>
        <tbody>
          {deviations.map((dev, i) => (
            <tr key={i}>
              <td>{dev.cscaSubjectDN}</td>
              <td>{dev.cscaSerialNumber}</td>
              <td>{dev.description}</td>
              <td>{dev.codeOID}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
```

---

## 📊 Implementation Plan (Revised)

### Phase 1: Shared Library Foundation (3 days)

**Tasks**:
- [ ] Create `shared/lib/certificate-parser/` directory structure
- [ ] Implement `FileDetector` (extension + content detection)
- [ ] Implement `CertTypeDetector` (Basic Constraints, Key Usage, EKU)
- [ ] Implement `PemParser` (single + multiple certs)
- [ ] Implement `DerParser` (DER/CER/BIN)
- [ ] Implement `CertValidator` (expiration, signature)
- [ ] Create CMakeLists.txt with export configuration
- [ ] Write unit tests for each component
- [ ] Create library README and CHANGELOG

**Deliverables**:
- `libicao-certificate-parser.a` static library
- Header files in `shared/lib/certificate-parser/include/`
- Unit tests with 90%+ coverage

### Phase 2: DVL Parser Implementation (2 days)

**Tasks**:
- [ ] Implement `DvlParser` (CMS SignedData parsing)
- [ ] Add deviation entry extraction logic
- [ ] Implement signature verification
- [ ] Add DVL-specific unit tests
- [ ] Document DVL parsing algorithm

**Deliverables**:
- DVL parsing functionality in shared library
- DVL unit tests

### Phase 3: Database Schema Updates (1 day)

**Tasks**:
- [ ] Create migration: Add source tracking columns
- [ ] Create migration: Add deviation_list table
- [ ] Create migration: Add batch tracking columns
- [ ] Update file_format constraint
- [ ] Test migrations on development database

**Deliverables**:
- 3 SQL migration files
- Updated schema documentation

### Phase 4: LDAP Schema Extensions (1 day)

**Tasks**:
- [ ] Define new LDAP attributes (certificateSource, etc.)
- [ ] Create pkdDeviation objectClass
- [ ] Update LDAP initialization scripts
- [ ] Test LDAP schema updates

**Deliverables**:
- LDAP schema LDIF files
- Updated docker/ldap/schema/

### Phase 5: Strategy Pattern Integration (3 days)

**Tasks**:
- [ ] Extend `ProcessingStrategy` interface
- [ ] Implement `processCertificateFile()` in AUTO mode
- [ ] Implement `processCertificateFile()` in MANUAL mode
- [ ] Implement `processDeviationList()` in AUTO mode
- [ ] Implement `processCertificateBatch()` for multi-file
- [ ] Update CMakeLists.txt to link shared library
- [ ] Integration tests

**Deliverables**:
- Extended processing strategies
- Integration tests (5+ test scenarios)

### Phase 6: Backend API Endpoints (2 days)

**Tasks**:
- [ ] Implement `POST /api/upload/certificate`
- [ ] Implement `POST /api/upload/certificates/batch`
- [ ] Implement `POST /api/upload/deviation-list`
- [ ] Add source tracking parameters
- [ ] Add auto-detection logic
- [ ] API integration tests

**Deliverables**:
- 3 new API endpoints
- OpenAPI specification updates

### Phase 7: Frontend UI (3 days)

**Tasks**:
- [ ] Add multi-file drag & drop
- [ ] Add source type selector
- [ ] Add source organization input
- [ ] Implement batch upload progress tracking
- [ ] Create DeviationListViewer component
- [ ] Update UploadHistory to show source info
- [ ] Dark mode support for all new components

**Deliverables**:
- Enhanced FileUpload.tsx
- New DeviationListViewer component
- Updated UploadHistory UI

### Phase 8: Testing & Documentation (2 days)

**Tasks**:
- [ ] End-to-end testing (all file types)
- [ ] Multi-file upload testing (10+ files)
- [ ] DVL upload and verification
- [ ] Source tracking verification
- [ ] Performance testing (batch upload)
- [ ] Update CLAUDE.md with v2.5.0 entry
- [ ] Create user guide
- [ ] Update API documentation

**Deliverables**:
- E2E test suite (20+ scenarios)
- User guide PDF
- Updated documentation

---

## 🧪 Testing Strategy

### Unit Tests (Shared Library)

**Test Files**:
1. `test_file_detector.cpp`
   - PEM detection (header-based)
   - DER detection (ASN.1 tag)
   - DVL detection (PKCS#7)
   - Extension-based detection
   - Content-based fallback

2. `test_cert_type_detector.cpp`
   - CSCA detection (self-signed CA)
   - DSC detection (non-CA)
   - MLSC detection (EKU OID)
   - Link cert detection (non-self-signed CA)
   - Country extraction

3. `test_pem_parser.cpp`
   - Single certificate parsing
   - Multiple certificates parsing
   - Invalid PEM handling
   - Base64 decoding errors

4. `test_der_parser.cpp`
   - DER parsing
   - CER parsing (Windows convention)
   - BIN parsing
   - Invalid DER handling

5. `test_dvl_parser.cpp`
   - CMS SignedData parsing
   - Deviation entry extraction
   - Signature verification
   - Invalid DVL handling

6. `test_cert_validator.cpp`
   - Expiration checking
   - Signature verification
   - Trust chain validation
   - Invalid certificate handling

### Integration Tests

**Test Scenarios**:
1. Upload single PEM CSCA certificate (AUTO mode)
2. Upload single DER DSC certificate (MANUAL mode)
3. Upload CER certificate from BSI Germany (with source)
4. Upload DVL file and verify deviations
5. Multi-file upload (5 PEM + 3 DER + 1 DVL)
6. Duplicate certificate detection (same fingerprint)
7. Invalid certificate file (corrupted data)
8. Auto-detection: PEM without extension
9. Auto-detection: DER without extension
10. Batch upload with mixed success/failure

### Performance Tests

**Benchmarks**:
- Single certificate upload: < 2 seconds
- 10 certificate batch: < 10 seconds
- 100 certificate batch: < 60 seconds
- DVL parsing: < 3 seconds
- File format detection: < 100 ms
- Certificate type detection: < 50 ms

---

## 🚀 Success Criteria

- ✅ All 6 file types supported (PEM, DER, CER, BIN, DVL, ML)
- ✅ Shared library architecture implemented
- ✅ Multi-file upload working (tested with 10+ files)
- ✅ Source tracking functional (DB + LDAP)
- ✅ Auto-detection accuracy > 95%
- ✅ DVL parsing and verification working
- ✅ Zero regression in existing LDIF/ML functionality
- ✅ All unit tests passing (90%+ coverage)
- ✅ All integration tests passing
- ✅ Performance benchmarks met
- ✅ Complete API documentation
- ✅ User guide with examples

---

## 📚 References

### Standards
- [ICAO Doc 9303 Part 12 - PKI for MRTDs](https://store.icao.int/en/machine-readable-travel-documents-part-12-public-key-infrastructure-for-mrtds-doc-9303-12)
- [RFC 3852 - CMS (Cryptographic Message Syntax)](https://datatracker.ietf.org/doc/html/rfc3852)
- [RFC 5280 - X.509 Certificate and CRL Profile](https://datatracker.ietf.org/doc/html/rfc5280)

### Implementation Resources
- [OpenSSL X509 Certificate API](https://www.openssl.org/docs/man3.0/man3/X509.html)
- [OpenSSL CMS API](https://www.openssl.org/docs/man3.0/man3/CMS_verify.html)
- [CMake Export/Import Guide](https://cmake.org/cmake/help/latest/guide/importing-exporting/index.html)

### Internal Documentation
- [DVL_ANALYSIS.md](DVL_ANALYSIS.md) - Deviation List file analysis
- [CERTIFICATE_FILE_UPLOAD_DESIGN.md](CERTIFICATE_FILE_UPLOAD_DESIGN.md) - Previous design (V1)
- [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md) - Development workflow

---

**Design Version**: 2.0
**Last Updated**: 2026-02-04
**Status**: 📝 Ready for Review
**Estimated Effort**: 17 days (136 hours)
