# MLSC Extraction - Root Cause Analysis

**Date**: 2026-01-27
**Upload ID**: 9755b7e4-015c-4bc6-a390-0e8a02acf216
**File**: ICAO_ml_December2025.ml (810,009 bytes)

---

## Critical Discovery

You were **absolutely correct** - the Master List file contains only **1-2 signer certificates**, NOT 59.

The current system incorrectly extracted **59 Link Certificates** from pkiData and classified them as "MLSC".

---

## Actual Master List CMS Structure

### CMS Signature Layer (Outer Structure)

```
CMS_ContentInfo
├── SignerInfo (1 entry)
│   └── Uses: "CN=ICAO Master List Signer,OU=Master List Signers,O=United Nations,C=UN"
│       └── (This is the REAL MLSC that signs the Master List)
│
└── Certificates (2 certificates for signature verification)
    ├── [1] MLSC: CN=ICAO Master List Signer,OU=Master List Signers,O=United Nations,C=UN
    │         Issuer: CN=United Nations CSCA,OU=Certification Authorities,O=United Nations,C=UN
    │         (Cross-signed by UN CSCA, used to sign the Master List)
    │
    └── [2] UN CSCA: CN=United Nations CSCA,OU=Certification Authorities,O=United Nations,C=UN
              Issuer: CN=United Nations CSCA,... (same - self-signed)
              (Root certificate, used to verify MLSC signature)
```

### pkiData (Encapsulated Content)

```
pkiData contains 536 certificates:
├── 477 Self-signed CSCAs (C=LV, C=KR, C=US, etc.)
└── 59 Link Certificates (cross-signed for key rollover)
    └── Examples:
        ├── LV: serial 002←001, 003←002, ... (key rollover chain)
        ├── HU: 8 link certificates
        ├── NL: 7 link certificates
        └── ... (total 59 across multiple countries)
```

**OpenSSL Verification**:

```bash
# Verify SignerInfo count
openssl cms -inform DER -in ICAO_ml_December2025.ml -cmsout -print | grep "signerInfos:"
# Output: Only 1 SignerInfo

# Verify signature certificates
openssl cms -inform DER -in ICAO_ml_December2025.ml -cmsout -print | grep "subject:"
# Output: 2 certificates (MLSC + UN CSCA)
```

---

## The Bug: Incorrect MLSC Detection

### Current Broken Code (main.cpp)

**Location**: `services/pkd-management/src/main.cpp`

**Function**: `isLinkCertificate(X509* cert)`

```cpp
static bool isLinkCertificate(X509* cert) {
    if (!cert) return false;

    // Must NOT be self-signed
    if (isSelfSigned(cert)) {
        return false;
    }

    // Check BasicConstraints: CA:TRUE
    BASIC_CONSTRAINTS* bc = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr);
    if (!bc || !bc->ca) {
        if (bc) BASIC_CONSTRAINTS_free(bc);
        return false;
    }
    BASIC_CONSTRAINTS_free(bc);

    // Check KeyUsage: keyCertSign
    ASN1_BIT_STRING* usage = (ASN1_BIT_STRING*)X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr);
    if (!usage) {
        return false;
    }

    bool hasKeyCertSign = (ASN1_BIT_STRING_get_bit(usage, 5) == 1);
    ASN1_BIT_STRING_free(usage);

    return hasKeyCertSign;
}
```

**Then, in Master List processing (lines ~3918-4200)**:

```cpp
else if (isLinkCertificate(cert)) {
    // Master List Signer Certificate: verify it has CA:TRUE and keyCertSign
    // Master List Signer certificates are cross-signed by CSCAs
    validationStatus = "VALID";
    isLinkCert = true;
    ldapCertType = "MLSC";  // Store in o=mlsc branch ← WRONG!!!
    spdlog::info("Master List: Master List Signer Certificate detected (will save to o=mlsc): {}", subjectDn);
}
```

### Why This is Wrong

The `isLinkCertificate()` function returns `true` for ANY certificate that is:
- Cross-signed (Subject ≠ Issuer)
- Has CA:TRUE
- Has keyCertSign usage

**This describes LINK CERTIFICATES, NOT MLSC!**

