# PKD Management Refactoring Phase 4.4 - Implementation Plan

**Date**: 2026-01-30
**Version**: v2.2.0 (planned)
**Status**: 📋 Planning
**Estimated Duration**: 3-4 days

---

## Overview

Phase 4.4 completes the Repository Pattern migration by moving async processing logic into Service layer, while simultaneously enhancing X.509 metadata extraction capabilities and optimizing performance.

### Objectives

1. **Complete Repository Pattern Migration** - Move all database operations from main.cpp to Service/Repository layers
2. **Enhance X.509 Metadata Extraction** - Improve RSA-PSS detection and EKU handling
3. **Performance Optimization** - Introduce parallel processing for large LDIF files

---

## Part 1: Async Processing Migration to Service Layer

### 1.1 Current State Analysis

**Functions in main.cpp requiring migration**:

```cpp
// Async Processing Functions (~900 lines total)
void processLdifFileAsync(uploadId, data)           // ~500 lines
void processMasterListFileAsync(uploadId, data)     // ~400 lines

// Trust Chain & Validation Functions (~300 lines)
X509* findCscaByIssuerDn(PGconn*, issuerDn)        // Line 776
std::vector<X509*> findAllCscasBySubjectDn(conn, dn) // Line 999
DscValidationResult validateDscCertificate(...)     // Line 1274
bool saveValidationResult(conn, record)             // Line 1403
void updateValidationStatistics(conn, uploadId, ...) // Line 1491

// Helper Functions (~200 lines)
bool certificateExistsByFingerprint(conn, fp)       // Line 1680
Json::Value checkDuplicateFile(conn, hash)          // Line 1698
std::string saveUploadRecord(conn, ...)             // Line 1878
void updateUploadStatus(conn, uploadId, status)     // Line 1909
std::string escapeSqlString(conn, str)              // Line 1867
```

**Total**: ~1,400 lines with 88 PostgreSQL API calls to migrate

### 1.2 Target Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Controller (main.cpp)                                       │
│  - Endpoint handlers only                                   │
│  - No SQL, no PGconn                                        │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│  UploadService                                               │
│  - processLdifAsync(uploadId, data)         (NEW)           │
│  - processMasterListAsync(uploadId, data)   (NEW)           │
│  - updateUploadStatus(uploadId, status)     (NEW)           │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│  ValidationService                                           │
│  - validateDscCertificate(dscCert, issuerDn)  (NEW)         │
│  - saveValidationResult(record)               (NEW)         │
│  - updateValidationStatistics(uploadId, ...)  (NEW)         │
│  - findCscaByIssuerDn(issuerDn)              (MOVE)        │
│  - findAllCscasBySubjectDn(subjectDn)        (MOVE)        │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│  Repositories                                                │
│  - CertificateRepository::existsByFingerprint()  (NEW)      │
│  - UploadRepository::checkDuplicateFile()        (NEW)      │
│  - UploadRepository::updateStatus()              (NEW)      │
│  - ValidationRepository::save()                  (NEW)      │
│  - ValidationRepository::updateStatistics()      (NEW)      │
└─────────────────────────────────────────────────────────────┘
```

### 1.3 Implementation Tasks

#### Task 1.1: ValidationService Enhancement (2 days)

**New Methods**:

```cpp
// validation_service.h
class ValidationService {
public:
    // Trust Chain Building (move from main.cpp)
    X509* findCscaByIssuerDn(const std::string& issuerDn);
    std::vector<X509*> findAllCscasBySubjectDn(const std::string& subjectDn);

    // DSC Validation (move from main.cpp)
    DscValidationResult validateDscCertificate(
        X509* dscCert,
        const std::string& issuerDn
    );

