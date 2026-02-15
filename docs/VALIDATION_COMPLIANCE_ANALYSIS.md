# ICAO Doc 9303 Validation Compliance Analysis

**Date**: 2026-02-15
**Version**: v2.10.3
**Analyst**: Automated Code Review (Claude)
**Scope**: PA Service, PKD Management - Trust Chain Validation, CRL Checking, Certificate Compliance

---

## 1. Overview

This document presents a comprehensive analysis of the ICAO Local PKD system's compliance with **ICAO Doc 9303** (Machine Readable Travel Documents) Parts 10, 11, and 12, as well as **RFC 5280** (Internet X.509 PKI Certificate and CRL Profile).

### Scope of Analysis

| Component | Files Analyzed |
|-----------|---------------|
| PA Service | `certificate_validation_service.cpp`, `pa_verification_service.cpp`, `certificate_chain_validation.h` |
| PKD Management | `validation_service.cpp`, `validation_repository.cpp`, `main.cpp` |
| Frontend | `ValidationDemo.tsx`, `types/index.ts`, `types/validation.ts` |

---

## 2. Correctly Implemented Requirements

### 2.1 DSC to CSCA Trust Chain Building

**Reference**: ICAO Doc 9303 Part 12, Section 5.2

**Implementation**: `certificate_validation_service.cpp:validateCertificateChain()`

- Multi-CSCA key rollover support via `findAllCscasByCountry()`
- Signature-verified CSCA selection (not just DN matching)
- Country code extraction from DSC issuer DN
- Meaningful error messages on CSCA not found

**Quality**: Excellent

### 2.2 Multi-CSCA Key Rollover

**Reference**: ICAO Doc 9303 Part 12, Section 5.3

When multiple CSCAs share the same issuer DN (key rollover scenario), the system:
1. Retrieves ALL CSCA candidates
2. Performs signature verification on each candidate
3. Selects the CSCA whose public key successfully verifies the DSC signature
4. Falls back to DN-only match if no signature verification succeeds

**Quality**: Excellent

### 2.3 CRL Checking (PA Service)

**Reference**: ICAO Doc 9303 Part 11, Section 4.3

Comprehensive CRL handling with 6 status types:

| CRL Status | Description | Severity |
|-----------|-------------|----------|
| VALID | CRL check passed, not revoked | INFO |
| REVOKED | Certificate revoked by issuing authority | CRITICAL |
| CRL_UNAVAILABLE | CRL not found (fail-open per ICAO) | WARNING |
| CRL_EXPIRED | CRL expired, revocation status uncertain | WARNING |
| CRL_INVALID | CRL signature verification failed | CRITICAL |
| NOT_CHECKED | CRL check skipped | INFO |

- CRL date extraction via `X509_CRL_get0_lastUpdate()`/`X509_CRL_get0_nextUpdate()`
- Expiration check before revocation checking
- ICAO fail-open principle: `CRL_UNAVAILABLE` does not cause hard failure

**Quality**: Excellent

### 2.4 Expiration Handling (Hybrid Chain Model)

**Reference**: ICAO Doc 9303 Part 12, Section 5.4

ICAO's "hybrid model" treats certificate expiration as **informational**, not a hard failure:

- CSCA validity: 13-15 years
- DSC validity: ~3 months
- Passport validity: ~10 years
- An expired CSCA's public key can still cryptographically verify DSC signatures

**Status Codes**:
- `VALID`: Both certificates not expired, signature valid
- `EXPIRED_VALID`: Signature verified but certificate(s) expired
- `INVALID`: Signature verification failed

**Quality**: Excellent

### 2.5 8-Step PA Verification Workflow

**Reference**: ICAO Doc 9303 Parts 10 & 11

| Step | Action | Implementation |
|------|--------|---------------|
| 1 | SOD Parsing | Extract DSC from Security Object Document |
| 2 | Certificate Chain Validation | Validate DSC -> CSCA chain |
| 3 | SOD Signature Verification | Verify SOD signed by DSC |
| 4 | Data Group Hash Verification | Verify each DG hash against SOD |
| 5 | PA Record Creation | Store verification result in DB |
| 5.5 | DSC Auto-Registration | Register extracted DSC if new |
| 6 | Data Group Storage | Save all data groups |
| 7 | Response Construction | Return result with timing |

