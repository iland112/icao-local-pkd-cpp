# Phase 2 - SQL Injection Remaining Fixes: Detailed Analysis

**Version**: v1.9.0
**Date**: 2026-01-22
**Status**: Planning Complete

---

## Executive Summary

Phase 2 targets **56 remaining SQL queries** not covered in Phase 1. These queries involve:
- **Large INSERT statements** (25-30 fields) with custom escaping functions
- **UPDATE statements** with string concatenation
- **Binary data handling** (bytea for certificates)

**Priority**: Medium (no direct user input, but completeness required for 100% parameterization)

**Estimated Effort**: 2-3 days

---

## Query Categories and Counts

### Category 1: Large INSERT Queries (3 queries)
**File**: `services/pkd-management/src/main.cpp`

| Function | Line Range | Fields | Complexity | Priority |
|----------|-----------|--------|------------|----------|
| `saveValidationResult()` | 806-872 | 30 fields | HIGH | HIGH |
| ~~`saveCertificate()`~~ | ~~2194-2210~~ | ~~15 fields~~ | ~~HIGH~~ | N/A (LDAP only) |
| ~~`saveCrl()`~~ | ~~2231-2244~~ | ~~10 fields~~ | ~~MEDIUM~~ | N/A (LDAP only) |
| ~~`saveMasterList()`~~ | ~~2283-2291~~ | ~~8 fields~~ | ~~MEDIUM~~ | N/A (LDAP only) |

**Note**: Certificate/CRL/ML storage is **LDAP-only** (no database INSERT). Only validation results need parameterization.

---

### Category 2: UPDATE Queries with String Concatenation (6 queries)
**File**: `services/pkd-management/src/main.cpp`

| Function | Line | Query Type | User Input | Priority |
|----------|------|------------|------------|----------|
| `updateValidationStatistics()` | 887-899 | UPDATE uploaded_file | uploadId (UUID) | MEDIUM |
| `updateCertificateLdapStatus()` | 2120-2128 | UPDATE certificate | certId (UUID), ldapDn | LOW |
| `updateCrlLdapStatus()` | 2136-2144 | UPDATE crl | crlId (UUID), ldapDn | LOW |
| `updateMasterListLdapStatus()` | 2309-2317 | UPDATE master_list | mlId (UUID), ldapDn | LOW |

**File**: `services/pkd-management/src/processing_strategy.cpp`

| Function | Line | Query Type | User Input | Priority |
|----------|------|------------|------------|----------|
| `processLdifEntries()` (MANUAL Stage 1) | 320-324 | UPDATE uploaded_file | uploadId (UUID) | MEDIUM |
| `validateAndSaveToDb()` (check query) | 360-361 | SELECT uploaded_file | uploadId (UUID) | MEDIUM |

---

### Category 3: Parameterized Queries - Already Fixed ✅

The following queries are **already using `PQexecParams`** and do not need conversion:

**File**: `services/pkd-management/src/processing_strategy.cpp`
- Line 95: Master List count update (Stage 2)
- Line 343: Upload status update (MANUAL Stage 1 ML)
- Line 430: Master List count update (MANUAL Stage 2)
- Line 500-536: DELETE queries (Phase 1 completed)

**File**: `services/pkd-management/src/main.cpp`
- All Phase 1 WHERE clause queries (lines 1048, 2967, 3983, 4069, 4167, 4271, 4476, 4643, 4832, 4864)

---

### Category 4: Static Queries - No Parameterization Needed ✅

These queries use **no variables** (statistics, counts, schema queries):

**File**: `services/pkd-management/src/main.cpp`
- Lines 1164: `SELECT version()` (PostgreSQL version check)
- Lines 4965-5049: Statistics queries (COUNT(*), no WHERE clauses)
  - Total uploads, completed, failed
  - Total certificates, CRLs, CSCAs, DSCs, DSC_NCs
  - Validation statistics
  - Trust chain statistics
- Lines 5118-5473: Upload history, certificate detail queries with parameterized WHERE clauses (already safe)
- Line 6329: Static query

---

## Phase 2 Target Queries (Detailed)

### 1. Validation Result INSERT (Priority: HIGH)

**File**: `services/pkd-management/src/main.cpp`
**Function**: `saveValidationResult()`
**Lines**: 806-872