Link Certificates are cross-signed CSCAs used for key rollover (e.g., Latvia serial 002 signed by serial 001).

The real MLSC is in the CMS **SignerInfo**, not in pkiData!

---

## What Actually Happened

### Processing Flow (BROKEN)

1. ✅ CMS SignerInfo parsed successfully
2. ❌ **SignerInfo certificate NOT extracted** (no code to extract it)
3. ✅ pkiData certificates extracted (536 certificates)
4. ❌ **59 Link Certificates misclassified as "MLSC"** using `isLinkCertificate()`
5. ❌ **Saved to o=mlsc** (should be in o=lc)

### Database Evidence

```sql
-- What we have now (WRONG)
SELECT COUNT(*), subject_dn = issuer_dn as is_self_signed
FROM certificate
WHERE ldap_dn_v2 LIKE '%o=mlsc%'
  AND upload_id = '9755b7e4-015c-4bc6-a390-0e8a02acf216'
GROUP BY is_self_signed;

-- Result:
-- 59 certificates, ALL cross-signed (subject_dn ≠ issuer_dn)
-- These are Link Certificates, NOT MLSC!
```

### Log Evidence

```
[2026-01-27 10:16:42] Master List: Master List Signer Certificate detected (will save to o=mlsc):
  serialNumber=005,CN=CSCA Latvia,O=National Security Authority,C=LV
[2026-01-27 10:16:42] Master List: Master List Signer Certificate detected (will save to o=mlsc):
  serialNumber=002,CN=CSCA Latvia,O=National Security Authority,C=LV
...
(59 times total - all Link Certificates from pkiData)
```

**NOTICE**: These are CSCAs from Latvia, Hungary, Netherlands, etc. - NOT "ICAO Master List Signer"!

---

## The New Code (masterlist_processor.cpp) - NOT BEING USED!

**Location**: `services/pkd-management/src/common/masterlist_processor.cpp` (Lines 195-281)

The MLSC extraction code was added to Step 2.5:

```cpp
// Step 2.5: Extract and save Master List Signer Certificate (MLSC) from SignerInfo
int mlscCount = 0;
if (cms) {
    STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
    if (signerInfos) {
        int numSigners = sk_CMS_SignerInfo_num(signerInfos);
        spdlog::info("[ML] Found {} SignerInfo(s) - extracting MLSC certificates", numSigners);

        for (int i = 0; i < numSigners; i++) {
            CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, i);
            if (!si) continue;

            // Get signer certificate from SignerInfo
            X509* signerCert = nullptr;
            CMS_SignerInfo_get0_algs(si, nullptr, &signerCert, nullptr, nullptr);

            if (!signerCert) {
                spdlog::warn("[ML] SignerInfo #{}/{} - No certificate found", i + 1, numSigners);
                continue;
            }

            // Extract MLSC metadata and save to o=mlsc
            // ...
        }
    }
}
```

**BUT** this code is in `masterlist_processor.cpp`, which is **NEVER CALLED** for Master List file uploads!

---

## Why masterlist_processor.cpp is Not Used

### Call Chain for Master List File Upload

```
AutoProcessingStrategy::processMasterListContent()  (processing_strategy.cpp:130)
  └─→ processMasterListContentCore()  (main.cpp:3918) ← OLD CODE!
        └─→ Uses isLinkCertificate() to detect "MLSC" ← WRONG!
```

### Call Chain for LDIF Master List Entry

```
LdifProcessor::processEntries()
  └─→ processMasterListEntry()
        └─→ parseMasterListEntryV2()  (masterlist_processor.cpp:56) ← NEW CODE!
              └─→ Uses CMS_get0_SignerInfos() ← CORRECT!
```

**The Problem**: Master List **files** (.ml) use the OLD code in main.cpp, not the NEW code in masterlist_processor.cpp!

---

## Correct Classification

| Certificate Type | Location | Count | Characteristics | Should be stored in |
|-----------------|----------|-------|-----------------|---------------------|
| **MLSC** | CMS SignerInfo | **1** | CN=ICAO Master List Signer, C=UN | **o=mlsc** |
| **UN CSCA** | CMS certificates | **1** | CN=United Nations CSCA, C=UN | **o=csca** (?) |
| **Link Certificate** | CMS pkiData | **59** | Subject ≠ Issuer, CA:TRUE, keyCertSign | **o=lc** |
| **Self-signed CSCA** | CMS pkiData | **477** | Subject = Issuer, CA:TRUE, keyCertSign | **o=csca** |

