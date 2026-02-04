# Certificate File Upload Feature - Design Document

**Feature Branch**: `feature/certificate-file-upload`
**Target Version**: v2.5.0
**Date**: 2026-02-04
**Status**: 🎯 Planning Phase

---

## 📋 Executive Summary

현재 시스템은 LDIF와 Master List 파일만 업로드 가능합니다. 본 기능은 개별 인증서 파일(PEM, DER, CER, BIN) 업로드를 지원하여 시스템 유연성을 향상시킵니다.

**Use Cases**:
- 단일 인증서 수동 등록
- 테스트용 인증서 업로드
- ICAO PKD에 없는 개별 인증서 추가
- 인증서 파일 형식 변환 없이 직접 업로드

---

## 🏗️ Current Architecture Analysis

### Backend Structure

**Current Endpoints**:
```
POST /api/upload/ldif        → processLdifFileAsync()
POST /api/upload/masterlist   → processMasterListFileAsync()
```

**Processing Flow**:
```
1. Multipart form parsing
2. File validation (size, extension)
3. File save to uploads/ directory
4. Database record creation (uploaded_file table)
5. Async processing (parse → validate → save to DB/LDAP)
6. SSE progress updates
```

**Key Components**:
- `ldif_processor.{h,cpp}` - LDIF 파일 파싱
- `masterlist_processor.{h,cpp}` - Master List 파싱
- `processing_strategy.{h,cpp}` - Strategy Pattern (AUTO/MANUAL modes)
- `certificate_utils.{h,cpp}` - X.509 인증서 유틸리티

**Database Schema**:
```sql
-- uploaded_file table
file_format VARCHAR(10) CHECK (file_format IN ('LDIF', 'ML', 'PEM', 'CER', 'DER', 'BIN'))
```
✅ Database schema already supports new file types!

### Frontend Structure

**Current UI**: `frontend/src/pages/FileUpload.tsx`
- Drag & drop file upload
- Processing mode selection (AUTO/MANUAL)
- Real-time progress tracking (SSE)
- 3-stage display (Upload → Parse → DB Save)

---

## 🎯 Design Proposal

### Option 3: Hybrid Approach (Recommended) ⭐

**Rationale**:
- Maintains backward compatibility (keep existing LDIF/ML endpoints)
- Adds unified certificate endpoint with auto-detection
- Provides flexibility for future file types

### Architecture Design

#### 1. New Backend Endpoint

```cpp
POST /api/upload/certificate
```

**Request**:
```
Content-Type: multipart/form-data
- file: Certificate file (PEM, DER, CER, BIN)
- mode: "AUTO" | "MANUAL" (optional, default: AUTO)
- certType: "CSCA" | "DSC" | "MLSC" (optional, auto-detect if not provided)
- country: ISO 3166-1 alpha-2 country code (optional)
```

**Response**:
```json
{
  "success": true,
  "uploadId": "uuid",
  "fileFormat": "PEM",
  "detectedCertType": "CSCA",
  "message": "Certificate file uploaded successfully"
}
```

#### 2. File Format Detection Logic

**New File**: `services/pkd-management/src/common/certificate_file_detector.h`

```cpp
class CertificateFileDetector {
public:
    // Detect file format from extension and content
    static std::string detectFileFormat(
        const std::string& filename,
        const std::vector<uint8_t>& content
    );

    // Detect certificate type from X.509 attributes
    static std::string detectCertificateType(X509* cert);

private:
    static bool isPEM(const std::vector<uint8_t>& content);
    static bool isDER(const std::vector<uint8_t>& content);
    static bool isBIN(const std::vector<uint8_t>& content);
};
```

**Detection Algorithm**:
1. **Extension-based**: `.pem`, `.crt`, `.cer`, `.der`, `.bin`
2. **Content-based**:
   - PEM: `-----BEGIN CERTIFICATE-----` header
   - DER: ASN.1 DER encoding (starts with `0x30` tag)
   - CER: Alias for DER (same detection)
   - BIN: Raw binary data