**Current Implementation**:
```cpp
auto escapeStr = [](const std::string& str) -> std::string {
    std::string result = str;
    size_t pos = 0;
    while ((pos = result.find("'", pos)) != std::string::npos) {
        result.replace(pos, 1, "''");
        pos += 2;
    }
    return result;
};

std::ostringstream sql;
sql << "INSERT INTO validation_result ("
    << "certificate_id, upload_id, certificate_type, country_code, "
    << "subject_dn, issuer_dn, serial_number, "
    << "validation_status, trust_chain_valid, trust_chain_message, "
    << "csca_found, csca_subject_dn, csca_fingerprint, signature_verified, signature_algorithm, "
    << "validity_check_passed, is_expired, is_not_yet_valid, not_before, not_after, "
    << "is_ca, is_self_signed, path_length_constraint, "
    << "key_usage_valid, key_usage_flags, "
    << "crl_check_status, crl_check_message, "
    << "error_code, error_message, validation_duration_ms"
    << ") VALUES ("
    << "'" << escapeStr(record.certificateId) << "', "
    << "'" << escapeStr(record.uploadId) << "', "
    // ... 28 more fields with manual escaping
    << ")";

PGresult* res = PQexec(conn, sql.str().c_str());
```

**Issues**:
- Custom `escapeStr` lambda only handles single quotes
- Doesn't handle NULL bytes, backslashes, or other special characters
- Fragile and error-prone

**Proposed Fix**:
```cpp
const char* query =
    "INSERT INTO validation_result ("
    "certificate_id, upload_id, certificate_type, country_code, "
    "subject_dn, issuer_dn, serial_number, "
    "validation_status, trust_chain_valid, trust_chain_message, "
    "csca_found, csca_subject_dn, csca_fingerprint, signature_verified, signature_algorithm, "
    "validity_check_passed, is_expired, is_not_yet_valid, not_before, not_after, "
    "is_ca, is_self_signed, path_length_constraint, "
    "key_usage_valid, key_usage_flags, "
    "crl_check_status, crl_check_message, "
    "error_code, error_message, validation_duration_ms"
    ") VALUES ("
    "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, "
    "$11, $12, $13, $14, $15, $16, $17, $18, $19, $20, "
    "$21, $22, $23, $24, $25, $26, $27, $28, $29, $30"
    ")";

// Prepare boolean strings
const std::string trustChainValidStr = record.trustChainValid ? "true" : "false";
const std::string cscaFoundStr = record.cscaFound ? "true" : "false";
const std::string signatureVerifiedStr = record.signatureVerified ? "true" : "false";
const std::string validityCheckPassedStr = record.validityCheckPassed ? "true" : "false";
const std::string isExpiredStr = record.isExpired ? "true" : "false";
const std::string isNotYetValidStr = record.isNotYetValid ? "true" : "false";
const std::string isCaStr = record.isCa ? "true" : "false";
const std::string isSelfSignedStr = record.isSelfSigned ? "true" : "false";
const std::string keyUsageValidStr = record.keyUsageValid ? "true" : "false";

// Prepare integer strings
const std::string pathLengthConstraintStr = (record.pathLengthConstraint >= 0)
    ? std::to_string(record.pathLengthConstraint) : "";
const std::string validationDurationMsStr = std::to_string(record.validationDurationMs);

// Build parameter array
const char* paramValues[30];
paramValues[0] = record.certificateId.c_str();
paramValues[1] = record.uploadId.c_str();
paramValues[2] = record.certificateType.c_str();
paramValues[3] = record.countryCode.c_str();
paramValues[4] = record.subjectDn.c_str();
paramValues[5] = record.issuerDn.c_str();
paramValues[6] = record.serialNumber.c_str();
paramValues[7] = record.validationStatus.c_str();
paramValues[8] = trustChainValidStr.c_str();
paramValues[9] = record.trustChainMessage.c_str();
paramValues[10] = cscaFoundStr.c_str();
paramValues[11] = record.cscaSubjectDn.c_str();
paramValues[12] = record.cscaFingerprint.c_str();
paramValues[13] = signatureVerifiedStr.c_str();
paramValues[14] = record.signatureAlgorithm.c_str();
paramValues[15] = validityCheckPassedStr.c_str();
paramValues[16] = isExpiredStr.c_str();
paramValues[17] = isNotYetValidStr.c_str();
paramValues[18] = record.notBefore.empty() ? nullptr : record.notBefore.c_str();
paramValues[19] = record.notAfter.empty() ? nullptr : record.notAfter.c_str();
paramValues[20] = isCaStr.c_str();
paramValues[21] = isSelfSignedStr.c_str();
paramValues[22] = (record.pathLengthConstraint >= 0) ? pathLengthConstraintStr.c_str() : nullptr;
paramValues[23] = keyUsageValidStr.c_str();
paramValues[24] = record.keyUsageFlags.c_str();
paramValues[25] = record.crlCheckStatus.c_str();
paramValues[26] = record.crlCheckMessage.c_str();
paramValues[27] = record.errorCode.c_str();
paramValues[28] = record.errorMessage.c_str();
paramValues[29] = validationDurationMsStr.c_str();

PGresult* res = PQexecParams(conn, query, 30, nullptr, paramValues,
                             nullptr, nullptr, 0);
```