**Quality**: Excellent

### 2.6 DSC Non-Conformance Checking

**Reference**: ICAO PKD nc-data specification

- SHA-256 fingerprint-based LDAP lookup in `dc=nc-data`
- Conformance code and text extraction
- Fallback search: `dc=data` first, then `dc=nc-data`
- Response fields: `dscNonConformant`, `pkdConformanceCode`, `pkdConformanceText`

**Quality**: Excellent

### 2.7 SOD and Data Group Hash Verification

**Reference**: ICAO Doc 9303 Part 11, Section 4.2

- SOD signature verification against extracted DSC
- Individual data group hash computation and comparison
- Hash algorithm extracted from SOD metadata

**Quality**: Excellent

### 2.8 Frontend-Backend Consistency

Validation status enums, CRL status messages, trust chain messages, and NC conformance fields are all **exactly aligned** between frontend types and backend responses.

**Quality**: Excellent

---

## 3. Gaps and Improvement Areas

### Priority 1 - Critical

#### 3.1 Self-Signed CSCA Self-Signature Verification

**Reference**: RFC 5280 Section 6.1, ICAO Doc 9303 Part 12

**Issue**: When a trust chain terminates at a self-signed CSCA, the self-signature is NOT verified. The chain simply stops when `subject DN == issuer DN`.

**Risk**: A tampered root CSCA with correct DN structure but invalid self-signature could be accepted as valid.

**Current Code** (PKD Management `validation_service.cpp:buildTrustChain()`):
```cpp
if (isSelfSigned(current)) {
    chain.isValid = true;  // Accepted without self-signature check
    break;
}
```

**Required Fix**: Add `verifyCertificateSignature(root, root)` before accepting root.

**Affected Files**:
- `services/pa-service/src/services/certificate_validation_service.cpp`
- `services/pkd-management/src/services/validation_service.cpp`

#### 3.2 Point-in-Time Validation

**Reference**: ICAO Doc 9303 Part 12, Section 5.4

**Issue**: Certificate validity is checked against **current time** instead of **document signing time**.

ICAO requires validation at the point in time when the document was signed:
- Certificate expired AFTER signing: Acceptable (EXPIRED_VALID)
- Certificate expired BEFORE signing: Not acceptable (INVALID)
- Certificate not-yet-valid AT signing time: Not acceptable (INVALID)

**Current Implementation**: `validAtSigningTime` field exists but is always set to `true`.

**Required Fix**:
1. Extract signing time from SOD `eContentInfo`
2. Compare certificate `notBefore`/`notAfter` against signing time
3. Distinguish "expired at verification time" vs "expired at signing time"

**Affected Files**:
- `services/pa-service/src/services/pa_verification_service.cpp`
- `services/pa-service/src/services/certificate_validation_service.cpp`
- `services/pa-service/src/domain/models/certificate_chain_validation.h`

---

### Priority 2 - High

#### 3.3 Link Certificate Validation

**Reference**: ICAO Doc 9303 Part 12, Section 5.3.2

**Issue**: Link certificates can be uploaded and stored but formal validation is not implemented.

**Current Code** (`validation_service.cpp:validateLinkCertificate()`):
```cpp
spdlog::warn("TODO: Implement Link Certificate validation");
result.isValid = false;
result.message = "Not yet implemented";
```

**Required Fix**: Implement validation using existing `buildTrustChain()` infrastructure.

**Affected Files**:
- `services/pkd-management/src/services/validation_service.cpp`

#### 3.4 Upload Processing CRL Check

**Reference**: ICAO Doc 9303 Part 11

**Issue**: PA Service has full CRL checking, but PKD Management upload processing does NOT check CRL during certificate validation.

**Current Code** (`validation_service.cpp:checkCrlRevocation()`):
```cpp
spdlog::warn("TODO: Implement CRL revocation check");
return false;
```

**Required Fix**: Implement CRL lookup and revocation check reusing PA Service patterns.

**Affected Files**:
- `services/pkd-management/src/services/validation_service.cpp`
- `services/pkd-management/src/services/validation_service.h`

#### 3.5 Signature Algorithm ICAO Compliance

**Reference**: ICAO Doc 9303 Part 12, Appendix A

