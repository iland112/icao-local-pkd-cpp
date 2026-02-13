# PA Service Auto-Registration Design

**Feature**: Automatic DSC Registration from Passport Verification
**Integration**: PA Service → PKD Management
**Date**: 2026-02-04

---

## 🎯 Overview

During Passive Authentication (PA) verification, the system extracts DSC (Document Signer Certificate) from the passport's SOD (Security Object Document). If the DSC is not already registered in the database, it should be automatically registered to expand the certificate collection.

---

## 📊 Concept: Certificate Source vs File Source

### File Source (`uploaded_file.source_type`)
**Question**: Where did the **file** come from?
- ✅ BSI Germany website
- ✅ ICAO PKD portal
- ✅ Diplomatic channel
- ✅ Manual upload

**Applies to**: File-based uploads (LDIF, ML, PEM, DER, DVL files)

### Certificate Source (`certificate.source_type`)
**Question**: How did the **certificate** enter the system?
- ✅ Parsed from LDIF file
- ✅ Extracted from Master List
- ✅ **Extracted from passport during PA verification** ⭐
- ✅ Direct API registration
- ✅ System generated (testing)

**Applies to**: All certificates, regardless of entry method

---

## 🗄️ Database Schema

### Certificate Table Extension

```sql
-- Add source tracking columns to certificate table
ALTER TABLE certificate
ADD COLUMN source_type VARCHAR(50) DEFAULT 'FILE_UPLOAD',
ADD COLUMN source_context JSONB,
ADD COLUMN extracted_from VARCHAR(100),
ADD COLUMN registered_at TIMESTAMP DEFAULT NOW();

-- Add constraint for certificate source types
ALTER TABLE certificate
ADD CONSTRAINT chk_cert_source_type CHECK (
    source_type IN (
        'FILE_UPLOAD',       -- Direct file upload (PEM, DER, etc.)
        'PA_EXTRACTED',      -- Extracted from passport during PA verification
        'LDIF_PARSED',       -- Parsed from LDIF file
        'ML_PARSED',         -- Parsed from Master List
        'DVL_PARSED',        -- Parsed from Deviation List
        'API_REGISTERED',    -- Direct API call
        'SYSTEM_GENERATED'   -- System generated (testing)
    )
);

-- Create index for source-based queries
CREATE INDEX idx_certificate_source ON certificate(source_type);
CREATE INDEX idx_certificate_extracted_from ON certificate(extracted_from);
```

### Example Data

**PA-Extracted DSC**:
```sql
INSERT INTO certificate (
    id,
    fingerprint_sha256,
    certificate_type,
    country_code,
    subject_dn,
    issuer_dn,
    source_type,
    source_context,
    extracted_from,
    userCertificate
) VALUES (
    gen_random_uuid(),
    'a1b2c3d4e5f6...',
    'DSC',
    'KR',
    'C=KR,O=MOFA,OU=Consular,CN=Document Signer',
    'C=KR,O=MOFA,CN=CSCA-KOREA',
    'PA_EXTRACTED',
    '{
        "verification_id": "pa-uuid-123",
        "passport_mrz": "P<KORKIM<<GILDONG<<...",
        "document_number": "M12345678",
        "extraction_timestamp": "2026-02-04T10:30:00Z",
        "extraction_country": "KR"
    }'::jsonb,
    'pa-uuid-123',
    decode('308201...', 'hex')  -- DER-encoded certificate
);
```

**LDIF-Parsed DSC** (for comparison):
```sql
INSERT INTO certificate (
    fingerprint_sha256,
    certificate_type,
    source_type,
    extracted_from
) VALUES (
    'f7e8d9c0...',
    'DSC',
    'LDIF_PARSED',
    'upload-uuid-456'  -- References uploaded_file.id
);
```

---

## 🔄 Workflow

### Complete PA Auto-Registration Flow