#### 3. Certificate File Parser

**New File**: `services/pkd-management/src/common/certificate_file_parser.h`

```cpp
class CertificateFileParser {
public:
    struct ParseResult {
        bool success;
        std::string certType;        // CSCA, DSC, MLSC
        std::string country;         // Extracted from DN
        X509* certificate;           // OpenSSL X509 structure
        std::string fingerprint;     // SHA-256 hex
        std::string errorMessage;
    };

    // Parse certificate from different formats
    static ParseResult parsePEM(const std::vector<uint8_t>& content);
    static ParseResult parseDER(const std::vector<uint8_t>& content);
    static ParseResult parseCER(const std::vector<uint8_t>& content);
    static ParseResult parseBIN(const std::vector<uint8_t>& content);

    // Unified entry point
    static ParseResult parse(
        const std::string& fileFormat,
        const std::vector<uint8_t>& content
    );

private:
    static X509* loadFromPEM(const std::vector<uint8_t>& content);
    static X509* loadFromDER(const std::vector<uint8_t>& content);
    static std::string extractCertType(X509* cert);
    static std::string extractCountry(X509* cert);
};
```

#### 4. Processing Strategy Extension

**Modify**: `services/pkd-management/src/processing_strategy.h`

```cpp
class ProcessingStrategy {
public:
    // ... existing methods ...

    // NEW: Process individual certificate file
    virtual void processCertificateFile(
        const std::string& uploadId,
        const std::string& fileFormat,
        const std::vector<uint8_t>& content,
        PGconn* conn,
        LDAP* ld
    ) = 0;
};
```

#### 5. Frontend UI Enhancement

**Modify**: `frontend/src/pages/FileUpload.tsx`

**Changes**:
1. Add file type selector (optional):
   ```tsx
   <select onChange={handleFileTypeChange}>
     <option value="auto">자동 감지</option>
     <option value="LDIF">LDIF 파일</option>
     <option value="ML">Master List</option>
     <option value="PEM">PEM 인증서</option>
     <option value="DER">DER 인증서</option>
     <option value="CER">CER 인증서</option>
     <option value="BIN">BIN 인증서</option>
   </select>
   ```

2. Smart endpoint selection:
   ```typescript
   const uploadEndpoint = () => {
     if (fileType === 'LDIF') return '/api/upload/ldif';
     if (fileType === 'ML') return '/api/upload/masterlist';
     return '/api/upload/certificate'; // PEM, DER, CER, BIN
   };
   ```

3. Add certificate type input (optional):
   ```tsx
   <select name="certType" optional>
     <option value="">자동 감지</option>
     <option value="CSCA">CSCA</option>
     <option value="DSC">DSC</option>
     <option value="MLSC">MLSC</option>
   </select>
   ```

---

## 📊 Implementation Plan

### Phase 1: Backend Foundation (Day 1-2)

**Tasks**:
- [ ] Create `certificate_file_detector.{h,cpp}` - File format detection
- [ ] Create `certificate_file_parser.{h,cpp}` - Certificate parsing
- [ ] Add unit tests for detector and parser
- [ ] Update CMakeLists.txt

**Estimated Time**: 8 hours

### Phase 2: Backend API Endpoint (Day 2-3)

**Tasks**:
- [ ] Implement `POST /api/upload/certificate` endpoint
- [ ] Extend `ProcessingStrategy` with `processCertificateFile()`
- [ ] Implement AUTO mode strategy
- [ ] Implement MANUAL mode strategy
- [ ] Add endpoint integration tests

**Estimated Time**: 10 hours

### Phase 3: Frontend UI (Day 3-4)

**Tasks**:
- [ ] Add file type selector to FileUpload.tsx
- [ ] Add certificate type input (optional)
- [ ] Update upload API service (pkdApi.ts)
- [ ] Add progress tracking for certificate upload
- [ ] Test with real certificate files

