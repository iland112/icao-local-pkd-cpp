# Sprint 2: Link Certificate Validation Core - Planning Document

**Version**: 1.0.0
**Created**: 2026-01-24
**Sprint Duration**: 2 weeks
**Priority**: HIGH
**Dependencies**: Sprint 1 (LDAP DN Migration) Complete ✅

---

## Executive Summary

Sprint 2 focuses on implementing the **Link Certificate (LC) validation core** for ICAO eMRTD security. This sprint builds upon Sprint 1's LDAP DN infrastructure to establish a comprehensive certificate chain validation system.

**Goal**: Validate Link Certificates against CSCA/DSC trust chains with CRL/OCSP revocation checking.

---

## Background

### What are Link Certificates?

Link Certificates (LCs) are X.509 certificates used in **ICAO Doc 9303 Part 12 (Active Authentication with Chip Authentication)**:

- **Purpose**: Bridge CSCA key changes during transition periods
- **Trust Chain**: CSCA (old) → LC → CSCA (new)
- **Validity**: Typically 1-2 years during migration
- **RFC 5280**: Subject ≠ Issuer (intermediate CA)

### Current System Limitations

| Area | Current State | Sprint 2 Target |
|------|--------------|-----------------|
| **LC Storage** | ❌ No LC table | ✅ Dedicated LC table with metadata |
| **LC Validation** | ❌ Not implemented | ✅ Full trust chain + revocation |
| **CRL Checking** | ⚠️ Stored but not used | ✅ Active CRL validation |
| **OCSP Support** | ❌ Not implemented | 🔄 Phase 2 (optional) |
| **Certificate Search** | ✅ CSCA/DSC only | ✅ LC included |
| **LDAP DIT** | ❌ No LC branch | ✅ `o=lc` branch added |

---

## Sprint 2 Objectives

### Primary Goals

1. **Database Schema Enhancement**
   - Create `link_certificate` table
   - Add LC-specific metadata (old/new CSCA references)
   - Track LC lifecycle (issuing, transition, expiry)

2. **LC Validation Engine**
   - Verify LC signature by old CSCA
   - Verify new CSCA signature by LC
   - Check LC validity period
   - Validate certificate extensions (BasicConstraints, KeyUsage)

3. **CRL Revocation Checking**
   - Implement `checkCrlRevocation()` function
   - Parse CRL binary (X509_CRL_new)
   - Match certificate serial numbers
   - Return revocation status + reason

4. **LDAP Integration**
   - Add `o=lc` branch to LDAP DIT
   - Store LC in LDAP with fingerprint-based DN
   - Update `saveCertificateToLdap()` for LC type

5. **API Endpoints**
   - `POST /api/validate/link-cert` - LC validation endpoint
   - `GET /api/link-certs/search` - LC search API
   - `GET /api/link-certs/{id}` - LC detail API

### Secondary Goals (Phase 2)

- OCSP revocation checking (if time permits)
- LC upload via Master List parsing
- Frontend LC dashboard widget

---

## Technical Design

### 1. Database Schema

#### New Table: `link_certificate`