```
┌─────────────────────────────────────────────────────────────────┐
│ 1. User uploads passport data to PA Service                     │
│    POST /api/pa/verify                                           │
│    - Files: SOD, DG1, DG2, DG14, etc.                           │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 2. PA Service: Parse SOD and Extract DSC                        │
│    - OpenSSL CMS_verify()                                        │
│    - Extract X509 certificate from SignerInfo                    │
│    - Calculate fingerprint: SHA-256(DER-encoded cert)            │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 3. Check if DSC already exists                                  │
│    SELECT id FROM certificate                                    │
│    WHERE fingerprint_sha256 = ?                                 │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ├─── YES (EXISTS) ──→ Skip registration, continue PA
                 │
                 └─── NO (NOT FOUND)
                      │
                      ▼
┌─────────────────────────────────────────────────────────────────┐
│ 4. Auto-register DSC to PKD Management                          │
│    HTTP Request:                                                 │
│    POST http://pkd-management:8081/api/upload/certificate/from-pa│
│    Authorization: Bearer {INTERNAL_SERVICE_SECRET}               │
│    Body: {                                                       │
│      "certificate": "base64-encoded-DER",                        │
│      "verificationId": "pa-uuid-123",                            │
│      "sourceContext": {                                          │
│        "mrzLine1": "P<KORKIM<<GILDONG<<...",                    │
│        "mrzLine2": "M123456789KOR...",                          │
│        "country": "KR",                                          │
│        "documentNumber": "M12345678"                             │
│      }                                                           │
│    }                                                             │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 5. PKD Management: Verify Internal Auth                         │
│    - Check Authorization header                                  │
│    - Validate INTERNAL_SERVICE_SECRET                            │
│    - Reject if invalid (401 Unauthorized)                        │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 6. PKD Management: Parse and Validate Certificate               │
│    using icao::certificate_parser library:                       │
│    - Base64 decode certificate                                   │
│    - DerParser::parse(certDer)                                   │
│    - CertTypeDetector::detectType(cert)                          │
│    - CertValidator::validate(cert)                               │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 7. Double-check for duplicates (race condition prevention)      │
│    SELECT id FROM certificate                                    │
│    WHERE fingerprint_sha256 = ? FOR UPDATE                      │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ├─── FOUND (race condition) ──→ Return existing ID
                 │
                 └─── NOT FOUND
                      │
                      ▼
┌─────────────────────────────────────────────────────────────────┐
│ 8. Save Certificate to Database                                 │
│    INSERT INTO certificate (                                     │
│        fingerprint_sha256,                                       │
│        certificate_type,                                         │
│        source_type,              -- 'PA_EXTRACTED'              │
│        source_context,           -- JSON with MRZ, etc.         │
│        extracted_from,           -- verificationId              │
│        ...                                                       │
│    )                                                            │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 9. Save Certificate to LDAP                                     │
│    dn: cn={fingerprint},o=dsc,c={country},dc=data,...          │
│    objectClass: pkdCertificate                                   │
│    userCertificate;binary: {DER}                                │
│    certificateSource: PA_EXTRACTED                               │
│    sourceContext: {JSON}                                         │
│    extractedFrom: {verificationId}                               │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 10. Return Success Response to PA Service                       │
│     Response: {                                                  │
│       "success": true,                                           │
│       "registered": true,                                        │
│       "fingerprint": "a1b2c3...",                                │
│       "certificateType": "DSC",                                  │
│       "country": "KR"                                            │
│     }                                                            │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 11. PA Service: Log registration and continue verification      │
│     spdlog::info("DSC auto-registered: {}", fingerprint);       │
│     Continue with trust chain validation...                      │
└─────────────────────────────────────────────────────────────────┘
```

---

## 🌐 API Specification

### Internal Endpoint: Certificate from PA

**Endpoint**: `POST /api/upload/certificate/from-pa`
**Access**: Internal service only (requires INTERNAL_SERVICE_SECRET)
**Purpose**: Register DSC extracted during PA verification

#### Request

