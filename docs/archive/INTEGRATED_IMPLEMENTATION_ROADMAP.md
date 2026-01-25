# ICAO Local PKD - Integrated Implementation Roadmap

**Version**: 1.0.0
**Created**: 2026-01-23
**Target Completion**: 2026-03-20 (8 weeks)
**Status**: 📋 Planning Phase

---

## Executive Summary

This document integrates two major implementation initiatives:
1. **CSCA Storage & Link Certificate Issues** (4 weeks)
2. **Security Hardening** (Already Complete - Phase 1-4)

The combined roadmap provides a comprehensive 8-week implementation plan that addresses both functional correctness (ICAO compliance) and security hardening.

---

## Quick Navigation

### Security Hardening (✅ Complete)
- [Phase 1: Critical Security Fixes](#phase-1-critical-security-fixes-complete) - v1.8.0 (Complete)
- [Phase 2: SQL Injection Prevention](#phase-2-sql-injection-prevention-complete) - v1.9.0 (Complete)
- [Phase 3: Authentication & Authorization](#phase-3-authentication--authorization-complete) - v2.0.0 (Complete)
- [Phase 4: Additional Security Hardening](#phase-4-additional-security-hardening-complete) - v2.1.0 (Complete)

### CSCA & Link Certificate (📋 Planned)
- [Week 5: LDAP Storage Fix](#week-5-ldap-storage-fix-sprint-1)
- [Week 6: Link Certificate Validation Core](#week-6-link-certificate-validation-core-sprint-2)
- [Week 7: Integration & Validation Details](#week-7-integration--validation-details-sprint-3--35)
- [Week 8: Testing & Deployment](#week-8-testing--deployment-sprint-4)

**Related Documents**:
- [CSCA_STORAGE_AND_LINK_CERT_ISSUES.md](CSCA_STORAGE_AND_LINK_CERT_ISSUES.md) - Issue Analysis
- [CSCA_ISSUES_IMPLEMENTATION_PLAN.md](CSCA_ISSUES_IMPLEMENTATION_PLAN.md) - Detailed Sprint Plan
- [VALIDATION_DETAIL_ENHANCEMENT_PLAN.md](VALIDATION_DETAIL_ENHANCEMENT_PLAN.md) - Validation UX Details
- [SECURITY_HARDENING_STATUS.md](SECURITY_HARDENING_STATUS.md) - Security Status

---

## Overall Timeline

```
Week 1-4: Security Hardening (✅ COMPLETE)
├─ Phase 1: Critical Security Fixes (v1.8.0) ✅
├─ Phase 2: SQL Injection Prevention (v1.9.0) ✅
├─ Phase 3: Authentication & Authorization (v2.0.0) ✅
└─ Phase 4: Additional Hardening (v2.1.0) ✅

Week 5: LDAP Storage Fix (Sprint 1)
├─ Day 1-2: Design & Preparation
├─ Day 3-4: Implementation
├─ Day 5-6: Migration Execution
└─ Day 7: Verification & Rollback Testing

Week 6: Link Certificate Validation Core (Sprint 2)
├─ Day 1-2: Core Algorithm Implementation
├─ Day 3-4: Unit Testing
├─ Day 5: Integration Preparation
├─ Day 6: Test Certificate Generation
└─ Day 7: Code Review & Documentation

Week 7: Integration & Validation Details (Sprint 3 + 3.5)
├─ Day 1-2: DSC Validation Integration
├─ Day 3-4: Database Schema & Performance
├─ Day 5: API & Frontend (Sprint 3)
├─ Day 5-7: Validation Detail Enhancement (Sprint 3.5 - Parallel)
│   ├─ Day 5: Database Schema (validation_result_detail)
│   ├─ Day 6: Backend (ValidationDetailRecorder)
│   └─ Day 7: Frontend (ValidationTimelineStepper)

Week 8: Testing & Deployment (Sprint 4)
├─ Day 1-2: Re-validation Campaign
├─ Day 3-4: Integration Testing
├─ Day 5-6: Production Deployment
└─ Day 7: Post-deployment Monitoring
```

---

## Security Hardening Summary (✅ Complete)

### Phase 1: Critical Security Fixes ✅

**Version**: v1.8.0
**Deployed**: 2026-01-22 00:40 (KST)
**Target**: Luckfox ARM64 (192.168.100.11)

**Completed**:
- ✅ Credential Externalization (15+ locations)
- ✅ SQL Injection - Critical DELETE Queries (4 queries)
- ✅ SQL Injection - WHERE Clauses with UUIDs (17 queries)
- ✅ File Upload Security (path, sanitization, MIME validation)
- ✅ Logging Credential Scrubbing

**Security Improvements**:
- Zero hardcoded credentials
- 21 SQL queries converted to parameterized
- Path traversal prevention
- MIME type validation for LDIF/PKCS#7

---

### Phase 2: SQL Injection Prevention ✅

**Version**: v1.9.0
**Deployed**: 2026-01-22 10:48 (KST)
**Target**: Luckfox ARM64 (192.168.100.11)

**Completed**:
- ✅ Validation Result INSERT (30 parameters)
- ✅ Validation Statistics UPDATE (10 parameters)
- ✅ LDAP Status UPDATEs (3 functions, 2 params each)
- ✅ MANUAL Mode Processing (2 queries)

**Statistics**:
- 7 queries converted (Phase 2)
- 28 total queries converted (Phase 1: 21 + Phase 2: 7)
- 100% user input queries use `PQexecParams`
- Zero custom escaping functions

---

### Phase 3: Authentication & Authorization ✅

**Version**: v2.0.0
**Deployed**: 2026-01-22 23:35 (KST)
**Target**: Local Docker (http://localhost:3000)

**Completed**:
- ✅ JWT Library Integration (jwt-cpp)
- ✅ Database Schema for Users (users, auth_audit_log tables)
- ✅ JWT Service Implementation (HS256 signing)
- ✅ Authentication Middleware (global filter)
- ✅ Permission Filter (route-level RBAC)
- ✅ Login Handler (`POST /api/auth/login`)
- ✅ Frontend Integration (Login page, route guards)
- ✅ Environment Variables (JWT_SECRET_KEY)

**Features**:
- JWT-based stateless authentication
- RBAC with granular permissions
- Public endpoint configuration
- Token expiration (1 hour default)
- Audit logging for auth events

---

### Phase 4: Additional Security Hardening ✅

**Version**: v2.1.0
**Deployed**: 2026-01-23 (KST)
**Target**: Local Docker

**Completed**:
- ✅ LDAP DN Escaping (RFC 4514, RFC 4515)
- ✅ TLS Certificate Validation for ICAO Portal
- ✅ Luckfox Network Isolation (bridge network)
- ✅ Audit Logging System
- ✅ Rate Limiting Per User

**Security Posture**:
- All 13 critical/high vulnerabilities addressed
- Complete credential externalization
- JWT authentication with RBAC
- SQL injection prevention (100% coverage)
- LDAP injection prevention
- Network segmentation

---

## CSCA & Link Certificate Implementation (📋 Planned)

### Project Goals

1. ✅ **100% DB-LDAP Consistency**: All CSCA certificates in database must exist in LDAP
2. ✅ **ICAO Compliance**: Implement ICAO Doc 9303-12 link certificate validation
3. ✅ **Trust Chain Improvement**: Increase valid DSC rate from 19.8% to > 40%
4. ✅ **No Regressions**: Existing valid certificates remain valid

### Success Metrics

| Metric | Current | Target | Measurement |
|--------|---------|--------|-------------|
| LDAP Completeness | 531/536 (99.07%) | 536/536 (100%) | DB-LDAP Sync Status |
| DSC Valid Rate | 5,868/29,610 (19.8%) | > 12,000/29,610 (40%) | Trust Chain Validation |
| LDAP Upload Conflicts | 5 (LDAP_ALREADY_EXISTS) | 0 | Upload Error Logs |
| Link Cert Support | ❌ No | ✅ Yes | Algorithm Verification |

---

## Week 5: LDAP Storage Fix (Sprint 1)

**Goal**: Resolve DB-LDAP discrepancy by implementing fingerprint-based LDAP DN structure.

### Day 1-2: Design & Preparation

#### Task 5.1: Finalize LDAP DN Schema Design

**Assignee**: Backend Team Lead
**Estimated Time**: 4 hours

**Current DN Structure**:
```
dn: cn={SUBJECT-DN}+sn={SERIAL},o={TYPE},c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

**New DN Structure**:
```
dn: cn={SHA256_FINGERPRINT},certType={TYPE},c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

**Rationale**:
- SHA-256 fingerprint guarantees uniqueness (RFC 5280 serial numbers don't)
- Resolves 5 CSCA conflicts (CN, DE, KZ duplicate serials)
- Backward compatible (existing attributes preserved)

**Deliverables**:
- [ ] Schema design document (0.5h)
- [ ] LDAP attribute mapping table (0.5h)
- [ ] Migration strategy document (1h)
- [ ] Rollback procedure (1h)
- [ ] Team review and approval (1h)

**Example Entry (New)**:
```ldif
dn: cn=72b3f2a05a3ec5e8ff9c8a07b634cd4b1c3f7d45ef70cf5b3aece09befd09fc0,certType=csca,c=CN,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: top
objectClass: pkdDownload
cn: 72b3f2a05a3ec5e8ff9c8a07b634cd4b1c3f7d45ef70cf5b3aece09befd09fc0
certType: csca
c: CN
userCertificate;binary:: MIIFZjCCBE6gAwIBAgIIQ05E...
serialNumber: 434E445343410005
subjectDN: C=CN, O=中华人民共和国, OU=出入境管理局, CN=CSCA
issuerDN: C=CN, O=中华人民共和国, OU=出入境管理局, CN=CSCA
```

#### Task 5.2: Create Backup & Rollback Scripts

**Assignee**: DevOps Engineer
**Estimated Time**: 3 hours

**Files to Create**:
- `scripts/ldap-backup.sh` (1h)
- `scripts/ldap-rollback.sh` (1h)
- `scripts/ldap-verify-migration.sh` (1h)

**Backup Script Requirements**:
```bash
#!/bin/bash
# ldap-backup.sh - Full LDAP backup before migration

BACKUP_DIR="./backups/ldap-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP_DIR"

# Backup all PKD entries
ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "$LDAP_ADMIN_PASSWORD" \
  -b "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  > "$BACKUP_DIR/pkd-full.ldif"

# Backup only CSCA entries
ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "$LDAP_ADMIN_PASSWORD" \
  -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(certType=csca)" \
  > "$BACKUP_DIR/csca-only.ldif"

echo "Backup completed: $BACKUP_DIR"
```

**Deliverables**:
- [ ] Backup script created (1h)
- [ ] Rollback script created (1h)
- [ ] Verification script created (1h)

---

### Day 3-4: Implementation

#### Task 5.3: Implement Fingerprint-Based DN Construction

**Assignee**: Backend Developer
**Estimated Time**: 6 hours

**File**: `services/pkd-management/src/main.cpp`

**Current Code** (Line 2007-2015):
```cpp
std::string dn = "cn=" + subjDn + "+sn=" + serialNum +
                 ",o=" + certType + ",c=" + countryCode +
                 ",dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
```

**New Code**:
```cpp
// Calculate SHA-256 fingerprint
std::string calculateFingerprint(X509* cert) {
    unsigned char md[SHA256_DIGEST_LENGTH];
    unsigned int len = 0;

    unsigned char* derData = nullptr;
    int derLen = i2d_X509(cert, &derData);

    SHA256(derData, derLen, md);
    OPENSSL_free(derData);

    // Convert to hex string
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)md[i];
    }
    return ss.str();
}

// Build DN with fingerprint
std::string buildLdapDn(X509* cert, const std::string& certType, const std::string& countryCode) {
    std::string fingerprint = calculateFingerprint(cert);

    std::stringstream dn;
    dn << "cn=" << fingerprint
       << ",certType=" << certType
       << ",c=" << countryCode
       << ",dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

    return dn.str();
}
```

**Integration Points**:
- Line 2007: Certificate upload to LDAP
- Line 3810: Master List processing
- Line 3922: CRL upload to LDAP

**Deliverables**:
- [ ] `calculateFingerprint()` function (2h)
- [ ] `buildLdapDn()` function (1h)
- [ ] Integration into 3 upload functions (2h)
- [ ] Unit tests (1h)

---

### Day 5-6: Migration Execution

#### Task 5.4: Execute LDAP Data Migration

**Assignee**: Database Administrator
**Estimated Time**: 8 hours

**Migration Steps**:

1. **Backup** (30 minutes):
   ```bash
   ./scripts/ldap-backup.sh
   ```

2. **Stop Services** (10 minutes):
   ```bash
   docker compose -f docker/docker-compose.yaml stop pkd-management
   ```

3. **Export Existing Data** (1 hour):
   ```bash
   # Export all 531 existing CSCA entries
   ldapsearch -x -H ldap://localhost:389 \
     -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
     -w "$LDAP_ADMIN_PASSWORD" \
     -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
     "(certType=csca)" \
     > /tmp/csca-old-dn.ldif
   ```

4. **Transform DNs** (2 hours):
   ```python
   #!/usr/bin/env python3
   # scripts/transform-ldap-dn.py

   import hashlib
   import base64
   from ldif import LDIFParser, LDIFWriter

   class DNTransformer(LDIFParser):
       def __init__(self, input_file, output_file):
           LDIFParser.__init__(self, input_file)
           self.writer = LDIFWriter(output_file)

       def handle(self, dn, entry):
           # Extract certificate binary
           cert_binary = base64.b64decode(entry['userCertificate;binary'][0])

           # Calculate SHA-256 fingerprint
           fingerprint = hashlib.sha256(cert_binary).hexdigest()

           # Extract certType and country from old DN
           cert_type = self.extract_cert_type(dn)
           country = self.extract_country(dn)

           # Build new DN
           new_dn = f"cn={fingerprint},certType={cert_type},c={country},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"

           # Update cn attribute
           entry['cn'] = [fingerprint]

           # Write transformed entry
           self.writer.unparse(new_dn, entry)

   if __name__ == '__main__':
       with open('/tmp/csca-old-dn.ldif', 'rb') as input_file:
           with open('/tmp/csca-new-dn.ldif', 'wb') as output_file:
               parser = DNTransformer(input_file, output_file)
               parser.parse()
   ```

5. **Delete Old Entries** (1 hour):
   ```bash
   # Delete all 531 old CSCA entries
   ldapdelete -x -H ldap://localhost:389 \
     -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
     -w "$LDAP_ADMIN_PASSWORD" \
     -f /tmp/csca-old-dn-list.txt
   ```

6. **Import New Entries** (1 hour):
   ```bash
   # Import 536 CSCA entries with new DN structure
   ldapadd -x -H ldap://localhost:389 \
     -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
     -w "$LDAP_ADMIN_PASSWORD" \
     -f /tmp/csca-new-dn.ldif
   ```

7. **Verify** (30 minutes):
   ```bash
   ./scripts/ldap-verify-migration.sh
   ```

8. **Restart Services** (10 minutes):
   ```bash
   docker compose -f docker/docker-compose.yaml start pkd-management
   ```

**Deliverables**:
- [ ] Backup completed (30m)
- [ ] Data exported (1h)
- [ ] DNs transformed (2h)
- [ ] Old entries deleted (1h)
- [ ] New entries imported (1h)
- [ ] Migration verified (30m)
- [ ] Services restarted (10m)

**Rollback Procedure**:
```bash
#!/bin/bash
# If migration fails, rollback to backup

BACKUP_DIR="./backups/ldap-20260123-120000"

# Stop services
docker compose -f docker/docker-compose.yaml stop pkd-management

# Delete new entries
ldapdelete -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "$LDAP_ADMIN_PASSWORD" \
  -r "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"

# Restore from backup
ldapadd -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "$LDAP_ADMIN_PASSWORD" \
  -f "$BACKUP_DIR/pkd-full.ldif"

# Restart services
docker compose -f docker/docker-compose.yaml start pkd-management

echo "Rollback completed"
```

---

### Day 7: Verification & Rollback Testing

#### Task 5.5: Comprehensive Verification

**Assignee**: QA Engineer
**Estimated Time**: 6 hours

**Verification Checklist**:

1. **LDAP Entry Count** (30m):
   ```bash
   # Before: 531 CSCAs
   # After: 536 CSCAs (including 5 previously conflicting)

   ldapsearch -x -H ldap://localhost:389 \
     -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
     -w "$LDAP_ADMIN_PASSWORD" \
     -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
     "(certType=csca)" | grep -c "^dn:"
   ```

2. **Duplicate Serial Number Resolution** (1h):
   ```bash
   # Verify all 5 conflicting certificates are now in LDAP

   # CN serial 434E445343410005 (2 certs)
   ldapsearch -x -H ldap://localhost:389 \
     -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
     -w "$LDAP_ADMIN_PASSWORD" \
     -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
     "(serialNumber=434E445343410005)" | grep -c "^dn:"
   # Expected: 2

   # DE serial 4445433430303031 (2 certs)
   # KZ serial 4B5A434143 (1 cert)
   # Total: 5 certificates now present
   ```

3. **DB-LDAP Consistency** (1h):
   ```bash
   # All certificates in DB must exist in LDAP

   curl http://localhost:8080/api/sync/status | jq '.discrepancies'
   # Expected: 0 (was 5)
   ```

4. **Upload Test** (1h):
   ```bash
   # Upload new certificate with conflicting serial
   # Should succeed (fingerprint is unique)

   curl -X POST http://localhost:8080/api/upload/ldif \
     -F "file=@test-data/csca-duplicate-serial.ldif"
   # Expected: success
   ```

5. **Certificate Search** (30m):
   ```bash
   # Search by fingerprint (new DN)
   curl "http://localhost:8080/api/certificates/search?fingerprint=72b3f2a0..."
   # Expected: Found

   # Search by serial (attribute still present)
   curl "http://localhost:8080/api/certificates/search?serial=434E445343410005"
   # Expected: 2 certificates
   ```

6. **Performance Test** (1h):
   ```bash
   # Measure LDAP query performance
   time ldapsearch -x -H ldap://localhost:389 \
     -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
     -w "$LDAP_ADMIN_PASSWORD" \
     -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
     "(certType=csca)"
   # Expected: < 500ms (similar to before)
   ```

7. **Rollback Test** (1h):
   ```bash
   # Test rollback procedure on staging
   ./scripts/ldap-rollback.sh "$BACKUP_DIR"

   # Verify rollback success
   ldapsearch ... | grep -c "^dn:"
   # Expected: 531 (original count)
   ```

**Deliverables**:
- [ ] Entry count verified (30m)
- [ ] Duplicate resolution verified (1h)
- [ ] DB-LDAP consistency verified (1h)
- [ ] Upload test passed (1h)
- [ ] Certificate search working (30m)
- [ ] Performance acceptable (1h)
- [ ] Rollback tested (1h)

---

### Sprint 1 Deliverables

- ✅ **LDAP DN Schema**: Fingerprint-based DN structure implemented
- ✅ **Migration Completed**: 536/536 CSCAs in LDAP (100%)
- ✅ **Conflict Resolution**: 5 duplicate serial number issues resolved
- ✅ **Backup & Rollback**: Tested and verified
- ✅ **Performance**: No degradation (< 500ms queries)

---

## Week 6: Link Certificate Validation Core (Sprint 2)

**Goal**: Implement trust chain building algorithm with link certificate support.

### Day 1-2: Core Algorithm Implementation

#### Task 6.1: Implement findAllCscasBySubjectDn()

**Assignee**: Backend Developer
**Estimated Time**: 4 hours

**File**: `services/pkd-management/src/main.cpp`

**Current Code** (Line 637-730):
```cpp
X509* findCscaByIssuerDn(PGconn* conn, const std::string& issuerDn) {
    const char* query =
        "SELECT certificate_data FROM certificate "
        "WHERE certificate_type = 'CSCA' "
        "AND subject_dn ILIKE $1 "
        "LIMIT 1";  // ❌ Only returns one CSCA
    // ...
}
```

**New Code**:
```cpp
std::vector<X509*> findAllCscasBySubjectDn(PGconn* conn, const std::string& subjectDn) {
    std::vector<X509*> cscas;

    const char* query =
        "SELECT certificate_data, subject_dn, serial_number, fingerprint_sha256 "
        "FROM certificate "
        "WHERE certificate_type = 'CSCA' "
        "AND subject_dn ILIKE $1 "
        "ORDER BY not_after DESC";  // ✅ Returns all matching CSCAs, newest first

    const char* paramValues[1] = {subjectDn.c_str()};
    PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::error("findAllCscasBySubjectDn query failed: {}", PQerrorMessage(conn));
        PQclear(res);
        return cscas;
    }

    int numRows = PQntuples(res);
    spdlog::info("Found {} CSCA(s) for subject DN: {}", numRows, subjectDn);

    for (int i = 0; i < numRows; i++) {
        // Extract bytea data
        size_t dataLen = 0;
        const char* certDataHex = PQgetvalue(res, i, 0);
        unsigned char* certData = PQunescapeBytea(
            reinterpret_cast<const unsigned char*>(certDataHex), &dataLen);

        if (!certData) {
            spdlog::error("Failed to unescape bytea for CSCA");
            continue;
        }

        // Parse DER certificate
        const unsigned char* p = certData;
        X509* cert = d2i_X509(nullptr, &p, dataLen);
        PQfreemem(certData);

        if (!cert) {
            spdlog::error("Failed to parse CSCA certificate");
            continue;
        }

        // Log certificate info
        std::string fingerprint = PQgetvalue(res, i, 3);
        std::string serial = PQgetvalue(res, i, 2);
        spdlog::debug("  CSCA {}: serial={}, fingerprint={}", i+1, serial, fingerprint);

        cscas.push_back(cert);
    }

    PQclear(res);
    return cscas;
}
```

**Key Changes**:
- Removed `LIMIT 1` → Returns all matching CSCAs
- Added `ORDER BY not_after DESC` → Prioritize newer certificates
- Added logging for each CSCA found
- Returns `std::vector<X509*>` instead of single `X509*`

**Deliverables**:
- [ ] Function implemented (3h)
- [ ] Logging added (30m)
- [ ] Unit tests (30m)

---

#### Task 6.2: Implement buildTrustChain()

**Assignee**: Backend Developer
**Estimated Time**: 8 hours

**File**: `services/pkd-management/src/trust_chain/chain_builder.h` (new)

**Data Structure**:
```cpp
struct TrustChain {
    std::vector<X509*> certificates;  // DSC → CSCA_old → Link → CSCA_new
    bool isValid;
    std::string path;                 // Human-readable: "DSC → CN=CSCA_OLD → CN=Link → CN=CSCA_NEW"
    std::string errorMessage;
    int depth;

    TrustChain() : isValid(false), depth(0) {}
};
```

**Algorithm**:
```cpp
TrustChain buildTrustChain(X509* targetCert,
                           const std::vector<X509*>& allCscas,
                           int maxDepth = 5) {
    TrustChain chain;
    std::set<std::string> visited;  // Track fingerprints to detect circular refs

    // Step 1: Start with target certificate (DSC)
    chain.certificates.push_back(targetCert);
    chain.path = getCertSubject(targetCert);
    visited.insert(calculateFingerprint(targetCert));

    X509* currentCert = targetCert;

    // Step 2: Iteratively build chain
    while (chain.depth < maxDepth) {
        // Get issuer DN of current certificate
        std::string issuerDn = getCertIssuer(currentCert);

        // Find issuer certificate in CSCA pool
        X509* issuerCert = findMatchingCsca(issuerDn, allCscas, visited);

        if (!issuerCert) {
            // Chain broken - issuer not found
            chain.errorMessage = "Chain broken: Issuer not found for " + issuerDn;
            chain.isValid = false;
            return chain;
        }

        // Check for circular reference
        std::string issuerFingerprint = calculateFingerprint(issuerCert);
        if (visited.count(issuerFingerprint) > 0) {
            chain.errorMessage = "Circular reference detected at depth " + std::to_string(chain.depth);
            chain.isValid = false;
            return chain;
        }

        // Add issuer to chain
        chain.certificates.push_back(issuerCert);
        chain.path += " → " + getCertSubject(issuerCert);
        visited.insert(issuerFingerprint);
        chain.depth++;

        // Check if we reached self-signed root
        if (isSelfSigned(issuerCert)) {
            spdlog::info("Reached self-signed root CSCA at depth {}", chain.depth);
            chain.isValid = true;
            return chain;
        }

        // Move to next level
        currentCert = issuerCert;
    }

    // Max depth exceeded
    chain.errorMessage = "Max chain depth exceeded (" + std::to_string(maxDepth) + ")";
    chain.isValid = false;
    return chain;
}

// Helper: Find matching CSCA by issuer DN
X509* findMatchingCsca(const std::string& issuerDn,
                       const std::vector<X509*>& allCscas,
                       const std::set<std::string>& visited) {
    for (X509* csca : allCscas) {
        std::string cscaSubjectDn = getCertSubject(csca);

        // Case-insensitive DN comparison
        if (strcasecmp(cscaSubjectDn.c_str(), issuerDn.c_str()) == 0) {
            std::string fingerprint = calculateFingerprint(csca);

            // Skip if already visited
            if (visited.count(fingerprint) > 0) {
                continue;
            }

            return csca;
        }
    }

    return nullptr;
}

// Helper: Check if certificate is self-signed
bool isSelfSigned(X509* cert) {
    std::string subjectDn = getCertSubject(cert);
    std::string issuerDn = getCertIssuer(cert);

    return (strcasecmp(subjectDn.c_str(), issuerDn.c_str()) == 0);
}
```

**Test Scenarios**:

1. **Direct Chain** (DSC → Self-signed CSCA):
   ```
   DSC → CN=CSCA_SELF
   Depth: 1, Valid: true
   ```

2. **Link Certificate Chain** (DSC → CSCA_old → Link → CSCA_new):
   ```
   DSC → CN=CSCA_OLD → CN=CSCA_NEW (Link) → CN=CSCA_NEW (Self-signed)
   Depth: 3, Valid: true
   ```

3. **Broken Chain** (DSC → Missing CSCA):
   ```
   DSC → (not found)
   Depth: 0, Valid: false, Error: "Chain broken"
   ```

4. **Circular Reference** (Malicious):
   ```
   A → B → A (circular)
   Depth: 2, Valid: false, Error: "Circular reference"
   ```

**Deliverables**:
- [ ] TrustChain struct defined (1h)
- [ ] buildTrustChain() implemented (4h)
- [ ] Helper functions (isSelfSigned, findMatchingCsca) (2h)
- [ ] Test scenarios (1h)

---

#### Task 6.3: Implement validateTrustChain()

**Assignee**: Backend Developer
**Estimated Time**: 4 hours

**File**: `services/pkd-management/src/trust_chain/chain_validator.cpp` (new)

**Algorithm**:
```cpp
bool validateTrustChain(const TrustChain& chain) {
    if (!chain.isValid) {
        spdlog::warn("Chain is already marked invalid: {}", chain.errorMessage);
        return false;
    }

    if (chain.certificates.size() < 2) {
        spdlog::error("Chain too short (need at least 2 certs)");
        return false;
    }

    // Verify each certificate signature with its issuer
    for (size_t i = 0; i < chain.certificates.size() - 1; i++) {
        X509* cert = chain.certificates[i];
        X509* issuer = chain.certificates[i + 1];

        // Extract issuer public key
        EVP_PKEY* issuerPubKey = X509_get_pubkey(issuer);
        if (!issuerPubKey) {
            spdlog::error("Failed to extract public key from issuer at depth {}", i+1);
            return false;
        }

        // Verify signature
        int verifyResult = X509_verify(cert, issuerPubKey);
        EVP_PKEY_free(issuerPubKey);

        if (verifyResult != 1) {
            spdlog::error("Signature verification failed at depth {}: {} → {}",
                          i, getCertSubject(cert), getCertSubject(issuer));
            return false;
        }

        spdlog::debug("✓ Signature valid: {} signed by {}",
                      getCertSubject(cert), getCertSubject(issuer));
    }

    // Verify root is self-signed
    X509* root = chain.certificates.back();
    if (!isSelfSigned(root)) {
        spdlog::error("Root certificate is not self-signed: {}", getCertSubject(root));
        return false;
    }

    // Verify root signature
    EVP_PKEY* rootPubKey = X509_get_pubkey(root);
    int rootVerifyResult = X509_verify(root, rootPubKey);
    EVP_PKEY_free(rootPubKey);

    if (rootVerifyResult != 1) {
        spdlog::error("Root self-signature verification failed");
        return false;
    }

    spdlog::info("✓ Trust chain fully validated ({} certificates)", chain.certificates.size());
    return true;
}
```

**Validation Steps**:
1. Check chain is marked valid (from buildTrustChain)
2. Verify chain has at least 2 certificates
3. For each certificate-issuer pair:
   - Extract issuer public key
   - Verify certificate signature: `X509_verify(cert, issuer_pubkey)`
4. Verify root is self-signed
5. Verify root self-signature

**Deliverables**:
- [ ] validateTrustChain() implemented (3h)
- [ ] Signature verification logic (1h)

---

### Day 3-4: Unit Testing

#### Task 6.4: Create Unit Tests

**Assignee**: Backend Developer
**Estimated Time**: 8 hours

**File**: `services/pkd-management/tests/trust_chain_test.cpp`

**Test Framework**: Google Test (gtest)

**Test Cases**:

```cpp
#include <gtest/gtest.h>
#include "trust_chain/chain_builder.h"
#include "trust_chain/chain_validator.h"

class TrustChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Load test certificates
        dscDirect = loadTestCertificate("test/data/dsc_direct.crt");
        cscaSelfSigned = loadTestCertificate("test/data/csca_self_signed.crt");

        dscWithLink = loadTestCertificate("test/data/dsc_with_link.crt");
        cscaOld = loadTestCertificate("test/data/csca_old.crt");
        linkCert = loadTestCertificate("test/data/link_cert.crt");
        cscaNew = loadTestCertificate("test/data/csca_new.crt");
    }

    void TearDown() override {
        // Cleanup
        if (dscDirect) X509_free(dscDirect);
        if (cscaSelfSigned) X509_free(cscaSelfSigned);
        // ...
    }

    X509* dscDirect;
    X509* cscaSelfSigned;
    X509* dscWithLink;
    X509* cscaOld;
    X509* linkCert;
    X509* cscaNew;
};

TEST_F(TrustChainTest, BuildChain_DirectToSelfSigned) {
    // Setup: DSC → Self-signed CSCA
    std::vector<X509*> cscas = {cscaSelfSigned};

    // Execute
    TrustChain chain = buildTrustChain(dscDirect, cscas);

    // Verify
    ASSERT_TRUE(chain.isValid);
    ASSERT_EQ(chain.certificates.size(), 2);  // DSC + CSCA
    ASSERT_EQ(chain.depth, 1);
    EXPECT_THAT(chain.path, HasSubstr("DSC → CN=CSCA_SELF"));
}

TEST_F(TrustChainTest, BuildChain_WithLinkCertificate) {
    // Setup: DSC → CSCA_old → Link → CSCA_new
    std::vector<X509*> cscas = {cscaOld, linkCert, cscaNew};

    // Execute
    TrustChain chain = buildTrustChain(dscWithLink, cscas);

    // Verify
    ASSERT_TRUE(chain.isValid);
    ASSERT_EQ(chain.certificates.size(), 4);  // DSC + CSCA_old + Link + CSCA_new
    ASSERT_EQ(chain.depth, 3);
    EXPECT_THAT(chain.path, HasSubstr("DSC → CN=CSCA_OLD → CN=Link → CN=CSCA_NEW"));
}

TEST_F(TrustChainTest, BuildChain_BrokenChain) {
    // Setup: DSC → (missing CSCA)
    std::vector<X509*> cscas = {};  // Empty list

    // Execute
    TrustChain chain = buildTrustChain(dscDirect, cscas);

    // Verify
    ASSERT_FALSE(chain.isValid);
    ASSERT_EQ(chain.certificates.size(), 1);  // Only DSC
    EXPECT_THAT(chain.errorMessage, HasSubstr("Chain broken"));
}

TEST_F(TrustChainTest, BuildChain_CircularReference) {
    // Setup: Create circular chain (A → B → A)
    X509* certA = createCircularCertificate("A", "B");
    X509* certB = createCircularCertificate("B", "A");
    std::vector<X509*> cscas = {certA, certB};

    // Execute
    TrustChain chain = buildTrustChain(certA, cscas);

    // Verify
    ASSERT_FALSE(chain.isValid);
    EXPECT_THAT(chain.errorMessage, HasSubstr("Circular reference"));

    // Cleanup
    X509_free(certA);
    X509_free(certB);
}

TEST_F(TrustChainTest, ValidateChain_ValidSignatures) {
    // Setup: Build valid chain
    std::vector<X509*> cscas = {cscaSelfSigned};
    TrustChain chain = buildTrustChain(dscDirect, cscas);

    // Execute
    bool isValid = validateTrustChain(chain);

    // Verify
    ASSERT_TRUE(isValid);
}

TEST_F(TrustChainTest, ValidateChain_InvalidSignature) {
    // Setup: Build chain with mismatched keys
    TrustChain chain;
    chain.isValid = true;
    chain.certificates = {dscDirect, cscaOld};  // Wrong issuer

    // Execute
    bool isValid = validateTrustChain(chain);

    // Verify
    ASSERT_FALSE(isValid);
}

TEST_F(TrustChainTest, ValidateChain_LinkCertificateChain) {
    // Setup: Build link certificate chain
    std::vector<X509*> cscas = {cscaOld, linkCert, cscaNew};
    TrustChain chain = buildTrustChain(dscWithLink, cscas);

    // Execute
    bool isValid = validateTrustChain(chain);

    // Verify
    ASSERT_TRUE(isValid);
    ASSERT_EQ(chain.certificates.size(), 4);
}
```

**Deliverables**:
- [ ] Test suite setup (1h)
- [ ] 7 test cases implemented (5h)
- [ ] Test data preparation (1h)
- [ ] CI/CD integration (1h)

---

### Day 5-6: Test Certificate Generation

#### Task 6.5: Generate Test Certificates with OpenSSL

**Assignee**: DevOps Engineer
**Estimated Time**: 6 hours

**File**: `test/data/generate_test_certificates.sh`

**Script**:
```bash
#!/bin/bash
# Generate test certificates for trust chain validation

set -e

# Create directory structure
mkdir -p test/data
cd test/data

# 1. Self-signed CSCA
echo "=== Generating Self-signed CSCA ==="
openssl req -x509 -newkey rsa:2048 -keyout csca_self_signed.key \
        -out csca_self_signed.crt \
        -days 3650 -nodes \
        -subj "/C=TEST/O=TestCountry/CN=CSCA_SELF"

# 2. Self-signed CSCA_old (for key rollover)
echo "=== Generating CSCA_OLD ==="
openssl req -x509 -newkey rsa:2048 -keyout csca_old.key \
        -out csca_old.crt \
        -days 3650 -nodes \
        -subj "/C=TEST/O=TestCountry/CN=CSCA_OLD"

# 3. Self-signed CSCA_new (for key rollover)
echo "=== Generating CSCA_NEW ==="
openssl req -x509 -newkey rsa:2048 -keyout csca_new.key \
        -out csca_new.crt \
        -days 3650 -nodes \
        -subj "/C=TEST/O=TestCountry/CN=CSCA_NEW"

# 4. Link certificate (CSCA_new public key signed by CSCA_old private key)
echo "=== Generating Link Certificate ==="
openssl req -new -key csca_new.key \
        -out link_cert.csr \
        -subj "/C=TEST/O=TestCountry/CN=CSCA_NEW"

openssl x509 -req -in link_cert.csr \
        -CA csca_old.crt \
        -CAkey csca_old.key \
        -out link_cert.crt \
        -days 365 -CAcreateserial

# 5. DSC signed by self-signed CSCA (direct chain)
echo "=== Generating DSC (Direct Chain) ==="
openssl req -newkey rsa:2048 -keyout dsc_direct.key \
        -out dsc_direct.csr \
        -nodes -subj "/C=TEST/O=TestCountry/CN=DSC_DIRECT"

openssl x509 -req -in dsc_direct.csr \
        -CA csca_self_signed.crt \
        -CAkey csca_self_signed.key \
        -out dsc_direct.crt \
        -days 1095 -CAcreateserial

# 6. DSC signed by CSCA_old (for link chain)
echo "=== Generating DSC (Link Chain) ==="
openssl req -newkey rsa:2048 -keyout dsc_with_link.key \
        -out dsc_with_link.csr \
        -nodes -subj "/C=TEST/O=TestCountry/CN=DSC_WITH_LINK"

openssl x509 -req -in dsc_with_link.csr \
        -CA csca_old.crt \
        -CAkey csca_old.key \
        -out dsc_with_link.crt \
        -days 1095 -CAcreateserial

# 7. Orphan DSC (no issuer CSCA available)
echo "=== Generating Orphan DSC ==="
openssl req -x509 -newkey rsa:2048 -keyout orphan_issuer.key \
        -out orphan_issuer.crt \
        -days 1 -nodes \
        -subj "/C=TEST/O=TestCountry/CN=ORPHAN_ISSUER"

openssl req -newkey rsa:2048 -keyout dsc_orphan.key \
        -out dsc_orphan.csr \
        -nodes -subj "/C=TEST/O=TestCountry/CN=DSC_ORPHAN"

openssl x509 -req -in dsc_orphan.csr \
        -CA orphan_issuer.crt \
        -CAkey orphan_issuer.key \
        -out dsc_orphan.crt \
        -days 1095 -CAcreateserial

# Delete orphan issuer (to simulate broken chain)
rm orphan_issuer.crt orphan_issuer.key

# Cleanup CSR files
rm *.csr

echo "=== Certificate generation complete ==="
ls -lh *.crt *.key
```

**Verification**:
```bash
# Verify certificate chain
openssl verify -CAfile csca_self_signed.crt dsc_direct.crt
# Expected: dsc_direct.crt: OK

openssl verify -CAfile csca_new.crt -untrusted link_cert.crt -untrusted csca_old.crt dsc_with_link.crt
# Expected: dsc_with_link.crt: OK

# Display certificate info
openssl x509 -in dsc_direct.crt -noout -text | grep -A 2 "Subject:"
openssl x509 -in dsc_direct.crt -noout -text | grep -A 2 "Issuer:"
```

**Deliverables**:
- [ ] Test certificate generation script (3h)
- [ ] 7 test certificates created (included)
- [ ] Verification script (1h)
- [ ] Documentation (1h)

---

### Day 7: Code Review & Documentation

#### Task 6.6: Code Review

**Assignee**: Senior Backend Developer
**Estimated Time**: 4 hours

**Review Checklist**:
- [ ] Algorithm correctness (circular reference detection, max depth)
- [ ] Memory management (X509* ownership, cleanup)
- [ ] Error handling (all failure paths covered)
- [ ] Logging (debug/info/error levels appropriate)
- [ ] Performance (O(n) complexity, no unnecessary copies)
- [ ] Code style (naming conventions, comments)

**Deliverables**:
- [ ] Code review completed (3h)
- [ ] Issues resolved (1h)

---

#### Task 6.7: Documentation

**Assignee**: Technical Writer
**Estimated Time**: 4 hours

**Documents to Create**:
1. **Algorithm Design Document** (2h):
   - Trust chain building algorithm explanation
   - Flowchart (DSC → CSCA_old → Link → CSCA_new)
   - Edge cases (circular refs, broken chains, max depth)

2. **API Documentation** (1h):
   - `findAllCscasBySubjectDn()` function signature and usage
   - `buildTrustChain()` parameters and return values
   - `validateTrustChain()` validation rules

3. **Testing Guide** (1h):
   - How to run unit tests
   - Test certificate generation
   - Manual testing procedures

**Deliverables**:
- [ ] Algorithm design doc (2h)
- [ ] API documentation (1h)
- [ ] Testing guide (1h)

---

### Sprint 2 Deliverables

- ✅ **Core Algorithm**: buildTrustChain() with circular reference detection
- ✅ **Validation**: validateTrustChain() with signature verification
- ✅ **Multi-CSCA Support**: findAllCscasBySubjectDn() returns all matches
- ✅ **Unit Tests**: 7 test cases with 90%+ coverage
- ✅ **Test Certificates**: 7 OpenSSL-generated certificates
- ✅ **Documentation**: Algorithm design, API docs, testing guide

---

## Week 7: Integration & Validation Details (Sprint 3 + 3.5)

**Goal**: Integrate trust chain validation into DSC validation and add detailed validation recording.

### Sprint 3 (Day 1-5): Integration

See [CSCA_ISSUES_IMPLEMENTATION_PLAN.md](CSCA_ISSUES_IMPLEMENTATION_PLAN.md#sprint-3-link-certificate-validation---integration-week-3) for detailed tasks.

**Key Tasks**:
- Day 1-2: Refactor `validateDscCertificate()` to use `buildTrustChain()`
- Day 3: Database schema update (`trust_chain_path` column)
- Day 4: Performance optimization (CSCA caching)
- Day 5: API updates and frontend trust chain visualization

---

### Sprint 3.5 (Day 5-7): Validation Detail Enhancement (Parallel)

See [VALIDATION_DETAIL_ENHANCEMENT_PLAN.md](VALIDATION_DETAIL_ENHANCEMENT_PLAN.md) for detailed implementation.

**Key Tasks**:
- Day 5: Database schema (`validation_result_detail`, `validation_error_codes` tables)
- Day 6: Backend (`ValidationDetailRecorder` class integration)
- Day 7: Frontend (`ValidationTimelineStepper` component)

**Sprint 3.5 runs in parallel with Sprint 3 Day 5-7**.

---

## Week 8: Testing & Deployment (Sprint 4)

**Goal**: Comprehensive testing, re-validation campaign, and production deployment.

See [CSCA_ISSUES_IMPLEMENTATION_PLAN.md](CSCA_ISSUES_IMPLEMENTATION_PLAN.md#sprint-4-testing--deployment-week-4) for detailed tasks.

### Deployment Strategy

**CRITICAL**: Luckfox ARM64 배포는 Local System에서 완전히 검증된 후에만 실행합니다.

```
Development Flow:
1. Local Development (Docker Desktop / WSL2)
   ├─ Code implementation
   ├─ Unit testing
   ├─ Integration testing
   └─ Feature verification

2. Local System Testing (http://localhost:3000)
   ├─ Full feature testing
   ├─ Performance benchmarking
   ├─ Security verification
   ├─ Migration scripts testing
   └─ ✅ COMPLETE VERIFICATION

3. Production Deployment (Luckfox ARM64)
   ├─ Prerequisites: Local testing 100% complete
   ├─ GitHub Actions ARM64 build
   ├─ Artifact download and deployment
   └─ Production monitoring
```

**Deployment Checkpoints**:
- [ ] Local system: All tests passing
- [ ] Local system: Performance acceptable (< 15ms/DSC)
- [ ] Local system: Migration scripts verified
- [ ] Local system: Rollback procedure tested
- [ ] **ONLY THEN** → Proceed to Luckfox production

**Key Tasks**:
- Day 1-2: Trigger full DSC re-validation (29,610 certificates) - **Local System**
- Day 3-4: Integration testing (all scenarios) - **Local System**
- Day 5: Production-ready verification - **Local System**
- Day 6: Luckfox ARM64 deployment - **Production**
- Day 7: Post-deployment monitoring - **Production**

**Expected Results**:
- LDAP Completeness: 531/536 → 536/536 (100%)
- DSC Valid Rate: 5,868/29,610 (19.8%) → > 12,000/29,610 (40%)
- LDAP Upload Conflicts: 5 → 0
- Link Certificate Support: ❌ → ✅

---

## Risk Assessment & Mitigation

### High-Priority Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| **LDAP Migration Failure** | Critical | Medium | Backup/rollback tested, staged migration |
| **Trust Chain Algorithm Bug** | High | Medium | Extensive unit tests, code review |
| **Performance Degradation** | Medium | Low | Caching strategy, benchmarking |
| **Production Downtime** | High | Low | Blue-green deployment, quick rollback |

### Risk Mitigation Strategies

1. **LDAP Migration**:
   - Full backup before migration
   - Tested rollback procedure
   - Staged migration (staging → production)
   - Verification script at each step

2. **Trust Chain Algorithm**:
   - 90%+ unit test coverage
   - Test with real-world certificates
   - Peer code review
   - Gradual rollout (sample certificates first)

3. **Performance**:
   - CSCA caching (in-memory map)
   - Benchmarking with 29,610 DSCs
   - Load testing (concurrent requests)
   - Monitoring (< 15ms per DSC target)

4. **Production Deployment**:
   - Blue-green deployment strategy
   - Database migration scripts tested
   - Quick rollback procedure documented
   - 24-hour post-deployment monitoring

---

## Success Criteria

### Functional Requirements

- ✅ All 536 CSCA certificates exist in LDAP (100%)
- ✅ Trust chain validation supports link certificates
- ✅ DSC valid rate > 40% (from 19.8%)
- ✅ No LDAP upload conflicts (fingerprint-based DN)
- ✅ Validation details stored and displayed to users

### Non-Functional Requirements

- ✅ Performance: < 15ms per DSC validation
- ✅ Availability: < 30 minutes downtime during migration
- ✅ Security: No regressions (Phase 1-4 complete)
- ✅ Reliability: 99.9% validation success rate
- ✅ Usability: Detailed validation steps visible to users

### Deliverables

- ✅ Production-ready code (all sprints)
- ✅ Comprehensive documentation (3 documents)
- ✅ Test suite (90%+ coverage)
- ✅ Migration scripts (backup/rollback)
- ✅ Deployment guide (step-by-step)

---

## Project Team & Responsibilities

| Role | Responsibility | Availability |
|------|----------------|--------------|
| **Backend Team Lead** | Sprint planning, architecture decisions | 100% |
| **Backend Developer** | Core algorithm, integration, testing | 100% |
| **Database Administrator** | LDAP migration, schema updates | 50% |
| **DevOps Engineer** | CI/CD, deployment automation | 50% |
| **QA Engineer** | Testing, verification | 100% |
| **Frontend Developer** | Validation UI, trust chain visualization | 50% |
| **Technical Writer** | Documentation | 30% |

---

## Communication Plan

### Daily Standups (15 minutes)

- **Time**: 10:00 AM (KST)
- **Format**:
  - What did you do yesterday?
  - What will you do today?
  - Any blockers?

### Sprint Reviews (1 hour)

- **Frequency**: End of each sprint (Week 5, 6, 7, 8)
- **Participants**: All team members
- **Agenda**:
  - Demo completed features
  - Review sprint metrics
  - Plan next sprint

### Incident Response

- **Critical Issues**: Slack #icao-pkd-critical (immediate response)
- **Non-Critical Issues**: JIRA tickets (response within 24h)
- **Production Outage**: Phone tree escalation

---

## Monitoring & Metrics

### Key Performance Indicators (KPIs)

| Metric | Current | Target | Measurement |
|--------|---------|--------|-------------|
| LDAP Completeness | 99.07% | 100% | DB-LDAP sync status |
| DSC Valid Rate | 19.8% | > 40% | Validation statistics |
| LDAP Conflicts | 5 | 0 | Upload error logs |
| Validation Time | ~10ms | < 15ms | Performance profiling |

### Monitoring Dashboards

1. **Validation Statistics**:
   - Total DSCs: 29,610
   - Valid: 5,868 → Target: > 12,000
   - CSCA Not Found: 6,299 → Target: < 5,000
   - Invalid Signature: Monitored

2. **LDAP Sync Status**:
   - DB Count: 536 CSCAs
   - LDAP Count: 531 → Target: 536
   - Discrepancies: 5 → Target: 0

3. **Performance Metrics**:
   - Avg validation time: ~10ms
   - P95 validation time: Target < 15ms
   - P99 validation time: Target < 20ms

---

## Change Log

| Date | Version | Changes |
|------|---------|---------|
| 2026-01-23 | 1.0.0 | Initial integrated roadmap created |

---

## Appendix

### A. Related Documents

1. **Issue Analysis**:
   - [CSCA_STORAGE_AND_LINK_CERT_ISSUES.md](CSCA_STORAGE_AND_LINK_CERT_ISSUES.md) - 33,000+ words
   - Problem description, root cause analysis, resolution strategy

2. **Implementation Plans**:
   - [CSCA_ISSUES_IMPLEMENTATION_PLAN.md](CSCA_ISSUES_IMPLEMENTATION_PLAN.md) - 25,000+ words
   - Sprint 1-4 detailed task breakdown
   - [VALIDATION_DETAIL_ENHANCEMENT_PLAN.md](VALIDATION_DETAIL_ENHANCEMENT_PLAN.md) - 30,000+ words
   - Sprint 3.5 validation UX details

3. **Security Hardening**:
   - [SECURITY_HARDENING_STATUS.md](SECURITY_HARDENING_STATUS.md) - Phase 1-4 complete
   - [PHASE1_SECURITY_IMPLEMENTATION.md](PHASE1_SECURITY_IMPLEMENTATION.md) - v1.8.0
   - [PHASE2_SECURITY_IMPLEMENTATION.md](PHASE2_SECURITY_IMPLEMENTATION.md) - v1.9.0

### B. Reference Standards

- **ICAO Doc 9303 Part 11**: Security Mechanisms for MRTDs
- **ICAO Doc 9303 Part 12**: PKI for MRTDs (Section 7.1.2 - Link Certificates)
- **RFC 5280**: X.509 PKI Certificate and CRL Profile
- **RFC 4514**: LDAP Distinguished Name (DN) Format
- **RFC 4515**: LDAP Search Filter Format

### C. Glossary

- **CSCA**: Country Signing Certificate Authority (root CA)
- **DSC**: Document Signer Certificate (issued by CSCA)
- **Link Certificate**: Cross-signed certificate during CSCA key rollover
- **Trust Chain**: Path from DSC to self-signed root CSCA
- **Fingerprint**: SHA-256 hash of certificate binary (DER)
- **DN**: Distinguished Name (LDAP entity identifier)

---

**End of Integrated Implementation Roadmap**