**Issue**: Signature algorithm is recorded but NOT validated against ICAO-approved algorithms.

ICAO 9303 mandatory algorithms:
- Hash: SHA-256, SHA-384, SHA-512
- Signature: RSA (2048+ bits), ECDSA (P-256, P-384, P-521)
- Deprecated: SHA-1 (should warn), RSA < 2048 (should warn)

**Required Fix**: Create algorithm whitelist and validate during chain verification.

**Affected Files**:
- `services/pa-service/src/services/certificate_validation_service.cpp`
- New utility: algorithm validation helper

---

### Priority 3 - Medium

#### 3.6 Critical Extensions Validation

**Reference**: RFC 5280 Section 4.2

**Issue**: Unknown critical extensions are silently ignored. Per RFC 5280, any unrecognized critical extension MUST cause certificate rejection.

**Required Fix**: Iterate certificate extensions, reject on unknown critical extensions.

**Affected Files**:
- `services/pa-service/src/services/certificate_validation_service.cpp`
- `services/pkd-management/src/services/validation_service.cpp`

#### 3.7 Key Usage Extensions Validation

**Reference**: ICAO Doc 9303 Part 12, Section 4.6

Required Key Usage bits per certificate role:

| Role | Required Key Usage |
|------|-------------------|
| DSC | digitalSignature |
| CSCA | keyCertSign, cRLSign |
| Link Certificate | keyCertSign |

**Issue**: Key usage is extracted and stored but NOT validated against role requirements.

**Affected Files**:
- `services/pa-service/src/services/certificate_validation_service.cpp`

#### 3.8 CRL Revocation Reason Code

**Reference**: RFC 5280 Section 5.3.1

**Issue**: CRL revocation status is checked (revoked/not-revoked) but the revocation reason code (e.g., keyCompromise, affiliationChanged, superseded) is NOT extracted.

**Required Fix**: Extract `CRLReason` from revoked certificate entry.

**Affected Files**:
- `services/pa-service/src/services/certificate_validation_service.cpp`

---

## 4. Compliance Summary Matrix

| ICAO 9303 Requirement | Status | Quality | Priority |
|----------------------|--------|---------|----------|
| DSC -> CSCA Chain Building | PASS | Excellent | - |
| Multi-CSCA Key Rollover | PASS | Excellent | - |
| CRL Checking (PA) | PASS | Excellent | - |
| CRL Checking (Upload) | FAIL | Not Implemented | P2 |
| Expiration Handling (Hybrid) | PASS | Excellent | - |
| 8-Step PA Verification | PASS | Excellent | - |
| DSC Non-Conformance | PASS | Excellent | - |
| SOD Signature Verification | PASS | Excellent | - |
| DG Hash Verification | PASS | Excellent | - |
| Self-Signed CSCA Verification | FAIL | Not Implemented | P1 |
| Point-in-Time Validation | FAIL | Not Implemented | P1 |
| Link Certificate Validation | FAIL | Stub Only | P2 |
| Signature Algorithm Validation | WARN | Recorded, Not Validated | P2 |
| Critical Extensions Check | FAIL | Not Implemented | P3 |
| Key Usage Validation | WARN | Extracted, Not Validated | P3 |
| CRL Revocation Reason | WARN | Not Extracted | P3 |
| Frontend-Backend Consistency | PASS | Excellent | - |

---

## 5. Implementation Plan

### Phase 1: Critical Fixes (P1)
1. Self-signed CSCA self-signature verification
2. Point-in-time validation with SOD signing time extraction

### Phase 2: High Priority (P2)
3. Link certificate validation
4. Upload processing CRL check
5. Signature algorithm ICAO compliance validation

### Phase 3: Medium Priority (P3)
6. Critical extensions validation (RFC 5280)
7. Key usage extensions validation
8. CRL revocation reason code extraction

---

## 6. References

- ICAO Doc 9303 Part 10: Logical Data Structure (LDS) for Storage of Biometrics
- ICAO Doc 9303 Part 11: Security Mechanisms for MRTDs
- ICAO Doc 9303 Part 12: Public Key Infrastructure for MRTDs
- RFC 5280: Internet X.509 PKI Certificate and CRL Profile
- RFC 3279: Algorithms and Identifiers for the Internet X.509 PKI