**Estimated Time**: 6 hours

### Phase 4: Integration Testing (Day 4-5)

**Tasks**:
- [ ] End-to-end testing with all file types (PEM, DER, CER, BIN)
- [ ] Test AUTO vs MANUAL mode
- [ ] Test certificate type detection
- [ ] Test LDAP storage
- [ ] Test error handling

**Estimated Time**: 6 hours

### Phase 5: Documentation & Review (Day 5)

**Tasks**:
- [ ] Update CLAUDE.md with v2.5.0 entry
- [ ] Create user guide for certificate file upload
- [ ] Update API documentation
- [ ] Code review and refactoring

**Estimated Time**: 4 hours

---

## 🔍 Technical Considerations

### 1. File Format Support

**PEM (Privacy-Enhanced Mail)**:
- Text format with Base64 encoding
- Header: `-----BEGIN CERTIFICATE-----`
- Footer: `-----END CERTIFICATE-----`
- OpenSSL function: `PEM_read_bio_X509()`

**DER (Distinguished Encoding Rules)**:
- Binary ASN.1 encoding
- No headers/footers
- Most compact format
- OpenSSL function: `d2i_X509_bio()`

**CER (Certificate)**:
- Windows convention for DER format
- Identical to DER (same parsing logic)

**BIN (Binary)**:
- Generic binary certificate
- Same as DER (fallback format)

### 2. Certificate Type Detection

**Auto-detection logic** (from X.509 attributes):
```cpp
std::string detectCertificateType(X509* cert) {
    // 1. Check Basic Constraints extension
    BASIC_CONSTRAINTS* bc = X509_get_ext_d2i(cert, NID_basic_constraints, NULL, NULL);
    if (bc && bc->ca) {
        // CA certificate

        // 2. Check Key Usage extension
        ASN1_BIT_STRING* keyUsage = X509_get_ext_d2i(cert, NID_key_usage, NULL, NULL);
        if (keyUsage) {
            bool hasKeyCertSign = ASN1_BIT_STRING_get_bit(keyUsage, 5);
            if (hasKeyCertSign) {
                // Check if self-signed → CSCA
                // Check if issuer != subject → Link Certificate (CSCA)
                if (isSelfSigned(cert)) {
                    return "CSCA";
                }
                // Check Extended Key Usage for MLSC
                // id-icao-mrtd-security-masterListSigner (OID: 2.23.136.1.1.9)
                if (hasMLSCExtendedKeyUsage(cert)) {
                    return "MLSC";
                }
                return "CSCA"; // Link certificate
            }
        }
    }

    // 3. Document Signer Certificate
    return "DSC";
}
```

### 3. Error Handling

**Validation Rules**:
- Maximum file size: 10 MB per certificate
- Supported formats: PEM, DER, CER, BIN only
- Certificate must be valid X.509 structure
- Subject DN must contain country (C=XX)

**Error Messages**:
- "Unsupported file format"
- "Invalid X.509 certificate structure"
- "Certificate missing country code in Subject DN"
- "Certificate type detection failed"
- "File exceeds maximum size limit (10 MB)"

### 4. Database Storage

**No schema changes required**:
- `uploaded_file.file_format` already supports new types
- `certificate` table unchanged
- `crl` table unchanged

**Processing**:
- Single certificate → Single `uploaded_file` record
- Certificate saved to `certificate` table
- LDAP DN: `cn={fingerprint},o={type},c={country},dc=data,...`

---

## 📝 API Specification

### POST /api/upload/certificate

**Description**: Upload individual certificate file (PEM, DER, CER, BIN)

**Request**:
```http
POST /api/upload/certificate HTTP/1.1
Content-Type: multipart/form-data

--boundary
Content-Disposition: form-data; name="file"; filename="csca.pem"
Content-Type: application/x-pem-file

-----BEGIN CERTIFICATE-----
MIIDXTCCAkWgAwIBAgIBAT...
-----END CERTIFICATE-----
--boundary
Content-Disposition: form-data; name="mode"

AUTO
--boundary
Content-Disposition: form-data; name="certType"

CSCA
--boundary--
```