    // Validation Result Management (move from main.cpp)
    bool saveValidationResult(const ValidationResultRecord& record);
    void updateValidationStatistics(
        const std::string& uploadId,
        int totalValidated,
        int trustChainValid,
        int trustChainInvalid,
        int cscaNotFound,
        int expired,
        int revoked
    );
};
```

**Dependencies**:
- CertificateRepository::findCscaByIssuerDn() (already exists)
- CertificateRepository::findAllCscasBySubjectDn() (already exists)
- ValidationRepository::save() (NEW)
- ValidationRepository::updateStatistics() (NEW)

**Implementation Steps**:

1. Add ValidationRepository::save() method
   ```cpp
   bool ValidationRepository::save(const ValidationResultRecord& record) {
       // 22-parameter INSERT query (parameterized)
       // Returns: success/failure
   }
   ```

2. Add ValidationRepository::updateStatistics() method
   ```cpp
   void ValidationRepository::updateStatistics(
       const std::string& uploadId,
       const ValidationStats& stats
   ) {
       // UPDATE uploaded_file SET
       //   total_validated = $1,
       //   trust_chain_valid = $2,
       //   ...
       // WHERE id = $uploadId
   }
   ```

3. Move trust chain functions from main.cpp to ValidationService
   - Keep CSCA cache logic intact
   - Use CertificateRepository for database queries

4. Move validateDscCertificate() from main.cpp to ValidationService
   - Integrate with existing trust chain methods
   - Use ValidationRepository::save() for results

#### Task 1.2: UploadService Enhancement (2 days)

**New Methods**:

```cpp
// upload_service.h
class UploadService {
public:
    // Async Processing (move from main.cpp)
    void processLdifAsync(
        const std::string& uploadId,
        const std::vector<uint8_t>& fileData
    );

    void processMasterListAsync(
        const std::string& uploadId,
        const std::vector<uint8_t>& fileData
    );

    // Status Management (move from main.cpp)
    void updateUploadStatus(
        const std::string& uploadId,
        const std::string& status,
        const std::string& errorMessage = ""
    );
};
```

**Dependencies**:
- UploadRepository::updateStatus() (NEW)
- UploadRepository::checkDuplicateFile() (NEW)
- ValidationService (for DSC validation)
- certificate_utils::saveCertificateWithDuplicateCheck() (existing)
- ldif_processor, masterlist_processor (existing)

**Implementation Steps**:

1. Add UploadRepository::updateStatus() method
   ```cpp
   void UploadRepository::updateStatus(
       const std::string& uploadId,
       const std::string& status,
       const std::string& errorMessage
   ) {
       // UPDATE uploaded_file
       // SET status = $1, error_message = $2, updated_at = NOW()
       // WHERE id = $uploadId
   }
   ```

2. Add UploadRepository::checkDuplicateFile() method
   ```cpp
   std::optional<Upload> UploadRepository::findByFileHash(
       const std::string& fileHash
   ) {
       // SELECT * FROM uploaded_file WHERE file_hash = $1
       // Returns: Upload object or nullopt
   }
   ```

3. Move processLdifFileAsync() to UploadService::processLdifAsync()
   - Keep thread creation in main.cpp (endpoint handler)
   - Move all business logic into Service
   - Use ValidationService for DSC validation
   - Use UploadRepository::updateStatus()

4. Move processMasterListFileAsync() to UploadService::processMasterListAsync()
   - Similar pattern as processLdifAsync()
   - Use ValidationService for CSCA validation

#### Task 1.3: Helper Functions Migration (1 day)

**CertificateRepository Enhancement**:

```cpp
// certificate_repository.h
class CertificateRepository {
public:
    // NEW: Check if certificate exists by fingerprint
    bool existsByFingerprint(const std::string& fingerprint);
};
```

**UploadRepository Enhancement**:

```cpp
// upload_repository.h
class UploadRepository {
public:
    // NEW: Find upload by file hash (for deduplication)
    std::optional<Upload> findByFileHash(const std::string& fileHash);

    // NEW: Update upload status and error message
    void updateStatus(
        const std::string& uploadId,
        const std::string& status,
        const std::string& errorMessage = ""
    );
};
```

**Remove from main.cpp**:
- `escapeSqlString()` - Use parameterized queries instead
- `certificateExistsByFingerprint()` - Move to CertificateRepository
- `checkDuplicateFile()` - Move to UploadRepository
- `saveUploadRecord()` - Already in UploadRepository::insert()
- `updateUploadStatus()` - Move to UploadRepository::updateStatus()

#### Task 1.4: Endpoint Handlers Update (0.5 days)

**Update POST /api/upload/ldif**:

```cpp
// BEFORE
auto uploadResult = uploadService->uploadLdif(...);
if (uploadResult.success) {
    processLdifFileAsync(uploadResult.uploadId, contentBytes);  // main.cpp function
}