**Complexity**: High (30 parameters, boolean/integer conversions, NULL handling)

---

### 2. Validation Statistics UPDATE (Priority: MEDIUM)

**File**: `services/pkd-management/src/main.cpp`
**Function**: `updateValidationStatistics()`
**Lines**: 882-899

**Current Implementation**:
```cpp
void updateValidationStatistics(PGconn* conn, const std::string& uploadId,
                                 int validCount, int invalidCount, int pendingCount, int errorCount,
                                 int trustChainValidCount, int trustChainInvalidCount, int cscaNotFoundCount,
                                 int expiredCount, int revokedCount) {
    std::ostringstream sql;
    sql << "UPDATE uploaded_file SET "
        << "validation_valid_count = " << validCount << ", "
        << "validation_invalid_count = " << invalidCount << ", "
        << "validation_pending_count = " << pendingCount << ", "
        << "validation_error_count = " << errorCount << ", "
        << "trust_chain_valid_count = " << trustChainValidCount << ", "
        << "trust_chain_invalid_count = " << trustChainInvalidCount << ", "
        << "csca_not_found_count = " << cscaNotFoundCount << ", "
        << "expired_count = " << expiredCount << ", "
        << "revoked_count = " << revokedCount
        << " WHERE id = '" << uploadId << "'";

    PGresult* res = PQexec(conn, sql.str().c_str());
```

**Issues**:
- Integer concatenation (safe from SQL injection but inconsistent with best practices)
- uploadId uses string concatenation

**Proposed Fix**:
```cpp
const char* query =
    "UPDATE uploaded_file SET "
    "validation_valid_count = $1, "
    "validation_invalid_count = $2, "
    "validation_pending_count = $3, "
    "validation_error_count = $4, "
    "trust_chain_valid_count = $5, "
    "trust_chain_invalid_count = $6, "
    "csca_not_found_count = $7, "
    "expired_count = $8, "
    "revoked_count = $9 "
    "WHERE id = $10";

// Prepare integer strings
std::string validCountStr = std::to_string(validCount);
std::string invalidCountStr = std::to_string(invalidCount);
std::string pendingCountStr = std::to_string(pendingCount);
std::string errorCountStr = std::to_string(errorCount);
std::string trustChainValidCountStr = std::to_string(trustChainValidCount);
std::string trustChainInvalidCountStr = std::to_string(trustChainInvalidCount);
std::string cscaNotFoundCountStr = std::to_string(cscaNotFoundCount);
std::string expiredCountStr = std::to_string(expiredCount);
std::string revokedCountStr = std::to_string(revokedCount);

const char* paramValues[10] = {
    validCountStr.c_str(),
    invalidCountStr.c_str(),
    pendingCountStr.c_str(),
    errorCountStr.c_str(),
    trustChainValidCountStr.c_str(),
    trustChainInvalidCountStr.c_str(),
    cscaNotFoundCountStr.c_str(),
    expiredCountStr.c_str(),
    revokedCountStr.c_str(),
    uploadId.c_str()
};

PGresult* res = PQexecParams(conn, query, 10, nullptr, paramValues,
                             nullptr, nullptr, 0);
```