```sql
CREATE TABLE IF NOT EXISTS link_certificate (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    upload_id UUID REFERENCES uploaded_file(id) ON DELETE CASCADE,

    -- Certificate Identity
    subject_dn VARCHAR(512) NOT NULL,
    issuer_dn VARCHAR(512) NOT NULL,
    serial_number VARCHAR(128) NOT NULL,
    fingerprint_sha256 VARCHAR(64) UNIQUE NOT NULL,

    -- Certificate Metadata
    not_before TIMESTAMP NOT NULL,
    not_after TIMESTAMP NOT NULL,
    country_code VARCHAR(3),

    -- Link Certificate Specifics
    old_csca_subject_dn VARCHAR(512),  -- CSCA being phased out
    old_csca_fingerprint VARCHAR(64),
    new_csca_subject_dn VARCHAR(512),  -- New CSCA being introduced
    new_csca_fingerprint VARCHAR(64),

    -- Validation Results
    trust_chain_valid BOOLEAN DEFAULT false,
    old_csca_signature_valid BOOLEAN DEFAULT false,
    new_csca_signature_valid BOOLEAN DEFAULT false,
    revocation_status VARCHAR(20) DEFAULT 'UNKNOWN',  -- GOOD, REVOKED, UNKNOWN

    -- Binary Data
    certificate_binary BYTEA NOT NULL,

    -- LDAP Synchronization
    ldap_dn_v2 VARCHAR(512),  -- Fingerprint-based DN
    stored_in_ldap BOOLEAN DEFAULT false,
    ldap_stored_at TIMESTAMP,

    -- Audit
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT uk_link_cert_fingerprint UNIQUE (fingerprint_sha256)
);

-- Indexes
CREATE INDEX idx_link_cert_country ON link_certificate(country_code);
CREATE INDEX idx_link_cert_old_csca ON link_certificate(old_csca_fingerprint);
CREATE INDEX idx_link_cert_new_csca ON link_certificate(new_csca_fingerprint);
CREATE INDEX idx_link_cert_validity ON link_certificate(not_before, not_after);
CREATE INDEX idx_link_cert_ldap_dn ON link_certificate(ldap_dn_v2) WHERE ldap_dn_v2 IS NOT NULL;
CREATE INDEX idx_link_cert_stored ON link_certificate(stored_in_ldap);
```

#### CRL Revocation Log Table

```sql
CREATE TABLE IF NOT EXISTS crl_revocation_log (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    certificate_id UUID,  -- References any certificate table (CSCA/DSC/LC)
    certificate_type VARCHAR(10) NOT NULL,  -- CSCA, DSC, LC
    serial_number VARCHAR(128) NOT NULL,
    fingerprint_sha256 VARCHAR(64) NOT NULL,

    -- Revocation Details
    revocation_status VARCHAR(20) NOT NULL,  -- REVOKED, GOOD, UNKNOWN
    revocation_reason VARCHAR(50),  -- keyCompromise, cACompromise, ...
    revocation_date TIMESTAMP,

    -- CRL Source
    crl_id UUID REFERENCES crl(id),
    crl_issuer_dn VARCHAR(512),
    crl_this_update TIMESTAMP,
    crl_next_update TIMESTAMP,

    -- Audit
    checked_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT uk_revocation_log UNIQUE (certificate_id, checked_at)
);

CREATE INDEX idx_revocation_cert_id ON crl_revocation_log(certificate_id);
CREATE INDEX idx_revocation_fingerprint ON crl_revocation_log(fingerprint_sha256);
CREATE INDEX idx_revocation_status ON crl_revocation_log(revocation_status);
```

---

### 2. LC Validation Logic

#### Trust Chain Validation

```cpp
struct LinkCertValidationResult {
    bool trustChainValid;
    bool oldCscaSignatureValid;
    bool newCscaSignatureValid;
    bool validityPeriodValid;
    bool extensionsValid;
    std::string revocationStatus;  // GOOD, REVOKED, UNKNOWN
    std::string validationMessage;
};

LinkCertValidationResult validateLinkCertificate(
    X509* linkCert,
    PGconn* conn
) {
    LinkCertValidationResult result;

    // Step 1: Extract LC metadata
    std::string issuerDn = extractSubjectDn(X509_get_issuer_name(linkCert));
    std::string subjectDn = extractSubjectDn(X509_get_subject_name(linkCert));
    std::string serialNumber = getSerialNumber(linkCert);

    // Step 2: Find old CSCA (issuer of LC)
    X509* oldCsca = findCscaBySubjectDn(conn, issuerDn);
    if (!oldCsca) {
        result.trustChainValid = false;
        result.validationMessage = "Old CSCA not found";
        return result;
    }

    // Step 3: Verify LC signature by old CSCA
    EVP_PKEY* oldCscaPubKey = X509_get_pubkey(oldCsca);
    result.oldCscaSignatureValid = (X509_verify(linkCert, oldCscaPubKey) == 1);
    EVP_PKEY_free(oldCscaPubKey);

    // Step 4: Find new CSCA (certificate signed by LC)
    // Note: This requires forward lookup - find CSCA where issuer DN matches LC subject DN
    X509* newCsca = findCscaByIssuerDn(conn, subjectDn);
    if (!newCsca) {
        result.trustChainValid = false;
        result.validationMessage = "New CSCA not found";
        X509_free(oldCsca);
        return result;
    }

    // Step 5: Verify new CSCA signature by LC
    EVP_PKEY* linkCertPubKey = X509_get_pubkey(linkCert);
    result.newCscaSignatureValid = (X509_verify(newCsca, linkCertPubKey) == 1);
    EVP_PKEY_free(linkCertPubKey);

    // Step 6: Check validity period
    result.validityPeriodValid = checkValidityPeriod(linkCert);

    // Step 7: Validate certificate extensions
    result.extensionsValid = validateLcExtensions(linkCert);

    // Step 8: Check CRL revocation
    result.revocationStatus = checkCrlRevocation(conn, serialNumber, issuerDn);

    // Final result
    result.trustChainValid =
        result.oldCscaSignatureValid &&
        result.newCscaSignatureValid &&
        result.validityPeriodValid &&
        result.extensionsValid &&
        (result.revocationStatus != "REVOKED");

    if (result.trustChainValid) {
        result.validationMessage = "LC trust chain valid";
    } else {
        result.validationMessage = "LC validation failed";
    }

    X509_free(oldCsca);
    X509_free(newCsca);

    return result;
}
```