// AFTER
auto uploadResult = uploadService->uploadLdif(...);
if (uploadResult.success) {
    // Launch async processing in new thread
    std::thread([uploadService, uploadId = uploadResult.uploadId, data = contentBytes]() {
        uploadService->processLdifAsync(uploadId, data);
    }).detach();
}
```

**Update POST /api/upload/masterlist**:
- Similar pattern as /api/upload/ldif

**Benefits**:
- Thread creation still in main.cpp (lightweight)
- All business logic in Service layer
- No PGconn in main.cpp

---

## Part 2: X.509 Metadata Enhancements

### 2.1 RSA-PSS Hash Algorithm Extraction

**Problem**: 3,016 certificates (9.7%) show "unknown" for signature_hash_algorithm

**Current Limitation**:
```cpp
std::string extractSignatureHashAlgorithm(X509* cert) {
    const X509_ALGOR* sigAlg = X509_get0_tbs_sigalg(cert);

    int nid;
    int pknid;
    if (OBJ_find_sigid_algs(OBJ_obj2nid(sigAlg->algorithm), &nid, &pknid)) {
        if (nid == NID_undef) {
            return "unknown";  // RSA-PSS case
        }
        return std::string(OBJ_nid2sn(nid));
    }
    return "unknown";
}
```

**Solution**:

```cpp
std::string extractSignatureHashAlgorithm(X509* cert) {
    const X509_ALGOR* sigAlg = X509_get0_tbs_sigalg(cert);

    int nid;
    int pknid;
    if (OBJ_find_sigid_algs(OBJ_obj2nid(sigAlg->algorithm), &nid, &pknid)) {
        if (nid == NID_undef) {
            // RSA-PSS case: parse parameters
            if (pknid == NID_rsassaPss) {
                return extractRsaPssHashAlgorithm(sigAlg);  // NEW function
            }
            return "unknown";
        }
        return std::string(OBJ_nid2sn(nid));
    }
    return "unknown";
}