**Complexity**: Medium (10 parameters, all numeric conversions)

---

### 3. LDAP Status UPDATE Queries (Priority: LOW)

**Certificate LDAP Status** (`updateCertificateLdapStatus()` - Lines 2120-2128):
```cpp
// CURRENT:
std::string query = "UPDATE certificate SET "
                   "ldap_dn = " + escapeSqlString(conn, ldapDn) + ", "
                   "stored_in_ldap = TRUE, "
                   "stored_at = NOW() "
                   "WHERE id = '" + certId + "'";

// PROPOSED:
const char* query = "UPDATE certificate SET "
                   "ldap_dn = $1, stored_in_ldap = TRUE, stored_at = NOW() "
                   "WHERE id = $2";
const char* paramValues[2] = {ldapDn.c_str(), certId.c_str()};
PGresult* res = PQexecParams(conn, query, 2, nullptr, paramValues,
                             nullptr, nullptr, 0);
```

**CRL LDAP Status** (`updateCrlLdapStatus()` - Lines 2136-2144):
- Same pattern as certificate (2 parameters)

**Master List LDAP Status** (`updateMasterListLdapStatus()` - Lines 2309-2317):
- Same pattern as certificate (2 parameters)

**Complexity**: Low (2 parameters each, simple UUID + string)

---

### 4. MANUAL Mode Processing Strategy Updates

**MANUAL Stage 1 - LDIF** (`processLdifEntries()` - Lines 320-324):
```cpp
// CURRENT:
std::string updateQuery = "UPDATE uploaded_file SET status = 'PENDING', "
                         "total_entries = " + std::to_string(entries.size()) + " "
                         "WHERE id = '" + uploadId + "'";
PGresult* res = PQexec(conn, updateQuery.c_str());

// PROPOSED:
const char* query = "UPDATE uploaded_file SET status = 'PENDING', total_entries = $1 WHERE id = $2";
std::string totalEntriesStr = std::to_string(entries.size());
const char* paramValues[2] = {totalEntriesStr.c_str(), uploadId.c_str()};
PGresult* res = PQexecParams(conn, query, 2, nullptr, paramValues,
                             nullptr, nullptr, 0);
```

**MANUAL Stage 2 - Check Query** (`validateAndSaveToDb()` - Lines 360-361):
```cpp
// CURRENT:
std::string checkQuery = "SELECT file_format, status FROM uploaded_file WHERE id = '" + uploadId + "'";
PGresult* res = PQexec(conn, checkQuery.c_str());

// PROPOSED:
const char* query = "SELECT file_format, status FROM uploaded_file WHERE id = $1";
const char* paramValues[1] = {uploadId.c_str()};
PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                             nullptr, nullptr, 0);
```

**Complexity**: Low (1-2 parameters)

---

## Implementation Plan

### Step 1: Validation Result INSERT (1 day)
**Target**: `saveValidationResult()` - main.cpp:806-872

**Tasks**:
1. Create parameterized query with 30 placeholders ($1-$30)
2. Convert all boolean fields to "true"/"false" strings
3. Convert integer fields to strings
4. Handle NULL values for optional fields (notBefore, notAfter, pathLengthConstraint)
5. Build parameter array
6. Test with existing LDIF uploads

**Testing**:
- Upload LDIF file with special characters in Subject DN
- Verify validation results stored correctly
- Check no SQL syntax errors in logs

---

### Step 2: UPDATE Queries (0.5 days)
**Targets**:
- `updateValidationStatistics()` - main.cpp:882-899
- `updateCertificateLdapStatus()` - main.cpp:2120-2128
- `updateCrlLdapStatus()` - main.cpp:2136-2144
- `updateMasterListLdapStatus()` - main.cpp:2309-2317
- MANUAL mode queries - processing_strategy.cpp:320-324, 360-361

**Tasks**:
1. Convert 6 UPDATE queries to parameterized format
2. Integer-to-string conversions where needed
3. UUID parameters for WHERE clauses

**Testing**:
- Full upload pipeline (AUTO and MANUAL modes)
- Verify statistics updates
- Check LDAP synchronization status updates

---