#### CRL Revocation Checking

```cpp
std::string checkCrlRevocation(
    PGconn* conn,
    const std::string& serialNumber,
    const std::string& issuerDn
) {
    // Step 1: Find latest CRL for issuer
    const char* query =
        "SELECT crl_binary, this_update, next_update "
        "FROM crl "
        "WHERE issuer_dn = $1 "
        "ORDER BY this_update DESC "
        "LIMIT 1";

    const char* paramValues[1] = {issuerDn.c_str()};
    PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        spdlog::warn("No CRL found for issuer: {}", issuerDn);
        return "UNKNOWN";
    }

    // Step 2: Parse CRL binary
    size_t crlLen;
    unsigned char* crlData = PQunescapeBytea(
        reinterpret_cast<const unsigned char*>(PQgetvalue(res, 0, 0)),
        &crlLen
    );

    const unsigned char* p = crlData;
    X509_CRL* crl = d2i_X509_CRL(nullptr, &p, crlLen);
    PQfreemem(crlData);
    PQclear(res);

    if (!crl) {
        spdlog::error("Failed to parse CRL binary");
        return "UNKNOWN";
    }

    // Step 3: Check if certificate serial is in revoked list
    STACK_OF(X509_REVOKED)* revokedList = X509_CRL_get_REVOKED(crl);
    if (!revokedList || sk_X509_REVOKED_num(revokedList) == 0) {
        X509_CRL_free(crl);
        return "GOOD";  // CRL exists but no revoked certs
    }

    // Convert serial number hex string to BIGNUM
    BIGNUM* serialBn = nullptr;
    BN_hex2bn(&serialBn, serialNumber.c_str());
    ASN1_INTEGER* serialAsn1 = BN_to_ASN1_INTEGER(serialBn, nullptr);
    BN_free(serialBn);

    // Step 4: Search for serial in revoked list
    for (int i = 0; i < sk_X509_REVOKED_num(revokedList); i++) {
        X509_REVOKED* revoked = sk_X509_REVOKED_value(revokedList, i);
        const ASN1_INTEGER* revokedSerial = X509_REVOKED_get0_serialNumber(revoked);

        if (ASN1_INTEGER_cmp(serialAsn1, revokedSerial) == 0) {
            // Certificate is revoked!
            const ASN1_TIME* revocationDate = X509_REVOKED_get0_revocationDate(revoked);

            // Get revocation reason (optional extension)
            int reason = -1;
            int criticalFlag;
            ASN1_ENUMERATED* reasonExt = static_cast<ASN1_ENUMERATED*>(
                X509_REVOKED_get_ext_d2i(revoked, NID_crl_reason, &criticalFlag, nullptr)
            );
            if (reasonExt) {
                reason = ASN1_ENUMERATED_get(reasonExt);
                ASN1_ENUMERATED_free(reasonExt);
            }

            spdlog::warn("Certificate REVOKED - Serial: {}, Reason: {}",
                         serialNumber, getCrlReasonString(reason));

            ASN1_INTEGER_free(serialAsn1);
            X509_CRL_free(crl);
            return "REVOKED";
        }
    }

    // Not found in revoked list = GOOD
    ASN1_INTEGER_free(serialAsn1);
    X509_CRL_free(crl);
    return "GOOD";
}

std::string getCrlReasonString(int reason) {
    switch (reason) {
        case 0: return "unspecified";
        case 1: return "keyCompromise";
        case 2: return "cACompromise";
        case 3: return "affiliationChanged";
        case 4: return "superseded";
        case 5: return "cessationOfOperation";
        case 6: return "certificateHold";
        case 8: return "removeFromCRL";
        case 9: return "privilegeWithdrawn";
        case 10: return "aACompromise";
        default: return "unknown";
    }
}
```

