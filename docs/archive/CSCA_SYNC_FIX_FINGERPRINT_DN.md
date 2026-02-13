# CSCA Sync Issue Fix: Fingerprint-Based DN Implementation

**Date**: 2026-01-26
**Version**: v2.1.0
**Severity**: Critical
**Status**: ✅ Resolved

---

## Executive Summary

Critical data integrity issue where 5 out of 536 CSCA certificates failed to sync to LDAP despite being marked as stored in the database. Root cause identified as duplicate LDAP DNs caused by legacy DN format that stripped non-standard attributes from subject DN. Resolved by implementing fingerprint-based DN format.

**Impact**:
- **Before**: 531/536 certificates in LDAP (5 missing, 0.93% data loss)
- **After**: 536/536 certificates in LDAP (100% integrity, Discrepancy=0)

---

## Issue Description

### Symptoms

**DB-LDAP Sync Discrepancy**:
```
Database:      536 CSCA certificates (all marked stored_in_ldap=TRUE)
LDAP:          531 CSCA certificates (actual count)
Discrepancy:   +5 certificates missing in LDAP
```

**Severity Assessment**:
- Data integrity violation: Database claims certificates are stored but LDAP doesn't have them
- Silent data loss: No error logs, upload appears successful
- Affects trust chain validation: Missing CSCA certificates break validation chains
- Reproducible: Issue persisted across multiple clean data uploads

### Discovery Process

1. **Initial Detection** (2026-01-26):
   - User noticed Discrepancy=-5 on Sync Status page after Master List upload
   - Upload logs showed "Master List processing completed: 536 CSCA, LDAP: 536"
   - Database query confirmed all 536 marked as stored_in_ldap=TRUE

2. **Data Verification**:
   - Direct LDAP count: 531 certificates
   - Fingerprint comparison between DB and LDAP revealed exactly 5 missing certificates

3. **Persistence Confirmation**:
   - Cleaned all data (DB + LDAP)
   - Re-uploaded Master List from scratch
   - Issue reproduced identically: Same 5 certificates missing

---

## Missing Certificates Analysis

### Identified Missing Certificates

| Country | Fingerprint (First 8 chars) | Subject DN Pattern | Issue |
|---------|----------------------------|-------------------|-------|
| CN (China) | e3dbd849 | CN=China Passport...,serialNumber=X | serialNumber in subject DN |
| CN (China) | af46df95 | CN=China Passport...,serialNumber=Y | serialNumber in subject DN |
| CN (China) | c2bec8da | CN=China Passport...,serialNumber=Z | serialNumber in subject DN |
| DE (Germany) | 7f3835cb | CN=csca-germany,serialNumber=101 | serialNumber=101 vs 100 |
| KZ (Kazakhstan) | 1de03715 | C=KZ,O=Republic of Kazakhstan... | Non-standard DN attributes |

**Pattern Identified**: All missing certificates had non-standard attributes in subject DN that caused DN collisions after attribute removal.

### China Certificates Example

Three different China CSCA certificates:
```
Certificate 1: CN=China Passport...,serialNumber=X,OU=...,O=...,C=CN
Certificate 2: CN=China Passport...,serialNumber=Y,OU=...,O=...,C=CN
Certificate 3: CN=China Passport...,serialNumber=Z,OU=...,O=...,C=CN
```

After `extractStandardAttributes()` removes non-standard `serialNumber`:
```
All three become: CN=China Passport...,OU=...,O=...,C=CN
```

**Result**: All three generate identical LDAP DN → Second and third overwrite first.

---

## Root Cause Analysis

### Legacy DN Format Vulnerability

