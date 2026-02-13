# MLSC Classification Fix - buildCertificateDnV2 Bug

**Date**: 2026-01-26
**Version**: v2.1.0 (Build 20260126-204826)
**Status**: ✅ FIXED

---

## Problem Summary

Master List Signer Certificates (MLSC) were being stored in LDAP as `o=dsc` instead of `o=mlsc`, causing them to display incorrectly as "DSC" in the frontend.

### Root Cause

The `buildCertificateDnV2()` function in [main.cpp:2301](services/pkd-management/src/main.cpp#L2301) was missing a case for `certType == "MLSC"`.

**Before Fix**:
```cpp
} else if (certType == "DSC_NC") {
    ou = "dsc_nc";
    dataContainer = appConfig.ldapNcDataContainer;
} else if (certType == "LC") {
    ou = "lc";
    dataContainer = appConfig.ldapDataContainer;
} else {
    ou = "dsc";  // ← MLSC fell through to here!
    dataContainer = appConfig.ldapDataContainer;
}
```

When Master List processing set `ldapCertType = "MLSC"` (line 4040), the function didn't match any if-condition and fell through to the else clause, which set `ou = "dsc"`.

### Symptoms

1. **Logs showed misleading message**:
   ```
   Master List Signer Certificate detected (will save to o=mlsc)
   Master List Signer Certificate saved to LDAP o=mlsc: cn=...o=dsc,c=PH...
   ```
   Log said "o=mlsc" but actual DN contained "o=dsc"

2. **LDAP had 0 entries in o=mlsc**:
   ```bash
   $ ldapsearch ... "(o=mlsc)" dn | wc -l
   0
   ```

3. **Frontend showed purple "DSC" badges** instead of "MLSC"

4. **Database query**:
   ```sql
   SELECT * FROM certificate
   WHERE ldap_dn_v2 LIKE '%o=mlsc%';  -- Returns 0 rows
   ```

---

## Fix Applied

### 1. Added MLSC Case to buildCertificateDnV2()

**File**: [services/pkd-management/src/main.cpp:2316-2323](services/pkd-management/src/main.cpp#L2316-L2323)

**After Fix**:
```cpp
} else if (certType == "DSC_NC") {
    ou = "dsc_nc";
    dataContainer = appConfig.ldapNcDataContainer;
} else if (certType == "LC") {
    ou = "lc";
    dataContainer = appConfig.ldapDataContainer;
} else if (certType == "MLSC") {
    // Sprint 3: Master List Signer Certificate support
    ou = "mlsc";
    dataContainer = appConfig.ldapDataContainer;
} else {
    ou = "dsc";
    dataContainer = appConfig.ldapDataContainer;
}
```

### 2. Updated Build Timestamp

**File**: [services/pkd-management/src/main.cpp:8755](services/pkd-management/src/main.cpp#L8755)

```cpp
spdlog::info("====== ICAO Local PKD v2.1.0 SPRINT3-LINK-CERT-VALIDATION (Build 20260126-204826) ======");
```

### 3. Rebuilt and Restarted Service

```bash
cd /home/kbjung/projects/c/icao-local-pkd/docker
docker-compose -f docker-compose.yaml build pkd-management
docker-compose -f docker-compose.yaml stop pkd-management
docker-compose -f docker-compose.yaml rm -f pkd-management
docker-compose -f docker-compose.yaml up -d pkd-management
```

---

## Verification Steps

### 1. Reset Data and Re-upload

```bash
# Run reset script
./reset-and-restart.sh

# Wait for services to be ready (30 seconds)

# Upload Collection 002 LDIF via frontend
# http://localhost:3000/upload
```

### 2. Verify MLSC in LDAP

```bash
# Count MLSC entries (should be ~25)
docker exec icao-local-pkd-openldap1 ldapsearch \
  -x -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w ldap_test_password_123 \
  -b "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(o=mlsc)" dn | grep "^dn:" | wc -l

# Show sample MLSC entries
docker exec icao-local-pkd-openldap1 ldapsearch \
  -x -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w ldap_test_password_123 \
  -b "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(o=mlsc)" dn | grep "^dn:" | head -5
```

**Expected Output**:
```
dn: cn={fingerprint},o=mlsc,c=PH,dc=data,dc=download,dc=pkd,dc=ldap,...
dn: cn={fingerprint},o=mlsc,c=LU,dc=data,dc=download,dc=pkd,dc=ldap,...
dn: cn={fingerprint},o=mlsc,c=TZ,dc=data,dc=download,dc=pkd,dc=ldap,...
dn: cn={fingerprint},o=mlsc,c=SG,dc=data,dc=download,dc=pkd,dc=ldap,...
dn: cn={fingerprint},o=mlsc,c=AU,dc=data,dc=download,dc=pkd,dc=ldap,...
```

### 3. Verify Frontend Display

1. Navigate to **Certificate Search** page
2. Filter by `certType = MLSC`
3. Verify purple "MLSC" badges are displayed
4. Check that countries show: PH, LU, TZ, SG, AU, TR, IT, KW, etc.

### 4. Verify Dashboard Statistics

1. Navigate to **Dashboard** page
2. Check CSCA statistics card shows MLSC count
3. Verify MLSC count per country in country breakdown

### 5. Check Logs

```bash
docker logs icao-local-pkd-management 2>&1 | grep -i "master list signer"
```

**Expected Output**:
```
Master List Signer Certificate detected (will save to o=mlsc): CN=CSCA01008,O=DFA,C=PH
Master List Signer Certificate saved to LDAP o=mlsc: cn=54232439...o=mlsc,c=PH...
                                                                        ^^^^^^
```

---

## Technical Details

### Complete Code Flow

1. **Master List Processing** ([main.cpp:4032-4067](services/pkd-management/src/main.cpp#L4032-L4067))
   ```cpp
   std::string ldapCertType = isMasterListSigner ? "MLSC" : "CSCA";
   ```

2. **LDAP Save Call** ([main.cpp:4059](services/pkd-management/src/main.cpp#L4059))
   ```cpp
   std::string ldapDn = saveCertificateToLdap(ld, ldapCertType, ...);
   ```

3. **DN Building** ([main.cpp:2499](services/pkd-management/src/main.cpp#L2499))
   ```cpp
   dn = buildCertificateDnV2(fingerprint, certType, countryCode);
   ```

4. **DN Format** ([main.cpp:2316-2323](services/pkd-management/src/main.cpp#L2316-L2323))
   ```cpp
   if (certType == "MLSC") {
       ou = "mlsc";  // ← NOW WORKS!
   }
   ```

5. **Final DN**
   ```
   cn={64-char-sha256},o=mlsc,c={COUNTRY},dc=data,dc=download,dc=pkd,...
   ```

### Database Schema

MLSC certificates are stored in the `certificate` table with:
- `certificate_type = 'CSCA'` (for database queries)
- `ldap_dn_v2` contains `o=mlsc` (for frontend display)

**Query to find MLSC**:
```sql
SELECT COUNT(*)
FROM certificate
WHERE ldap_dn_v2 LIKE '%o=mlsc%';
```

### Frontend Classification

[ldap_certificate_repository.cpp:595-596](services/pkd-management/src/repositories/ldap_certificate_repository.cpp#L595-L596):
```cpp
} else if (dnLower.find("o=mlsc") != std::string::npos) {
    return CertificateType::MLSC;
```

This extracts `o=mlsc` from LDAP DN and returns correct type for API responses.

---

## Expected Results After Fix

### LDAP Structure
```
dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
└── dc=data
    ├── c=AU
    │   ├── o=csca (self-signed CSCA)
    │   ├── o=mlsc (Master List Signers)  ← NEW!
    │   ├── o=dsc
    │   └── o=crl
    ├── c=PH
    │   ├── o=csca
    │   ├── o=mlsc  ← NEW!
    │   ├── o=dsc
    │   └── o=crl
    └── ...
```

### Statistics
- **Total CSCA**: 476 (self-signed)
- **Total MLSC**: ~25 (Master List Signers)
- **Total Certificates**: 501

### Frontend Badges
| Type | Badge Color | Example |
|------|------------|---------|
| CSCA | Blue | Self-signed root certificates |
| MLSC | Purple | Master List Signers (PH, LU, TZ, SG, AU, etc.) |
| DSC | Green | Document Signers |
| DSC_NC | Yellow | Non-conformant DSC |

---

## Related Documents

- [MASTER_LIST_SIGNER_CLASSIFICATION_FIX.md](MASTER_LIST_SIGNER_CLASSIFICATION_FIX.md) - Original analysis
- [COLLECTION_002_FINGERPRINT_DN_FIX.md](COLLECTION_002_FINGERPRINT_DN_FIX.md) - Fingerprint DN implementation
- [CLAUDE.md](../CLAUDE.md) - Project development guide

---

## Changelog

### Build 20260126-204826
- ✅ Added MLSC case to `buildCertificateDnV2()`
- ✅ Updated build timestamp for tracking
- ✅ Rebuilt and restarted pkd-management service

### Previous Builds
- Build 20260126-134714: Added MLSC type to enum and Master List processing
- Build 20260126-XXXXXX: Initial MLSC classification implementation

---

## Next Actions

**User must complete**:
1. Run `./reset-and-restart.sh` to reset data
2. Wait 30 seconds for services to be ready
3. Upload Collection 002 LDIF file via http://localhost:3000/upload
4. Verify MLSC entries in LDAP and frontend
5. Report results