---

### 3. LDAP DIT Structure Update

```
dc=ldap,dc=smartcoreinc,dc=com
└── dc=pkd
    └── dc=download
        ├── dc=data
        │   └── c={COUNTRY}
        │       ├── o=csca    (CSCA certificates)
        │       ├── o=dsc     (DSC certificates)
        │       ├── o=lc      (Link Certificates) ← NEW
        │       ├── o=crl     (CRL)
        │       └── o=ml      (Master Lists)
        └── dc=nc-data
            └── c={COUNTRY}
                └── o=dsc     (DSC_NC - Non-Conformant)
```

**LC DN Format** (Sprint 1 fingerprint-based):
```
cn={SHA256_FINGERPRINT},o=lc,c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

**Example**:
```
cn=a1b2c3d4...64chars,o=lc,c=US,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

---

### 4. API Endpoints

#### POST `/api/validate/link-cert`

**Request**:
```json
{
  "certificateBinary": "base64_encoded_der_cert"
}
```

**Response**:
```json
{
  "success": true,
  "validation": {
    "trustChainValid": true,
    "oldCscaSignatureValid": true,
    "newCscaSignatureValid": true,
    "validityPeriodValid": true,
    "extensionsValid": true,
    "revocationStatus": "GOOD",
    "validationMessage": "LC trust chain valid"
  },
  "certificate": {
    "subjectDn": "CN=Link Certificate US 2025",
    "issuerDn": "CN=CSCA-US-OLD",
    "serialNumber": "1A2B3C",
    "notBefore": "2025-01-01T00:00:00Z",
    "notAfter": "2027-01-01T00:00:00Z",
    "fingerprint": "a1b2c3d4..."
  },
  "cscaInfo": {
    "oldCsca": {
      "subjectDn": "CN=CSCA-US-OLD",
      "fingerprint": "e1f2g3h4...",
      "validUntil": "2026-12-31T23:59:59Z"
    },
    "newCsca": {
      "subjectDn": "CN=CSCA-US-NEW",
      "fingerprint": "i1j2k3l4...",
      "validFrom": "2025-01-01T00:00:00Z"
    }
  }
}
```

#### GET `/api/link-certs/search`

**Query Parameters**:
- `country` (optional): 2-letter country code
- `validOnly` (optional): true/false (filter expired LCs)
- `limit` (default: 50): max results
- `offset` (default: 0): pagination

**Response**:
```json
{
  "success": true,
  "total": 12,
  "linkCertificates": [
    {
      "id": "uuid",
      "subjectDn": "CN=Link Certificate US 2025",
      "issuerDn": "CN=CSCA-US-OLD",
      "serialNumber": "1A2B3C",
      "fingerprint": "a1b2c3d4...",
      "countryCode": "US",
      "notBefore": "2025-01-01T00:00:00Z",
      "notAfter": "2027-01-01T00:00:00Z",
      "trustChainValid": true,
      "revocationStatus": "GOOD"
    }
  ]
}
```

---

## Implementation Phases

### Phase 1: Foundation (Days 1-3)

**Day 1**:
- [ ] Create database schema (link_certificate, crl_revocation_log tables)
- [ ] Add database migration script (06-link-certificate.sql)
- [ ] Update docker init-scripts