```http
POST /api/upload/certificate/from-pa HTTP/1.1
Host: pkd-management:8081
Content-Type: application/json
Authorization: Bearer ${INTERNAL_SERVICE_SECRET}

{
  "certificate": "MIIEFjCCA36gAwIBAgICAT...",  // Base64-encoded DER
  "verificationId": "pa-verification-uuid-123",
  "sourceContext": {
    "mrzLine1": "P<KORKIM<<GILDONG<<<<<<<<<<<<<<<<<<<<<<<<",
    "mrzLine2": "M123456789KOR8901011M2501011234567890123<<",
    "country": "KR",
    "documentNumber": "M12345678",
    "extractionTimestamp": "2026-02-04T10:30:00Z"
  }
}
```

#### Response (Success - New Registration)

```json
{
  "success": true,
  "registered": true,
  "fingerprint": "a1b2c3d4e5f6789...",
  "certificateType": "DSC",
  "country": "KR",
  "subjectDN": "C=KR,O=MOFA,OU=Consular,CN=Document Signer",
  "issuerDN": "C=KR,O=MOFA,CN=CSCA-KOREA",
  "validFrom": "2020-01-01T00:00:00Z",
  "validUntil": "2025-12-31T23:59:59Z",
  "message": "DSC automatically registered from PA verification"
}
```

#### Response (Already Exists)

```json
{
  "success": true,
  "registered": false,
  "fingerprint": "a1b2c3d4e5f6789...",
  "existingId": "cert-uuid-456",
  "certificateType": "DSC",
  "country": "KR",
  "message": "Certificate already exists in database",
  "existingSource": "LDIF_PARSED"
}
```

#### Response (Error - Invalid Certificate)

```json
{
  "success": false,
  "error": "INVALID_CERTIFICATE",
  "message": "Failed to parse certificate: invalid DER encoding",
  "details": "asn1 encoding routines:ASN1_get_object:too long"
}
```

#### Response (Error - Unauthorized)

```json
{
  "success": false,
  "error": "UNAUTHORIZED",
  "message": "Invalid or missing internal service token"
}
```

---

## 🔒 Security Design

### Internal Service Authentication

**Mechanism**: Shared secret token
**Storage**: Environment variable
**Transmission**: HTTP Authorization header

#### Docker Compose Configuration

```yaml
# docker/docker-compose.yaml
services:
  pa-service:
    environment:
      - INTERNAL_SERVICE_SECRET=${INTERNAL_SERVICE_SECRET}
      - PKD_MANAGEMENT_URL=http://pkd-management:8081

  pkd-management:
    environment:
      - INTERNAL_SERVICE_SECRET=${INTERNAL_SERVICE_SECRET}
```

#### .env File

```bash
# Internal service authentication
INTERNAL_SERVICE_SECRET=your-secure-random-secret-here-min-32-chars
```

**Generate secure secret**:
```bash
openssl rand -hex 32
```

#### Backend Implementation

**PA Service** (`services/pa-service/src/main.cpp`):
```cpp
// Get configuration
const std::string PKD_MANAGEMENT_URL = getenv("PKD_MANAGEMENT_URL")
    ?: "http://pkd-management:8081";
const std::string INTERNAL_SECRET = getenv("INTERNAL_SERVICE_SECRET")
    ?: throw std::runtime_error("INTERNAL_SERVICE_SECRET not set");

// Make authenticated request
Json::Value registerDscInPkd(X509* dsc, const std::string& verificationId,
                              const Json::Value& sourceContext) {
    HttpClient client(PKD_MANAGEMENT_URL);

    Json::Value request;
    request["certificate"] = base64Encode(dsc);
    request["verificationId"] = verificationId;
    request["sourceContext"] = sourceContext;

    HttpRequest req;
    req.setMethod(Post);
    req.setPath("/api/upload/certificate/from-pa");
    req.addHeader("Authorization", "Bearer " + INTERNAL_SECRET);
    req.addHeader("Content-Type", "application/json");
    req.setBody(Json::writeString(builder, request));

    auto resp = client.sendRequest(req);
    return *resp->getJsonObject();
}
```

