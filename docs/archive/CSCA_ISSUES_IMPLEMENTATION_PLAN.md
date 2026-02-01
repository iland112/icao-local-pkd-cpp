# CSCA Storage & Link Certificate Issues - Implementation Plan

**Plan Version**: 1.0.0
**Created**: 2026-01-23
**Target Completion**: 2026-02-20 (4 weeks)
**Status**: 📋 Planning Phase

---

## Quick Navigation

- [Sprint 1: LDAP Storage Fix](#sprint-1-ldap-storage-fix-week-1)
- [Sprint 2: Link Certificate Validation - Core](#sprint-2-link-certificate-validation---core-week-2)
- [Sprint 3: Link Certificate Validation - Integration](#sprint-3-link-certificate-validation---integration-week-3)
- [Sprint 3.5: Validation Detail Enhancement](#sprint-35-validation-detail-enhancement-3-days-parallel)
- [Sprint 4: Testing & Deployment](#sprint-4-testing--deployment-week-4)

**Related Documents**:
- [CSCA_STORAGE_AND_LINK_CERT_ISSUES.md](CSCA_STORAGE_AND_LINK_CERT_ISSUES.md) - Issue Analysis
- [VALIDATION_DETAIL_ENHANCEMENT_PLAN.md](VALIDATION_DETAIL_ENHANCEMENT_PLAN.md) - Validation UX Details

---

## Project Goals

### Primary Objectives

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

## Sprint 1: LDAP Storage Fix (Week 1)

**Goal**: Resolve DB-LDAP discrepancy by implementing fingerprint-based LDAP DN structure.

### Day 1-2: Design & Preparation

#### Task 1.1: Finalize LDAP DN Schema Design

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

#### Task 1.2: Create Backup & Rollback Scripts

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

BACKUP_DIR="./backups/ldap_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BACKUP_DIR"

# Backup PKD subtree
ldapsearch -x -H ldap://localhost:389 \
           -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
           -w "$LDAP_PASSWORD" \
           -b "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
           > "$BACKUP_DIR/pkd_data.ldif"

# Count entries
ENTRY_COUNT=$(grep -c "^dn:" "$BACKUP_DIR/pkd_data.ldif")
echo "Backup complete: $ENTRY_COUNT entries"
echo "$ENTRY_COUNT" > "$BACKUP_DIR/entry_count.txt"
```

**Deliverables**:
- [ ] Backup script with verification (1h)
- [ ] Rollback script with safety checks (1h)
- [ ] Verification script (LDAP count vs DB count) (1h)

#### Task 1.3: Local Development Environment Setup

**Assignee**: All Developers
**Estimated Time**: 2 hours

**Setup Steps**:
- [ ] Pull latest main branch
- [ ] Create feature branch: `feature/ldap-dn-fingerprint-migration`
- [ ] Verify local LDAP has test data (0.5h)
- [ ] Run backup script on local LDAP (0.5h)
- [ ] Verify PostgreSQL has 536 CSCAs (0.5h)
- [ ] Document local setup in team wiki (0.5h)

### Day 3-4: Code Implementation

#### Task 1.4: Modify LDAP DN Construction Logic

**Assignee**: Backend Developer
**Estimated Time**: 6 hours

**Files to Modify**:
1. [services/pkd-management/src/main.cpp](services/pkd-management/src/main.cpp#L2007-L2015) (3h)
2. [services/pkd-management/src/repositories/ldap_certificate_repository.cpp](services/pkd-management/src/repositories/ldap_certificate_repository.cpp) (2h)
3. Unit tests (1h)

**Implementation Details**:

**File 1: main.cpp - uploadCertificateToLdap() function**

Location: [main.cpp:2007-2015](services/pkd-management/src/main.cpp#L2007-L2015)

```cpp
// BEFORE (OLD CODE):
std::string dn = "cn=" + subjectDn + "+sn=" + serialNumber + ",o=" + certType +
                 ",c=" + countryCode + ",dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

// AFTER (NEW CODE):
// Use fingerprint as primary identifier in DN
std::string fingerprintHex = fingerprintSha256;  // Already available in function scope
std::string dn = "cn=" + fingerprintHex + ",certType=" + certType +
                 ",c=" + countryCode + ",dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

// Add certType and serialNumber as attributes (not in DN)
LDAPMod* mod_certType = new LDAPMod;
mod_certType->mod_op = LDAP_MOD_ADD;
mod_certType->mod_type = const_cast<char*>("certType");
char* certTypeVals[] = {const_cast<char*>(certType.c_str()), nullptr};
mod_certType->mod_values = certTypeVals;

LDAPMod* mod_serial = new LDAPMod;
mod_serial->mod_op = LDAP_MOD_ADD;
mod_serial->mod_type = const_cast<char*>("serialNumber");
char* serialVals[] = {const_cast<char*>(serialNumber.c_str()), nullptr};
mod_serial->mod_values = serialVals;

// Add to mods array
LDAPMod* mods[] = {
    mod_objectClass,
    mod_cn,
    mod_certType,      // NEW
    mod_c,
    mod_cert,
    mod_serial,        // NEW (moved from DN to attribute)
    mod_subjectDn,
    mod_issuerDn,
    nullptr
};
```

**Verification**:
- [ ] Compile successfully (no syntax errors)
- [ ] Unit test: Upload CSCA with duplicate serial → Both stored in LDAP
- [ ] Unit test: LDAP DN uniqueness (different fingerprints → different DNs)

**File 2: ldap_certificate_repository.cpp - Search Logic**

Location: [ldap_certificate_repository.cpp:160-200](services/pkd-management/src/repositories/ldap_certificate_repository.cpp#L160-L200)

```cpp
// BEFORE (OLD CODE):
std::string filter = "(objectClass=pkdDownload)";
if (criteria.certType.has_value()) {
    filter = "(&(objectClass=pkdDownload)(o=" + certTypeToString(*criteria.certType) + "))";
}

// AFTER (NEW CODE):
std::string filter = "(objectClass=pkdDownload)";
if (criteria.certType.has_value()) {
    // certType is now an attribute, not part of DN
    filter = "(&(objectClass=pkdDownload)(certType=" + certTypeToString(*criteria.certType) + "))";
}

// Also update text search filter
if (!criteria.searchText.empty()) {
    // OLD: Search in cn (which was subject DN)
    // NEW: Search in subjectDN attribute and serialNumber
    filter = "(&" + filter + "(|(subjectDN=*" + criteria.searchText + "*)(serialNumber=*" + criteria.searchText + "*)))";
}
```

**Verification**:
- [ ] Certificate search by country: Returns all certificates
- [ ] Certificate search by type: Filters correctly
- [ ] Certificate search by text: Finds by subject DN and serial number

#### Task 1.5: Update OpenLDAP Schema (if needed)

**Assignee**: DevOps Engineer
**Estimated Time**: 2 hours

**Check if LDAP schema allows new attributes**:
- `certType` attribute (may need to add to pkdDownload objectClass)
- `serialNumber` attribute (standard attribute, should exist)

**If schema update needed**:

File: `openldap/schemas/icao-pkd.ldif`

```ldif
# Add certType attribute definition
attributetype ( 1.3.6.1.4.1.99999.1.1.5
    NAME 'certType'
    DESC 'Certificate type: CSCA, DSC, DSC_NC, CRL, ML'
    EQUALITY caseIgnoreMatch
    SUBSTR caseIgnoreSubstringsMatch
    SYNTAX 1.3.6.1.4.1.1466.115.121.1.15
    SINGLE-VALUE )

# Update pkdDownload objectClass
objectclass ( 1.3.6.1.4.1.99999.1.2.1
    NAME 'pkdDownload'
    DESC 'ICAO PKD Certificate Container'
    SUP top
    STRUCTURAL
    MUST ( cn $ c )
    MAY ( userCertificate $ cACertificate $ certificateRevocationList $
          subjectDN $ issuerDN $ serialNumber $ certType ) )
```

**Deliverables**:
- [ ] Schema verification (check if update needed) (0.5h)
- [ ] Schema LDIF file (if needed) (0.5h)
- [ ] Apply schema to local LDAP (0.5h)
- [ ] Test schema with ldapadd (0.5h)

#### Task 1.6: Update Frontend Certificate Search

**Assignee**: Frontend Developer
**Estimated Time**: 2 hours

**Files to Modify**:
- [frontend/src/pages/CertificateSearch.tsx](frontend/src/pages/CertificateSearch.tsx) (1h)
- [frontend/src/api/pkdApi.ts](frontend/src/api/pkdApi.ts) (0.5h)
- Integration testing (0.5h)

**Changes**:
- Ensure search by serial number still works (now searches serialNumber attribute)
- Update DN display (if any UI shows DN)
- Test search filters

**No major UI changes expected** (API contract remains the same).

### Day 5: Data Migration

#### Task 1.7: Execute LDAP Migration (Local Environment)

**Assignee**: Backend Developer + DevOps Engineer
**Estimated Time**: 4 hours

**Migration Steps**:

**Step 1: Backup** (0.5h)
```bash
./scripts/ldap-backup.sh
# Verify backup: backups/ldap_20260123_140000/pkd_data.ldif
# Entry count: 531 entries
```

**Step 2: Stop Application** (0.1h)
```bash
docker compose -f docker/docker-compose.yaml stop pkd-management
```

**Step 3: Delete Old LDAP Entries** (0.5h)
```bash
# Delete all entries under dc=pkd
ldapdelete -x -r -H ldap://localhost:389 \
           -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
           -w "$LDAP_PASSWORD" \
           "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"

# Verify deletion
ldapsearch -x -H ldap://localhost:389 \
           -b "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
           | grep -c "^dn:"
# Expected: 0
```

**Step 4: Rebuild LDAP from PostgreSQL** (2h)
```bash
# Start updated application
docker compose -f docker/docker-compose.yaml up -d pkd-management

# Trigger full sync API endpoint (to be implemented)
curl -X POST http://localhost:8081/api/internal/sync-db-to-ldap \
     -H "Content-Type: application/json" \
     -d '{"force": true}'

# Monitor logs
docker logs -f icao-pkd-management
```

**Step 5: Verify Migration** (1h)
```bash
# Count LDAP entries
ldapsearch -x -H ldap://localhost:389 \
           -b "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
           | grep -c "^dn:"
# Expected: 536 (including 5 previously missing)

# Verify specific missing certificates now exist
ldapsearch -x -H ldap://localhost:389 \
           -b "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
           "(cn=72b3f2a05a3ec5e8ff9c8a07b634cd4b1c3f7d45ef70cf5b3aece09befd09fc0)"
# Expected: 1 entry (China CSCA 434E445343410005, notBefore 2015-03-28)

ldapsearch -x -H ldap://localhost:389 \
           -b "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
           "(cn=e3dbd84925fb24fb50ba7cc8db71b90a8bd46f64efe2ce2aff6cde6ea4fcf52f)"
# Expected: 1 entry (China CSCA 434E445343410005, notBefore 2015-04-27)
```

**Rollback Plan** (if migration fails):
```bash
# Stop application
docker compose stop pkd-management

# Restore LDAP from backup
ldapdelete -x -r "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"
ldapadd -x -D "cn=admin,..." -w "$LDAP_PASSWORD" \
        -f backups/ldap_20260123_140000/pkd_data.ldif

# Revert code changes (git revert)
git revert HEAD
docker compose up -d --build pkd-management
```

**Deliverables**:
- [ ] Migration executed successfully (2h)
- [ ] All 536 CSCAs in LDAP (verification) (1h)
- [ ] Backup verified (can restore) (0.5h)
- [ ] Migration log documented (0.5h)

### Day 6-7: Testing & Verification

#### Task 1.8: Integration Testing

**Assignee**: QA Engineer
**Estimated Time**: 8 hours

**Test Suite 1: Certificate Upload** (2h)
- [ ] Upload LDIF with duplicate serial numbers → Both stored
- [ ] Upload same certificate twice → Idempotent (no duplicate)
- [ ] Verify LDAP DN format (fingerprint-based)
- [ ] Verify `stored_in_ldap=true` flag accurate

**Test Suite 2: Certificate Search** (2h)
- [ ] Search by country: Returns all certificates
- [ ] Search by certificate type: Filters correctly
- [ ] Search by serial number: Finds correct certificates
- [ ] Search by subject DN: Finds correct certificates

**Test Suite 3: Certificate Export** (2h)
- [ ] Export China (CN) certificates → ZIP contains all 34 CSCAs
- [ ] Export Germany (DE) certificates → ZIP contains all 13 CSCAs
- [ ] Verify previously missing certificates in ZIP

**Test Suite 4: DB-LDAP Sync** (2h)
- [ ] Sync Status API: Reports 0 discrepancies
- [ ] Auto Reconcile: No LDAP_ALREADY_EXISTS errors
- [ ] Manual sync trigger: No errors in logs

**Deliverables**:
- [ ] Test report with pass/fail status (2h)
- [ ] Bug reports (if any failures) (included)
- [ ] Performance metrics (search/export times) (included)

#### Task 1.9: Code Review & Documentation

**Assignee**: Team Lead
**Estimated Time**: 4 hours

**Code Review Checklist**:
- [ ] DN construction logic correct (1h)
- [ ] Error handling for LDAP operations (0.5h)
- [ ] Memory leaks (X509* freed correctly) (0.5h)
- [ ] Unit test coverage > 80% (0.5h)

**Documentation**:
- [ ] Update CLAUDE.md with new LDAP structure (0.5h)
- [ ] Update API documentation (OpenAPI spec) (0.5h)
- [ ] Migration guide for production deployment (0.5h)

**Deliverables**:
- [ ] Code review feedback (2h)
- [ ] Documentation updates (1h)
- [ ] Approval for merge to main (0.5h)

### Sprint 1 Deliverables

- ✅ **New LDAP DN Structure**: Fingerprint-based, unique for all certificates
- ✅ **5 Missing CSCAs Stored**: All 536 CSCAs in LDAP
- ✅ **Migration Scripts**: Backup, rollback, verification
- ✅ **Test Suite**: 100% pass rate
- ✅ **Documentation**: Updated architecture docs

---

## Sprint 2: Link Certificate Validation - Core (Week 2)

**Goal**: Implement core link certificate validation algorithm (chain building and validation).

### Day 1-2: Multi-CSCA Lookup & Identification

#### Task 2.1: Implement Multi-CSCA Query Function

**Assignee**: Backend Developer
**Estimated Time**: 4 hours

**Files to Modify**:
- [services/pkd-management/src/main.cpp](services/pkd-management/src/main.cpp#L637-L730) (3h)
- Unit tests (1h)

**Implementation**:

```cpp
// NEW FUNCTION (add after findCscaByIssuerDn)
/**
 * @brief Find ALL CSCA certificates matching subject DN
 * @param conn PostgreSQL connection
 * @param subjectDn Subject DN to search for
 * @return Vector of X509* pointers (caller must free ALL)
 */
std::vector<X509*> findAllCscasBySubjectDn(PGconn* conn, const std::string& subjectDn) {
    std::vector<X509*> result;

    if (!conn || subjectDn.empty()) {
        return result;
    }

    // Escape DN for SQL (prevent SQL injection)
    std::string escapedDn = subjectDn;
    size_t pos = 0;
    while ((pos = escapedDn.find("'", pos)) != std::string::npos) {
        escapedDn.replace(pos, 1, "''");
        pos += 2;
    }

    // Query ALL CSCAs with matching subject DN (no LIMIT clause)
    std::string query = "SELECT certificate_binary FROM certificate "
                        "WHERE certificate_type = 'CSCA' "
                        "AND LOWER(subject_dn) = LOWER('" + escapedDn + "') "
                        "ORDER BY created_at ASC";  // Older certificates first

    PGresult* res = PQexec(conn, query.c_str());

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::error("Failed to query CSCAs: {}", PQerrorMessage(conn));
        PQclear(res);
        return result;
    }

    int rowCount = PQntuples(res);
    spdlog::debug("Found {} CSCA(s) for subject DN: {}", rowCount, subjectDn.substr(0, 50));

    for (int i = 0; i < rowCount; i++) {
        if (PQgetisnull(res, i, 0)) {
            continue;
        }

        size_t dataLen = 0;
        unsigned char* binData = PQunescapeBytea(
            reinterpret_cast<const unsigned char*>(PQgetvalue(res, i, 0)),
            &dataLen
        );

        if (!binData || dataLen == 0) {
            spdlog::error("Failed to unescape bytea for CSCA {}", i);
            continue;
        }

        const unsigned char* p = binData;
        X509* cert = d2i_X509(nullptr, &p, dataLen);
        PQfreemem(binData);

        if (cert) {
            result.push_back(cert);
            spdlog::debug("Loaded CSCA {} from database", i);
        } else {
            spdlog::error("Failed to parse certificate for CSCA {}", i);
        }
    }

    PQclear(res);
    return result;
}
```

**Unit Test**:
```cpp
TEST(CscaLookupTest, FindAllCscasBySubjectDn_MultipleCerts) {
    // Setup: Insert 2 CSCAs with same subject DN (different fingerprints)
    // Expected: Returns vector with 2 X509* pointers
    auto cscas = findAllCscasBySubjectDn(conn, "C=CN, O=Test, CN=CSCA");
    ASSERT_EQ(cscas.size(), 2);

    // Cleanup
    for (X509* cert : cscas) X509_free(cert);
}
```

**Deliverables**:
- [ ] Function implemented and tested (3h)
- [ ] Unit tests pass (1h)
- [ ] Code review (included in Task 2.5)

#### Task 2.2: Implement Link Certificate Identification

**Assignee**: Backend Developer
**Estimated Time**: 3 hours

**Files to Modify**:
- [services/pkd-management/src/main.cpp](services/pkd-management/src/main.cpp) (2h)
- Unit tests (1h)

**Implementation**:

```cpp
// NEW HELPER FUNCTIONS (add to anonymous namespace or utility section)

/**
 * @brief Check if certificate is self-signed
 * @param cert X509 certificate
 * @return true if subject DN == issuer DN
 */
bool isSelfSigned(X509* cert) {
    if (!cert) return false;

    X509_NAME* subject = X509_get_subject_name(cert);
    X509_NAME* issuer = X509_get_issuer_name(cert);

    if (!subject || !issuer) return false;

    return (X509_NAME_cmp(subject, issuer) == 0);
}

/**
 * @brief Check if certificate is a link certificate (cross-signed)
 * @param cert X509 certificate
 * @return true if subject DN != issuer DN (not self-signed)
 */
bool isLinkCertificate(X509* cert) {
    return !isSelfSigned(cert);
}

/**
 * @brief Get human-readable subject DN from certificate
 * @param cert X509 certificate
 * @return Subject DN as string
 */
std::string getCertSubjectDn(X509* cert) {
    if (!cert) return "";

    char buffer[512];
    X509_NAME* subject = X509_get_subject_name(cert);
    X509_NAME_oneline(subject, buffer, sizeof(buffer));

    return std::string(buffer);
}

/**
 * @brief Get human-readable issuer DN from certificate
 * @param cert X509 certificate
 * @return Issuer DN as string
 */
std::string getCertIssuerDn(X509* cert) {
    if (!cert) return "";

    char buffer[512];
    X509_NAME* issuer = X509_get_issuer_name(cert);
    X509_NAME_oneline(issuer, buffer, sizeof(buffer));

    return std::string(buffer);
}
```

**Unit Test**:
```cpp
TEST(LinkCertTest, IsSelfSigned_SelfSignedCsca) {
    // Load self-signed CSCA
    X509* csca = loadTestCertificate("test/data/csca_self_signed.crt");
    ASSERT_TRUE(isSelfSigned(csca));
    ASSERT_FALSE(isLinkCertificate(csca));
    X509_free(csca);
}

TEST(LinkCertTest, IsLinkCertificate_CrossSigned) {
    // Load link certificate (subject != issuer)
    X509* link = loadTestCertificate("test/data/link_cert.crt");
    ASSERT_FALSE(isSelfSigned(link));
    ASSERT_TRUE(isLinkCertificate(link));
    X509_free(link);
}
```

**Test Data Creation** (OpenSSL commands):
```bash
# Create self-signed CSCA
openssl req -x509 -newkey rsa:2048 -keyout csca_old.key \
        -out test/data/csca_self_signed.crt \
        -days 3650 -nodes \
        -subj "/C=TEST/O=TestCountry/CN=CSCA_OLD"

# Create link certificate (cross-signed)
# (Details in Task 2.3)
```

**Deliverables**:
- [ ] Utility functions implemented (2h)
- [ ] Unit tests pass (1h)

### Day 3-4: Trust Chain Building

#### Task 2.3: Implement Chain Building Algorithm

**Assignee**: Senior Backend Developer
**Estimated Time**: 8 hours

**Files to Modify**:
- [services/pkd-management/src/main.cpp](services/pkd-management/src/main.cpp) (6h)
- Unit tests (2h)

**Implementation**:

```cpp
// NEW STRUCT: Trust Chain
struct TrustChain {
    std::vector<X509*> certificates;  // DSC → CSCA_old → Link → CSCA_new
    bool isValid;
    std::string path;  // Human-readable: "DSC → CN=CSCA_old → CN=Link → CN=CSCA_new"
    std::string errorMessage;
};

/**
 * @brief Build trust chain from DSC to root CSCA
 * @param dscCert DSC certificate to validate
 * @param allCscas Vector of all available CSCA certificates
 * @param maxDepth Maximum chain depth (default: 5)
 * @return TrustChain structure with certificates and validity status
 */
TrustChain buildTrustChain(X509* dscCert,
                           const std::vector<X509*>& allCscas,
                           int maxDepth = 5) {
    TrustChain chain;
    chain.isValid = false;

    if (!dscCert) {
        chain.errorMessage = "DSC certificate is null";
        return chain;
    }

    // Step 1: Add DSC as first certificate in chain
    chain.certificates.push_back(dscCert);

    // Step 2: Build chain iteratively
    X509* current = dscCert;
    std::set<std::string> visitedDns;  // Prevent circular references
    int depth = 0;

    while (depth < maxDepth) {
        depth++;

        // Get issuer DN of current certificate
        std::string currentIssuerDn = getCertIssuerDn(current);

        if (currentIssuerDn.empty()) {
            chain.errorMessage = "Failed to extract issuer DN";
            return chain;
        }

        // Prevent circular references
        if (visitedDns.count(currentIssuerDn) > 0) {
            chain.errorMessage = "Circular reference detected at depth " + std::to_string(depth);
            spdlog::error("Chain building: {}", chain.errorMessage);
            return chain;
        }
        visitedDns.insert(currentIssuerDn);

        // Check if current certificate is self-signed (root)
        if (isSelfSigned(current)) {
            chain.isValid = true;
            spdlog::info("Chain building: Reached root CSCA at depth {}", depth);
            break;
        }

        // Find issuer certificate in CSCA list
        X509* issuer = nullptr;
        for (X509* csca : allCscas) {
            std::string cscaSubjectDn = getCertSubjectDn(csca);

            // Case-insensitive DN comparison (RFC 4517)
            if (strcasecmp(currentIssuerDn.c_str(), cscaSubjectDn.c_str()) == 0) {
                issuer = csca;
                spdlog::debug("Chain building: Found issuer at depth {}: {}",
                              depth, cscaSubjectDn.substr(0, 50));
                break;
            }
        }

        if (!issuer) {
            chain.errorMessage = "Chain broken: Issuer not found at depth " +
                                 std::to_string(depth) + " (issuer: " +
                                 currentIssuerDn.substr(0, 80) + ")";
            spdlog::warn("Chain building: {}", chain.errorMessage);
            return chain;
        }

        // Add issuer to chain
        chain.certificates.push_back(issuer);
        current = issuer;
    }

    if (depth >= maxDepth) {
        chain.errorMessage = "Maximum chain depth exceeded (" + std::to_string(maxDepth) + ")";
        chain.isValid = false;
        return chain;
    }

    // Step 3: Build human-readable path
    chain.path = "DSC";
    for (size_t i = 1; i < chain.certificates.size(); i++) {
        std::string subjectDn = getCertSubjectDn(chain.certificates[i]);
        // Extract CN from DN for readability
        size_t cnPos = subjectDn.find("CN=");
        std::string cnPart = (cnPos != std::string::npos)
                             ? subjectDn.substr(cnPos, 30)
                             : subjectDn.substr(0, 30);
        chain.path += " → " + cnPart;
    }

    return chain;
}
```

**Unit Test Scenarios**:

```cpp
TEST(TrustChainTest, BuildChain_DirectChain) {
    // Setup: DSC → CSCA (self-signed)
    X509* dsc = loadTestCertificate("test/data/dsc_direct.crt");
    X509* csca = loadTestCertificate("test/data/csca_self_signed.crt");
    std::vector<X509*> cscas = {csca};

    // Execute
    TrustChain chain = buildTrustChain(dsc, cscas);

    // Verify
    ASSERT_TRUE(chain.isValid);
    ASSERT_EQ(chain.certificates.size(), 2);  // DSC + CSCA
    EXPECT_THAT(chain.path, HasSubstr("DSC → CN=CSCA"));

    // Cleanup
    X509_free(dsc);
    X509_free(csca);
}

TEST(TrustChainTest, BuildChain_WithLinkCertificate) {
    // Setup: DSC → CSCA_old → Link_Cert → CSCA_new
    X509* dsc = loadTestCertificate("test/data/dsc_with_link.crt");
    X509* cscaOld = loadTestCertificate("test/data/csca_old.crt");
    X509* linkCert = loadTestCertificate("test/data/link_cert.crt");
    X509* cscaNew = loadTestCertificate("test/data/csca_new.crt");
    std::vector<X509*> cscas = {cscaOld, linkCert, cscaNew};

    // Execute
    TrustChain chain = buildTrustChain(dsc, cscas);

    // Verify
    ASSERT_TRUE(chain.isValid);
    ASSERT_EQ(chain.certificates.size(), 4);  // DSC + CSCA_old + Link + CSCA_new
    EXPECT_THAT(chain.path, HasSubstr("DSC → CN=CSCA_OLD → CN=Link → CN=CSCA_NEW"));

    // Cleanup (test framework handles)
}

TEST(TrustChainTest, BuildChain_BrokenChain) {
    // Setup: DSC → (missing CSCA)
    X509* dsc = loadTestCertificate("test/data/dsc_orphan.crt");
    std::vector<X509*> cscas = {};  // Empty list

    // Execute
    TrustChain chain = buildTrustChain(dsc, cscas);

    // Verify
    ASSERT_FALSE(chain.isValid);
    ASSERT_EQ(chain.certificates.size(), 1);  // Only DSC
    EXPECT_THAT(chain.errorMessage, HasSubstr("Chain broken"));

    // Cleanup
    X509_free(dsc);
}

TEST(TrustChainTest, BuildChain_CircularReference) {
    // Setup: Create circular chain (A → B → A)
    // This is a malicious scenario
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
```

**Test Certificate Generation Script**:
```bash
#!/bin/bash
# test/data/generate_test_certificates.sh

# Create CA directory structure
mkdir -p test/data

# 1. Self-signed CSCA_old
openssl req -x509 -newkey rsa:2048 -keyout test/data/csca_old.key \
        -out test/data/csca_old.crt \
        -days 3650 -nodes \
        -subj "/C=TEST/O=TestCountry/CN=CSCA_OLD"

# 2. Self-signed CSCA_new
openssl req -x509 -newkey rsa:2048 -keyout test/data/csca_new.key \
        -out test/data/csca_new.crt \
        -days 3650 -nodes \
        -subj "/C=TEST/O=TestCountry/CN=CSCA_NEW"

# 3. Link certificate (CSCA_new public key signed by CSCA_old private key)
openssl req -new -key test/data/csca_new.key \
        -out test/data/link_cert.csr \
        -subj "/C=TEST/O=TestCountry/CN=CSCA_NEW"

openssl x509 -req -in test/data/link_cert.csr \
        -CA test/data/csca_old.crt \
        -CAkey test/data/csca_old.key \
        -out test/data/link_cert.crt \
        -days 365 -CAcreateserial

# 4. DSC signed by CSCA_old
openssl req -newkey rsa:2048 -keyout test/data/dsc_with_link.key \
        -out test/data/dsc_with_link.csr \
        -nodes -subj "/C=TEST/O=TestCountry/CN=DSC"

openssl x509 -req -in test/data/dsc_with_link.csr \
        -CA test/data/csca_old.crt \
        -CAkey test/data/csca_old.key \
        -out test/data/dsc_with_link.crt \
        -days 1095 -CAcreateserial

# 5. DSC signed by self-signed CSCA (direct chain)
openssl req -newkey rsa:2048 -keyout test/data/dsc_direct.key \
        -out test/data/dsc_direct.csr \
        -nodes -subj "/C=TEST/O=TestCountry/CN=DSC_DIRECT"

openssl x509 -req -in test/data/dsc_direct.csr \
        -CA test/data/csca_old.crt \
        -CAkey test/data/csca_old.key \
        -out test/data/dsc_direct.crt \
        -days 1095 -CAcreateserial

echo "Test certificates generated successfully"
```

**Deliverables**:
- [ ] Chain building algorithm implemented (6h)
- [ ] Unit tests pass (all scenarios) (2h)
- [ ] Test certificate generation script (included)

### Day 5: Chain Validation

#### Task 2.4: Implement Chain Validation Logic

**Assignee**: Senior Backend Developer
**Estimated Time**: 6 hours

**Files to Modify**:
- [services/pkd-management/src/main.cpp](services/pkd-management/src/main.cpp) (5h)
- Unit tests (1h)

**Implementation**:

```cpp
/**
 * @brief Validate trust chain by verifying signatures and expiration
 * @param chain TrustChain structure to validate
 * @return true if all signatures valid and all certs not expired
 */
bool validateTrustChain(const TrustChain& chain) {
    if (!chain.isValid) {
        spdlog::warn("Chain validation: Chain marked as invalid by builder");
        return false;
    }

    if (chain.certificates.size() < 2) {
        spdlog::error("Chain validation: Chain too short (need >= 2 certificates)");
        return false;
    }

    time_t now = time(nullptr);

    // Step 1: Validate each certificate expiration
    for (size_t i = 0; i < chain.certificates.size(); i++) {
        X509* cert = chain.certificates[i];

        // Check not expired
        if (X509_cmp_time(X509_get0_notAfter(cert), &now) < 0) {
            spdlog::warn("Chain validation: Certificate {} is EXPIRED", i);
            return false;
        }

        // Check not yet valid
        if (X509_cmp_time(X509_get0_notBefore(cert), &now) > 0) {
            spdlog::warn("Chain validation: Certificate {} is NOT YET VALID", i);
            return false;
        }

        spdlog::debug("Chain validation: Certificate {} validity OK", i);
    }

    // Step 2: Validate signatures (cert[i] signed by cert[i+1])
    for (size_t i = 0; i < chain.certificates.size() - 1; i++) {
        X509* cert = chain.certificates[i];
        X509* issuer = chain.certificates[i + 1];

        EVP_PKEY* issuerPubKey = X509_get_pubkey(issuer);
        if (!issuerPubKey) {
            spdlog::error("Chain validation: Failed to extract public key from issuer {}",
i);
            return false;
        }

        int verifyResult = X509_verify(cert, issuerPubKey);
        EVP_PKEY_free(issuerPubKey);

        if (verifyResult != 1) {
            unsigned long err = ERR_get_error();
            char errBuf[256];
            ERR_error_string_n(err, errBuf, sizeof(errBuf));
            spdlog::error("Chain validation: Signature verification failed at step {} -> {}: {}",
                          i, i + 1, errBuf);
            return false;
        }

        spdlog::debug("Chain validation: Signature {} → {} verified", i, i + 1);
    }

    // Step 3: Verify root is self-signed
    X509* root = chain.certificates.back();
    if (!isSelfSigned(root)) {
        spdlog::warn("Chain validation: Root certificate is not self-signed");
        return false;
    }

    spdlog::info("Chain validation: Trust chain VALID ({} certificates)",
                 chain.certificates.size());
    return true;
}
```

**Unit Test**:
```cpp
TEST(ChainValidationTest, ValidateChain_AllValid) {
    // Setup: Build valid chain
    TrustChain chain = /* build test chain */;
    chain.isValid = true;

    // Execute
    bool result = validateTrustChain(chain);

    // Verify
    ASSERT_TRUE(result);
}

TEST(ChainValidationTest, ValidateChain_ExpiredCertInChain) {
    // Setup: Chain with expired certificate
    TrustChain chain = /* build chain with expired cert */;

    // Execute
    bool result = validateTrustChain(chain);

    // Verify
    ASSERT_FALSE(result);
}

TEST(ChainValidationTest, ValidateChain_InvalidSignature) {
    // Setup: Chain with invalid signature
    TrustChain chain = /* build chain with wrong issuer */;

    // Execute
    bool result = validateTrustChain(chain);

    // Verify
    ASSERT_FALSE(result);
}
```

**Deliverables**:
- [ ] Validation logic implemented (5h)
- [ ] Unit tests pass (1h)

#### Task 2.5: Code Review & Integration

**Assignee**: Team Lead + Senior Developer
**Estimated Time**: 4 hours

**Review Checklist**:
- [ ] Algorithm correctness (1h)
- [ ] Memory management (X509* freed correctly) (1h)
- [ ] Edge case handling (circular refs, max depth) (0.5h)
- [ ] Unit test coverage > 90% (0.5h)
- [ ] Performance acceptable (< 50ms per chain) (0.5h)
- [ ] Documentation (code comments) (0.5h)

**Deliverables**:
- [ ] Code approved for merge (3h)
- [ ] Refactoring feedback (if needed) (1h)

### Sprint 2 Deliverables

- ✅ **Multi-CSCA Lookup**: `findAllCscasBySubjectDn()` function
- ✅ **Link Certificate Identification**: `isLinkCertificate()`, `isSelfSigned()`
- ✅ **Chain Building Algorithm**: `buildTrustChain()` with circular ref protection
- ✅ **Chain Validation Logic**: `validateTrustChain()` with signature verification
- ✅ **Unit Tests**: 90%+ coverage, all edge cases tested
- ✅ **Test Certificates**: OpenSSL-generated test data

---

## Sprint 3: Link Certificate Validation - Integration (Week 3)

**Goal**: Integrate chain building/validation into DSC validation pipeline and update database schema.

### Day 1-2: DSC Validation Integration

#### Task 3.1: Refactor validateDscCertificate()

**Assignee**: Backend Developer
**Estimated Time**: 6 hours

**Files to Modify**:
- [services/pkd-management/src/main.cpp](services/pkd-management/src/main.cpp#L739-L798) (5h)
- Unit tests (1h)

**Implementation**:

```cpp
// UPDATED FUNCTION: validateDscCertificate()
DscValidationResult validateDscCertificate(PGconn* conn, X509* dscCert, const std::string& issuerDn) {
    DscValidationResult result = {false, false, false, false, false, "", "", ""};  // Added trustChainPath field

    if (!dscCert) {
        result.errorMessage = "DSC certificate is null";
        return result;
    }

    // Step 1: Check DSC expiration
    time_t now = time(nullptr);
    if (X509_cmp_time(X509_get0_notAfter(dscCert), &now) < 0) {
        result.errorMessage = "DSC certificate is expired";
        spdlog::warn("DSC validation: DSC is EXPIRED");
        return result;
    }
    if (X509_cmp_time(X509_get0_notBefore(dscCert), &now) > 0) {
        result.errorMessage = "DSC certificate is not yet valid";
        spdlog::warn("DSC validation: DSC is NOT YET VALID");
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

    spdlog::info("DSC validation: Found {} CSCA(s) for issuer (may include link certs)",
                 allCscas.size());

    // Step 3: Build trust chain (may traverse link certificates)
    TrustChain chain = buildTrustChain(dscCert, allCscas);

    if (!chain.isValid) {
        result.errorMessage = "Failed to build trust chain: " + chain.errorMessage;
        spdlog::warn("DSC validation: {}", result.errorMessage);

        // Cleanup
        for (X509* csca : allCscas) X509_free(csca);
        return result;
    }

    spdlog::info("DSC validation: Trust chain built successfully ({} steps)",
                 chain.certificates.size());

    // Step 4: Validate entire chain (signatures + expiration)
    bool chainValid = validateTrustChain(chain);

    if (chainValid) {
        result.signatureValid = true;
        result.isValid = true;
        result.trustChainPath = chain.path;

        spdlog::info("DSC validation: Trust Chain VERIFIED");
        spdlog::info("  Path: {}", chain.path);
    } else {
        result.errorMessage = "Trust chain validation failed";
        spdlog::error("DSC validation: Trust Chain INVALID");
    }

    // Cleanup
    for (X509* csca : allCscas) X509_free(csca);

    return result;
}
```

**DscValidationResult Struct Update**:
```cpp
struct DscValidationResult {
    bool isValid;
    bool notExpired;
    bool cscaFound;
    bool signatureValid;
    bool trustChainValid;  // Deprecated (use isValid)
    std::string cscaSubjectDn;
    std::string errorMessage;
    std::string trustChainPath;  // NEW: e.g., "DSC → CN=CSCA_old → CN=Link → CN=CSCA_new"
};
```

**Integration Test**:
```cpp
TEST(DscValidationTest, ValidateDsc_WithLinkCertificate) {
    // Setup: Load real DSC + CSCAs with link certificate
    X509* dsc = loadRealCertificate("data/real_dsc_cn.der");
    std::string issuerDn = extractIssuerDn(dsc);

    // Execute
    DscValidationResult result = validateDscCertificate(testDbConn, dsc, issuerDn);

    // Verify
    ASSERT_TRUE(result.isValid);
    ASSERT_TRUE(result.signatureValid);
    EXPECT_THAT(result.trustChainPath, Not(IsEmpty()));

    // Cleanup
    X509_free(dsc);
}
```

**Deliverables**:
- [ ] validateDscCertificate() refactored (5h)
- [ ] Integration tests pass (1h)

#### Task 3.2: Database Schema Update (validation_result table)

**Assignee**: Database Administrator
**Estimated Time**: 3 hours

**Migration SQL**:

File: `docker/init-scripts/05-link-cert-validation-schema.sql`

```sql
-- Add trust_chain_path column to validation_result table
ALTER TABLE validation_result
ADD COLUMN IF NOT EXISTS trust_chain_path TEXT;

COMMENT ON COLUMN validation_result.trust_chain_path IS
    'Human-readable trust chain path, e.g., "DSC → CN=CSCA_old → CN=Link → CN=CSCA_new"';

-- Add index for faster queries
CREATE INDEX IF NOT EXISTS idx_validation_result_trust_chain_path
ON validation_result USING gin(to_tsvector('english', trust_chain_path));

-- Update existing records (set to empty string for now)
UPDATE validation_result
SET trust_chain_path = ''
WHERE trust_chain_path IS NULL;

-- Verify migration
DO $$
DECLARE
    col_exists BOOLEAN;
BEGIN
    SELECT EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_name = 'validation_result'
          AND column_name = 'trust_chain_path'
    ) INTO col_exists;

    IF col_exists THEN
        RAISE NOTICE 'Migration successful: trust_chain_path column added';
    ELSE
        RAISE EXCEPTION 'Migration failed: trust_chain_path column not found';
    END IF;
END $$;
```

**Rollback SQL**:
```sql
-- Rollback migration
ALTER TABLE validation_result
DROP COLUMN IF EXISTS trust_chain_path;

DROP INDEX IF EXISTS idx_validation_result_trust_chain_path;
```

**Deliverables**:
- [ ] Migration SQL script (1h)
- [ ] Apply migration to local DB (0.5h)
- [ ] Verify schema (0.5h)
- [ ] Rollback script tested (0.5h)
- [ ] Documentation updated (0.5h)

### Day 3-4: Master List Processing & Performance

#### Task 3.3: Ensure Master List Link Certificates Stored

**Assignee**: Backend Developer
**Estimated Time**: 4 hours

**Files to Check**:
- [services/pkd-management/src/main.cpp](services/pkd-management/src/main.cpp#L3800-L3950) (Master List processing)

**Verification**:
- [ ] Link certificates (cross-signed CSCAs) are stored as `certificate_type='CSCA'` (1h)
- [ ] Both self-signed and link certificates extracted from Master List (1h)
- [ ] Upload pipeline stores all CSCA types (2h)

**Current Code Review** (line 3810, 3922, 3945):
```cpp
// Line 3810: Master List contains ONLY CSCA certificates
std::string certType = "CSCA";

// Line 3922: Log cross-signed CSCA
spdlog::debug("Cross-signed CSCA: subject={}, issuer={}", ...);

// VERIFICATION NEEDED: Ensure both stored with same certificate_type
```

**Test**:
```bash
# Upload Master List, then query for link certificates
SELECT
    country_code,
    serial_number,
    subject_dn,
    issuer_dn,
    CASE
        WHEN subject_dn = issuer_dn THEN 'Self-signed'
        ELSE 'Link Certificate'
    END as cert_category
FROM certificate
WHERE certificate_type = 'CSCA'
  AND country_code = 'CN'  -- Test with China
ORDER BY created_at;
```

**Expected**: Both self-signed and link CSCAs present with `certificate_type='CSCA'`.

**Deliverables**:
- [ ] Verification complete (link certs stored correctly) (3h)
- [ ] Bug fixes (if any storage issues found) (1h)

#### Task 3.4: Performance Optimization

**Assignee**: Senior Backend Developer
**Estimated Time**: 6 hours

**Optimization 1: CSCA Caching** (3h)

Implement in-memory cache to avoid repeated DB queries during validation.

```cpp
// Global CSCA cache (thread-safe)
struct CscaCache {
    std::unordered_map<std::string, std::vector<X509*>> cache;  // Key: subject DN
    std::mutex mtx;
    time_t lastRefresh = 0;
    const int CACHE_TTL_SECONDS = 3600;  // 1 hour

    std::vector<X509*> get(const std::string& subjectDn) {
        std::lock_guard<std::mutex> lock(mtx);

        // Refresh cache if expired
        time_t now = time(nullptr);
        if (now - lastRefresh > CACHE_TTL_SECONDS) {
            clear();
            return {};  // Cache expired, force DB query
        }

        auto it = cache.find(subjectDn);
        if (it != cache.end()) {
            return it->second;  // Return cached copies
        }
        return {};
    }

    void put(const std::string& subjectDn, const std::vector<X509*>& cscas) {
        std::lock_guard<std::mutex> lock(mtx);

        // Deep copy X509* certificates
        std::vector<X509*> copies;
        for (X509* cert : cscas) {
            copies.push_back(X509_dup(cert));
        }

        cache[subjectDn] = copies;
        lastRefresh = time(nullptr);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx);

        // Free all cached certificates
        for (auto& pair : cache) {
            for (X509* cert : pair.second) {
                X509_free(cert);
            }
        }
        cache.clear();
        lastRefresh = 0;
    }
};

// Global cache instance
CscaCache g_cscaCache;
```

**Optimization 2: Chain Depth Limit** (1h)

Already implemented in `buildTrustChain()` with `maxDepth = 5`.

**Optimization 3: Parallel Validation** (2h)

```cpp
// Validate multiple DSCs in parallel
void validateDscBatch(PGconn* conn,
                      const std::vector<std::pair<X509*, std::string>>& dscPairs) {
    #pragma omp parallel for
    for (size_t i = 0; i < dscPairs.size(); i++) {
        X509* dsc = dscPairs[i].first;
        std::string issuerDn = dscPairs[i].second;

        DscValidationResult result = validateDscCertificate(conn, dsc, issuerDn);
        // Store result in database (thread-safe)
    }
}
```

**Performance Benchmark**:
```cpp
TEST(PerformanceTest, ValidateDscChain_Performance) {
    // Setup: 1000 DSCs
    std::vector<std::pair<X509*, std::string>> dscs = loadTestDscs(1000);

    // Benchmark
    auto start = std::chrono::high_resolution_clock::now();

    for (const auto& pair : dscs) {
        validateDscCertificate(testDbConn, pair.first, pair.second);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Verify
    EXPECT_LT(duration.count(), 15000);  // < 15 seconds for 1000 DSCs
    double avgPerDsc = duration.count() / 1000.0;
    EXPECT_LT(avgPerDsc, 15);  // < 15ms per DSC

    spdlog::info("Performance: Validated 1000 DSCs in {} ms (avg {} ms/DSC)",
                 duration.count(), avgPerDsc);
}
```

**Deliverables**:
- [ ] CSCA caching implemented (3h)
- [ ] Performance benchmarks pass (2h)
- [ ] Documentation (1h)

### Day 5: Frontend & API Updates

#### Task 3.5: API Response Update

**Assignee**: Backend Developer
**Estimated Time**: 2 hours

**Files to Modify**:
- [services/pkd-management/src/main.cpp](services/pkd-management/src/main.cpp) (validation API handlers)

**Changes**:
- Add `trust_chain_path` field to API responses
- Update OpenAPI specification

**Example API Response**:
```json
{
  "certificateId": "abc123",
  "validationStatus": "VALID",
  "trustChainValid": true,
  "trustChainPath": "DSC → CN=CSCA_OLD,C=CN → CN=Link,C=CN → CN=CSCA_NEW,C=CN",
  "cscaFound": true,
  "signatureValid": true,
  "notExpired": true
}
```

**OpenAPI Spec Update**:

File: `docs/openapi/pkd-management-api.yaml`

```yaml
ValidationResult:
  type: object
  properties:
    certificateId:
      type: string
    validationStatus:
      type: string
      enum: [VALID, INVALID, UNKNOWN]
    trustChainValid:
      type: boolean
    trustChainPath:
      type: string
      description: "Human-readable trust chain path (e.g., 'DSC → CN=CSCA_old → CN=Link → CN=CSCA_new')"
      example: "DSC → CN=CSCA_OLD,C=CN → CN=Link,C=CN → CN=CSCA_NEW,C=CN"
    cscaFound:
      type: boolean
    signatureValid:
      type: boolean
    notExpired:
      type: boolean
```

**Deliverables**:
- [ ] API response updated (1h)
- [ ] OpenAPI spec updated (0.5h)
- [ ] API documentation deployed (0.5h)

#### Task 3.6: Frontend Display Enhancement

**Assignee**: Frontend Developer
**Estimated Time**: 3 hours

**Files to Modify**:
- [frontend/src/pages/UploadDashboard.tsx](frontend/src/pages/UploadDashboard.tsx) (1h)
- [frontend/src/pages/UploadHistory.tsx](frontend/src/pages/UploadHistory.tsx) (1h)
- [frontend/src/components/ValidationDetails.tsx](frontend/src/components/ValidationDetails.tsx) (1h)

**UI Enhancement**:

**Upload Dashboard - Trust Chain Visualization**:
```tsx
// Add trust chain path display in validation statistics
<div className="trust-chain-path">
  <h4>Trust Chain Path</h4>
  {validationResult.trustChainPath ? (
    <div className="chain-path">
      {validationResult.trustChainPath.split(' → ').map((step, idx) => (
        <React.Fragment key={idx}>
          <span className="chain-step">{step}</span>
          {idx < validationResult.trustChainPath.split(' → ').length - 1 && (
            <span className="chain-arrow">→</span>
          )}
        </React.Fragment>
      ))}
    </div>
  ) : (
    <span className="no-chain">No trust chain available</span>
  )}
</div>
```

**CSS Styling**:
```css
.trust-chain-path {
  margin-top: 1rem;
}

.chain-path {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 0.5rem;
}

.chain-step {
  padding: 0.25rem 0.5rem;
  background: #e3f2fd;
  border-radius: 4px;
  font-size: 0.9rem;
  font-family: monospace;
}

.chain-arrow {
  color: #1976d2;
  font-weight: bold;
}

.no-chain {
  color: #757575;
  font-style: italic;
}
```

**Deliverables**:
- [ ] Frontend UI updated (3h)
- [ ] Visual testing (included)

### Sprint 3 Deliverables

- ✅ **DSC Validation Integration**: Link certificate chain validation fully integrated
- ✅ **Database Schema**: `trust_chain_path` column added
- ✅ **Performance Optimization**: CSCA caching, < 15ms per DSC
- ✅ **API Updates**: `trust_chain_path` in responses, OpenAPI spec updated
- ✅ **Frontend Enhancement**: Trust chain path visualization

---

## Sprint 3.5: Validation Detail Enhancement (3 Days, Parallel)

**Goal**: Provide detailed validation step information to users for better debugging and transparency.

**Timeline**: 3 days (parallel to Sprint 3, Days 5-7)
**Detailed Plan**: See [VALIDATION_DETAIL_ENHANCEMENT_PLAN.md](VALIDATION_DETAIL_ENHANCEMENT_PLAN.md)

### Overview

This sprint enhances the validation system to record and display detailed step-by-step information for every certificate validation, enabling users to:
- Understand exactly why a certificate failed validation
- See the complete trust chain building process
- Get actionable recommendations for fixing issues
- Access detailed metadata for each validation step

### Day 1: Database Schema Implementation

#### Task 3.5.1: Create Validation Detail Tables

**Assignee**: Database Engineer
**Estimated Time**: 4 hours

**Create validation_result_detail table**:
```sql
-- Validation step enumeration
CREATE TYPE validation_step AS ENUM (
    'PARSE_CERTIFICATE',
    'CHECK_EXPIRATION',
    'FIND_CSCA',
    'BUILD_TRUST_CHAIN',
    'VERIFY_SIGNATURES',
    'CHECK_EXTENSIONS',
    'CHECK_REVOCATION',
    'FINAL_DECISION'
);

-- Step status enumeration
CREATE TYPE step_status AS ENUM (
    'SUCCESS',
    'WARNING',
    'FAILED',
    'SKIPPED'
);

-- Detailed validation results table
CREATE TABLE validation_result_detail (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    validation_result_id UUID NOT NULL REFERENCES validation_result(id) ON DELETE CASCADE,
    certificate_id UUID NOT NULL REFERENCES certificate(id) ON DELETE CASCADE,
    step_number INT NOT NULL,
    step_name validation_step NOT NULL,
    step_status step_status NOT NULL,
    step_started_at TIMESTAMP NOT NULL,
    step_completed_at TIMESTAMP NOT NULL,
    step_duration_ms INT NOT NULL,
    success BOOLEAN NOT NULL,
    error_code VARCHAR(50),
    error_message TEXT,
    warning_message TEXT,
    step_data JSONB,
    recommendation TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_validation_result FOREIGN KEY (validation_result_id)
        REFERENCES validation_result(id) ON DELETE CASCADE,
    CONSTRAINT fk_certificate FOREIGN KEY (certificate_id)
        REFERENCES certificate(id) ON DELETE CASCADE
);

-- Indexes for performance
CREATE INDEX idx_validation_detail_result_id ON validation_result_detail(validation_result_id);
CREATE INDEX idx_validation_detail_cert_id ON validation_result_detail(certificate_id);
CREATE INDEX idx_validation_detail_step_name ON validation_result_detail(step_name);
CREATE INDEX idx_validation_detail_step_status ON validation_result_detail(step_status);
CREATE INDEX idx_validation_detail_error_code ON validation_result_detail(error_code);
```

#### Task 3.5.2: Create Error Code Taxonomy Table

**Assignee**: Database Engineer
**Estimated Time**: 2 hours

**Create validation_error_codes table**:
```sql
CREATE TABLE validation_error_codes (
    error_code VARCHAR(50) PRIMARY KEY,
    category VARCHAR(30) NOT NULL,  -- PARSING, EXPIRATION, TRUST_CHAIN, SIGNATURE, etc.
    severity VARCHAR(20) NOT NULL,  -- CRITICAL, HIGH, MEDIUM, LOW
    default_message TEXT NOT NULL,
    recommendation TEXT NOT NULL,
    icao_reference TEXT,  -- e.g., "ICAO Doc 9303-12 Section 7.1.2"
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Populate with 46 error codes
INSERT INTO validation_error_codes (error_code, category, severity, default_message, recommendation, icao_reference) VALUES
    ('CERT_PARSE_FAILED', 'PARSING', 'CRITICAL', 'Failed to parse certificate', 'Verify certificate format is DER or PEM', 'RFC 5280'),
    ('CERT_EXPIRED', 'EXPIRATION', 'HIGH', 'Certificate has expired', 'Renew certificate with issuing authority', 'RFC 5280 Section 4.1.2.5'),
    ('CERT_NOT_YET_VALID', 'EXPIRATION', 'MEDIUM', 'Certificate not yet valid', 'Check system time or wait for validity period', 'RFC 5280 Section 4.1.2.5'),
    ('CSCA_NOT_FOUND', 'TRUST_CHAIN', 'CRITICAL', 'No CSCA found for issuer DN', 'Upload CSCA certificate to database', 'ICAO Doc 9303-12 Section 7.1'),
    ('TRUST_CHAIN_BROKEN', 'TRUST_CHAIN', 'CRITICAL', 'Trust chain could not be built', 'Check if all intermediate certificates are available', 'ICAO Doc 9303-12 Section 7.1.2'),
    ('SIGNATURE_VERIFICATION_FAILED', 'SIGNATURE', 'CRITICAL', 'Certificate signature verification failed', 'Certificate may be corrupted or tampered', 'RFC 5280 Section 4.1.1.3'),
    ('CIRCULAR_REFERENCE', 'TRUST_CHAIN', 'CRITICAL', 'Circular reference detected in chain', 'Remove circular dependency in certificate chain', 'RFC 5280'),
    ('MAX_CHAIN_DEPTH_EXCEEDED', 'TRUST_CHAIN', 'HIGH', 'Maximum chain depth exceeded', 'Chain too long, check for configuration issues', 'Implementation specific'),
    -- ... (38 more error codes)
    ('MULTIPLE_CSCA_WARNING', 'TRUST_CHAIN', 'LOW', 'Multiple CSCAs found (link certificates)', 'This is normal during key rollover', 'ICAO Doc 9303-12 Section 7.2');
```

**Deliverables**:
- [ ] Database migration script created (2h)
- [ ] Error codes populated (1h)
- [ ] Indexes created (included)
- [ ] Schema verification (1h)

### Day 2: Backend Implementation

#### Task 3.5.3: Implement ValidationDetailRecorder Class

**Assignee**: Backend Developer
**Estimated Time**: 6 hours

**File**: `services/pkd-management/src/validation/validation_detail_recorder.h`

```cpp
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <libpq-fe.h>
#include <json/json.h>
#include <chrono>

namespace validation {

enum class ValidationStep {
    PARSE_CERTIFICATE,
    CHECK_EXPIRATION,
    FIND_CSCA,
    BUILD_TRUST_CHAIN,
    VERIFY_SIGNATURES,
    CHECK_EXTENSIONS,
    CHECK_REVOCATION,
    FINAL_DECISION
};

enum class StepStatus {
    SUCCESS,
    WARNING,
    FAILED,
    SKIPPED
};

struct ValidationStepResult {
    int stepNumber;
    ValidationStep stepName;
    StepStatus stepStatus;
    std::chrono::system_clock::time_point stepStartedAt;
    std::chrono::system_clock::time_point stepCompletedAt;
    int stepDurationMs;
    bool success;
    std::string errorCode;
    std::string errorMessage;
    std::string warningMessage;
    Json::Value stepData;
    std::string recommendation;
};

class ValidationDetailRecorder {
public:
    explicit ValidationDetailRecorder(PGconn* conn,
                                      const std::string& validationResultId,
                                      const std::string& certificateId);

    ~ValidationDetailRecorder();

    // Step management
    void startStep(ValidationStep step);
    void completeStep(StepStatus status, const Json::Value& stepData = Json::Value());
    void completeStepWithError(const std::string& errorCode,
                               const std::string& errorMessage,
                               const Json::Value& stepData = Json::Value());
    void completeStepWithWarning(const std::string& warningMessage,
                                 const Json::Value& stepData = Json::Value());

    // Database persistence
    bool saveToDatabase();

    // Utilities
    static std::string validationStepToString(ValidationStep step);
    static std::string stepStatusToString(StepStatus status);
    std::string getRecommendation(const std::string& errorCode);

private:
    PGconn* conn_;
    std::string validationResultId_;
    std::string certificateId_;
    std::vector<ValidationStepResult> steps_;
    ValidationStepResult* currentStep_;
    int stepCounter_;
};

} // namespace validation
```

**File**: `services/pkd-management/src/validation/validation_detail_recorder.cpp`

```cpp
#include "validation_detail_recorder.h"
#include <spdlog/spdlog.h>

namespace validation {

ValidationDetailRecorder::ValidationDetailRecorder(
    PGconn* conn,
    const std::string& validationResultId,
    const std::string& certificateId)
    : conn_(conn)
    , validationResultId_(validationResultId)
    , certificateId_(certificateId)
    , currentStep_(nullptr)
    , stepCounter_(0) {
}

ValidationDetailRecorder::~ValidationDetailRecorder() {
    // Auto-save on destruction (RAII pattern)
    if (!steps_.empty()) {
        saveToDatabase();
    }
}

void ValidationDetailRecorder::startStep(ValidationStep step) {
    ValidationStepResult stepResult;
    stepResult.stepNumber = ++stepCounter_;
    stepResult.stepName = step;
    stepResult.stepStartedAt = std::chrono::system_clock::now();
    stepResult.success = false;  // Default, will be updated on complete

    steps_.push_back(stepResult);
    currentStep_ = &steps_.back();
}

void ValidationDetailRecorder::completeStep(StepStatus status, const Json::Value& stepData) {
    if (!currentStep_) {
        spdlog::error("completeStep called without startStep");
        return;
    }

    currentStep_->stepCompletedAt = std::chrono::system_clock::now();
    currentStep_->stepDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentStep_->stepCompletedAt - currentStep_->stepStartedAt
    ).count();

    currentStep_->stepStatus = status;
    currentStep_->success = (status == StepStatus::SUCCESS || status == StepStatus::WARNING);
    currentStep_->stepData = stepData;

    currentStep_ = nullptr;
}

void ValidationDetailRecorder::completeStepWithError(
    const std::string& errorCode,
    const std::string& errorMessage,
    const Json::Value& stepData) {

    if (!currentStep_) {
        spdlog::error("completeStepWithError called without startStep");
        return;
    }

    currentStep_->errorCode = errorCode;
    currentStep_->errorMessage = errorMessage;
    currentStep_->recommendation = getRecommendation(errorCode);

    completeStep(StepStatus::FAILED, stepData);
}

void ValidationDetailRecorder::completeStepWithWarning(
    const std::string& warningMessage,
    const Json::Value& stepData) {

    if (!currentStep_) {
        spdlog::error("completeStepWithWarning called without startStep");
        return;
    }

    currentStep_->warningMessage = warningMessage;
    completeStep(StepStatus::WARNING, stepData);
}

bool ValidationDetailRecorder::saveToDatabase() {
    if (steps_.empty()) {
        return true;  // Nothing to save
    }

    for (const auto& step : steps_) {
        const char* query =
            "INSERT INTO validation_result_detail ("
            "validation_result_id, certificate_id, step_number, step_name, "
            "step_status, step_started_at, step_completed_at, step_duration_ms, "
            "success, error_code, error_message, warning_message, step_data, recommendation"
            ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14)";

        std::string stepStartedAtStr = timeToIsoString(step.stepStartedAt);
        std::string stepCompletedAtStr = timeToIsoString(step.stepCompletedAt);
        std::string stepNumberStr = std::to_string(step.stepNumber);
        std::string stepDurationMsStr = std::to_string(step.stepDurationMs);
        std::string successStr = step.success ? "true" : "false";
        std::string stepDataStr = step.stepData.toStyledString();

        const char* paramValues[14] = {
            validationResultId_.c_str(),
            certificateId_.c_str(),
            stepNumberStr.c_str(),
            validationStepToString(step.stepName).c_str(),
            stepStatusToString(step.stepStatus).c_str(),
            stepStartedAtStr.c_str(),
            stepCompletedAtStr.c_str(),
            stepDurationMsStr.c_str(),
            successStr.c_str(),
            step.errorCode.empty() ? nullptr : step.errorCode.c_str(),
            step.errorMessage.empty() ? nullptr : step.errorMessage.c_str(),
            step.warningMessage.empty() ? nullptr : step.warningMessage.c_str(),
            stepDataStr.c_str(),
            step.recommendation.empty() ? nullptr : step.recommendation.c_str()
        };

        PGresult* res = PQexecParams(conn_, query, 14, nullptr, paramValues,
                                     nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            spdlog::error("Failed to insert validation detail: {}", PQerrorMessage(conn_));
            PQclear(res);
            return false;
        }

        PQclear(res);
    }

    return true;
}

std::string ValidationDetailRecorder::validationStepToString(ValidationStep step) {
    switch (step) {
        case ValidationStep::PARSE_CERTIFICATE: return "PARSE_CERTIFICATE";
        case ValidationStep::CHECK_EXPIRATION: return "CHECK_EXPIRATION";
        case ValidationStep::FIND_CSCA: return "FIND_CSCA";
        case ValidationStep::BUILD_TRUST_CHAIN: return "BUILD_TRUST_CHAIN";
        case ValidationStep::VERIFY_SIGNATURES: return "VERIFY_SIGNATURES";
        case ValidationStep::CHECK_EXTENSIONS: return "CHECK_EXTENSIONS";
        case ValidationStep::CHECK_REVOCATION: return "CHECK_REVOCATION";
        case ValidationStep::FINAL_DECISION: return "FINAL_DECISION";
        default: return "UNKNOWN";
    }
}

std::string ValidationDetailRecorder::stepStatusToString(StepStatus status) {
    switch (status) {
        case StepStatus::SUCCESS: return "SUCCESS";
        case StepStatus::WARNING: return "WARNING";
        case StepStatus::FAILED: return "FAILED";
        case StepStatus::SKIPPED: return "SKIPPED";
        default: return "UNKNOWN";
    }
}

std::string ValidationDetailRecorder::getRecommendation(const std::string& errorCode) {
    // Query error_codes table for recommendation
    const char* query = "SELECT recommendation FROM validation_error_codes WHERE error_code = $1";
    const char* paramValues[1] = {errorCode.c_str()};

    PGresult* res = PQexecParams(conn_, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    std::string recommendation;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        recommendation = PQgetvalue(res, 0, 0);
    }

    PQclear(res);
    return recommendation;
}

} // namespace validation
```

**Deliverables**:
- [ ] ValidationDetailRecorder class implemented (4h)
- [ ] Unit tests for recorder (2h)

#### Task 3.5.4: Integrate Recorder into validateDscCertificate()

**Assignee**: Backend Developer
**Estimated Time**: 4 hours

**File**: `services/pkd-management/src/main.cpp` (lines 739-798)

```cpp
// Modified validateDscCertificate() with detailed recording

#include "validation/validation_detail_recorder.h"

DscValidationResult validateDscCertificate(
    PGconn* conn,
    X509* dscCert,
    const std::string& issuerDn,
    const std::string& validationResultId,
    const std::string& certificateId) {

    using namespace validation;

    DscValidationResult result;
    result.trustChainValid = false;

    // Create validation detail recorder
    ValidationDetailRecorder recorder(conn, validationResultId, certificateId);

    // Step 1: Parse Certificate
    recorder.startStep(ValidationStep::PARSE_CERTIFICATE);
    Json::Value parseData;
    parseData["certificate_format"] = "DER";
    parseData["certificate_version"] = X509_get_version(dscCert) + 1;
    recorder.completeStep(StepStatus::SUCCESS, parseData);

    // Step 2: Check Expiration
    recorder.startStep(ValidationStep::CHECK_EXPIRATION);
    Json::Value expirationData;

    time_t now = time(nullptr);
    ASN1_TIME* notBefore = X509_get_notBefore(dscCert);
    ASN1_TIME* notAfter = X509_get_notAfter(dscCert);

    expirationData["not_before"] = asn1TimeToString(notBefore);
    expirationData["not_after"] = asn1TimeToString(notAfter);
    expirationData["current_time"] = timeToIsoString(now);

    if (X509_cmp_time(notAfter, &now) < 0) {
        result.validityPeriodValid = false;
        recorder.completeStepWithError("CERT_EXPIRED",
            "Certificate has expired", expirationData);
        return result;
    }

    if (X509_cmp_time(notBefore, &now) > 0) {
        result.validityPeriodValid = false;
        recorder.completeStepWithError("CERT_NOT_YET_VALID",
            "Certificate not yet valid", expirationData);
        return result;
    }

    result.validityPeriodValid = true;
    recorder.completeStep(StepStatus::SUCCESS, expirationData);

    // Step 3: Find CSCA
    recorder.startStep(ValidationStep::FIND_CSCA);
    Json::Value cscaData;

    std::vector<X509*> allCscas = findAllCscasBySubjectDn(conn, issuerDn);
    cscaData["issuer_dn"] = issuerDn;
    cscaData["csca_count_found"] = (int)allCscas.size();

    if (allCscas.empty()) {
        result.cscaFound = false;
        result.cscaSubjectDn = "NOT_FOUND";
        recorder.completeStepWithError("CSCA_NOT_FOUND",
            "No CSCA found for issuer DN: " + issuerDn, cscaData);
        return result;
    }

    result.cscaFound = true;

    if (allCscas.size() > 1) {
        cscaData["note"] = "Multiple CSCAs found (including link certificates)";
        recorder.completeStepWithWarning(
            "Multiple CSCAs found (this is normal during key rollover)", cscaData);
    } else {
        recorder.completeStep(StepStatus::SUCCESS, cscaData);
    }

    // Step 4: Build Trust Chain
    recorder.startStep(ValidationStep::BUILD_TRUST_CHAIN);
    Json::Value chainData;

    TrustChain chain = buildTrustChain(dscCert, allCscas);
    chainData["chain_length"] = (int)chain.certificates.size();
    chainData["chain_path"] = chain.path;
    chainData["is_valid"] = chain.isValid;

    if (!chain.isValid) {
        result.trustChainMessage = chain.errorMessage;
        recorder.completeStepWithError("TRUST_CHAIN_BROKEN",
            chain.errorMessage, chainData);
        return result;
    }

    result.trustChainPath = chain.path;
    recorder.completeStep(StepStatus::SUCCESS, chainData);

    // Step 5: Verify Signatures
    recorder.startStep(ValidationStep::VERIFY_SIGNATURES);
    Json::Value signatureData;

    bool allSignaturesValid = validateTrustChain(chain);
    signatureData["all_signatures_valid"] = allSignaturesValid;
    signatureData["chain_certificates_count"] = (int)chain.certificates.size();

    if (!allSignaturesValid) {
        result.signatureValid = false;
        result.trustChainMessage = "Signature verification failed in chain";
        recorder.completeStepWithError("SIGNATURE_VERIFICATION_FAILED",
            "One or more signatures in chain are invalid", signatureData);
        return result;
    }

    result.signatureValid = true;
    recorder.completeStep(StepStatus::SUCCESS, signatureData);

    // Step 6: Check Extensions (Optional)
    recorder.startStep(ValidationStep::CHECK_EXTENSIONS);
    Json::Value extensionsData;
    // ... extension checking logic
    recorder.completeStep(StepStatus::SUCCESS, extensionsData);

    // Step 7: Check Revocation (Skipped for now)
    recorder.startStep(ValidationStep::CHECK_REVOCATION);
    Json::Value revocationData;
    revocationData["crl_checked"] = false;
    revocationData["ocsp_checked"] = false;
    recorder.completeStep(StepStatus::SKIPPED, revocationData);

    // Step 8: Final Decision
    recorder.startStep(ValidationStep::FINAL_DECISION);
    Json::Value finalData;
    finalData["validation_status"] = "VALID";
    finalData["trust_chain_valid"] = true;
    recorder.completeStep(StepStatus::SUCCESS, finalData);

    result.trustChainValid = true;
    result.trustChainMessage = "All validation steps passed";

    // Auto-save will happen in recorder destructor
    return result;
}
```

**Deliverables**:
- [ ] Integration into validateDscCertificate() (3h)
- [ ] Integration testing (1h)

### Day 3: API & Frontend Implementation

#### Task 3.5.5: Implement Validation Detail API Endpoints

**Assignee**: Backend Developer
**Estimated Time**: 3 hours

**API Endpoints**:

```cpp
// GET /api/certificates/{certificateId}/validation-detail
app.registerHandler(
    "/api/certificates/{id}/validation-detail",
    [](const HttpRequestPtr& req,
       std::function<void(const HttpResponsePtr&)>&& callback,
       const std::string& certificateId) {

        PGconn* conn = getDbConnection();

        // Query validation details
        const char* query =
            "SELECT vrd.*, vr.validation_status, vr.trust_chain_path "
            "FROM validation_result_detail vrd "
            "JOIN validation_result vr ON vrd.validation_result_id = vr.id "
            "WHERE vrd.certificate_id = $1 "
            "ORDER BY vrd.step_number ASC";

        const char* paramValues[1] = {certificateId.c_str()};
        PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                     nullptr, nullptr, 0);

        Json::Value response;
        response["certificateId"] = certificateId;
        response["validationSteps"] = Json::Value(Json::arrayValue);

        for (int i = 0; i < PQntuples(res); i++) {
            Json::Value step;
            step["stepNumber"] = std::stoi(PQgetvalue(res, i, 2));
            step["stepName"] = PQgetvalue(res, i, 3);
            step["stepStatus"] = PQgetvalue(res, i, 4);
            step["stepDurationMs"] = std::stoi(PQgetvalue(res, i, 7));
            step["success"] = strcmp(PQgetvalue(res, i, 8), "t") == 0;

            if (!PQgetisnull(res, i, 9)) {
                step["errorCode"] = PQgetvalue(res, i, 9);
                step["errorMessage"] = PQgetvalue(res, i, 10);
                step["recommendation"] = PQgetvalue(res, i, 13);
            }

            if (!PQgetisnull(res, i, 11)) {
                step["warningMessage"] = PQgetvalue(res, i, 11);
            }

            // Parse step_data JSON
            Json::Reader reader;
            Json::Value stepData;
            reader.parse(PQgetvalue(res, i, 12), stepData);
            step["stepData"] = stepData;

            response["validationSteps"].append(step);
        }

        PQclear(res);
        PQfinish(conn);

        auto httpResponse = HttpResponse::newHttpJsonResponse(response);
        callback(httpResponse);
    },
    {Get}
);
```

**Deliverables**:
- [ ] API endpoint implemented (2h)
- [ ] OpenAPI spec updated (1h)

#### Task 3.5.6: Implement Frontend Components

**Assignee**: Frontend Developer
**Estimated Time**: 6 hours

**Component 1: ValidationTimelineStepper.tsx**
```typescript
import React from 'react';
import { Check, X, AlertTriangle, Clock } from 'lucide-react';

interface ValidationStep {
  stepNumber: number;
  stepName: string;
  stepStatus: 'SUCCESS' | 'WARNING' | 'FAILED' | 'SKIPPED';
  stepDurationMs: number;
  success: boolean;
  errorCode?: string;
  errorMessage?: string;
  warningMessage?: string;
  stepData?: any;
  recommendation?: string;
}

interface Props {
  steps: ValidationStep[];
}

const ValidationTimelineStepper: React.FC<Props> = ({ steps }) => {
  const getStepIcon = (status: string) => {
    switch (status) {
      case 'SUCCESS':
        return <Check className="w-5 h-5 text-green-600" />;
      case 'WARNING':
        return <AlertTriangle className="w-5 h-5 text-yellow-600" />;
      case 'FAILED':
        return <X className="w-5 h-5 text-red-600" />;
      case 'SKIPPED':
        return <Clock className="w-5 h-5 text-gray-400" />;
      default:
        return null;
    }
  };

  const getStepColor = (status: string) => {
    switch (status) {
      case 'SUCCESS': return 'border-green-500 bg-green-50';
      case 'WARNING': return 'border-yellow-500 bg-yellow-50';
      case 'FAILED': return 'border-red-500 bg-red-50';
      case 'SKIPPED': return 'border-gray-300 bg-gray-50';
      default: return 'border-gray-300 bg-white';
    }
  };

  return (
    <div className="bg-white rounded-xl shadow-sm border border-gray-200 p-6">
      <h2 className="text-xl font-semibold mb-6">Validation Timeline</h2>

      <div className="relative">
        {steps.map((step, index) => (
          <div key={step.stepNumber} className="relative pb-8">
            {/* Connector Line */}
            {index < steps.length - 1 && (
              <div className="absolute left-4 top-10 bottom-0 w-0.5 bg-gray-300" />
            )}

            {/* Step Circle */}
            <div className={`relative flex items-start gap-4 p-4 rounded-lg border-2 ${getStepColor(step.stepStatus)}`}>
              <div className="flex-shrink-0 w-8 h-8 rounded-full bg-white border-2 border-current flex items-center justify-center z-10">
                {getStepIcon(step.stepStatus)}
              </div>

              {/* Step Content */}
              <div className="flex-1">
                <div className="flex items-center justify-between mb-2">
                  <h3 className="font-semibold text-gray-900">
                    Step {step.stepNumber}: {step.stepName.replace(/_/g, ' ')}
                  </h3>
                  <span className="text-sm text-gray-500">
                    {step.stepDurationMs}ms
                  </span>
                </div>

                {step.errorMessage && (
                  <div className="text-sm text-red-700 mb-2">
                    <strong>Error:</strong> {step.errorMessage}
                    {step.errorCode && <span className="ml-2 text-xs">({step.errorCode})</span>}
                  </div>
                )}

                {step.warningMessage && (
                  <div className="text-sm text-yellow-700 mb-2">
                    <strong>Warning:</strong> {step.warningMessage}
                  </div>
                )}

                {step.recommendation && (
                  <div className="text-sm text-blue-700 bg-blue-50 p-3 rounded mt-2">
                    <strong>Recommendation:</strong> {step.recommendation}
                  </div>
                )}

                {step.stepData && Object.keys(step.stepData).length > 0 && (
                  <details className="mt-2">
                    <summary className="text-sm text-gray-600 cursor-pointer">
                      View Details
                    </summary>
                    <pre className="text-xs bg-gray-100 p-2 rounded mt-2 overflow-x-auto">
                      {JSON.stringify(step.stepData, null, 2)}
                    </pre>
                  </details>
                )}
              </div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};

export default ValidationTimelineStepper;
```

**Component 2: Add to UploadHistory.tsx**
```typescript
// In certificate detail modal, fetch and display validation details
const [validationDetails, setValidationDetails] = useState<ValidationStep[]>([]);

useEffect(() => {
  if (selectedCertificate) {
    fetch(`/api/certificates/${selectedCertificate.id}/validation-detail`)
      .then(res => res.json())
      .then(data => setValidationDetails(data.validationSteps));
  }
}, [selectedCertificate]);

// In modal body
{validationDetails.length > 0 && (
  <ValidationTimelineStepper steps={validationDetails} />
)}
```

**Deliverables**:
- [ ] ValidationTimelineStepper component (3h)
- [ ] Integration into UploadHistory (2h)
- [ ] Visual testing (1h)

### Sprint 3.5 Deliverables

- ✅ **Database Schema**: validation_result_detail and validation_error_codes tables
- ✅ **Backend Recording**: ValidationDetailRecorder class with 8-step validation recording
- ✅ **API Endpoints**: GET /api/certificates/{id}/validation-detail
- ✅ **Frontend Components**: ValidationTimelineStepper with expandable details
- ✅ **Error Taxonomy**: 46 error codes with recommendations
- ✅ **Documentation**: Validation detail enhancement plan

---

## Sprint 4: Testing & Deployment (Week 4)

**Goal**: Comprehensive testing, re-validation campaign, and production deployment.

### Day 1-2: Re-validation Campaign

#### Task 4.1: Trigger Full DSC Re-validation

**Assignee**: Backend Developer
**Estimated Time**: 6 hours

**Create Re-validation API Endpoint**:

```cpp
// POST /api/internal/revalidate-all
// Trigger re-validation of all 29,610 DSCs

app.registerHandler(
    "/api/internal/revalidate-all",
    [](const HttpRequestPtr& req,
       std::function<void(const HttpResponsePtr&)>&& callback) {

        // Start async re-validation task
        std::thread([callback]() {
            PGconn* conn = getDbConnection();

            // Query all DSCs
            const char* query = "SELECT id, certificate_binary, issuer_dn "
                                "FROM certificate "
                                "WHERE certificate_type IN ('DSC', 'DSC_NC')";
            PGresult* res = PQexec(conn, query);

            int totalDscs = PQntuples(res);
            int validCount = 0;
            int invalidCount = 0;

            spdlog::info("Re-validation: Starting for {} DSCs", totalDscs);

            for (int i = 0; i < totalDscs; i++) {
                // Parse DSC
                std::string certId = PQgetvalue(res, i, 0);
                std::string issuerDn = PQgetvalue(res, i, 2);

                size_t dataLen = 0;
                unsigned char* binData = PQunescapeBytea(...);
                const unsigned char* p = binData;
                X509* dsc = d2i_X509(nullptr, &p, dataLen);
                PQfreemem(binData);

                // Validate with new link certificate logic
                DscValidationResult result = validateDscCertificate(conn, dsc, issuerDn);

                // Update validation_result table
                updateValidationResult(conn, certId, result);

                if (result.isValid) {
                    validCount++;
                } else {
                    invalidCount++;
                }

                X509_free(dsc);

                // Progress logging
                if ((i + 1) % 1000 == 0) {
                    spdlog::info("Re-validation progress: {}/{} ({:.1f}%)",
                                 i + 1, totalDscs, (i + 1) * 100.0 / totalDscs);
                }
            }

            PQclear(res);
            PQfinish(conn);

            spdlog::info("Re-validation complete: {} valid, {} invalid",
                         validCount, invalidCount);

            // Return response
            Json::Value resp;
            resp["success"] = true;
            resp["totalDscs"] = totalDscs;
            resp["validCount"] = validCount;
            resp["invalidCount"] = invalidCount;
            resp["validRate"] = (validCount * 100.0) / totalDscs;

            auto response = HttpResponse::newHttpJsonResponse(resp);
            callback(response);
        }).detach();

        // Immediate response
        Json::Value resp;
        resp["message"] = "Re-validation started in background";
        auto response = HttpResponse::newHttpJsonResponse(resp);
        callback(response);
    },
    {Post}
);
```

**Execute Re-validation**:
```bash
# Trigger re-validation
curl -X POST http://localhost:8081/api/internal/revalidate-all

# Monitor logs
docker logs -f icao-pkd-management | grep "Re-validation"
```

**Expected Results**:

| Metric | Before | After (Expected) | Improvement |
|--------|--------|------------------|-------------|
| Total DSC | 29,610 | 29,610 | - |
| Valid | 5,868 (19.8%) | ~12,000-15,000 (40-50%) | +6,132-9,132 |
| Invalid | 23,742 (80.2%) | ~14,610-17,610 (50-60%) | -9,132-6,132 |

**Deliverables**:
- [ ] Re-validation API implemented (3h)
- [ ] Re-validation executed (2h)
- [ ] Results documented (1h)

#### Task 4.2: Analyze Re-validation Results

**Assignee**: QA Engineer
**Estimated Time**: 4 hours

**Analysis Queries**:

```sql
-- 1. Overall validation statistics (before vs after)
SELECT
    COUNT(*) as total_dsc,
    SUM(CASE WHEN trust_chain_valid = true THEN 1 ELSE 0 END) as valid_count,
    SUM(CASE WHEN trust_chain_valid = false THEN 1 ELSE 0 END) as invalid_count,
    ROUND(100.0 * SUM(CASE WHEN trust_chain_valid = true THEN 1 ELSE 0 END) / COUNT(*), 2) as valid_percentage
FROM certificate
WHERE certificate_type IN ('DSC', 'DSC_NC');

-- 2. Validation rate by country
SELECT
    country_code,
    COUNT(*) as total_dsc,
    SUM(CASE WHEN trust_chain_valid = true THEN 1 ELSE 0 END) as valid_count,
    ROUND(100.0 * SUM(CASE WHEN trust_chain_valid = true THEN 1 ELSE 0 END) / COUNT(*), 2) as valid_rate
FROM certificate
WHERE certificate_type IN ('DSC', 'DSC_NC')
GROUP BY country_code
ORDER BY total_dsc DESC
LIMIT 20;

-- 3. Trust chain path analysis (link certificate usage)
SELECT
    CASE
        WHEN trust_chain_path LIKE '%→%→%→%' THEN '4+ steps (link cert)'
        WHEN trust_chain_path LIKE '%→%→%' THEN '3 steps (link cert)'
        WHEN trust_chain_path LIKE '%→%' THEN '2 steps (direct)'
        ELSE '1 step or empty'
    END as chain_type,
    COUNT(*) as count
FROM validation_result
WHERE trust_chain_valid = true
GROUP BY chain_type;

-- 4. Identify DSCs that changed from INVALID to VALID (link cert impact)
SELECT
    c.country_code,
    c.serial_number,
    c.subject_dn,
    vr_old.trust_chain_valid as old_validation,
    vr_new.trust_chain_valid as new_validation,
    vr_new.trust_chain_path
FROM certificate c
JOIN validation_result_backup vr_old ON c.id = vr_old.certificate_id  -- Backup before re-validation
JOIN validation_result vr_new ON c.id = vr_new.certificate_id  -- After re-validation
WHERE c.certificate_type IN ('DSC', 'DSC_NC')
  AND vr_old.trust_chain_valid = false
  AND vr_new.trust_chain_valid = true
ORDER BY c.country_code
LIMIT 100;
```

**Analysis Report Template**:
```markdown
# DSC Re-validation Analysis Report

## Executive Summary
- Total DSCs re-validated: 29,610
- Validation rate improvement: 19.8% → 45.3% (+128% increase)
- DSCs now valid due to link certificate: 6,132

## Link Certificate Impact
- Countries using link certificates: 15
- DSCs validated via link cert chains: 6,132 (20.7%)
- Average chain length: 2.8 steps

## Top Improvements by Country
| Country | Total DSC | Valid Rate Before | Valid Rate After | Improvement |
|---------|-----------|-------------------|------------------|-------------|
| CN | 3,456 | 15.2% | 52.1% | +2,275 DSCs |
| DE | 2,103 | 22.5% | 48.9% | +1,208 DSCs |
| ...

## Validation Failures (Remaining Invalid)
- CSCA not found: 8,234 (27.8%)
- Expired DSC: 4,103 (13.9%)
- Signature verification failed: 2,273 (7.7%)

## Recommendations
- Contact countries with high invalid rates
- Investigate signature verification failures
```

**Deliverables**:
- [ ] SQL analysis queries executed (2h)
- [ ] Analysis report written (2h)

### Day 3-4: Integration & Load Testing

#### Task 4.3: End-to-End Integration Tests

**Assignee**: QA Engineer
**Estimated Time**: 8 hours

**Test Scenarios**:

**Scenario 1: Upload Master List with Link Certificates** (2h)
```bash
# Upload China Master List (contains link certificates)
curl -X POST http://localhost:8081/api/upload/masterlist \
     -F "file=@test/data/icaopkd-001-complete-009668.ml"

# Expected:
# - Link certificates stored as CSCA
# - Both self-signed and link CSCAs in database
```

**Scenario 2: Upload LDIF with DSCs signed by old CSCA** (2h)
```bash
# Upload LDIF (DSCs signed by CSCA_old)
curl -X POST http://localhost:8081/api/upload/ldif \
     -F "file=@test/data/cn_dsc_collection.ldif"

# Expected:
# - DSCs validated successfully via link certificate chain
# - trust_chain_path populated: "DSC → CSCA_old → Link → CSCA_new"
```

**Scenario 3: Certificate Search with Link Certificates** (1h)
```bash
# Search for CSCAs
curl "http://localhost:8081/api/certificates/search?country=CN&certType=CSCA"

# Expected:
# - Returns both self-signed and link CSCAs
# - UI displays link certificates correctly
```

**Scenario 4: Export with Link Certificates** (1h)
```bash
# Export China certificates
curl "http://localhost:8081/api/certificates/export/country?country=CN&format=ZIP"

# Expected:
# - ZIP contains link certificates
# - File naming: CN_CSCA_LINK_{serial}.crt
```

**Scenario 5: DB-LDAP Sync with New DN Structure** (1h)
```bash
# Trigger sync
curl -X POST http://localhost:8081/api/sync/trigger

# Expected:
# - 0 discrepancies
# - No LDAP_ALREADY_EXISTS errors
```

**Scenario 6: Performance Under Load** (1h)
```bash
# Load test: Upload 10 LDIFs concurrently
for i in {1..10}; do
    curl -X POST http://localhost:8081/api/upload/ldif \
         -F "file=@test/data/collection_$i.ldif" &
done
wait

# Monitor:
# - CPU usage < 80%
# - Memory usage < 2GB
# - Average validation time < 20ms per DSC
```

**Deliverables**:
- [ ] All integration tests pass (8h)
- [ ] Test report with pass/fail status (included)

#### Task 4.4: Load & Stress Testing

**Assignee**: DevOps Engineer
**Estimated Time**: 4 hours

**Load Test 1: Concurrent Uploads** (2h)
```bash
# Apache Bench: 100 concurrent uploads
ab -n 100 -c 10 -T "multipart/form-data" \
   -p test_upload.txt \
   http://localhost:8081/api/upload/ldif
```

**Load Test 2: Certificate Search** (1h)
```bash
# JMeter: 1000 search requests
jmeter -n -t test/jmeter/certificate_search.jmx \
       -l results.jtl
```

**Load Test 3: Re-validation Stress** (1h)
```bash
# Trigger re-validation while system under load
curl -X POST http://localhost:8081/api/internal/revalidate-all &

# Concurrent operations
ab -n 500 -c 20 http://localhost:8081/api/certificates/search?country=CN
```

**Performance Metrics**:
| Metric | Target | Actual | Status |
|--------|--------|--------|--------|
| Upload throughput | > 10 files/min | ___ | ⏳ |
| Search response time | < 500ms | ___ | ⏳ |
| Validation throughput | > 100 DSC/sec | ___ | ⏳ |
| Memory usage | < 2GB | ___ | ⏳ |

**Deliverables**:
- [ ] Load tests executed (3h)
- [ ] Performance report (1h)

### Day 5-7: Production Deployment

#### Task 4.5: Staging Deployment

**Assignee**: DevOps Engineer
**Estimated Time**: 4 hours

**Deployment Steps**:

**Step 1: Backup Production Data** (1h)
```bash
# Backup PostgreSQL
pg_dump -h production-db -U pkd pkd > production_backup_20260220.sql

# Backup LDAP
ldapsearch -x -H ldap://production-ldap:389 \
           -b "dc=pkd,..." > production_ldap_backup_20260220.ldif

# Upload backups to S3
aws s3 cp production_backup_20260220.sql s3://icao-pkd-backups/
```

**Step 2: Deploy to Staging** (1h)
```bash
# Pull latest code
git pull origin main

# Build ARM64 images (GitHub Actions)
# Trigger: git push origin main

# Deploy to staging
./scripts/deploy-from-github-artifacts.sh all --env=staging
```

**Step 3: Run Smoke Tests on Staging** (1h)
```bash
# Health check
curl https://staging.icao-pkd.com/api/health

# Upload test file
curl -X POST https://staging.icao-pkd.com/api/upload/ldif \
     -F "file=@test/data/test_ldif.ldif"

# Verify trust chain validation
curl https://staging.icao-pkd.com/api/certificates/search?country=CN
```

**Step 4: Staging Acceptance Testing** (1h)
- [ ] Certificate upload works
- [ ] Link certificate validation works
- [ ] Trust chain path displayed correctly
- [ ] DB-LDAP sync reports 0 discrepancies

**Deliverables**:
- [ ] Staging deployment successful (3h)
- [ ] Smoke tests pass (1h)

#### Task 4.6: Production Deployment

**Assignee**: DevOps Engineer + Team Lead
**Estimated Time**: 6 hours

**Pre-deployment Checklist**:
- [ ] Code review approved
- [ ] All tests pass (unit, integration, load)
- [ ] Staging deployment validated
- [ ] Backup verified (can restore)
- [ ] Rollback plan documented
- [ ] Team on standby

**Deployment Steps**:

**Step 1: Maintenance Window** (0.5h)
```bash
# Put system in maintenance mode
docker exec icao-frontend /scripts/enable-maintenance-mode.sh

# Display message: "System undergoing scheduled maintenance"
```

**Step 2: Database Migration** (0.5h)
```bash
# Apply migration script
psql -h production-db -U pkd -d pkd \
     -f docker/init-scripts/05-link-cert-validation-schema.sql

# Verify migration
psql -h production-db -U pkd -d pkd \
     -c "SELECT column_name FROM information_schema.columns WHERE table_name='validation_result'"
```

**Step 3: LDAP Migration** (2h)
```bash
# Execute migration script
./scripts/ldap-migrate-production.sh

# Verification:
# - 536 CSCAs in LDAP (previously 531)
# - New DN format (fingerprint-based)
```

**Step 4: Deploy New Code** (1h)
```bash
# Deploy updated services
./scripts/deploy-from-github-artifacts.sh all --env=production

# Verify services started
docker ps | grep icao-pkd
```

**Step 5: Re-validation Campaign** (1h)
```bash
# Trigger full DSC re-validation
curl -X POST https://production.icao-pkd.com/api/internal/revalidate-all

# Monitor progress (estimated 10 minutes for 29,610 DSCs)
docker logs -f icao-pkd-management | grep "Re-validation progress"
```

**Step 6: Smoke Tests** (0.5h)
```bash
# Health check
curl https://production.icao-pkd.com/api/health

# Certificate search
curl "https://production.icao-pkd.com/api/certificates/search?country=CN&certType=CSCA"

# DB-LDAP sync status
curl https://production.icao-pkd.com/api/sync/status
```

**Step 7: Exit Maintenance Mode** (0.5h)
```bash
docker exec icao-frontend /scripts/disable-maintenance-mode.sh
```

**Post-deployment Monitoring** (24 hours):
- [ ] Monitor error logs
- [ ] Check trust chain validation statistics
- [ ] Verify user reports (no issues)

**Rollback Plan** (if deployment fails):
```bash
# 1. Stop services
docker compose down

# 2. Restore PostgreSQL
psql -h production-db -U pkd -d pkd < production_backup_20260220.sql

# 3. Restore LDAP
ldapdelete -x -r "dc=pkd,..."
ldapadd -x -D "cn=admin,..." -f production_ldap_backup_20260220.ldif

# 4. Revert code
git revert <commit-hash>
docker compose up -d --build

# 5. Verify rollback
curl https://production.icao-pkd.com/api/health
```

**Deliverables**:
- [ ] Production deployment successful (6h)
- [ ] Re-validation complete (10 minutes)
- [ ] Statistics updated (validation rate improved)
- [ ] Documentation updated

#### Task 4.7: Documentation & Knowledge Transfer

**Assignee**: Team Lead
**Estimated Time**: 4 hours

**Documentation Updates**:

**1. CLAUDE.md** (1h)
- Update "Trust Chain Validation" section
- Add link certificate algorithm description
- Update validation statistics

**2. Architecture Diagrams** (1h)
- Add link certificate chain diagram
- Update trust chain validation flow

**3. API Documentation** (0.5h)
- Update OpenAPI specs (already done in Task 3.5)
- Add `trust_chain_path` field examples

**4. Deployment Guide** (1h)
- LDAP DN migration procedure
- Re-validation campaign steps
- Rollback instructions

**5. Knowledge Transfer Session** (0.5h)
- Present implementation to team
- Demo link certificate validation
- Q&A session

**Deliverables**:
- [ ] All documentation updated (3h)
- [ ] Knowledge transfer complete (1h)

### Sprint 4 Deliverables

- ✅ **Re-validation Complete**: All 29,610 DSCs re-validated with new algorithm
- ✅ **Validation Rate Improved**: From 19.8% to 40-50% (target met)
- ✅ **Production Deployment**: Fully deployed with 0 downtime
- ✅ **Documentation Complete**: Architecture, API, deployment guides updated
- ✅ **Monitoring Active**: 24-hour post-deployment monitoring

---

## Project Completion Checklist

### Technical Deliverables

- ✅ **LDAP Storage Fix**:
  - [ ] Fingerprint-based DN structure implemented
  - [ ] All 536 CSCAs in LDAP (0 discrepancies)
  - [ ] Migration scripts tested and documented

- ✅ **Link Certificate Validation**:
  - [ ] Multi-CSCA lookup function
  - [ ] Chain building algorithm (with circular ref protection)
  - [ ] Chain validation logic (signature + expiration)
  - [ ] DSC validation integration

- ✅ **Database Schema**:
  - [ ] `trust_chain_path` column added
  - [ ] Migration SQL scripts
  - [ ] Rollback scripts

- ✅ **Testing**:
  - [ ] Unit tests (90%+ coverage)
  - [ ] Integration tests (100% pass)
  - [ ] Load tests (performance targets met)

- ✅ **Documentation**:
  - [ ] CSCA_STORAGE_AND_LINK_CERT_ISSUES.md
  - [ ] CSCA_ISSUES_IMPLEMENTATION_PLAN.md (this document)
  - [ ] CLAUDE.md updated
  - [ ] API documentation updated

### Business Deliverables

- ✅ **ICAO Compliance**: Link certificate support per ICAO Doc 9303-12
- ✅ **Data Integrity**: 100% DB-LDAP consistency
- ✅ **Trust Chain Improvement**: DSC valid rate > 40%
- ✅ **No Regressions**: Existing valid certificates remain valid

### Deployment Checklist

- [ ] Staging deployment successful
- [ ] Production backup created
- [ ] Production deployment successful
- [ ] Re-validation campaign complete
- [ ] Post-deployment monitoring (24h)
- [ ] User acceptance testing

---

## Risk Management

### Critical Risks

| Risk | Probability | Impact | Mitigation | Owner |
|------|------------|--------|------------|-------|
| **LDAP Migration Failure** | Low | Critical | Full backup, rollback plan, staging validation | DevOps |
| **Performance Degradation** | Medium | High | CSCA caching, load testing, monitoring | Backend Team |
| **False Positive Validations** | Low | Critical | Manual verification of sample DSCs | QA Team |
| **Production Downtime** | Medium | High | Maintenance window, rollback plan | DevOps |

### Mitigation Strategies

**LDAP Migration**:
- Full LDAP backup before migration
- Test migration on staging environment
- Automated rollback script
- Verify entry count after migration

**Performance**:
- CSCA in-memory caching (1h TTL)
- Parallel validation for batch operations
- Chain depth limit (max 5 steps)
- Load testing with 1000+ DSCs

**Data Integrity**:
- Manual verification of 100 random DSCs before/after
- SQL queries to detect unexpected changes
- Audit log for all validation changes

**Rollback**:
- One-command rollback script
- Automated health checks after rollback
- Communication plan (user notification)

---

## Team & Resources

### Team Structure

| Role | Name | Responsibilities | Allocation |
|------|------|------------------|------------|
| **Team Lead** | TBD | Code review, architecture, deployment approval | 40% |
| **Senior Backend Developer** | TBD | Chain building, validation logic, optimization | 100% |
| **Backend Developer** | TBD | LDAP DN migration, API updates | 100% |
| **Frontend Developer** | TBD | UI updates, trust chain visualization | 50% |
| **QA Engineer** | TBD | Testing, validation analysis | 100% |
| **DevOps Engineer** | TBD | Deployment, LDAP migration, monitoring | 80% |
| **Database Administrator** | TBD | Schema migration, performance tuning | 30% |

### Resource Requirements

**Development Environment**:
- Docker Compose with PostgreSQL + OpenLDAP
- Test certificates (OpenSSL generated)
- Sample LDIF/Master List files

**Staging Environment**:
- Production-like infrastructure
- Copy of production data (anonymized)

**Production Environment**:
- Backup storage (S3 or equivalent)
- Monitoring tools (Grafana, Prometheus)
- Maintenance window (4-6 hours)

---

## Success Metrics

### Quantitative Metrics

| Metric | Baseline | Target | Actual | Status |
|--------|----------|--------|--------|--------|
| **LDAP Completeness** | 531/536 (99.07%) | 536/536 (100%) | ___ | ⏳ |
| **DSC Valid Rate** | 5,868/29,610 (19.8%) | > 12,000/29,610 (40%) | ___ | ⏳ |
| **Upload Conflicts** | 5 (LDAP_ALREADY_EXISTS) | 0 | ___ | ⏳ |
| **Link Cert Support** | ❌ No | ✅ Yes | ___ | ⏳ |
| **Average Validation Time** | ~5ms | < 15ms | ___ | ⏳ |
| **Test Coverage** | ~70% | > 90% | ___ | ⏳ |

### Qualitative Metrics

- ✅ **ICAO Compliance**: System fully implements ICAO Doc 9303-12 link certificate requirements
- ✅ **Code Quality**: Code review approved, no major issues
- ✅ **Documentation**: Complete and up-to-date
- ✅ **Team Knowledge**: All team members understand new algorithm
- ✅ **User Satisfaction**: No user complaints post-deployment

---

## Timeline Summary

| Sprint | Duration | Key Deliverables | Status |
|--------|----------|------------------|--------|
| **Sprint 1** | Week 1 | LDAP storage fix, DN migration | 📋 Planning |
| **Sprint 2** | Week 2 | Link cert validation core algorithm | 📋 Planning |
| **Sprint 3** | Week 3 | DSC validation integration, API updates | 📋 Planning |
| **Sprint 4** | Week 4 | Testing, re-validation, production deployment | 📋 Planning |

**Total Duration**: 4 weeks (2026-01-27 to 2026-02-20)

**Key Milestones**:
- ✅ **Week 1 End**: All 536 CSCAs in LDAP
- ✅ **Week 2 End**: Chain building algorithm complete
- ✅ **Week 3 End**: Full integration and API updates
- ✅ **Week 4 End**: Production deployment and validation rate > 40%

---

## Communication Plan

### Status Updates

**Daily Standups** (15 minutes):
- What did you complete yesterday?
- What will you work on today?
- Any blockers?

**Weekly Status Report** (Fridays):
- Sprint progress (completed tasks)
- Blockers and risks
- Next week's priorities

**Deployment Communication**:
- **T-48h**: Notify users of maintenance window
- **T-24h**: Reminder notification
- **T-1h**: System enters maintenance mode
- **T+0**: Deployment complete, system restored
- **T+24h**: Post-deployment report

---

## Appendix: Scripts & Tools

### Script 1: LDAP Backup Script

See [CSCA_STORAGE_AND_LINK_CERT_ISSUES.md - Appendix B](CSCA_STORAGE_AND_LINK_CERT_ISSUES.md#appendix-b-ldap-migration-script)

### Script 2: Test Certificate Generation

Included in Task 2.3

### Script 3: Re-validation API

Included in Task 4.1

---

**END OF IMPLEMENTATION PLAN**

**Next Actions**:
1. Schedule kickoff meeting
2. Assign team members to tasks
3. Set up development environments
4. Begin Sprint 1 (LDAP Storage Fix)
