# Master List Signer Certificate (MLSC) Extraction Implementation

**Date**: 2026-01-27
**Status**: ✅ CODE COMPLETE - PENDING DEPLOYMENT
**Version**: v2.1.1

---

## Overview

실제 ICAO Master List CMS 구조 분석을 통해 Master List Signer Certificate (MLSC)가 현재 시스템에서 누락되어 있음을 발견하고, SignerInfo에서 MLSC를 추출하여 저장하는 기능을 추가했습니다.

---

## Problem Analysis

### 발견된 문제

1. **o=mlsc에 저장된 59개 인증서가 모두 Link Certificate**
   ```sql
   SELECT COUNT(*), subject_dn = issuer_dn as is_self_signed
   FROM certificate
   WHERE ldap_dn_v2 LIKE '%o=mlsc%'
   GROUP BY is_self_signed;

   -- Result: 59 certificates, ALL non-self-signed (Link Certificates)
   ```

2. **진짜 MLSC는 추출되지 않음**
   - 현재 코드는 CMS pkiData만 처리
   - SignerInfo (실제 MLSC 위치)는 무시됨

### Master List CMS 구조

```
CMS_ContentInfo
├── SignerInfo (1개 이상)
│   └── Signer Certificate → **진짜 MLSC** (Master List에 서명한 인증서)
└── pkiData (여러 개)
    ├── Self-signed CSCA (subject_dn = issuer_dn)
    └── Link Certificate (subject_dn ≠ issuer_dn)
```

---

## Solution

### Code Changes

**File**: [services/pkd-management/src/common/masterlist_processor.cpp](../services/pkd-management/src/common/masterlist_processor.cpp)

#### 1. Step 2.5: MLSC 추출 및 저장 (NEW)

Lines 195-276에 추가:

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

            // Extract MLSC metadata
            CertificateMetadata mlscMeta = extractCertificateMetadata(signerCert);

            // ... (metadata validation, duplicate check)

            // Save MLSC to database
            auto [certId, isDuplicate] = certificate_utils::saveCertificateWithDuplicateCheck(
                conn, uploadId, "CSCA", mlscCountryCode,
                mlscMeta.subjectDn, mlscMeta.issuerDn, mlscMeta.serialNumber, mlscMeta.fingerprint,
                mlscMeta.notBefore, mlscMeta.notAfter, mlscMeta.derData,
                "UNKNOWN", ""
            );

            // Save to LDAP: o=mlsc (Master List Signer Certificate)
            if (ld) {
                std::string ldapDn = saveCertificateToLdap(
                    ld, "MLSC", mlscCountryCode,  // ← Type: "MLSC"
                    mlscMeta.subjectDn, mlscMeta.issuerDn, mlscMeta.serialNumber,
                    mlscMeta.fingerprint,
                    mlscMeta.derData,
                    "", "", "",
                    false  // useLegacyDn=false
                );

                if (!ldapDn.empty()) {
                    spdlog::info("[ML] MLSC {}/{} - Saved to LDAP: {}", i + 1, numSigners, ldapDn);
                    certificate_utils::updateCertificateLdapStatus(conn, certId, ldapDn);
                }
            }
        }
    }
}
```

#### 2. Step 3: Link Certificate 분류 수정 (UPDATED)

Lines 319-324에서 변경:

```cpp
// BEFORE: Incorrectly classified as "MLSC"
std::string ldapCertType = isMasterListSigner ? "MLSC" : "CSCA";

// AFTER: Correctly classified as "LC" (Link Certificate)
bool isLinkCertificate = (meta.subjectDn != meta.issuerDn);
std::string ldapCertType = isLinkCertificate ? "LC" : "CSCA";
```

---

## Certificate Classification (Final)

| Certificate Type | Source | Characteristics | LDAP Storage | DB Type |
|-----------------|--------|-----------------|--------------|---------|
| **MLSC** | CMS SignerInfo | Signs the Master List CMS | **o=mlsc** | CSCA |
| **Link Certificate** | CMS pkiData | subject_dn ≠ issuer_dn | **o=lc** | CSCA |
| **Self-signed CSCA** | CMS pkiData | subject_dn = issuer_dn | **o=csca** | CSCA |
| **DSC** | LDIF | Document Signer | **o=dsc** | DSC |

---

## Expected Results

### Before Fix

```
o=csca: 477 self-signed CSCA
o=mlsc: 59 Link Certificates (WRONG!)
o=lc: 0 (empty containers only)
```

### After Fix

```
o=csca: 477 self-signed CSCA
o=mlsc: ~60 MLSC from SignerInfo (CORRECT!)
o=lc: ~60 Link Certificates (CORRECT!)
```

### Log Output Example

```
[ML] Master List contains 8 certificates (CSCA/Link/MLSC): country=LV
[ML] Found 1 SignerInfo(s) - extracting MLSC certificates
[ML] MLSC 1/1 - NEW - fingerprint: a1b2c3d4..., cert_id: uuid, subject: CN=ML Signer,C=LV
[ML] MLSC 1/1 - Saved to LDAP: cn=a1b2c3d4...,o=mlsc,c=LV,dc=data,...
[ML] LC (Link Certificate) 1/8 - NEW - fingerprint: e5f6g7h8...
[ML] LC (Link Certificate) 1/8 - Saved to LDAP: cn=e5f6g7h8...,o=lc,c=LV,dc=data,...
[ML] CSCA (Self-signed) 2/8 - NEW - fingerprint: i9j0k1l2...
[ML] CSCA (Self-signed) 2/8 - Saved to LDAP: cn=i9j0k1l2...,o=csca,c=LV,dc=data,...
```

---

## Deployment Instructions

### Method 1: Docker Rebuild (Recommended)

```bash
cd /home/kbjung/projects/c/icao-local-pkd