**Day 2**:
- [ ] Implement `checkCrlRevocation()` function
- [ ] Add CRL parsing logic (X509_CRL API)
- [ ] Unit tests for CRL validation

**Day 3**:
- [ ] Implement `validateLinkCertificate()` function
- [ ] Add LC trust chain logic
- [ ] Unit tests for LC validation

### Phase 2: LDAP Integration (Days 4-5)

**Day 4**:
- [ ] Update LDAP schema (o=lc branch)
- [ ] Modify `saveCertificateToLdap()` for LC type
- [ ] Update `buildCertificateDnV2()` for LC

**Day 5**:
- [ ] Test LC storage in LDAP
- [ ] Verify LDAP MMR replication
- [ ] Update LDAP search filters

### Phase 3: API Implementation (Days 6-8)

**Day 6**:
- [ ] Implement `POST /api/validate/link-cert` endpoint
- [ ] Add request validation
- [ ] Add response formatting

**Day 7**:
- [ ] Implement `GET /api/link-certs/search` endpoint
- [ ] Add `GET /api/link-certs/{id}` endpoint
- [ ] Update API Gateway routing

**Day 8**:
- [ ] Integration testing (all endpoints)
- [ ] Error handling improvements
- [ ] API documentation (OpenAPI spec)

### Phase 4: Testing & Documentation (Days 9-10)

**Day 9**:
- [ ] Comprehensive integration tests
- [ ] Test with real ICAO PKD data (if available)
- [ ] Performance testing (1000+ LCs)

**Day 10**:
- [ ] Update CLAUDE.md with Sprint 2 completion
- [ ] Create Sprint 2 completion summary
- [ ] Code review and cleanup

---

## Success Criteria

### Functional Requirements

- ✅ LC validation correctly identifies valid trust chains
- ✅ CRL revocation checking returns accurate status
- ✅ LC stored in LDAP with fingerprint-based DN
- ✅ API endpoints return correct validation results
- ✅ Database schema supports LC lifecycle tracking

### Performance Requirements

- LC validation: < 500ms per certificate
- CRL parsing: < 100ms per CRL
- LDAP storage: < 200ms per LC
- API response time: < 1 second

### Quality Requirements

- Unit test coverage: > 80%
- All edge cases handled (expired LC, missing CSCA, etc.)
- Error messages are clear and actionable
- Code follows existing patterns (Sprint 1 style)

---

## Testing Strategy

### Unit Tests (GTest)

1. **CRL Revocation Tests** (`tests/crl_revocation_test.cpp`):
   - Empty CRL → GOOD status
   - Certificate in revoked list → REVOKED status
   - Certificate not in list → GOOD status
   - Invalid CRL binary → UNKNOWN status
   - CRL reason code extraction

2. **LC Validation Tests** (`tests/link_cert_test.cpp`):
   - Valid LC with both CSCAs present → VALID
   - LC with missing old CSCA → INVALID
   - LC with missing new CSCA → INVALID
   - LC with invalid signature → INVALID
   - Expired LC → INVALID

### Integration Tests

1. **LC Upload and Validation**:
   - Upload Master List containing LC
   - Parse and store LC in database
   - Validate LC trust chain
   - Store LC in LDAP
   - Verify LDAP replication

2. **CRL Integration**:
   - Upload CRL containing revoked certificates
   - Check revocation status for DSC/LC
   - Verify revocation reason codes
   - Test CRL expiry handling

3. **API Testing**:
   - POST /api/validate/link-cert with valid LC
   - POST /api/validate/link-cert with invalid LC
   - GET /api/link-certs/search with filters
   - GET /api/link-certs/{id} for existing LC

---

## Risk Assessment

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| No real LC data available for testing | HIGH | MEDIUM | Use synthetic LC generated with OpenSSL |
| CRL parsing complexity | MEDIUM | MEDIUM | Use OpenSSL X509_CRL API, extensive testing |
| LDAP schema conflicts | LOW | LOW | Follow Sprint 1 DN structure |
| Performance issues with large CRLs | MEDIUM | LOW | Implement CRL caching strategy |
| Forward CSCA lookup complexity | HIGH | MEDIUM | Add issuer_dn index, optimize SQL query |

---

## Dependencies

### Internal