std::string extractRsaPssHashAlgorithm(const X509_ALGOR* sigAlg) {
    if (!sigAlg->parameter || sigAlg->parameter->type != V_ASN1_SEQUENCE) {
        return "SHA-256";  // Default for RSA-PSS per RFC 4055
    }

    const unsigned char* p = sigAlg->parameter->value.sequence->data;
    RSA_PSS_PARAMS* pss = d2i_RSA_PSS_PARAMS(
        nullptr, &p, sigAlg->parameter->value.sequence->length
    );

    if (!pss) {
        return "SHA-256";  // Default
    }

    std::string hashAlg = "SHA-256";  // Default
    if (pss->hashAlgorithm) {
        int hash_nid = OBJ_obj2nid(pss->hashAlgorithm->algorithm);
        hashAlg = std::string(OBJ_nid2sn(hash_nid));
    }

    RSA_PSS_PARAMS_free(pss);
    return hashAlg;
}
```

**Files to Modify**:
- `services/pkd-management/src/common/x509_metadata_extractor.cpp`

**Expected Impact**:
- 3,016 certificates will show correct hash algorithm (likely SHA-256, SHA-384, SHA-512)
- 100% hash algorithm coverage

### 2.2 Extended Key Usage (EKU) Optimization

**Current State**: Only 2/31,146 certificates (0.4%) have EKU

**Analysis**: CA certificates typically don't have EKU extension, which is correct per RFC 5280

**Optimization**: Add logging to track why EKU is missing

```cpp
std::vector<std::string> extractExtendedKeyUsage(X509* cert) {
    std::vector<std::string> ekuList;

    EXTENDED_KEY_USAGE* eku = (EXTENDED_KEY_USAGE*)X509_get_ext_d2i(
        cert, NID_ext_key_usage, nullptr, nullptr);

    if (!eku) {
        // Not an error for CA certificates
        // Only log if this is NOT a CA cert
        BASIC_CONSTRAINTS* bc = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(
            cert, NID_basic_constraints, nullptr, nullptr);
        bool isCA = bc && bc->ca;
        BASIC_CONSTRAINTS_free(bc);

        if (!isCA) {
            spdlog::debug("Non-CA certificate without EKU extension");
        }
        return ekuList;  // Empty list
    }

    for (int i = 0; i < sk_ASN1_OBJECT_num(eku); i++) {
        ASN1_OBJECT* obj = sk_ASN1_OBJECT_value(eku, i);
        char buf[80];
        OBJ_obj2txt(buf, sizeof(buf), obj, 0);
        ekuList.push_back(std::string(buf));
    }

    EXTENDED_KEY_USAGE_free(eku);
    return ekuList;
}
```

**Files to Modify**:
- `services/pkd-management/src/common/x509_metadata_extractor.cpp`

**Expected Impact**:
- Better logging for EKU-less certificates
- No functional change (current behavior is correct)

### 2.3 Performance Optimization - Parallel Processing

**Current Performance** (Collection 001 - 76MB LDIF):
- Processing Time: ~6 min 30 sec
- Certificate Rate: ~76 certs/sec
- Bottleneck: Sequential processing + Database INSERTs

**Target Performance**:
- Processing Time: ~2-3 minutes (2-3x speedup)
- Certificate Rate: ~200 certs/sec
- Method: Parallel processing + Batch INSERT

**Implementation Strategy**:

#### Option A: Thread Pool for Certificate Processing

```cpp
void UploadService::processLdifAsync(
    const std::string& uploadId,
    const std::vector<uint8_t>& fileData
) {
    // Parse LDIF file (sequential - fast)
    auto entries = ldif_processor::parseLdif(fileData);

    // Create thread pool (4-8 threads)
    const int numThreads = std::thread::hardware_concurrency();
    std::vector<std::thread> workers;
    std::mutex resultMutex;
    std::vector<CertificateRecord> certificates;

    // Divide entries among threads
    size_t chunkSize = entries.size() / numThreads;

    for (int i = 0; i < numThreads; i++) {
        workers.emplace_back([&, i]() {
            size_t start = i * chunkSize;
            size_t end = (i == numThreads - 1) ? entries.size() : (i + 1) * chunkSize;

            for (size_t j = start; j < end; j++) {
                // Extract certificate metadata (CPU-bound)
                auto cert = extractCertificateFromEntry(entries[j]);
                auto metadata = x509::extractMetadata(cert);

                // Store in thread-local buffer
                {
                    std::lock_guard<std::mutex> lock(resultMutex);
                    certificates.push_back({cert, metadata});
                }
            }
        });
    }

    // Wait for all threads
    for (auto& worker : workers) {
        worker.join();
    }

    // Batch INSERT to database (sequential but batched)
    certificateRepository->insertBatch(certificates);
}
```

**Pros**:
- 2-3x speedup for metadata extraction
- Minimal code changes

**Cons**:
- Thread synchronization overhead
- Batch INSERT still sequential

#### Option B: Batch INSERT Optimization

```cpp
// certificate_repository.h
class CertificateRepository {
public:
    // NEW: Batch insert certificates
    std::vector<std::string> insertBatch(
        const std::vector<CertificateRecord>& certificates
    );
};