# Force rebuild with cache bust
docker stop icao-local-pkd-management
docker rm icao-local-pkd-management
docker rmi docker-pkd-management:latest

# Rebuild with timestamp
cd docker
docker compose build --build-arg CACHE_BUST=$(date +%s) pkd-management

# Start service
docker compose up -d pkd-management

# Verify logs
docker logs icao-local-pkd-management 2>&1 | grep -E "MLSC|SignerInfo|LC \(Link"
```

### Method 2: Manual Compilation

```bash
cd /home/kbjung/projects/c/icao-local-pkd/services/pkd-management

# Clean build
rm -rf build && mkdir build && cd build

# Configure with vcpkg
cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
         -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . -j$(nproc)

# Copy binary to container
docker cp build/bin/pkd-management icao-local-pkd-management:/app/
docker restart icao-local-pkd-management
```

---

## Testing Plan

### 1. Database Verification (Before)

```sql
-- Check current o=mlsc certificates (should be Link Certificates)
SELECT COUNT(*), subject_dn = issuer_dn as is_self_signed
FROM certificate
WHERE ldap_dn_v2 LIKE '%o=mlsc%'
GROUP BY is_self_signed;

-- Expected: 59 Link Certificates (non-self-signed)
```

### 2. Upload Test Master List

```bash
# Find a sample Master List file
find /home/kbjung -name "*.ml" 2>/dev/null | head -1

# Upload via API
curl -X POST http://localhost:8080/api/upload/masterlist \
  -H "Content-Type: multipart/form-data" \
  -F "file=@/path/to/masterlist.ml" \
  -F "mode=AUTO"
```

### 3. Monitor Logs

```bash
docker logs -f icao-local-pkd-management | grep -E "MLSC|SignerInfo|LC \(Link"
```

Expected output:
```
[ML] Found 1 SignerInfo(s) - extracting MLSC certificates
[ML] MLSC 1/1 - NEW - fingerprint: xxx
[ML] MLSC 1/1 - Saved to LDAP: cn=xxx,o=mlsc,c=XX,...
[ML] LC (Link Certificate) 1/5 - NEW - fingerprint: yyy
```

### 4. Database Verification (After)

```sql
-- Check new MLSC from SignerInfo
SELECT COUNT(*)
FROM certificate
WHERE ldap_dn_v2 LIKE '%o=mlsc%'
  AND subject_dn = issuer_dn;  -- Should have self-signed MLSCs now

-- Check Link Certificates moved to o=lc
SELECT COUNT(*)
FROM certificate
WHERE ldap_dn_v2 LIKE '%o=lc%';  -- Should have ~60 Link Certificates
```

### 5. LDAP Verification

```bash
# Count MLSC in LDAP
docker exec icao-local-pkd-openldap1 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w ldap_test_password_123 \
  -b "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(o=mlsc)" dn | grep "^dn:" | wc -l

# Count Link Certificates in LDAP
docker exec icao-local-pkd-openldap1 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w ldap_test_password_123 \
  -b "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(o=lc)" dn | grep "^dn:" | wc -l
```

---

## Frontend Impact

Frontend의 [CertificateSearch.tsx](../frontend/src/pages/CertificateSearch.tsx)는 이미 수정되어 있습니다:

- **Link Certificate 감지**: `subject_dn !== issuer_dn` → Cyan badge "Link Certificate"
- **MLSC 감지**: `subject_dn === issuer_dn AND o=mlsc` → Purple badge "Master List Signer"

새로운 MLSC가 추가되면 자동으로 올바른 배지가 표시됩니다.

---

## Related Documents

- [MASTER_LIST_SIGNER_CLASSIFICATION_FIX.md](MASTER_LIST_SIGNER_CLASSIFICATION_FIX.md) - Original MLSC analysis
- [MLSC_SYNC_UPDATE.md](MLSC_SYNC_UPDATE.md) - DB-LDAP sync MLSC support
- [CLAUDE.md](../CLAUDE.md) - Project development guide

---

## Summary

1. ✅ **분석 완료**: Master List CMS 구조 확인, SignerInfo에 진짜 MLSC 존재 확인
2. ✅ **코드 수정**: SignerInfo에서 MLSC 추출 및 o=mlsc 저장 기능 추가
3. ✅ **분류 수정**: pkiData의 non-self-signed → o=lc (Link Certificate)
4. ⏳ **배포 대기**: Docker 빌드 캐시 문제로 수동 배포 필요

이제 고객의 요구사항대로 **Master List Signer Certificate가 정확하게 추출되어 저장**됩니다!

---

**Next Steps**:
1. Docker 강제 재빌드 (--build-arg CACHE_BUST)
2. Master List 재업로드 테스트
3. o=mlsc, o=lc 통계 확인