### Step 3: Local Testing (0.5 days)
**Test Suite**:
1. Upload LDIF with special characters (UTF-8, quotes, backslashes)
2. Upload Master List
3. Run PA verification
4. Check validation statistics
5. Verify MANUAL mode 3-stage processing
6. Check all UPDATE queries in logs

**Success Criteria**:
- All queries use `PQexecParams`
- No SQL syntax errors
- No custom escaping functions
- Validation results match expected counts

---

### Step 4: Documentation and Deployment (0.5 days)
**Tasks**:
1. Create `docs/PHASE2_SECURITY_IMPLEMENTATION.md`
2. Update `CLAUDE.md` version to v1.9.0
3. Git commit and push
4. GitHub Actions build
5. Deploy to Luckfox
6. Health check and verification

---

## Files to Modify

| File | Functions | Lines | Queries |
|------|-----------|-------|---------|
| `services/pkd-management/src/main.cpp` | `saveValidationResult`, `updateValidationStatistics`, `updateCertificateLdapStatus`, `updateCrlLdapStatus`, `updateMasterListLdapStatus` | 806-872, 882-899, 2120-2128, 2136-2144, 2309-2317 | 5 |
| `services/pkd-management/src/processing_strategy.cpp` | `processLdifEntries`, `validateAndSaveToDb` | 320-324, 360-361 | 2 |

**Total**: 2 files, 7 functions, **7 queries**

---

## Verification Strategy

### Unit Testing
```bash
# Test validation result storage with special characters
curl -X POST http://localhost:8080/api/upload/ldif \
  -F "file=@test_files/special_chars.ldif" \
  -F "mode=AUTO"

# Verify no SQL errors in logs
docker logs icao-pkd-management --tail 100 | grep -i "error\|failed"

# Check validation statistics
psql -U pkd -d localpkd -c "SELECT * FROM validation_result WHERE upload_id = '{uploadId}' LIMIT 5;"
```

### Integration Testing
```bash
# Full pipeline test
1. Upload LDIF (AUTO mode)
2. Upload Master List
3. Run PA verification with special chars in MRZ
4. Check DB-LDAP sync status
5. Verify all statistics correct
```

### SQL Query Audit
```bash
# Grep all PQexec calls (should be ZERO user-input queries)
grep -n "PQexec(conn, .*\.c_str())" services/pkd-management/src/main.cpp
grep -n "PQexec(conn, .*\.c_str())" services/pkd-management/src/processing_strategy.cpp

# Expected: Only static queries (SELECT version(), COUNT(*), etc.)
```

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Parameter count mismatch (30 params) | MEDIUM | HIGH | Compile-time check, unit tests |
| Boolean conversion errors | LOW | MEDIUM | String literals ("true"/"false") |
| NULL handling for optional fields | MEDIUM | MEDIUM | nullptr checks, test coverage |
| Performance degradation | LOW | LOW | Parameterized queries are faster (prepared) |
| Regression in existing features | LOW | MEDIUM | Full integration test suite |

---

## Success Criteria

- ✅ 100% of user-input queries use `PQexecParams`
- ✅ Zero custom escaping functions (`escapeStr`, `escapeSqlString` removed)
- ✅ All tests pass (upload, validation, PA, sync)
- ✅ No SQL syntax errors in production logs
- ✅ Performance maintained (no degradation)
- ✅ Documentation complete

---

## Estimated Timeline

| Phase | Duration | Deliverable |
|-------|----------|-------------|
| **Step 1: Validation INSERT** | 1 day | Parameterized 30-field INSERT |
| **Step 2: UPDATE Queries** | 0.5 days | 6 queries converted |
| **Step 3: Local Testing** | 0.5 days | Full test suite passed |
| **Step 4: Documentation** | 0.5 days | Deploy to Luckfox |
| **Total** | **2.5 days** | Phase 2 Complete |

**Start Date**: 2026-01-22
**Target Completion**: 2026-01-24

---

## Next Steps

1. ✅ Phase 2 analysis complete (this document)
2. ⏳ Implement validation result INSERT (Step 1)
3. ⏳ Convert UPDATE queries (Step 2)
4. ⏳ Local system testing (Step 3)
5. ⏳ Documentation and deployment (Step 4)

---

This document provides a complete roadmap for Phase 2 implementation with detailed code examples, testing strategies, and risk mitigation.