// certificate_repository.cpp
std::vector<std::string> CertificateRepository::insertBatch(
    const std::vector<CertificateRecord>& certs
) {
    // Build multi-value INSERT query
    // INSERT INTO certificate (...) VALUES
    //   ($1, $2, ..., $27),
    //   ($28, $29, ..., $54),
    //   ...
    // RETURNING id

    const int batchSize = 100;  // 100 certs per INSERT
    std::vector<std::string> insertedIds;

    for (size_t i = 0; i < certs.size(); i += batchSize) {
        size_t end = std::min(i + batchSize, certs.size());

        // Build query for this batch
        std::string query = buildBatchInsertQuery(certs, i, end);

        // Execute with all parameters
        PGresult* res = PQexecParams(...);

        // Collect returned IDs
        for (int row = 0; row < PQntuples(res); row++) {
            insertedIds.push_back(PQgetvalue(res, row, 0));
        }

        PQclear(res);
    }

    return insertedIds;
}
```

**Pros**:
- 30-50% reduction in database round-trips
- No threading complexity

**Cons**:
- Complex query building
- Large parameter arrays

#### Recommended Approach: Hybrid (Option A + Option B)

1. **Thread Pool for Metadata Extraction** - 2x speedup
2. **Batch INSERT** - Additional 1.5x speedup
3. **Total Expected Speedup**: 3x (6.5 min → ~2 min)

**Implementation Priority**:
- Phase 1: Batch INSERT (easier, immediate benefit)
- Phase 2: Thread Pool (more complex, requires testing)

**Files to Modify**:
- `services/pkd-management/src/repositories/certificate_repository.{h,cpp}`
- `services/pkd-management/src/services/upload_service.cpp`

---

## Part 3: Implementation Schedule

### Week 1 (Days 1-3): Async Processing Migration

| Day | Task | Deliverable |
|-----|------|-------------|
| 1 | Task 1.1: ValidationService Enhancement | ValidationRepository::save(), updateStatistics() |
| 1 | Task 1.1: Move trust chain functions | findCscaByIssuerDn(), validateDscCertificate() in ValidationService |
| 2 | Task 1.2: UploadService Enhancement | UploadRepository::updateStatus(), checkDuplicateFile() |
| 2 | Task 1.2: Move processLdifAsync() | UploadService::processLdifAsync() implemented |
| 3 | Task 1.2: Move processMasterListAsync() | UploadService::processMasterListAsync() implemented |
| 3 | Task 1.3: Helper Functions Migration | CertificateRepository::existsByFingerprint() |
| 3 | Task 1.4: Update Endpoint Handlers | POST /api/upload/* updated |

### Week 2 (Day 4): X.509 Enhancements + Performance

| Day | Task | Deliverable |
|-----|------|-------------|
| 4 | Task 2.1: RSA-PSS Hash Algorithm | extractRsaPssHashAlgorithm() implemented |
| 4 | Task 2.2: EKU Logging | Enhanced logging in extractExtendedKeyUsage() |
| 4 | Task 2.3: Batch INSERT | CertificateRepository::insertBatch() implemented |

### Testing & Validation (Throughout)

- **Unit Tests**: Each new Repository/Service method
- **Integration Tests**: Upload workflows with real LDIF files
- **Performance Tests**: Before/after comparison on 76MB LDIF
- **Regression Tests**: Ensure existing functionality unchanged

---

## Part 4: Success Criteria

### 4.1 Functional Requirements

- ✅ All 88 PostgreSQL API calls removed from main.cpp
- ✅ processLdifFileAsync() fully implemented in UploadService
- ✅ processMasterListFileAsync() fully implemented in UploadService
- ✅ All trust chain functions in ValidationService
- ✅ All helper functions in appropriate Repository/Service
- ✅ Endpoints still work correctly with async processing

### 4.2 X.509 Metadata Requirements

- ✅ RSA-PSS hash algorithm extraction: 100% coverage (3,016 certs)
- ✅ Query result: `SELECT COUNT(*) FROM certificate WHERE signature_hash_algorithm = 'unknown'` → **0**
- ✅ Enhanced EKU logging without false warnings

### 4.3 Performance Requirements

- ✅ Collection 001 (76MB LDIF) processing: **< 3 minutes** (current: 6.5 min)
- ✅ Batch INSERT: **30-50% reduction** in database round-trips
- ✅ No regression in accuracy or validation quality

### 4.4 Code Quality Requirements

- ✅ Zero SQL queries in main.cpp endpoint handlers
- ✅ 100% parameterized queries in all new Repository methods
- ✅ Comprehensive error handling in all Service methods
- ✅ spdlog logging at appropriate levels (info, debug, error)
- ✅ Unit tests for all new public methods

---

## Part 5: Risk Assessment

### High Risk

**Risk**: Thread synchronization bugs in parallel processing
- **Mitigation**: Thorough testing with race condition detection tools (ThreadSanitizer)
- **Fallback**: Implement sequential version first, add threading later

**Risk**: Batch INSERT parameter limit exceeded (PostgreSQL limit: 65535 parameters)
- **Mitigation**: Limit batch size to 100 certs (27 params × 100 = 2,700 < 65,535)
- **Fallback**: Use smaller batch sizes (50 certs)

### Medium Risk

**Risk**: RSA-PSS parameter parsing failure for non-standard certificates
- **Mitigation**: Extensive testing with real-world RSA-PSS certificates
- **Fallback**: Return "SHA-256" default for parsing failures

**Risk**: Service method complexity increases testing burden
- **Mitigation**: Write unit tests incrementally as methods are added
- **Fallback**: Integration tests can cover if unit tests are delayed

### Low Risk

**Risk**: EKU logging noise
- **Mitigation**: Use debug level, only log for non-CA certs
- **Fallback**: Remove logging if too verbose

---

## Part 6: Testing Strategy

### 6.1 Unit Tests

**ValidationService Tests** (validation_service_test.cpp):
```cpp
TEST(ValidationServiceTest, ValidateDscCertificate_ValidChain) {
    // Mock CertificateRepository to return CSCA
    // Call validateDscCertificate()
    // Verify result.trustChainValid == true
}