**PKD Management** (`services/pkd-management/src/main.cpp`):
```cpp
// Authentication middleware
bool verifyInternalToken(const HttpRequestPtr& req) {
    const std::string EXPECTED_SECRET = getenv("INTERNAL_SERVICE_SECRET");
    if (EXPECTED_SECRET.empty()) {
        spdlog::error("INTERNAL_SERVICE_SECRET not configured");
        return false;
    }

    auto authHeader = req->getHeader("Authorization");
    if (authHeader.empty()) {
        return false;
    }

    // Remove "Bearer " prefix
    std::string token = authHeader;
    if (token.substr(0, 7) == "Bearer ") {
        token = token.substr(7);
    }

    return token == EXPECTED_SECRET;
}

// Endpoint handler
app().registerHandler(
    "/api/upload/certificate/from-pa",
    [](const HttpRequestPtr& req, Callback&& callback) {
        // 1. Verify internal authentication
        if (!verifyInternalToken(req)) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "UNAUTHORIZED";
            error["message"] = "Invalid or missing internal service token";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
            return;
        }

        // 2. Parse request
        auto json = req->getJsonObject();
        std::string certBase64 = (*json)["certificate"].asString();
        std::string verificationId = (*json)["verificationId"].asString();
        auto sourceContext = (*json)["sourceContext"];

        // 3. Decode certificate
        auto certDer = base64Decode(certBase64);

        // 4. Parse using shared library
        using namespace icao::certificate_parser;
        auto parseResult = DerParser::parse(certDer);

        if (!parseResult.success) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "INVALID_CERTIFICATE";
            error["message"] = parseResult.error_message;

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        // 5. Check for duplicates (with lock)
        auto fingerprint = parseResult.cert_info.fingerprint;
        if (certificateExists(fingerprint)) {
            Json::Value response;
            response["success"] = true;
            response["registered"] = false;
            response["fingerprint"] = fingerprint;
            response["message"] = "Certificate already exists";

            callback(HttpResponse::newHttpJsonResponse(response));
            X509_free(parseResult.certificate);
            return;
        }

        // 6. Save to database
        saveCertificateWithSource(
            parseResult.certificate,
            "PA_EXTRACTED",
            sourceContext,
            verificationId
        );

        // 7. Save to LDAP
        saveCertificateToLdap(parseResult.certificate, ...);

        // 8. Return success
        Json::Value response;
        response["success"] = true;
        response["registered"] = true;
        response["fingerprint"] = fingerprint;
        response["certificateType"] = parseResult.cert_info.type;
        response["country"] = parseResult.cert_info.country;

        callback(HttpResponse::newHttpJsonResponse(response));
        X509_free(parseResult.certificate);
    },
    {Post}
);
```

---

## 📊 Statistics & Monitoring

### Certificate Source Distribution

```sql
-- Query certificate counts by source
SELECT
    source_type,
    certificate_type,
    COUNT(*) as count
FROM certificate
GROUP BY source_type, certificate_type
ORDER BY source_type, certificate_type;
```

**Expected Output**:
```
source_type      | certificate_type | count
-----------------+------------------+-------
FILE_UPLOAD      | CSCA             | 123
FILE_UPLOAD      | DSC              | 456
LDIF_PARSED      | CSCA             | 789
LDIF_PARSED      | DSC              | 12345
LDIF_PARSED      | DSC_NC           | 234
ML_PARSED        | CSCA             | 456
ML_PARSED        | MLSC             | 12
PA_EXTRACTED     | DSC              | 678   ← Auto-registered from passports
DVL_PARSED       | DVL_SIGNER       | 5
```

### PA Auto-Registration Metrics

```sql
-- PA-extracted certificates by country
SELECT
    country_code,
    COUNT(*) as pa_extracted_count,
    MIN(registered_at) as first_extraction,
    MAX(registered_at) as latest_extraction
FROM certificate
WHERE source_type = 'PA_EXTRACTED'
GROUP BY country_code
ORDER BY pa_extracted_count DESC;
```

### Audit Log Integration

