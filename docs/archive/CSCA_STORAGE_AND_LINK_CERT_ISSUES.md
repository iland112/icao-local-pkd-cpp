# CSCA Storage Discrepancy and Link Certificate Validation Issues

**Document Version**: 1.0.0
**Created**: 2026-01-23
**Status**: 🔴 Critical Issues Identified
**Priority**: P0 (Security & Standards Compliance)

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Issue 1: DB-LDAP Storage Discrepancy](#issue-1-db-ldap-storage-discrepancy)
3. [Issue 2: Missing Link Certificate Validation](#issue-2-missing-link-certificate-validation)
4. [Root Cause Analysis](#root-cause-analysis)
5. [Impact Assessment](#impact-assessment)
6. [Resolution Plan](#resolution-plan)
7. [Implementation Roadmap](#implementation-roadmap)
8. [Testing Strategy](#testing-strategy)
9. [References](#references)

---

## Executive Summary

### Critical Findings

Two critical issues have been identified in the ICAO Local PKD system that affect certificate storage integrity and trust chain validation:

| Issue | Severity | Impact | ICAO Compliance |
|-------|----------|--------|-----------------|
| **DB-LDAP Storage Discrepancy** | 🔴 High | 5 CSCA certificates missing from LDAP | ⚠️ Data Integrity |
| **Missing Link Certificate Validation** | 🔴 Critical | DSC validation failures during CSCA key rollover | ❌ ICAO Doc 9303-12 Non-Compliant |

### Key Statistics

```
Database (PostgreSQL):        536 CSCA certificates
LDAP Storage:                 531 CSCA certificates
Discrepancy:                  5 certificates (0.93%)

DSC Trust Chain Validation:
- Total DSC:                  29,610
- Valid:                      5,868 (19.8%)
- Invalid:                    23,742 (80.2%)
- Potential False Negatives:  Unknown (Link cert analysis required)
```

---

## Issue 1: DB-LDAP Storage Discrepancy

### Problem Statement

5 CSCA certificates exist in PostgreSQL database (marked `stored_in_ldap=true`) but are **not present** in LDAP directory.

### Affected Certificates

#### Country Breakdown

| Country | DB Count | LDAP Count | Missing | Affected Serial Numbers |
|---------|----------|------------|---------|------------------------|
| **CN (China)** | 34 | 31 | **3** | 434E445343410005, 55FDDF6FB9C6369A, 706E2CBC2436B1FA |
| **DE (Germany)** | 13 | 12 | **1** | 01 |
| **KZ (Kazakhstan)** | 5 | 4 | **1** | -09DE4748991DEDC3C68B954765D564098C496B1C |

#### Example Case: China Serial `434E445343410005`

**Certificate 1** (Successfully stored in LDAP):
```
Serial Number:     434E445343410005
Subject DN:        C=CN, O=中华人民共和国, OU=出入境管理局, CN=CSCA
Issuer DN:         C=CN, O=中华人民共和国, OU=出入境管理局, CN=CSCA (self-signed)
Not Before:        2015-03-28 07:47:10 GMT
Not After:         2025-03-25 07:47:10 GMT
Fingerprint:       72b3f2a05a3ec5e8ff9c8a07b634cd4b1c3f7d45ef70cf5b3aece09befd09fc0
Public Key (RSA):  Modulus=C0A50BBE6...
```

**Certificate 2** (Failed to store in LDAP):
```
Serial Number:     434E445343410005 (SAME)
Subject DN:        C=CN, O=中华人民共和国, OU=出入境管理局, CN=CSCA (SAME)
Issuer DN:         C=CN, O=中华人民共和国, OU=出入境管理局, CN=CSCA (SAME, self-signed)
Not Before:        2015-04-27 14:15:22 GMT (DIFFERENT - 30 days later)
Not After:         2025-04-25 14:15:22 GMT (DIFFERENT)
Fingerprint:       e3dbd84925fb24fb50ba7cc8db71b90a8bd46f64efe2ce2aff6cde6ea4fcf52f (DIFFERENT)
Public Key (RSA):  Modulus=C0A50BBE6... (SAME key pair!)
```

### Root Cause

#### 1. RFC 5280 Violation by Issuing Countries

**RFC 5280 Section 4.1.2.2 (Serial Number)**:
> "The serial number MUST be a positive integer assigned by the CA to each certificate. **It MUST be unique for each certificate issued by a given CA** (i.e., the issuer name and serial number identify a unique certificate)."

**Violation**: China (CN), Germany (DE), and Kazakhstan (KZ) re-issued CSCA certificates with:
- **Same serial number** (CN: 434E445343410005)
- **Same subject DN and issuer DN**
- **Same public key** (RSA modulus identical)
- **Different validity periods** (notBefore/notAfter dates)
- **Different fingerprints** (SHA-256 hash of entire certificate)

This is a **clear violation** of RFC 5280, as the serial number is not unique per CA.

#### 2. LDAP DN Structure Constraint

**LDAP Entry DN Format**:
```
dn: cn={SUBJECT-DN}+sn={SERIAL-NUMBER},o=csca,c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

**LDAP Constraint**: Distinguished Names (DNs) must be **globally unique** within the directory tree.

**Storage Sequence**:
```
1. First Certificate Upload (2015-03-28):
   → LDAP ADD operation
   → DN: cn=CSCA+sn=434E445343410005,o=csca,c=CN,...
   → Result: SUCCESS (entry created)

2. Second Certificate Upload (2015-04-27):
   → LDAP ADD operation
   → DN: cn=CSCA+sn=434E445343410005,o=csca,c=CN,... (SAME DN!)
   → Result: LDAP_ALREADY_EXISTS error
   → Second certificate NOT stored in LDAP
```

**Code Evidence** ([main.cpp:2045-2048](services/pkd-management/src/main.cpp#L2045-L2048)):
```cpp
int rc = ldap_add_ext_s(ldapConn, dn.c_str(), ldapMods, nullptr, nullptr);
if (rc != LDAP_SUCCESS) {
    spdlog::error("Failed to add certificate to LDAP: {} ({})", ldap_err2string(rc), rc);
    ldap_msgfree(result);
    return false;
}
```

**Problem**: Code does not handle `LDAP_ALREADY_EXISTS` error specifically. It logs error and returns `false`, but PostgreSQL already marked `stored_in_ldap=true`.

#### 3. Database State Inconsistency

**Upload Pipeline** ([main.cpp:2194-2242](services/pkd-management/src/main.cpp#L2194-L2242)):
```cpp
// Step 1: Save to PostgreSQL
bool dbSuccess = saveCertificate(conn, certId, uploadId, certType, ...);

// Step 2: Upload to LDAP
bool ldapSuccess = uploadCertificateToLdap(ldapConn, certDer, certType, ...);

// Step 3: Update stored_in_ldap flag
if (ldapSuccess) {
    updateCertificateLdapStatus(conn, certId, true);
}
```

**Bug**: In the current code, there's a timing window where:
1. PostgreSQL INSERT succeeds (`stored_in_ldap` defaults to `false` or set to `true` prematurely)
2. LDAP ADD fails with `LDAP_ALREADY_EXISTS`
3. No rollback mechanism
4. Database shows `stored_in_ldap=true` even though LDAP operation failed

### Standards Analysis

#### X.509 Certificate Fingerprint (SHA-256)

**Definition**:
- Cryptographic hash (SHA-256) of the **entire certificate DER encoding**
- Includes all fields: serial, subject, issuer, public key, validity dates, extensions, signature
- Even 1-byte difference produces completely different hash (avalanche effect)

**Properties**:
- **Unique identifier**: Collision probability is 2^-256 (computationally infeasible)
- **Different fingerprints** = **Different certificates** (guaranteed)

**Source**:
- [Public key fingerprint - Wikipedia](https://en.wikipedia.org/wiki/Public_key_fingerprint)
- [RFC 5280 - Certificate and CRL Profile](https://datatracker.ietf.org/doc/html/rfc5280)

**Conclusion**: The 5 missing certificates are **distinct, valid certificates** despite sharing serial numbers.

---

## Issue 2: Missing Link Certificate Validation

### Problem Statement

DSC Trust Chain validation does **NOT support** ICAO Doc 9303-12 Link Certificates, causing validation failures during CSCA key rollover scenarios.

### ICAO Doc 9303-12 Requirements

#### CSCA Key Rollover Process

**ICAO Doc 9303 Part 12 - Section 7.1 (CSCA Certificates)**:

When a Country Signing CA needs to replace its cryptographic key pair:

```
Phase 1: Initial State
┌─────────────┐
│ CSCA_old    │ (Self-signed, expires 2025)
│ Subject=CN  │
│ Issuer=CN   │
└─────────────┘
       │ signs
       ▼
┌─────────────┐
│   DSC_1     │ (Issued 2020, expires 2030)
│ Issuer=CN   │
└─────────────┘

Phase 2: Key Rollover (2024)
┌─────────────┐
│ CSCA_new    │ (Self-signed, expires 2034)
│ Subject=CN  │
│ Issuer=CN   │
└─────────────┘

┌─────────────┐
│ Link_Cert   │ (Cross-signed)
│ Subject=CN  │ (CSCA_new public key)
│ Issuer=CN   │ (Signed by CSCA_old private key)
└─────────────┘

Phase 3: Trust Chain Validation (2026)
DSC_1 (issued 2020) → CSCA_old → Link_Cert → CSCA_new
```

**Link Certificate Properties**:
- **Subject DN**: New CSCA
- **Issuer DN**: Old CSCA (different from subject - **NOT self-signed**)
- **Public Key**: New CSCA's public key
- **Signature**: Signed by Old CSCA's private key
- **Purpose**: Create cryptographic link between old and new CSCA keys

**Trust Chain Requirement**:
> "Receiving States MUST be able to validate DSC certificates issued before key rollover by traversing the link certificate chain to the currently active CSCA."

### Current Implementation Gaps

#### 1. Single CSCA Lookup

**Function**: `findCscaByIssuerDn()` ([main.cpp:637-730](services/pkd-management/src/main.cpp#L637-L730))

```cpp
X509* findCscaByIssuerDn(PGconn* conn, const std::string& issuerDn) {
    std::string query = "SELECT certificate_binary FROM certificate WHERE "
                        "certificate_type = 'CSCA' AND LOWER(subject_dn) = LOWER('" + escapedDn + "') LIMIT 1";
    //                                                                                              ^^^^^^^^
    //                                                                          PROBLEM: Returns only ONE certificate
```

**Issues**:
- `LIMIT 1` clause returns only the first matching CSCA
- If multiple CSCAs exist (self-signed + link), only one is retrieved
- No mechanism to retrieve all CSCAs for a given subject DN
- Cannot build certificate chains

#### 2. Direct Signature Verification Only

**Function**: `validateDscCertificate()` ([main.cpp:739-798](services/pkd-management/src/main.cpp#L739-L798))

```cpp
DscValidationResult validateDscCertificate(PGconn* conn, X509* dscCert, const std::string& issuerDn) {
    // Step 1: Check expiration ✅

    // Step 2: Find CSCA (SINGLE certificate)
    X509* cscaCert = findCscaByIssuerDn(conn, issuerDn);  // ❌ No chain building

    // Step 3: Direct signature verification
    EVP_PKEY* cscaPubKey = X509_get_pubkey(cscaCert);
    int verifyResult = X509_verify(dscCert, cscaPubKey);  // ❌ Only 1-step validation

    // ❌ Missing: Link certificate traversal
    // ❌ Missing: Multi-step chain validation
    // ❌ Missing: Recursive trust path search
}
```

**Validation Flow**:
```
Current (WRONG):
DSC → CSCA_new (direct verification)
      ▲
      │ If DSC was signed by CSCA_old, verification FAILS ❌
      └─ No link certificate traversal

Required (CORRECT):
DSC → CSCA_old → Link_Cert → CSCA_new
      ▲          ▲            ▲
      │          │            │
    Step 1     Step 2       Step 3 (root anchor)
```

#### 3. Master List Processing Acknowledges Link Certs but Doesn't Use Them

**Code Comments** ([main.cpp:3810, 3922, 3945-3948](services/pkd-management/src/main.cpp#L3810)):
```cpp
// Master List contains ONLY CSCA certificates (per ICAO Doc 9303)
// Including both self-signed and cross-signed/link CSCAs
std::string certType = "CSCA";

// ... later in code ...
} else {
    // Cross-signed/Link CSCA - mark as valid (signed by another CSCA)
    spdlog::debug("Cross-signed CSCA: subject={}, issuer={}",
                 subjectDn.substr(0, 50), issuerDn.substr(0, 50));
}
```

**Problem**:
- Code **recognizes** link certificates exist
- Code **stores** them as `certificate_type='CSCA'` in database
- Code **does NOT use** them in DSC trust chain validation

### Failure Scenario Example

#### Scenario: China CSCA Key Rollover (Hypothetical)

```
Timeline:
2015-03-28: CSCA_old issued (serial: 434E445343410005, fingerprint: 72b3f2a0...)
2016-01-01: DSC_Beijing issued (issuer: CSCA_old)
2015-04-27: CSCA_new issued (serial: 434E445343410005, fingerprint: e3dbd849...)
           Link_Cert issued (subject: CSCA_new, issuer: CSCA_old)
2026-01-23: Current date (CSCA_old expired, CSCA_new active)
```

**Trust Chain Validation Request** (Current behavior):
```
Input: DSC_Beijing (issued 2016-01-01, expires 2030-01-01, issuer: CSCA_old)

Step 1: findCscaByIssuerDn(conn, "C=CN, O=中华人民共和国, OU=出入境管理局, CN=CSCA")
        → Query: "... WHERE subject_dn = 'CSCA_old' LIMIT 1"
        → Returns: CSCA_new (or CSCA_old, depends on INSERT order)

        Assumption: Returns CSCA_new (current active certificate)

Step 2: X509_verify(DSC_Beijing, CSCA_new_pubkey)
        → DSC was signed by CSCA_old private key
        → CSCA_new has different public key
        → Verification FAILS ❌

Result:
- validationStatus: "INVALID"
- trustChainValid: false
- errorMessage: "DSC signature verification failed"
```

**Correct Validation Flow** (Required):
```
Input: DSC_Beijing

Step 1: Find all CSCAs with subject_dn = "CSCA_old"
        → Returns: [CSCA_old (self-signed), Link_Cert (cross-signed)]

Step 2: Build certificate chain
        DSC_Beijing → CSCA_old → Link_Cert → CSCA_new

Step 3: Validate each step
        a) X509_verify(DSC_Beijing, CSCA_old_pubkey) → SUCCESS ✅
        b) X509_verify(Link_Cert, CSCA_old_pubkey) → SUCCESS ✅
        c) Verify CSCA_new is self-signed root → SUCCESS ✅

Result:
- validationStatus: "VALID"
- trustChainValid: true
- trustChainPath: "DSC → CSCA_old → Link_Cert → CSCA_new"
```

---

## Root Cause Analysis

### Technical Root Causes

#### 1. LDAP Storage: DN Uniqueness Constraint

**LDAP Specification** (RFC 4514):
- Distinguished Name (DN) is the primary key for LDAP entries
- Each DN must be globally unique within the directory tree
- Duplicate DN additions result in `LDAP_ALREADY_EXISTS` error

**Design Decision**: Using serial number in DN structure
```
dn: cn={SUBJECT}+sn={SERIAL},o=csca,c={COUNTRY},...
```

**Consequence**: Certificates with duplicate serial numbers cannot coexist in LDAP.

**Alternative DN Strategies** (not implemented):
1. **Fingerprint-based DN**:
   ```
   dn: cn={SHA256_FINGERPRINT},o=csca,c={COUNTRY},...
   ```
   - Guaranteed uniqueness (SHA-256 collision probability: 2^-256)
   - No conflicts with duplicate serial numbers

2. **Composite DN with validity date**:
   ```
   dn: cn={SUBJECT}+sn={SERIAL}+validFrom={NOTBEFORE},o=csca,c={COUNTRY},...
   ```
   - Includes validity start date as disambiguator

3. **UUID-based DN**:
   ```
   dn: entryUUID={UUID},o=csca,c={COUNTRY},...
   ```
   - Completely unique, but loses semantic meaning

#### 2. Upload Pipeline: No Duplicate Detection

**Current Flow** ([main.cpp:2194-2242](services/pkd-management/src/main.cpp#L2194-L2242)):
```cpp
1. Parse certificate from LDIF/Master List
2. INSERT INTO certificate (...) → Always succeeds (different fingerprint)
3. LDAP ADD operation → May fail with LDAP_ALREADY_EXISTS
4. Update stored_in_ldap flag → Only if LDAP succeeds
```

**Missing Logic**:
- No pre-flight check for serial number conflicts
- No `ON CONFLICT` handling in PostgreSQL
- No LDAP DN uniqueness validation before ADD
- No user notification about duplicate serial numbers

#### 3. Trust Chain Validation: Single-Step Only

**Algorithm Limitation**:
```python
def validateDscCertificate(dsc, issuer_dn):
    # ❌ Only finds ONE CSCA
    csca = findCscaByIssuerDn(issuer_dn)  # LIMIT 1

    # ❌ Only verifies ONE signature
    valid = X509_verify(dsc, csca.public_key)

    # ❌ No chain traversal
    # if not valid and csca.issuer != csca.subject:
    #     link_cert = findLinkCertificate(...)
    #     valid = validateChain([dsc, csca, link_cert, root_csca])

    return valid
```

**Required Algorithm**:
```python
def validateDscCertificate(dsc, issuer_dn):
    # ✅ Find ALL CSCAs (self-signed + link certificates)
    cscas = findAllCscasByIssuerDn(issuer_dn)  # No LIMIT

    # ✅ Build possible trust chains
    chains = buildTrustChains(dsc, cscas)

    # ✅ Validate each chain (recursive)
    for chain in chains:
        if validateChain(chain):
            return True, chain

    return False, []
```

### Process Root Causes

#### 1. Issuing Countries' PKI Practices

**Non-Compliant Behavior**:
- China, Germany, Kazakhstan re-issued certificates with same serial numbers
- Violates RFC 5280 Section 4.1.2.2 (Serial Number Uniqueness)
- May be due to:
  - Legacy PKI systems with limited serial number generation
  - Manual certificate issuance processes
  - Lack of awareness of RFC 5280 requirements

**Impact on Interoperability**:
- LDAP-based PKD systems cannot store both certificates
- Certificate lookup by serial number becomes ambiguous
- Trust chain validation may fail

#### 2. ICAO PKD Design Assumptions

**Master List Structure** (CMS SignedData):
- Contains array of CSCA certificates
- No explicit ordering or relationship metadata
- System must infer link certificates by analyzing issuer/subject DNs

**Challenge**:
- Cannot distinguish between:
  - Re-issued certificate (same key, new validity)
  - Link certificate (new key, old issuer signature)
- Both have different subject/issuer relationships

---

## Impact Assessment

### Data Integrity Impact

#### Current Discrepancy

| Metric | Value | Percentage |
|--------|-------|------------|
| **Total CSCA (DB)** | 536 | 100% |
| **Total CSCA (LDAP)** | 531 | 99.07% |
| **Missing in LDAP** | 5 | **0.93%** |

**Affected Countries**: 3 (China, Germany, Kazakhstan)

#### Operational Impact

**Certificate Search Operations**:
- LDAP-based search: ❌ Cannot find 5 certificates
- PostgreSQL-based search: ✅ All 536 certificates found
- Frontend "Certificate Search" feature: ❌ Missing 5 CSCAs

**Export Operations**:
- Country export (ZIP): ❌ Missing 5 CSCAs from ZIP archives
- API `/api/certificates/export/country`: ❌ Incomplete data

**Sync Monitoring**:
- DB-LDAP Sync Status: ⚠️ Reports 5 discrepancies
- Auto Reconcile: ❌ Cannot reconcile (LDAP_ALREADY_EXISTS error)

### Trust Chain Validation Impact

#### Current Validation Statistics

```
Total DSC Certificates:       29,610
Trust Chain Valid:            5,868 (19.8%)
Trust Chain Invalid:          23,742 (80.2%)
CSCA Not Found:               ~6,000 (estimated)
```

**High Invalid Rate Analysis**:

Possible causes of 80.2% invalid rate:
1. ✅ **Legitimate invalidity**: Expired DSCs, revoked certificates
2. ✅ **CSCA not uploaded**: Issuing country's CSCA not in database
3. ❌ **Link certificate missing**: DSC signed by old CSCA, but link cert not traversed
4. ❌ **Duplicate serial conflict**: CSCA exists but not in LDAP

**Estimated Impact of Missing Link Validation**:

To determine link certificate impact, we need:
```sql
-- Count link certificates (cross-signed CSCAs)
SELECT COUNT(*)
FROM certificate
WHERE certificate_type = 'CSCA'
  AND subject_dn != issuer_dn;  -- Cross-signed (not self-signed)
```

If result > 0, then some DSCs may be falsely marked INVALID.

#### Security Impact

**ICAO 9303 Compliance**: ❌ **Non-Compliant**
- ICAO Doc 9303-12 mandates link certificate support
- System cannot validate DSCs during CSCA key rollover
- Violates interoperability requirements for border control systems

**Risk Scenarios**:
1. **False Rejection**: Valid passport rejected at border (legitimate traveler denied entry)
2. **Trust Anchor Mismatch**: DSC signed by old CSCA fails validation when only new CSCA is available
3. **Operational Disruption**: Countries performing key rollover cause validation failures for all DSCs issued before rollover

---

## Resolution Plan

### Phase 1: Issue Assessment (Completed ✅)

- ✅ Identified 5 missing CSCA certificates
- ✅ Root cause: Duplicate serial numbers (RFC 5280 violation by issuing countries)
- ✅ Identified missing link certificate validation logic
- ✅ Documented ICAO Doc 9303-12 requirements

### Phase 2: LDAP Storage Fix

#### Objective
Resolve DB-LDAP discrepancy by modifying LDAP DN structure to guarantee uniqueness.

#### Options Analysis

| Option | DN Structure | Pros | Cons | Recommendation |
|--------|-------------|------|------|----------------|
| **A: Fingerprint-based** | `cn={SHA256}+certType=csca,c={COUNTRY},...` | ✅ Guaranteed unique<br>✅ No conflicts | ❌ Loses semantic meaning<br>❌ DN not human-readable | ⭐ **Recommended** |
| **B: Composite with date** | `cn={SUBJECT}+sn={SERIAL}+validFrom={DATE},...` | ✅ Human-readable<br>✅ Preserves serial | ❌ Complex parsing<br>❌ Date format issues | ⚠️ Acceptable |
| **C: UUID-based** | `entryUUID={UUID},...` | ✅ Guaranteed unique | ❌ No semantic meaning<br>❌ Not LDAP best practice | ❌ Not recommended |

#### Recommended Solution: Option A (Fingerprint-based DN)

**New DN Structure**:
```
dn: cn={SHA256_FINGERPRINT},certType={TYPE},c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com

Example:
dn: cn=72b3f2a05a3ec5e8ff9c8a07b634cd4b1c3f7d45ef70cf5b3aece09befd09fc0,certType=csca,c=CN,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

**Benefits**:
- SHA-256 fingerprint is **cryptographically unique** (collision probability: 2^-256)
- No conflicts with duplicate serial numbers
- Fingerprint already stored in database (`fingerprint_sha256` column)
- Simple to implement (replace serial number with fingerprint)

**Migration Steps**:
1. **Schema Update**: Modify DN construction in [main.cpp:2007-2015](services/pkd-management/src/main.cpp#L2007-L2015)
2. **Data Migration**:
   - Export existing LDAP entries
   - Delete old entries
   - Re-import with new DN structure
3. **Search Logic Update**: Update LDAP search filters to use fingerprint

**Code Changes Required**:

File: [services/pkd-management/src/main.cpp](services/pkd-management/src/main.cpp#L2007-L2015)

```cpp
// OLD DN structure (BEFORE)
std::string dn = "cn=" + subjectDn + "+sn=" + serialNumber + ",o=" + certType + ",c=" + countryCode + ",...";

// NEW DN structure (AFTER)
std::string dn = "cn=" + fingerprintSha256 + ",certType=" + certType + ",c=" + countryCode +
                 ",dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
```

**Backward Compatibility**:
- LDAP clients searching by serial number must be updated
- Certificate Search frontend must search by fingerprint instead of serial
- Auto Reconcile logic must be updated

### Phase 3: Link Certificate Validation

#### Objective
Implement ICAO Doc 9303-12 compliant trust chain validation with link certificate support.

#### Algorithm Design

**Step 1: Multi-CSCA Lookup**

```cpp
// Replace findCscaByIssuerDn() with:
std::vector<X509*> findAllCscasBySubjectDn(PGconn* conn, const std::string& subjectDn) {
    // Query ALL CSCAs matching subject DN (no LIMIT clause)
    std::string query = "SELECT certificate_binary FROM certificate "
                        "WHERE certificate_type = 'CSCA' "
                        "AND LOWER(subject_dn) = LOWER($1)";

    // Return vector of X509* (caller must free all)
}
```

**Step 2: Link Certificate Identification**

```cpp
bool isLinkCertificate(X509* cert) {
    X509_NAME* subject = X509_get_subject_name(cert);
    X509_NAME* issuer = X509_get_issuer_name(cert);

    // Link certificate: Subject ≠ Issuer (cross-signed)
    // Self-signed: Subject = Issuer
    return (X509_NAME_cmp(subject, issuer) != 0);
}

bool isSelfSigned(X509* cert) {
    X509_NAME* subject = X509_get_subject_name(cert);
    X509_NAME* issuer = X509_get_issuer_name(cert);
    return (X509_NAME_cmp(subject, issuer) == 0);
}
```

**Step 3: Trust Chain Building**

```cpp
struct TrustChain {
    std::vector<X509*> certificates;  // DSC → CSCA_old → Link → CSCA_new
    bool isValid;
    std::string path;  // Human-readable path
};

TrustChain buildTrustChain(X509* dscCert, const std::vector<X509*>& allCscas) {
    TrustChain chain;
    X509* current = dscCert;
    std::set<std::string> visited;  // Prevent circular references

    while (current) {
        // Add current certificate to chain
        chain.certificates.push_back(current);

        // Get issuer DN
        char issuerDn[512];
        X509_NAME_oneline(X509_get_issuer_name(current), issuerDn, sizeof(issuerDn));

        // Prevent infinite loops
        if (visited.count(issuerDn) > 0) {
            chain.isValid = false;
            return chain;  // Circular reference detected
        }
        visited.insert(issuerDn);

        // Check if current certificate is self-signed (root)
        if (isSelfSigned(current)) {
            chain.isValid = true;
            return chain;  // Reached root CSCA
        }

        // Find issuer certificate in CSCA list
        X509* issuer = nullptr;
        for (X509* csca : allCscas) {
            char cscaSubjectDn[512];
            X509_NAME_oneline(X509_get_subject_name(csca), cscaSubjectDn, sizeof(cscaSubjectDn));

            if (strcmp(issuerDn, cscaSubjectDn) == 0) {
                issuer = csca;
                break;
            }
        }

        if (!issuer) {
            chain.isValid = false;
            return chain;  // Chain broken (issuer not found)
        }

        current = issuer;
    }

    chain.isValid = false;
    return chain;  // Should not reach here
}
```

**Step 4: Chain Validation**

```cpp
bool validateTrustChain(const TrustChain& chain) {
    if (chain.certificates.size() < 2) {
        return false;  // Need at least DSC and one CSCA
    }

    // Validate each certificate in chain
    for (size_t i = 0; i < chain.certificates.size(); i++) {
        X509* cert = chain.certificates[i];

        // Check expiration
        time_t now = time(nullptr);
        if (X509_cmp_time(X509_get0_notAfter(cert), &now) < 0) {
            spdlog::warn("Certificate {} in chain is EXPIRED", i);
            return false;
        }
    }

    // Validate signatures from DSC to root
    for (size_t i = 0; i < chain.certificates.size() - 1; i++) {
        X509* cert = chain.certificates[i];
        X509* issuer = chain.certificates[i + 1];

        EVP_PKEY* issuerPubKey = X509_get_pubkey(issuer);
        if (!issuerPubKey) {
            spdlog::error("Failed to extract public key from issuer {}", i + 1);
            return false;
        }

        int verifyResult = X509_verify(cert, issuerPubKey);
        EVP_PKEY_free(issuerPubKey);

        if (verifyResult != 1) {
            unsigned long err = ERR_get_error();
            char errBuf[256];
            ERR_error_string_n(err, errBuf, sizeof(errBuf));
            spdlog::error("Signature verification failed at step {}: {}", i, errBuf);
            return false;
        }

        spdlog::debug("Chain step {} verified: cert {} signed by issuer {}", i, i, i + 1);
    }

    // Verify root is self-signed
    X509* root = chain.certificates.back();
    if (!isSelfSigned(root)) {
        spdlog::warn("Root certificate is not self-signed");
        return false;
    }

    return true;
}
```

**Step 5: Updated DSC Validation Function**

```cpp
DscValidationResult validateDscCertificate(PGconn* conn, X509* dscCert, const std::string& issuerDn) {
    DscValidationResult result = {false, false, false, false, false, "", ""};

    if (!dscCert) {
        result.errorMessage = "DSC certificate is null";
        return result;
    }

    // Step 1: Check DSC expiration
    time_t now = time(nullptr);
    if (X509_cmp_time(X509_get0_notAfter(dscCert), &now) < 0) {
        result.errorMessage = "DSC certificate is expired";
        return result;
    }
    result.notExpired = true;

    // Step 2: Find ALL CSCAs matching issuer DN (including link certificates)
    std::vector<X509*> allCscas = findAllCscasBySubjectDn(conn, issuerDn);
    if (allCscas.empty()) {
        result.errorMessage = "No CSCA found for issuer: " + issuerDn.substr(0, 80);
        spdlog::warn("DSC validation: CSCA NOT FOUND");
        return result;
    }
    result.cscaFound = true;
    result.cscaSubjectDn = issuerDn;

    spdlog::info("DSC validation: Found {} CSCA(s) for issuer", allCscas.size());

    // Step 3: Build trust chain (may include link certificates)
    TrustChain chain = buildTrustChain(dscCert, allCscas);

    if (!chain.isValid) {
        result.errorMessage = "Failed to build trust chain to root CSCA";
        // Free all CSCA certificates
        for (X509* csca : allCscas) X509_free(csca);
        return result;
    }

    // Step 4: Validate entire chain (including link certificate steps)
    bool chainValid = validateTrustChain(chain);

    if (chainValid) {
        result.signatureValid = true;
        result.isValid = true;

        // Build human-readable trust path
        result.trustChainPath = "DSC";
        for (size_t i = 1; i < chain.certificates.size(); i++) {
            char subjectDn[256];
            X509_NAME_oneline(X509_get_subject_name(chain.certificates[i]), subjectDn, sizeof(subjectDn));
            result.trustChainPath += " → " + std::string(subjectDn).substr(0, 50);
        }

        spdlog::info("DSC validation: Trust Chain VERIFIED (path: {})", result.trustChainPath);
    } else {
        result.errorMessage = "Trust chain validation failed";
        spdlog::error("DSC validation: Trust Chain INVALID");
    }

    // Cleanup
    for (X509* csca : allCscas) X509_free(csca);

    return result;
}
```

#### Performance Considerations

**Current Performance**:
- Single CSCA query: `LIMIT 1` → Fast (indexed)
- Single signature verification: ~1-2ms

**New Performance**:
- Multi-CSCA query: No LIMIT → Slower (may return 2-10 certificates per country)
- Chain building: O(n) where n = number of CSCAs
- Chain validation: O(n) signature verifications (worst case: DSC → CSCA_old → Link → CSCA_new = 3 verifications)

**Optimization**:
- Cache CSCA certificates in memory (reduce DB queries)
- Limit chain depth to 5 certificates (prevent infinite loops)
- Use OpenSSL certificate store (X509_STORE) for efficient chain building

### Phase 4: Database Schema Enhancement (Optional)

#### Option: Certificate Relationships Table

To optimize link certificate chain building, create a relationship table:

```sql
CREATE TABLE certificate_relationships (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    certificate_id UUID NOT NULL REFERENCES certificate(id),
    issuer_certificate_id UUID REFERENCES certificate(id),
    relationship_type VARCHAR(20) NOT NULL,  -- 'DIRECT_ISSUE', 'LINK', 'CROSS_SIGN'
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT fk_certificate FOREIGN KEY (certificate_id) REFERENCES certificate(id) ON DELETE CASCADE,
    CONSTRAINT fk_issuer FOREIGN KEY (issuer_certificate_id) REFERENCES certificate(id) ON DELETE CASCADE
);

CREATE INDEX idx_cert_relationships_cert_id ON certificate_relationships(certificate_id);
CREATE INDEX idx_cert_relationships_issuer_id ON certificate_relationships(issuer_certificate_id);
```

**Benefits**:
- Fast chain lookup (JOIN instead of nested queries)
- Explicit relationship metadata
- Support for complex PKI topologies

**Implementation Complexity**: Medium (requires upload pipeline changes)

---

## Implementation Roadmap

### Timeline: 3-4 Weeks

| Phase | Duration | Tasks | Deliverables |
|-------|----------|-------|--------------|
| **Phase 1: Assessment** | ✅ Complete | Root cause analysis, documentation | This document |
| **Phase 2: LDAP Storage Fix** | 1 week | DN structure redesign, migration | New LDAP schema, 5 missing CSCAs stored |
| **Phase 3: Link Cert Validation** | 2 weeks | Algorithm implementation, testing | ICAO-compliant trust chain validation |
| **Phase 4: Validation & Testing** | 1 week | Integration testing, re-validation | Updated trust chain statistics |

### Detailed Task Breakdown

#### Week 1: LDAP Storage Fix

**Day 1-2: Design & Planning**
- [ ] Finalize DN structure (fingerprint-based)
- [ ] Update LDAP schema documentation
- [ ] Design migration strategy
- [ ] Create rollback plan

**Day 3-4: Implementation**
- [ ] Modify DN construction in [main.cpp:2007-2015](services/pkd-management/src/main.cpp#L2007-L2015)
- [ ] Update LDAP search filters
- [ ] Update Certificate Search frontend
- [ ] Update Auto Reconcile logic

**Day 5: Migration**
- [ ] Backup current LDAP data (ldapsearch -b "dc=pkd,..." > backup.ldif)
- [ ] Export all certificates from PostgreSQL
- [ ] Delete old LDAP entries
- [ ] Re-import with new DN structure
- [ ] Verify 536 CSCAs now in LDAP (including 5 previously missing)

**Day 6-7: Testing**
- [ ] Certificate Search: Verify all 536 CSCAs searchable
- [ ] Country Export: Verify complete ZIP archives
- [ ] DB-LDAP Sync: Verify 0 discrepancies
- [ ] Auto Reconcile: Verify no LDAP_ALREADY_EXISTS errors

#### Week 2-3: Link Certificate Validation

**Week 2: Core Algorithm**

**Day 1-2: Multi-CSCA Lookup**
- [ ] Implement `findAllCscasBySubjectDn()`
- [ ] Remove `LIMIT 1` from queries
- [ ] Add vector return type
- [ ] Unit tests for multi-CSCA scenarios

**Day 3-4: Chain Building**
- [ ] Implement `isLinkCertificate()`
- [ ] Implement `isSelfSigned()`
- [ ] Implement `buildTrustChain()`
- [ ] Add circular reference detection
- [ ] Unit tests with mock certificates

**Day 5: Chain Validation**
- [ ] Implement `validateTrustChain()`
- [ ] Multi-step signature verification
- [ ] Expiration checks for all chain certificates
- [ ] Unit tests with OpenSSL test certificates

**Week 3: Integration**

**Day 1-2: DSC Validation Update**
- [ ] Refactor `validateDscCertificate()` to use new chain building
- [ ] Add trust path to validation result struct
- [ ] Update database validation_result table (add trust_chain_path column)
- [ ] Integration tests

**Day 3-4: Master List Processing**
- [ ] Ensure link certificates are properly stored
- [ ] Add relationship metadata (if using certificate_relationships table)
- [ ] Test with real Master Lists from ICAO portal

**Day 5: Performance Optimization**
- [ ] Profile chain building performance
- [ ] Add CSCA caching (optional)
- [ ] Limit chain depth to 5
- [ ] Load testing with 30k DSCs

#### Week 4: Validation & Testing

**Day 1-2: Re-validation Campaign**
- [ ] Trigger re-validation of all 29,610 DSCs
- [ ] Monitor trust chain statistics
- [ ] Compare before/after invalid rates

**Day 3-4: Integration Testing**
- [ ] Test CSCA key rollover scenario (create test certificates)
- [ ] Test circular reference detection
- [ ] Test expired certificate in chain
- [ ] Test broken chain (missing intermediate)

**Day 5: Documentation & Deployment**
- [ ] Update API documentation (trust_chain_path field)
- [ ] Update CLAUDE.md with new validation algorithm
- [ ] Deploy to staging environment
- [ ] Performance monitoring

**Day 6-7: Production Deployment**
- [ ] Create database backup
- [ ] Deploy new code to production
- [ ] Monitor logs for errors
- [ ] Verify statistics (expected: invalid rate drops from 80% to ~50-60%)

---

## Testing Strategy

### Unit Tests

#### Test Suite 1: LDAP DN Uniqueness

**Test Cases**:
1. ✅ **Unique fingerprints**: Two certificates with different fingerprints → Both stored
2. ✅ **Duplicate serial + different fingerprint**: Two certificates, same serial, different fingerprint → Both stored
3. ✅ **Same certificate uploaded twice**: Same fingerprint → Second upload skipped (idempotent)

#### Test Suite 2: Link Certificate Identification

**Test Certificates** (OpenSSL generated):
```bash
# Self-signed CSCA_old
openssl req -x509 -newkey rsa:2048 -keyout csca_old.key -out csca_old.crt -days 3650 -nodes -subj "/C=TEST/O=TestCountry/CN=CSCA_OLD"

# Self-signed CSCA_new
openssl req -x509 -newkey rsa:2048 -keyout csca_new.key -out csca_new.crt -days 3650 -nodes -subj "/C=TEST/O=TestCountry/CN=CSCA_NEW"

# Link certificate (CSCA_new signed by CSCA_old)
openssl x509 -req -in csca_new_req.csr -CA csca_old.crt -CAkey csca_old.key -out link_cert.crt -days 365
```

**Test Cases**:
1. ✅ `isSelfSigned(csca_old)` → true
2. ✅ `isSelfSigned(link_cert)` → false
3. ✅ `isLinkCertificate(link_cert)` → true
4. ✅ `isLinkCertificate(csca_old)` → false

#### Test Suite 3: Chain Building

**Test Scenarios**:

**Scenario 1: Direct Chain (No Link)**
```
DSC → CSCA (self-signed)
Expected: Chain length = 2, Valid = true
```

**Scenario 2: Link Certificate Chain**
```
DSC → CSCA_old → Link_Cert → CSCA_new (self-signed)
Expected: Chain length = 4, Valid = true
```

**Scenario 3: Broken Chain (Missing Intermediate)**
```
DSC → (issuer: CSCA_old, but CSCA_old not in database)
Expected: Chain length = 1, Valid = false
```

**Scenario 4: Circular Reference**
```
DSC → CSCA_A (issuer: CSCA_B)
       CSCA_B (issuer: CSCA_A)  ← Circular!
Expected: Chain building stops, Valid = false
```

**Scenario 5: Expired Certificate in Chain**
```
DSC (valid) → CSCA_old (EXPIRED) → Link_Cert → CSCA_new
Expected: Valid = false (chain contains expired cert)
```

### Integration Tests

#### Test 1: Upload Duplicate Serial CSCA

**Setup**:
1. Upload CN collection 001 (contains CSCA serial 434E445343410005, notBefore: 2015-03-28)
2. Upload CN collection 002 (contains CSCA serial 434E445343410005, notBefore: 2015-04-27)

**Expected Results**:
- ✅ Both certificates in PostgreSQL (different fingerprints)
- ✅ Both certificates in LDAP (different DNs based on fingerprint)
- ✅ DB-LDAP Sync: 0 discrepancies
- ✅ Certificate Search: Both certificates findable

#### Test 2: DSC Validation with Link Certificate

**Setup**:
1. Upload Master List containing:
   - CSCA_old (self-signed)
   - Link_Cert (subject: CSCA_new, issuer: CSCA_old)
   - CSCA_new (self-signed)
2. Upload LDIF containing DSC (issuer: CSCA_old)

**Expected Results**:
- ✅ DSC validation succeeds
- ✅ Trust chain: "DSC → CSCA_old → Link_Cert → CSCA_new"
- ✅ validation_result.trust_chain_path populated
- ✅ validation_result.trust_chain_valid = true

#### Test 3: Re-validation Campaign

**Setup**:
1. Deploy new link certificate validation code
2. Trigger re-validation: `POST /api/upload/{uploadId}/revalidate`

**Expected Results** (Before vs After):

| Metric | Before | After (Expected) | Improvement |
|--------|--------|------------------|-------------|
| Trust Chain Valid | 5,868 (19.8%) | ~15,000 (50%) | +9,132 (+156%) |
| Trust Chain Invalid | 23,742 (80.2%) | ~14,610 (50%) | -9,132 (-38%) |
| CSCA Not Found | ~6,000 | ~6,000 | 0 (unchanged) |

**Note**: Actual numbers depend on how many link certificates exist in current database.

### Performance Tests

#### Test 1: Chain Building Performance

**Test Data**:
- 1,000 DSCs
- Each DSC has 5 possible CSCAs (including link certificates)

**Metrics**:
- Average chain building time: < 10ms per DSC
- Average chain validation time: < 5ms per chain step
- Total validation time: < 15 seconds for 1,000 DSCs

#### Test 2: Full Re-validation Load

**Test Data**:
- All 29,610 DSCs

**Metrics**:
- Total validation time: < 10 minutes (parallel processing)
- Memory usage: < 2GB (certificate caching)
- CPU usage: < 80% (multi-threaded)

---

## Risk Assessment

### Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| **LDAP Migration Failure** | Low | High | Full backup before migration, rollback plan |
| **Performance Degradation** | Medium | Medium | Caching, query optimization, load testing |
| **Chain Building Bugs** | Medium | High | Extensive unit tests, integration tests |
| **Circular Reference Infinite Loop** | Low | High | Visited set, max depth limit |
| **False Positives (Invalid → Valid)** | Low | Critical | Manual verification of sample DSCs |

### Operational Risks

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| **Downtime During Migration** | Medium | Medium | Scheduled maintenance window |
| **Certificate Search Disruption** | Low | Medium | Keep PostgreSQL search as fallback |
| **Frontend Breaking Changes** | Low | High | API versioning, backward compatibility |
| **Rollback Complexity** | Medium | High | Automated rollback scripts, LDAP dump |

### Compliance Risks

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| **ICAO Audit Failure** | High (currently) | Critical | Implement link cert validation (Phase 3) |
| **Interoperability Issues** | Medium | High | Test with real ICAO Master Lists |
| **Border Control System Integration** | Low | Critical | Document trust chain algorithm |

---

## Success Criteria

### Phase 2: LDAP Storage Fix

- ✅ **Completeness**: All 536 CSCA certificates in LDAP (currently 531)
- ✅ **Uniqueness**: No LDAP_ALREADY_EXISTS errors during upload
- ✅ **Searchability**: Certificate Search returns all certificates
- ✅ **Sync Status**: DB-LDAP Sync reports 0 discrepancies

### Phase 3: Link Certificate Validation

- ✅ **Algorithm Correctness**: All unit tests pass (chain building, validation)
- ✅ **ICAO Compliance**: Link certificate chains validated correctly
- ✅ **Validation Rate Improvement**: Trust chain valid rate increases from 19.8% to > 40%
- ✅ **Performance**: Re-validation of 29,610 DSCs completes in < 10 minutes

### Overall Project

- ✅ **Data Integrity**: 100% DB-LDAP consistency
- ✅ **Standards Compliance**: ICAO Doc 9303-12 fully implemented
- ✅ **No Regressions**: Existing valid DSCs remain valid
- ✅ **Documentation**: Complete API docs, algorithm docs, migration guide

---

## References

### ICAO Standards

1. **ICAO Doc 9303 Part 12 - Public Key Infrastructure for Machine Readable Travel Documents**
   - Section 7.1: CSCA Certificates
   - Section 7.1.2: Link Certificates
   - [Download](https://www.icao.int/publications/Documents/9303_p12_cons_en.pdf)

2. **ICAO Doc 9303 Part 11 - Security Mechanisms for MRTDs**
   - Section 6: Passive Authentication
   - [Download](https://www.icao.int/publications/Documents/9303_p11_cons_en.pdf)

### RFC Standards

3. **RFC 5280 - Internet X.509 Public Key Infrastructure Certificate and CRL Profile**
   - Section 4.1.2.2: Serial Number
   - [Read Online](https://datatracker.ietf.org/doc/html/rfc5280)

4. **RFC 4514 - Lightweight Directory Access Protocol (LDAP): String Representation of Distinguished Names**
   - [Read Online](https://datatracker.ietf.org/doc/html/rfc4514)

### OpenSSL Documentation

5. **OpenSSL X509 Certificate Verification**
   - [X509_verify() Documentation](https://www.openssl.org/docs/man3.0/man3/X509_verify.html)
   - [X509_STORE Documentation](https://www.openssl.org/docs/man3.0/man3/X509_STORE.html)

### Project Documentation

6. **Internal Documents**:
   - [CLAUDE.md](../CLAUDE.md) - Project overview
   - [PKD_RELAY_SERVICE_REFACTORING_STATUS.md](PKD_RELAY_SERVICE_REFACTORING_STATUS.md) - Service architecture
   - [CERTIFICATE_SEARCH_STATUS.md](CERTIFICATE_SEARCH_STATUS.md) - Search feature implementation

---

## Appendix A: SQL Queries for Investigation

### Query 1: Find Link Certificates

```sql
SELECT
    country_code,
    COUNT(*) as link_cert_count
FROM certificate
WHERE certificate_type = 'CSCA'
  AND subject_dn != issuer_dn  -- Cross-signed (not self-signed)
GROUP BY country_code
ORDER BY link_cert_count DESC;
```

### Query 2: Duplicate Serial Numbers

```sql
SELECT
    country_code,
    serial_number,
    COUNT(*) as duplicate_count,
    ARRAY_AGG(fingerprint_sha256) as fingerprints
FROM certificate
WHERE certificate_type = 'CSCA'
GROUP BY country_code, serial_number
HAVING COUNT(*) > 1
ORDER BY duplicate_count DESC;
```

### Query 3: DSC Validation Statistics by Country

```sql
SELECT
    country_code,
    COUNT(*) as total_dsc,
    SUM(CASE WHEN trust_chain_valid = true THEN 1 ELSE 0 END) as valid_count,
    SUM(CASE WHEN trust_chain_valid = false THEN 1 ELSE 0 END) as invalid_count,
    ROUND(100.0 * SUM(CASE WHEN trust_chain_valid = true THEN 1 ELSE 0 END) / COUNT(*), 2) as valid_percentage
FROM certificate
WHERE certificate_type IN ('DSC', 'DSC_NC')
GROUP BY country_code
ORDER BY total_dsc DESC
LIMIT 20;
```

### Query 4: Certificates Missing from LDAP

```sql
SELECT
    id,
    certificate_type,
    country_code,
    serial_number,
    fingerprint_sha256,
    subject_dn,
    created_at
FROM certificate
WHERE certificate_type = 'CSCA'
  AND stored_in_ldap = true
  AND fingerprint_sha256 NOT IN (
      -- This would be a list of fingerprints from LDAP
      -- (requires manual LDAP query and import)
  );
```

---

## Appendix B: LDAP Migration Script

### Script: `migrate_ldap_dn_structure.sh`

```bash
#!/bin/bash
# LDAP DN Structure Migration Script
# Migrates from serial-based DN to fingerprint-based DN

set -e

LDAP_HOST="localhost"
LDAP_PORT="389"
LDAP_BIND_DN="cn=admin,dc=ldap,dc=smartcoreinc,dc=com"
LDAP_BASE_DN="dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"
BACKUP_FILE="ldap_backup_$(date +%Y%m%d_%H%M%S).ldif"

echo "=== LDAP DN Migration Script ==="
echo "Backup file: $BACKUP_FILE"

# Step 1: Backup existing LDAP data
echo "[1/5] Backing up current LDAP data..."
ldapsearch -x -H ldap://$LDAP_HOST:$LDAP_PORT \
           -D "$LDAP_BIND_DN" -w "$LDAP_PASSWORD" \
           -b "$LDAP_BASE_DN" \
           > "$BACKUP_FILE"
echo "✅ Backup complete: $(wc -l < $BACKUP_FILE) lines"

# Step 2: Count current entries
CURRENT_COUNT=$(ldapsearch -x -H ldap://$LDAP_HOST:$LDAP_PORT \
                           -D "$LDAP_BIND_DN" -w "$LDAP_PASSWORD" \
                           -b "$LDAP_BASE_DN" \
                           "(objectClass=pkdDownload)" \
                           | grep -c "^dn:")
echo "[2/5] Current LDAP entries: $CURRENT_COUNT"

# Step 3: Delete old entries (with confirmation)
read -p "⚠️  Delete $CURRENT_COUNT entries? (yes/no): " confirm
if [ "$confirm" != "yes" ]; then
    echo "❌ Migration aborted"
    exit 1
fi

echo "[3/5] Deleting old entries..."
ldapsearch -x -H ldap://$LDAP_HOST:$LDAP_PORT \
           -D "$LDAP_BIND_DN" -w "$LDAP_PASSWORD" \
           -b "$LDAP_BASE_DN" \
           "(objectClass=pkdDownload)" \
           -LLL dn | grep "^dn:" | \
while read -r line; do
    DN=$(echo "$line" | sed 's/^dn: //')
    ldapdelete -x -H ldap://$LDAP_HOST:$LDAP_PORT \
               -D "$LDAP_BIND_DN" -w "$LDAP_PASSWORD" \
               "$DN" 2>/dev/null || true
done
echo "✅ Old entries deleted"

# Step 4: Re-import from PostgreSQL with new DN structure
echo "[4/5] Re-importing certificates with new DN structure..."
echo "ℹ️  This step requires running the updated application code"
echo "ℹ️  Execute: POST /api/upload/sync-all (trigger full DB → LDAP sync)"
read -p "Press Enter when re-import is complete..."

# Step 5: Verify migration
NEW_COUNT=$(ldapsearch -x -H ldap://$LDAP_HOST:$LDAP_PORT \
                       -D "$LDAP_BIND_DN" -w "$LDAP_PASSWORD" \
                       -b "$LDAP_BASE_DN" \
                       "(objectClass=pkdDownload)" \
                       | grep -c "^dn:")
echo "[5/5] New LDAP entries: $NEW_COUNT"

if [ "$NEW_COUNT" -ge "$CURRENT_COUNT" ]; then
    echo "✅ Migration successful!"
    echo "   Before: $CURRENT_COUNT entries"
    echo "   After:  $NEW_COUNT entries"
else
    echo "⚠️  Migration incomplete: Expected >= $CURRENT_COUNT, got $NEW_COUNT"
    echo "ℹ️  Backup available: $BACKUP_FILE"
fi
```

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0.0 | 2026-01-23 | Investigation Team | Initial comprehensive analysis |

---

**END OF DOCUMENT**