**Legacy DN Construction** ([main.cpp:2250-2266](../services/pkd-management/src/main.cpp#L2250-L2266)):
```
DN format: cn={ESCAPED-SUBJECT-DN}+sn={SERIAL},o={csca|dsc|lc},c={COUNTRY},...
```

**Problem Chain**:

1. **Attribute Extraction** (`extractStandardAttributes()` function):
   - Removes non-standard attributes: `serialNumber`, `emailAddress`, etc.
   - X.509 standard defines: CN, OU, O, L, ST, C
   - Non-standard attributes are stripped to ensure LDAP compatibility

2. **DN Collision**:
   - Different certificates with same standard attributes generate identical DNs
   - Example: Germany certificates with serialNumber=100 and serialNumber=101
   - Both produce: `cn=CN\=csca-germany\,OU\=bsi\,O\=bund\,C\=DE+sn=1,o=csca,c=DE,...`

3. **LDAP Behavior**:
   ```cpp
   // main.cpp lines 2559-2568
   if (rc == LDAP_ALREADY_EXISTS) {
       // Try UPDATE instead of ADD
       LDAPMod* updateMods[] = { ... };
       int updateRc = ldap_modify_ext_s(ld, fullDn.c_str(), updateMods, ...);
       if (updateRc == LDAP_SUCCESS) {
           return fullDn;  // Returns DN as if successful
       }
   }
   ```

   - `LDAP_ALREADY_EXISTS` error triggers UPDATE operation
   - Second certificate overwrites first certificate's data
   - Function returns DN successfully (no error)
   - Database marks second certificate as stored
   - First certificate is lost in LDAP but marked as stored in DB

4. **Silent Data Loss**:
   - No error logged (UPDATE succeeded)
   - No validation that certificate content matches
   - Database consistency violated: Claims first certificate is stored but LDAP has second

### Why This Wasn't Caught Earlier

- Most CSCA certificates have unique subject DNs (no duplicates)
- Only 5 out of 536 certificates (0.93%) had this specific collision pattern
- Upload process appeared successful (no errors)
- Previous testing didn't include detailed fingerprint-level comparison

---

## Solution: Fingerprint-Based DN

### Design Decision

**Fingerprint-Based DN Format**:
```
DN format: cn={SHA256-FINGERPRINT},o={csca|dsc|lc},c={COUNTRY},...
```

**Advantages**:
1. **Guaranteed Uniqueness**: SHA-256 hash is cryptographically unique per certificate
2. **No Attribute Dependency**: Independent of subject DN content or structure
3. **Collision-Free**: No possibility of two certificates generating same DN
4. **Future-Proof**: Works regardless of certificate DN format changes
5. **Simple**: No need for attribute extraction or escaping logic

### Implementation

**Modified Master List Processing** ([main.cpp:4006-4018](../services/pkd-management/src/main.cpp#L4006-L4018)):

```cpp
// Sprint 3 Fix: Use fingerprint-based DN to avoid duplicates
std::string ldapDn = saveCertificateToLdap(ld, ldapCertType, countryCode,
                                            subjectDn, issuerDn, serialNumber,
                                            fingerprint, derBytes,
                                            "", "", "", false);  // useLegacyDn=false
```

**Key Change**: `useLegacyDn=false` parameter activates fingerprint-based DN construction.

**DN Examples**:
```
Before (Legacy):
  cn=CN\=csca-germany\,OU\=bsi\,O\=bund\,C\=DE+sn=1,o=csca,c=DE,...

After (Fingerprint):
  cn=7f3835cb1d2cc592535b7f8067a290eb4243e3807191eb8c6e38c27de1a229e2,o=csca,c=DE,...
```

---

## Additional Fixes

### 1. Auto-Create dc=data Container

**Problem**: When LDAP is completely empty, dc=data container doesn't exist, causing LDAP Error 32.

**Fix** ([main.cpp:2350-2400](../services/pkd-management/src/main.cpp#L2350-L2400)):

```cpp
bool ensureCountryOuExists(LDAP* ld, const std::string& countryCode, bool isNcData) {
    // Check if data container exists
    std::string dataContainer = isNcData ? appConfig.ldapNcDataContainer
                                         : appConfig.ldapDataContainer;
    std::string dataContainerDn = dataContainer + "," + appConfig.ldapBaseDn;

    // ... search for container ...

    if (rc == LDAP_NO_SUCH_OBJECT) {
        // Create dc=data or dc=nc-data container
        LDAPMod* dcMods[] = { ... };
        ldap_add_ext_s(ld, dataContainerDn.c_str(), dcMods, ...);
    }

    // ... then create country and OU entries ...
}
```

**Benefit**: Service can initialize LDAP from completely empty state.

### 2. Configurable Container Names

**Problem**: Container names "dc=data" and "dc=nc-data" were hardcoded throughout the codebase.

**Fix**:

1. **AppConfig Fields** ([main.cpp:151-153](../services/pkd-management/src/main.cpp#L151-L153)):
   ```cpp
   std::string ldapDataContainer = "dc=data";
   std::string ldapNcDataContainer = "dc=nc-data";
   ```

2. **Environment Variables** ([main.cpp:211-212](../services/pkd-management/src/main.cpp#L211-L212)):
   ```cpp
   if (auto val = std::getenv("LDAP_DATA_CONTAINER")) config.ldapDataContainer = val;
   if (auto val = std::getenv("LDAP_NC_DATA_CONTAINER")) config.ldapNcDataContainer = val;
   ```

3. **Docker Compose** ([docker-compose.yaml:90-91](../docker/docker-compose.yaml#L90-L91)):
   ```yaml
   - LDAP_DATA_CONTAINER=dc=data
   - LDAP_NC_DATA_CONTAINER=dc=nc-data
   ```

**Benefit**: Container names can be changed without recompiling the service.

---

## Verification Results

### Upload Verification

**Upload ID**: `90d4743c-7494-48fa-b2ea-2a0997a2b236`
**File**: `ICAO_ml_December2025.ml`
**Status**: `COMPLETED`
**Processing Time**: 3 seconds (2026-01-26 16:35:02 → 16:35:05)

### Database Status

```sql
SELECT certificate_type, COUNT(*) as count,
       COUNT(*) FILTER (WHERE stored_in_ldap = TRUE) as stored_in_ldap
FROM certificate
WHERE certificate_type = 'CSCA'
GROUP BY certificate_type;
```

```
certificate_type | count | stored_in_ldap
-----------------+-------+----------------
CSCA             |   536 |            536
```

✅ All 536 certificates marked as stored in LDAP.

### LDAP Status

```bash
$ ldapsearch -x ... "(objectClass=pkdDownload)" dn | grep "^dn:" | wc -l
536
```

✅ All 536 certificates present in LDAP.

### Previously Missing Certificates

```bash
$ # Check all 5 previously missing certificates
$ for fp in e3dbd849... af46df95... c2bec8da... 7f3835cb... 1de03715...; do
    ldapsearch -x ... "(cn=$fp)" dn
  done
```

```
✅ e3dbd849509013042e40273775188afcb1a7e033b8c5563e214a9e2715d7881f: FOUND
✅ af46df95a5705f07f3cd1a68eccc0110a18dc75265c0dd2b4eefcf630a28a575: FOUND
✅ c2bec8da4e19bcd8e38794e391c73246fd205270d53afed4c709a7078e5efd7a: FOUND
✅ 7f3835cb1d2cc592535b7f8067a290eb4243e3807191eb8c6e38c27de1a229e2: FOUND
✅ 1de03715e992007eff9c2a59204ed5a387324b95717e2ead2991b077ea6e5eb0: FOUND
```

✅ All previously missing certificates now present in LDAP.

### Sync Status API

```bash
$ curl -s http://localhost:8080/api/sync/status | jq '.discrepancy'
```

```json
{
  "csca": 0,
  "dsc": 0,
  "crl": 0
}
```

✅ **Discrepancy = 0** (Complete synchronization achieved)

### DN Format Verification

**Sample LDAP DNs**:
```
dn: cn=64b542aec2b5eb070a40dd37b78a66b91a9475053762d8d04f0ba13f5aea9963,o=csca,c=LV,...
dn: cn=2b73147a5a526b7d9838dbf9f398b976b9615167cc3a625d108fe04c7e10eb95,o=csca,c=LV,...
dn: cn=7f3835cb1d2cc592535b7f8067a290eb4243e3807191eb8c6e38c27de1a229e2,o=csca,c=DE,...
```

✅ Fingerprint-based DN format (64-character hex fingerprints) confirmed.

---

## Modified Files

### 1. services/pkd-management/src/main.cpp

**AppConfig Enhancement**:
```cpp
// Lines 151-153: Add container name configuration
std::string ldapDataContainer = "dc=data";
std::string ldapNcDataContainer = "dc=nc-data";

// Lines 211-212: Load from environment variables
if (auto val = std::getenv("LDAP_DATA_CONTAINER")) config.ldapDataContainer = val;
if (auto val = std::getenv("LDAP_NC_DATA_CONTAINER")) config.ldapNcDataContainer = val;
```

**LDAP Container Auto-Creation**:
```cpp
// Lines 2350-2400: ensureCountryOuExists() enhancement
// Check if dc=data exists, create if missing
// Create country entry (c=XX)
// Create organization unit entry (o=csca, o=dsc, o=lc, o=crl)
```

**Fingerprint-Based DN**:
```cpp
// Lines 4006-4018: Master List CMS processing
std::string ldapDn = saveCertificateToLdap(ld, ldapCertType, countryCode,
                                            subjectDn, issuerDn, serialNumber,
                                            fingerprint, derBytes,
                                            "", "", "", false);  // useLegacyDn=false

// Lines 4100-4115: Master List PKCS7 processing (same change)
```

**Build ID Update**:
```cpp
// Line 8702: Update version
spdlog::info("====== ICAO Local PKD v2.1.0 SPRINT3-LINK-CERT-VALIDATION (Build 20260126-134714) ======");
```

### 2. docker/docker-compose.yaml

```yaml
# Lines 90-91: Add LDAP container environment variables
- LDAP_DATA_CONTAINER=dc=data
- LDAP_NC_DATA_CONTAINER=dc=nc-data
```

### 3. services/pkd-management/BUILD_ID

```
20260126-134714
```

---

## Technical Notes

### CSCA vs LC Organization Unit Discrepancy

**Observed**:
- Database: 476 self-signed CSCA + 60 link certificates
- LDAP: 477 entries in o=csca + 59 entries in o=lc

**Cause**: Romania certificate edge case
```
Subject DN: CN=CSCA Romania,O=DGP,C=RO  (uppercase RO)
Issuer DN:  CN=CSCA Romania,O=DGP,C=ro  (lowercase ro)
```

**Analysis**:
- Technically subject_dn ≠ issuer_dn (different case)
- Database classifies as link certificate (subject ≠ issuer)
- Practically a self-signed certificate (same entity, case difference only)
- LDAP stored in o=csca (correct classification)

**Impact**: None. Total count matches (536=536), just different classification of 1 edge case.

### Why Fingerprint-Based DN is Superior

1. **Cryptographic Guarantee**: SHA-256 collision probability is negligible (2^-256)
2. **No Parsing Required**: No need to extract, standardize, or escape DN attributes
3. **Performance**: Simple string comparison instead of complex DN parsing
4. **Maintainability**: Simpler code, fewer edge cases
5. **Forward Compatible**: Works with any future certificate format

### Backward Compatibility

**Legacy DN format still supported** via `useLegacyDn=true` parameter for:
- Existing LDAP data migration scenarios
- Testing and comparison
- Compatibility with external systems expecting legacy format

**Migration Path**:
- New uploads use fingerprint-based DN
- Existing LDAP data remains unchanged
- No forced migration required (both formats coexist)

---

## Lessons Learned

### Detection Gaps

1. **No Fingerprint-Level Validation**: Upload process didn't verify all certificates made it to LDAP
2. **Silent UPDATE**: LDAP_ALREADY_EXISTS handling masked the duplicate DN problem
3. **Insufficient Monitoring**: Discrepancy count was visible but not investigated until user noticed

### Process Improvements

1. **Add Validation Step**: After upload, compare DB and LDAP fingerprint sets
2. **Alert on Discrepancy**: Trigger warning if discrepancy > 0 after upload
3. **Log Duplicate DNs**: Warn when LDAP_ALREADY_EXISTS occurs during certificate ADD
4. **Automated Testing**: Add test case for certificates with non-standard attributes

### Code Quality

1. **Prefer Simple Solutions**: Fingerprint-based DN is simpler than attribute extraction
2. **Fail Loudly**: UPDATE on LDAP_ALREADY_EXISTS should warn, not silently proceed
3. **Configuration Over Code**: Environment variables avoid recompilation

---

## Testing Recommendations

### Regression Testing

1. **Upload Master List with 536 certificates**:
   ```bash
   # Clean data
   source scripts/db-helpers.sh && db_clean_all
   source scripts/ldap-helpers.sh && ldap_delete_all

   # Upload
   # Upload via UI: ICAO_ml_December2025.ml

   # Verify
   db_count_certs  # Should be 536
   ldap_count_all  # Should be 536
   curl -s http://localhost:8080/api/sync/status | jq '.discrepancy'  # Should be all 0
   ```

2. **Test Previously Problematic Certificates**:
   ```bash
   # Query the 5 specific fingerprints
   for fp in e3dbd849... af46df95... c2bec8da... 7f3835cb... 1de03715...; do
       ldapsearch -x ... "(cn=$fp)" dn
   done
   # All should return results
   ```

3. **Test Empty LDAP Initialization**:
   ```bash
   # Complete LDAP cleanup
   ldap_delete_all
   ldapsearch -x ... "(objectClass=*)" dn  # Verify dc=data is gone

   # Upload should auto-create dc=data
   # Upload via UI

   # Verify dc=data exists
   ldapsearch -x ... -b "dc=data,..." "(objectClass=*)" dn
   ```

### Load Testing

- Upload multiple LDIF files in sequence
- Verify no DN collisions occur
- Monitor database stored_in_ldap flags vs actual LDAP count

---

## Future Considerations

### Performance

Fingerprint-based DN may have different LDAP indexing characteristics:
- 64-character hex string vs variable-length subject DN
- May affect search performance (better or worse depending on LDAP implementation)
- Monitor query performance after production deployment

### Migration Strategy

If needing to migrate existing legacy DN entries:
1. Export all certificates from LDAP
2. Rebuild LDAP with fingerprint-based DNs
3. Use reconciliation to sync missing entries
4. Verify fingerprint-level match

### DN Format Standardization

Consider documenting DN format choice in ICAO PKD specifications:
- Fingerprint-based DN avoids collision issues
- Simpler for implementers (no DN parsing required)
- Better interoperability across different LDAP implementations

---

## Related Documents

- **[SPRINT3_COMPLETION_SUMMARY.md](SPRINT3_COMPLETION_SUMMARY.md)** - Sprint 3 overview
- **[DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md)** - Development workflow
- **[SOFTWARE_ARCHITECTURE.md](SOFTWARE_ARCHITECTURE.md)** - LDAP DIT structure
- **[LDAP_QUERY_GUIDE.md](LDAP_QUERY_GUIDE.md)** - LDAP query examples

---

## Conclusion

Critical data integrity issue resolved by implementing fingerprint-based LDAP DN format. Root cause was legacy DN format creating collisions when non-standard X.509 attributes were stripped from subject DN. Solution provides:

✅ **100% Data Integrity**: All 536 certificates synchronized (Discrepancy=0)
✅ **Guaranteed Uniqueness**: SHA-256 fingerprint prevents all DN collisions
✅ **Simplified Logic**: No attribute extraction or escaping required
✅ **Enhanced Reliability**: Auto-creates LDAP containers, configurable via environment variables
✅ **Production Ready**: Verified with complete Master List upload and fingerprint-level validation

**Build**: 20260126-134714
**Status**: ✅ Deployed and Verified