**Response (Success)**:
```json
{
  "success": true,
  "uploadId": "123e4567-e89b-12d3-a456-426614174000",
  "fileFormat": "PEM",
  "detectedCertType": "CSCA",
  "country": "KR",
  "fingerprint": "a3b2c1d4e5f6...",
  "message": "Certificate uploaded and processed successfully"
}
```

**Response (Error)**:
```json
{
  "success": false,
  "message": "Invalid X.509 certificate structure",
  "details": "Failed to parse DER-encoded certificate: invalid ASN.1 tag"
}
```

**Status Codes**:
- `200 OK` - Success
- `400 Bad Request` - Invalid file or parameters
- `409 Conflict` - Duplicate certificate (same fingerprint)
- `413 Payload Too Large` - File exceeds size limit
- `415 Unsupported Media Type` - Invalid file format
- `500 Internal Server Error` - Processing error

---

## 🧪 Testing Strategy

### Unit Tests

**Test Files**:
1. `test_certificate_file_detector.cpp`
   - Test PEM detection
   - Test DER detection
   - Test CER/BIN detection
   - Test invalid files

2. `test_certificate_file_parser.cpp`
   - Test PEM parsing
   - Test DER parsing
   - Test certificate type detection
   - Test country extraction
   - Test error handling

### Integration Tests

**Test Scenarios**:
1. Upload valid PEM CSCA certificate (AUTO mode)
2. Upload valid DER DSC certificate (AUTO mode)
3. Upload CER certificate (MANUAL mode)
4. Upload BIN certificate with explicit certType
5. Upload duplicate certificate (expect 409)
6. Upload invalid certificate (expect 400)
7. Upload oversized file (expect 413)

**Test Data**:
- Sample certificates from ICAO PKD
- Self-signed test certificates
- Expired certificates
- Invalid certificates

---

## 🚀 Deployment Checklist

### Pre-deployment

- [ ] All unit tests passing
- [ ] All integration tests passing
- [ ] Code review completed
- [ ] Documentation updated
- [ ] Database migration tested

### Deployment Steps

1. **Database Migration**:
   ```bash
   # No migration needed - schema already supports new formats
   ```

2. **Backend Build**:
   ```bash
   cd docker
   docker-compose build --no-cache pkd-management
   docker-compose up -d pkd-management
   ```

3. **Frontend Build**:
   ```bash
   cd frontend
   npm run build
   docker-compose build --no-cache frontend
   docker-compose up -d frontend
   ```

4. **Verification**:
   ```bash
   # Test certificate upload
   curl -X POST http://localhost:8080/api/upload/certificate \
     -F "file=@test-csca.pem" \
     -F "mode=AUTO"

   # Check upload history
   curl http://localhost:8080/api/upload/history
   ```

---

## 📚 References

- [OpenSSL X509 Certificate API](https://www.openssl.org/docs/man3.0/man3/X509.html)
- [ICAO Doc 9303 Part 12 - PKI for Machine Readable Travel Documents](https://www.icao.int/publications/Documents/9303_p12_cons_en.pdf)
- [RFC 5280 - Internet X.509 Public Key Infrastructure Certificate](https://datatracker.ietf.org/doc/html/rfc5280)

---

## 🎯 Success Criteria

- ✅ All 4 file types (PEM, DER, CER, BIN) can be uploaded
- ✅ Certificate type auto-detection accuracy > 95%
- ✅ Upload processing time < 5 seconds per certificate
- ✅ LDAP storage success rate = 100%
- ✅ Zero regression in existing LDIF/ML upload functionality
- ✅ Complete API documentation
- ✅ User guide with examples

---

**Next Steps**: Review this design document and approve before implementation begins.