- ✅ Sprint 1 LDAP DN Migration (fingerprint-based DNs)
- ✅ Existing CSCA/DSC validation logic
- ✅ LDAP MMR infrastructure
- ✅ PostgreSQL schema (certificate, crl tables)

### External

- OpenSSL 3.x (X509_CRL API, signature verification)
- PostgreSQL 15+ (JSONB for metadata)
- OpenLDAP 2.5+ (o=lc branch support)
- Drogon 1.9+ (REST API framework)

---

## Future Work (Post-Sprint 2)

### Sprint 3: OCSP Integration

- OCSP responder client implementation
- OCSP response caching
- Fallback: CRL → OCSP

### Sprint 4: Certificate Transparency

- CT log monitoring
- Certificate pre-issuance validation
- CT log inclusion proof verification

### Sprint 5: Advanced Validation

- Certificate policy validation
- Extended Key Usage checks
- Name constraints validation

---

## References

- **ICAO Doc 9303 Part 12**: Public Key Infrastructure for MRTDs
- **RFC 5280**: Internet X.509 PKI Certificate and CRL Profile
- **RFC 6960**: X.509 Internet Public Key Infrastructure OCSP
- **OpenSSL X509_CRL**: https://www.openssl.org/docs/man3.0/man3/X509_CRL_new.html
- **Sprint 1 Design**: `docs/SPRINT1_LDAP_DN_DESIGN.md`

---

**Document Owner**: kbjung
**Reviewer**: TBD
**Approval Date**: TBD

---

## Appendix A: Sample Link Certificate Structure

```
Certificate:
    Data:
        Version: 3 (0x2)
        Serial Number: 1a:2b:3c:4d
    Signature Algorithm: sha256WithRSAEncryption
        Issuer: C=US, O=US Government, CN=CSCA-US-OLD
        Validity
            Not Before: Jan  1 00:00:00 2025 GMT
            Not After : Jan  1 00:00:00 2027 GMT
        Subject: C=US, O=US Government, CN=Link Certificate US 2025
        Subject Public Key Info:
            Public Key Algorithm: rsaEncryption
                Public-Key: (2048 bit)
        X509v3 extensions:
            X509v3 Basic Constraints: critical
                CA:TRUE, pathlen:0
            X509v3 Key Usage: critical
                Certificate Sign, CRL Sign
            X509v3 Subject Key Identifier:
                A1:B2:C3:D4:E5:F6:...
            X509v3 Authority Key Identifier:
                keyid:E1:F2:G3:H4:I5:J6:...
    Signature Algorithm: sha256WithRSAEncryption
         (signature bytes)
```

**Key Characteristics**:
- `CA:TRUE` (can sign other certificates)
- `pathlen:0` (can only sign end-entity certs, not other CAs)
- `Certificate Sign` in Key Usage
- Issuer = Old CSCA Subject DN
- Subject = Link Certificate DN (neither old nor new CSCA)

---

## Appendix B: SQL Query Examples

### Find LC by Country

```sql
SELECT
    lc.id,
    lc.subject_dn,
    lc.issuer_dn,
    lc.fingerprint_sha256,
    lc.trust_chain_valid,
    lc.revocation_status,
    old_csca.subject_dn AS old_csca_subject,
    new_csca.subject_dn AS new_csca_subject
FROM link_certificate lc
LEFT JOIN certificate old_csca ON lc.old_csca_fingerprint = old_csca.fingerprint_sha256
LEFT JOIN certificate new_csca ON lc.new_csca_fingerprint = new_csca.fingerprint_sha256
WHERE lc.country_code = 'US'
  AND lc.not_after > NOW()
ORDER BY lc.not_before DESC;
```

### Check Revocation Status

```sql
SELECT
    c.serial_number,
    c.fingerprint_sha256,
    crl.issuer_dn AS crl_issuer,
    crl.this_update,
    crl.next_update
FROM certificate c
LEFT JOIN crl ON c.issuer_dn = crl.issuer_dn
WHERE c.fingerprint_sha256 = 'a1b2c3d4...'
ORDER BY crl.this_update DESC
LIMIT 1;
```

---

**End of Sprint 2 Planning Document**