TEST(ValidationServiceTest, SaveValidationResult_Success) {
    // Mock ValidationRepository
    // Call saveValidationResult()
    // Verify repository method called with correct params
}
```

**UploadService Tests** (upload_service_test.cpp):
```cpp
TEST(UploadServiceTest, ProcessLdifAsync_ValidFile) {
    // Mock repositories
    // Call processLdifAsync() with test LDIF
    // Verify certificates saved, validation run, status updated
}

TEST(UploadServiceTest, UpdateUploadStatus_Success) {
    // Mock UploadRepository
    // Call updateUploadStatus()
    // Verify status updated in database
}
```

**CertificateRepository Tests** (certificate_repository_test.cpp):
```cpp
TEST(CertificateRepositoryTest, InsertBatch_100Certificates) {
    // Insert 100 certificates in one batch
    // Verify all 100 IDs returned
    // Verify all 100 exist in database
}
```

### 6.2 Integration Tests

**LDIF Upload Workflow**:
```bash
# Test with small LDIF (10 DSCs)
curl -X POST http://localhost:8080/api/upload/ldif \
  -F "file=@test-10-dsc.ldif" \
  -F "uploadMode=AUTO"

# Verify:
# 1. Upload record created (status=PROCESSING → COMPLETED)
# 2. 10 certificates inserted
# 3. 10 validation results created
# 4. Trust chain validated
# 5. Statistics updated
```

**Master List Upload Workflow**:
```bash
# Test with real Master List (537 certs)
curl -X POST http://localhost:8080/api/upload/masterlist \
  -F "file=@ICAO_ml_December2025.ml" \
  -F "uploadMode=AUTO"

# Verify:
# 1. CMS parsing successful
# 2. 537 certificates extracted
# 3. MLSC + CSCA + Link Certs classified correctly
# 4. LDAP storage successful
```

### 6.3 Performance Tests

**Benchmark Setup**:
```bash
# Before optimization
time curl -X POST http://localhost:8080/api/upload/ldif \
  -F "file=@icaopkd-001-complete-009667.ldif" \
  -F "uploadMode=AUTO"
# Expected: ~6 min 30 sec

# After optimization (with batch INSERT + thread pool)
time curl -X POST http://localhost:8080/api/upload/ldif \
  -F "file=@icaopkd-001-complete-009667.ldif" \
  -F "uploadMode=AUTO"
# Target: < 3 min
```

**Metrics to Track**:
- Total processing time
- Certificates processed per second
- Database INSERT time (total)
- Metadata extraction time (total)
- Trust chain validation time (total)

### 6.4 Regression Tests

**Ensure No Breakage**:
- All existing endpoints still work
- Certificate search returns same results
- Validation results unchanged
- LDAP DN format consistent
- Database schema unchanged

---

## Part 7: Documentation Updates

### 7.1 Code Documentation

**New Files**:
- Each new Repository/Service method gets comprehensive docstring
- Example:
  ```cpp
  /**
   * @brief Validates DSC certificate against CSCA trust chain
   *
   * @param dscCert The DSC X509 certificate to validate
   * @param issuerDn The issuer DN to search for CSCA
   * @return DscValidationResult containing validation status and trust chain path
   *
   * @throws std::runtime_error if certificate parsing fails
   *
   * This method:
   * 1. Finds CSCA by issuer DN (with DN normalization)
   * 2. Builds trust chain recursively
   * 3. Validates signatures at each level
   * 4. Checks expiration and revocation (if CRL available)
   * 5. Returns detailed validation result
   */
  DscValidationResult ValidationService::validateDscCertificate(
      X509* dscCert,
      const std::string& issuerDn
  );
  ```

### 7.2 CLAUDE.md Update

**Add v2.2.0 Section**:
```markdown
### v2.2.0 (2026-02-03) - Repository Pattern 100% Complete + X.509 Enhancements