```sql
-- Log PA auto-registration events
INSERT INTO operation_audit_log (
    operation_type,
    sub_type,
    resource_type,
    resource_id,
    username,
    ip_address,
    success,
    duration_ms,
    metadata
) VALUES (
    'CERTIFICATE_REGISTRATION',
    'PA_AUTO_EXTRACTED',
    'CERTIFICATE',
    '${certificateId}',
    'pa-service',
    '${requestIp}',
    true,
    ${durationMs},
    '{
        "verification_id": "${verificationId}",
        "fingerprint": "${fingerprint}",
        "country": "${country}",
        "mrz": "${mrzLine1}"
    }'::jsonb
);
```

---

## 🧪 Testing Strategy

### Unit Tests

**PA Service**:
```cpp
TEST(PaServiceTest, ExtractDscFromSod) {
    // 1. Load test SOD
    auto sodData = loadTestFile("test_sod_korea.bin");

    // 2. Extract DSC
    X509* dsc = extractDscFromSod(sodData);
    ASSERT_NE(dsc, nullptr);

    // 3. Verify certificate properties
    auto fingerprint = calculateFingerprint(dsc);
    EXPECT_EQ(fingerprint.length(), 64);  // SHA-256 hex

    X509_free(dsc);
}

TEST(PaServiceTest, RegisterDscInPkd_NewCertificate) {
    // Mock HTTP client response
    mockPkdManagementResponse({
        {"success", true},
        {"registered", true},
        {"fingerprint", "abc123..."}
    });

    auto result = registerDscInPkd(testDsc, "test-uuid", testContext);

    EXPECT_TRUE(result["success"].asBool());
    EXPECT_TRUE(result["registered"].asBool());
}

TEST(PaServiceTest, RegisterDscInPkd_AlreadyExists) {
    mockPkdManagementResponse({
        {"success", true},
        {"registered", false},
        {"message", "Certificate already exists"}
    });

    auto result = registerDscInPkd(testDsc, "test-uuid", testContext);

    EXPECT_TRUE(result["success"].asBool());
    EXPECT_FALSE(result["registered"].asBool());
}
```

**PKD Management**:
```cpp
TEST(PkdManagementTest, RegisterFromPa_ValidRequest) {
    // 1. Prepare request
    Json::Value request;
    request["certificate"] = base64Encode(testDscDer);
    request["verificationId"] = "test-uuid";
    request["sourceContext"]["country"] = "KR";

    // 2. Make request with valid token
    auto resp = makeInternalRequest("/api/upload/certificate/from-pa",
                                     request, VALID_SECRET);

    // 3. Verify response
    EXPECT_EQ(resp->statusCode(), k200OK);
    auto json = resp->getJsonObject();
    EXPECT_TRUE((*json)["success"].asBool());
    EXPECT_TRUE((*json)["registered"].asBool());
}

TEST(PkdManagementTest, RegisterFromPa_Unauthorized) {
    Json::Value request;
    request["certificate"] = base64Encode(testDscDer);

    auto resp = makeInternalRequest("/api/upload/certificate/from-pa",
                                     request, "wrong-secret");

    EXPECT_EQ(resp->statusCode(), k401Unauthorized);
}

TEST(PkdManagementTest, RegisterFromPa_DuplicateCertificate) {
    // Insert existing certificate
    insertTestCertificate(testFingerprint, "LDIF_PARSED");

    Json::Value request;
    request["certificate"] = base64Encode(testDscDer);

    auto resp = makeInternalRequest("/api/upload/certificate/from-pa", request);

    auto json = resp->getJsonObject();
    EXPECT_TRUE((*json)["success"].asBool());
    EXPECT_FALSE((*json)["registered"].asBool());
}
```

### Integration Tests