---

## The Fix Required

### Option 1: Use masterlist_processor.cpp for Master List Files

**Change**: `processing_strategy.cpp` line 139

```cpp
// BEFORE (BROKEN)
void AutoProcessingStrategy::processMasterListContent(...) {
    spdlog::info("AUTO mode: Processing Master List ({} bytes) for upload {}", content.size(), uploadId);
    processMasterListContentCore(uploadId, content, conn, ld);  // ← OLD CODE
    spdlog::info("AUTO mode: Master List processing completed");
}

// AFTER (FIXED)
void AutoProcessingStrategy::processMasterListContent(...) {
    spdlog::info("AUTO mode: Processing Master List ({} bytes) for upload {}", content.size(), uploadId);
    processMasterListFileV2(uploadId, content, conn, ld);  // ← NEW FUNCTION
    spdlog::info("AUTO mode: Master List processing completed");
}
```

**Requires**: Creating a new function `processMasterListFileV2()` in masterlist_processor.cpp that:
1. Parses CMS structure
2. Extracts 1 MLSC from SignerInfo → save to o=mlsc
3. Extracts 536 certificates from pkiData:
   - 477 self-signed → save to o=csca
   - 59 cross-signed → save to o=lc (NOT o=mlsc!)

### Option 2: Fix the Existing Code in main.cpp

**Change**: `main.cpp` processMasterListContentCore() function

1. **Add SignerInfo extraction** (before pkiData processing)
2. **Fix isLinkCertificate() logic** (rename to isLinkCertificate, save to o=lc)
3. **Remove MLSC misclassification** (don't use isLinkCertificate for MLSC detection)

---

## Impact Analysis

### Current State (BROKEN)

```
o=csca: 477 self-signed CSCAs ✅ Correct
o=mlsc: 59 Link Certificates ❌ WRONG! (should be 1 MLSC)
o=lc: 0 ❌ WRONG! (should be 59 Link Certificates)
```

### Expected State (FIXED)

```
o=csca: 477 self-signed CSCAs ✅
o=mlsc: 1 ICAO Master List Signer (C=UN) ✅
o=lc: 59 Link Certificates ✅
```

### Data Cleanup Required

After deploying the fix, we need to:

1. **Delete incorrect MLSC entries**:
   ```sql
   DELETE FROM certificate
   WHERE ldap_dn_v2 LIKE '%o=mlsc%'
     AND subject_dn != issuer_dn;  -- Remove cross-signed "MLSC" (actually Link Certs)
   ```

2. **Clean up LDAP**:
   ```bash
   # Delete all o=mlsc except the real UN ICAO Master List Signer
   ldapdelete ...
   ```

3. **Re-upload Master List** with fixed code to extract:
   - 1 real MLSC → o=mlsc
   - 59 Link Certificates → o=lc

---

## Summary

### Root Cause

1. ✅ **Code was written** to extract MLSC from SignerInfo (masterlist_processor.cpp:195-281)
2. ❌ **Code is NOT being used** for Master List file uploads
3. ❌ **Old code in main.cpp** misclassifies Link Certificates as MLSC
4. ❌ **Result**: 59 Link Certificates wrongly stored in o=mlsc instead of 1 MLSC

### User Observation

> "내가 알기로는 data/uploads/ICAO_ml_December2025.ml 에는 singer 인증서가 2개만 있는 걸로 아는데"

**You were RIGHT!** The Master List has:
- **1 SignerInfo** (uses the MLSC to sign)
- **2 certificates in CMS outer structure** (MLSC + UN CSCA)
- **536 certificates in pkiData** (content, not signers)

Our code mistakenly treated 59 of the 536 pkiData certificates as "MLSC" when they're actually Link Certificates.

---

**Next Steps**:
1. Decide on fix approach (Option 1 vs Option 2)
2. Implement the fix
3. Deploy with force rebuild
4. Clean up incorrect data
5. Re-upload Master List to verify correct extraction