#### Phase 4.4: Async Processing Migration

- ✅ **All SQL Removed from main.cpp** - 88 PostgreSQL API calls eliminated
- ✅ **UploadService::processLdifAsync()** - Async LDIF processing in Service layer
- ✅ **UploadService::processMasterListAsync()** - Async Master List processing in Service layer
- ✅ **ValidationService Enhancement** - Trust chain validation moved to Service
- ✅ **Helper Functions Migration** - All database helpers in Repository layer

#### X.509 Metadata Enhancements

- ✅ **RSA-PSS Hash Algorithm Extraction** - 100% coverage (3,016 certs)
- ✅ **Enhanced EKU Logging** - Better diagnostics for missing EKU
- ✅ **Performance Optimization** - 3x speedup for large LDIF files
  - Batch INSERT: 30-50% reduction in database round-trips
  - Thread Pool: 2x speedup for metadata extraction
  - Result: 76MB LDIF in ~2 minutes (was 6.5 minutes)

**Files Modified**:
- services/pkd-management/src/services/upload_service.{h,cpp}
- services/pkd-management/src/services/validation_service.{h,cpp}
- services/pkd-management/src/repositories/certificate_repository.{h,cpp}
- services/pkd-management/src/repositories/upload_repository.{h,cpp}
- services/pkd-management/src/repositories/validation_repository.{h,cpp}
- services/pkd-management/src/common/x509_metadata_extractor.cpp
- services/pkd-management/src/main.cpp (removed 1,400 lines)

**Documentation**:
- [PKD_MANAGEMENT_REFACTORING_PHASE_4.4_PLAN.md](docs/PKD_MANAGEMENT_REFACTORING_PHASE_4.4_PLAN.md)
- [X509_METADATA_EXTRACTION_IMPLEMENTATION.md](docs/X509_METADATA_EXTRACTION_IMPLEMENTATION.md) (updated)
```

### 7.3 Architecture Diagram Update

Update system architecture diagram to show:
- No PGconn in main.cpp
- All async processing in Service layer
- Batch INSERT flow
- Thread pool for parallel processing

---

## Part 8: Rollback Plan

### If Issues Arise During Implementation

**Phase 1: Keep Old Code**
- Keep old functions in main.cpp as fallback (commented out)
- Add feature flag to switch between old/new implementation
  ```cpp
  const bool USE_NEW_ASYNC_PROCESSING = true;  // Feature flag

  if (USE_NEW_ASYNC_PROCESSING) {
      std::thread([uploadService, uploadId, data]() {
          uploadService->processLdifAsync(uploadId, data);
      }).detach();
  } else {
      processLdifFileAsync(uploadId, data);  // Old implementation
  }
  ```

**Phase 2: Incremental Migration**
- Migrate processLdifAsync first, test thoroughly
- Only then migrate processMasterListAsync
- Only then remove old code

**Phase 3: Performance Regression**
- If batch INSERT causes issues, revert to single INSERT
- If thread pool causes issues, use sequential processing
- Measure performance at each step

---

## Conclusion

This comprehensive plan combines:

1. **Repository Pattern Completion** - Remove all SQL from main.cpp
2. **X.509 Metadata Enhancements** - 100% hash algorithm coverage
3. **Performance Optimization** - 3x speedup for large files

**Estimated Timeline**: 4 days (3 days implementation + 1 day testing/documentation)

**Success Metrics**:
- ✅ 0 SQL queries in main.cpp
- ✅ 0 "unknown" hash algorithms
- ✅ < 3 minutes for 76MB LDIF
- ✅ 100% test coverage for new methods

**Next Steps**:
1. Review and approve this plan
2. Set up development branch
3. Begin Task 1.1 (ValidationService Enhancement)

---

**Document Version**: 1.0
**Author**: Claude Sonnet 4.5 (with kbjung)
**Status**: ✅ Ready for Implementation