**End-to-End PA Auto-Registration**:
```bash
#!/bin/bash
# Test PA auto-registration flow

# 1. Start services
docker-compose up -d pa-service pkd-management

# 2. Verify database is empty
CERT_COUNT=$(psql -h localhost -U pkd -d localpkd -t -c \
  "SELECT COUNT(*) FROM certificate WHERE source_type='PA_EXTRACTED'")
echo "Initial PA_EXTRACTED count: $CERT_COUNT"

# 3. Perform PA verification with new passport
curl -X POST http://localhost:8080/api/pa/verify \
  -F "sod=@test_data/korea_passport_sod.bin" \
  -F "dg1=@test_data/korea_passport_dg1.bin" \
  -F "dg2=@test_data/korea_passport_dg2.bin"

# 4. Verify certificate was auto-registered
sleep 2
NEW_COUNT=$(psql -h localhost -U pkd -d localpkd -t -c \
  "SELECT COUNT(*) FROM certificate WHERE source_type='PA_EXTRACTED'")
echo "After PA: $NEW_COUNT"

if [ "$NEW_COUNT" -gt "$CERT_COUNT" ]; then
  echo "✅ Test PASSED: DSC auto-registered"
else
  echo "❌ Test FAILED: DSC not registered"
  exit 1
fi

# 5. Verify LDAP storage
ldapsearch -x -H ldap://localhost:3891 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w ldap_test_password_123 \
  -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(certificateSource=PA_EXTRACTED)" | grep "dn:"

echo "✅ Integration test complete"
```

---

## 📈 Performance Considerations

### Expected Load

**Assumption**: 1000 PA verifications per day

**Breakdown**:
- New DSCs: ~5% (50 per day) → Auto-registration triggered
- Known DSCs: ~95% (950 per day) → Skip registration, fast path

### Optimization Strategies

1. **In-Memory Fingerprint Cache** (PA Service):
   ```cpp
   // Cache recently checked fingerprints (TTL: 1 hour)
   std::unordered_map<std::string, bool> fingerprintCache;

   bool isDscKnown(const std::string& fingerprint) {
       // Check cache first
       if (fingerprintCache.count(fingerprint)) {
           return fingerprintCache[fingerprint];
       }

       // Query database
       bool exists = checkDatabaseForFingerprint(fingerprint);
       fingerprintCache[fingerprint] = exists;

       return exists;
   }
   ```

2. **Async Registration** (Non-blocking):
   ```cpp
   // Don't block PA verification for registration
   std::thread([dsc, verificationId, sourceContext]() {
       try {
           registerDscInPkd(dsc, verificationId, sourceContext);
       } catch (const std::exception& e) {
           spdlog::error("Async DSC registration failed: {}", e.what());
       }
   }).detach();

   // Continue PA verification immediately
   return continuePaVerification(dsc, ...);
   ```

3. **Database Connection Pool**:
   - PA Service already uses connection pool
   - PKD Management uses connection pool
   - No additional queries needed

### Monitoring Metrics

**Prometheus Metrics**:
```cpp
// PA Service
prometheus::Counter pa_dsc_extractions("pa_dsc_extractions_total");
prometheus::Counter pa_dsc_registrations("pa_dsc_auto_registrations_total");
prometheus::Counter pa_dsc_already_known("pa_dsc_already_known_total");
prometheus::Histogram pa_registration_duration("pa_dsc_registration_duration_ms");

// PKD Management
prometheus::Counter internal_api_requests("internal_api_requests_total", {"endpoint"});
prometheus::Counter internal_api_auth_failures("internal_api_auth_failures_total");
```

---

## 🎯 Success Criteria

- ✅ PA-extracted DSCs automatically registered to database
- ✅ LDAP synchronization working for PA-extracted certificates
- ✅ No performance degradation in PA verification (< 5% overhead)
- ✅ Internal authentication secure and working
- ✅ Race condition handling (duplicate prevention)
- ✅ Source tracking accurate (certificate.source_type = 'PA_EXTRACTED')
- ✅ Audit logging for all auto-registrations
- ✅ Unit tests passing (100% coverage for new code)
- ✅ Integration tests passing (E2E flow)

---

**Document Version**: 1.0
**Last Updated**: 2026-02-04
**Status**: ✅ Design Complete
**Ready for Implementation**: YES
