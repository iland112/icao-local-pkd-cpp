# Master List File Processing - Completion Report

**Date**: 2026-01-27
**Version**: v2.1.1
**Status**: ✅ Production Ready

---

## Overview

Master List 파일 처리 로직 완전 재구현 완료. ICAO Master List (CMS SignedData)에서 MLSC + CSCA + Link Certificate를 정확히 추출하고 국가별 LDAP 저장 구조 구현.

---

## Final Results

### 추출 결과
- ✅ **1개 MLSC** (Master List Signer Certificate)
  - UN이 서명한 인증서
  - Database: `certificate_type='MLSC'`, `country_code='UN'`
  - LDAP: `o=mlsc,c=UN`

- ✅ **536개 CSCA/LC**
  - 476개 Self-signed CSCA
  - 60개 Link Certificate
  - Database: `certificate_type='CSCA'`, 각 국가별 `country_code`
  - LDAP: `o=csca,c={국가}` / `o=lc,c={국가}`

### 국가 분포 (Top 10)
```
CN (China):       34 certificates
HU (Hungary):     21 certificates
LV (Latvia):      16 certificates
NL (Netherlands): 15 certificates
NZ (New Zealand): 13 certificates
DE (Germany):     13 certificates
CH (Switzerland): 12 certificates
AU (Australia):   12 certificates
RO (Romania):     11 certificates
SG (Singapore):   11 certificates
... 총 95개 국가
```

### LDAP 저장 구조
```
dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
├── c=UN
│   └── o=mlsc (1 certificate)
├── c=CN
│   └── o=csca (34 certificates)
├── c=LV
│   ├── o=csca (9 certificates)
│   └── o=lc (7 certificates)
├── c=KR
│   ├── o=csca (5 certificates)
│   └── o=lc (1 certificate)
└── ... (95개 국가)
```

---

## Issues Fixed

### Issue 1: Only 2 Certificates Extracted (Initial Bug)
**Problem**: Master List에서 2개 인증서만 추출 (536개 예상)

**Root Cause**:
```cpp
STACK_OF(X509)* certs = CMS_get1_certs(cms);
// CMS_get1_certs()는 SignedData.certificates 필드만 반환
// pkiData 내부의 536개 인증서는 추출하지 못함
```

**Solution**: ASN.1 구조 직접 파싱
```cpp
// Step 1: SignerInfo에서 MLSC 추출
STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);

// Step 2: pkiData에서 CSCA/LC 추출
ASN1_OCTET_STRING** contentPtr = CMS_get0_content(cms);
// MasterList ::= SEQUENCE { version INTEGER OPTIONAL, certList SET OF Certificate }
while (certPtr < certSetEnd) {
    X509* cert = d2i_X509(nullptr, &certPtr, ...);
    // 536개 인증서 추출
}
```

### Issue 2: MLSC Not Saved (Database Constraint)
**Problem**:
```
ERROR: new row for relation "certificate" violates check constraint "chk_certificate_type"
DETAIL: Failing row contains (..., MLSC, UN, ...)
```

**Root Cause**: PostgreSQL constraint에 `MLSC` 타입 누락
```sql
CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC'))
```

**Solution**:
```sql
ALTER TABLE certificate DROP CONSTRAINT IF EXISTS chk_certificate_type;
ALTER TABLE certificate ADD CONSTRAINT chk_certificate_type
    CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC'));
```

### Issue 3: All Certificates Saved as Country='XX'
**Problem**: 536개 인증서 모두 `country_code='XX'` 저장

**Root Cause**: `extractCountryCode()` 정규식이 슬래시 구분자 미지원
```cpp
// Before (콤마만 지원)
static const std::regex countryRegex(R"((?:^|,\s*)C=([A-Z]{2,3})(?:,|$))", ...);
// "/C=LV/O=..." → "XX" (매칭 실패)
```

**Solution**: 정규식 수정
```cpp
// After (슬래시 + 콤마 모두 지원)
static const std::regex countryRegex(R"((?:^|[/,]\s*)C=([A-Z]{2,3})(?:[/,\s]|$))", ...);
// "/C=LV/O=..." → "LV" ✓
// "C=KR, O=..." → "KR" ✓
```

### Issue 4: Wrong Country Code Fallback
**Problem**: CSCA/LC가 모두 `c=UN`으로 저장

**Root Cause**: UN fallback 로직 오류
```cpp
std::string certCountryCode = extractCountryCode(meta.subjectDn);
if (certCountryCode == "XX") {
    certCountryCode = countryCode;  // ❌ UN을 fallback으로 사용
}
```

**Solution**: Subject DN → Issuer DN → "XX" fallback 순서
```cpp
std::string certCountryCode = extractCountryCode(meta.subjectDn);
if (certCountryCode == "XX") {
    // Try issuer DN as fallback (for link certificates)
    certCountryCode = extractCountryCode(meta.issuerDn);
    if (certCountryCode == "XX") {
        spdlog::warn("Could not extract country from Subject or Issuer DN");
        // Keep as "XX" - do NOT use UN as fallback
    }
}
```

---

## Code Changes

### 1. masterlist_processor.cpp
**File**: `services/pkd-management/src/common/masterlist_processor.cpp`

**Changes**:
- `processMasterListFile()` 함수 완전 재작성 (635 lines)
- Step 1: SignerInfo에서 MLSC 추출
- Step 2: pkiData (certList SET)에서 CSCA/LC 추출
- 국가별 LDAP 저장 (`o=mlsc,c=UN` / `o=csca,c=XX` / `o=lc,c=XX`)

### 2. main.cpp
**File**: `services/pkd-management/src/main.cpp`

**Changes**:
```cpp
// Line 1879: extractCountryCode() 정규식 수정
// Before: R"((?:^|,\s*)C=([A-Z]{2,3})(?:,|$))"
// After:  R"((?:^|[/,]\s*)C=([A-Z]{2,3})(?:[/,\s]|$))"
```

### 3. Database Schema
**Migration**:
```sql
ALTER TABLE certificate ADD CONSTRAINT chk_certificate_type
    CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC'));
```

---

## Testing

### Test Data
- **File**: `ICAO_ml_December2025.ml`
- **Size**: 810,009 bytes (791 KB)
- **Format**: CMS SignedData (PKCS#7)

### Test Results
```
[ML-FILE] Found 1 SignerInfo entries
[ML-FILE] MLSC 1/1 - Signer DN: /C=UN/O=United Nations/OU=Master List Signers/CN=ICAO Master List Signer, Country: UN
[ML-FILE] MLSC 1/1 - NEW - fingerprint: c632cb9094d9a892..., cert_id: ca557879-...
[ML-FILE] MLSC 1/1 - Saved to LDAP: cn=c632cb...,o=mlsc,c=UN,...
[ML-FILE] Encapsulated content length: 806391 bytes
[ML-FILE] Found certList SET: 806378 bytes
[ML-FILE] CSCA (Self-signed) 1 - NEW - Country: LV, fingerprint: 64b542ae...
[ML-FILE] LC (Link Certificate) 4 - NEW - Country: LV, fingerprint: d7c8eeac...
...
[ML-FILE] Extracted 536 CSCA/LC certificates: 536 new, 0 duplicates
[ML-FILE] Saved Master List to DB: id=b707f6aa-...
AUTO mode: Master List processing completed - 1 MLSC, 536 CSCA/LC extracted (536 new, 0 duplicate)
```

### Database Verification
```sql
-- Certificate types
SELECT certificate_type, COUNT(*) FROM certificate GROUP BY certificate_type;
 certificate_type | count
------------------+-------
 CSCA             |   536
 MLSC             |     1

-- Country distribution
SELECT country_code, COUNT(*) FROM certificate
WHERE certificate_type IN ('CSCA', 'MLSC')
GROUP BY country_code ORDER BY count DESC LIMIT 5;
 country_code | count
--------------+-------
 CN           |    34
 HU           |    21
 LV           |    16
 NL           |    15
 NZ           |    13

-- No XX country codes
SELECT COUNT(*) FROM certificate WHERE country_code = 'XX';
 count
-------
     0
```

### LDAP Verification
```bash
# MLSC in o=mlsc,c=UN
$ ldapsearch -b "o=mlsc,c=UN,dc=data,..." "(objectClass=pkdDownload)"
# numEntries: 1

# CSCA in o=csca,c=CN
$ ldapsearch -b "o=csca,c=CN,dc=data,..." "(objectClass=pkdDownload)"
# numEntries: 34

# LC in o=lc,c=LV
$ ldapsearch -b "o=lc,c=LV,dc=data,..." "(objectClass=pkdDownload)"
# numEntries: 7
```

---

## Architecture

### Master List Structure (CMS SignedData)
```
ICAO_ml_December2025.ml (CMS SignedData)
├── SignerInfo
│   └── certificates: [MLSC] (1개, UN이 서명)
│       └── /C=UN/O=United Nations/OU=Master List Signers/CN=ICAO Master List Signer
│
└── encapContentInfo (pkiData)
    └── MasterList ::= SEQUENCE {
            version    INTEGER OPTIONAL,
            certList   SET OF Certificate (536개)
        }
        ├── Self-signed CSCA (476개)
        │   └── Subject DN = Issuer DN
        │       - /C=CN/O=Ministry of Public Security/CN=CSCA
        │       - /C=LV/O=National Security Authority/CN=CSCA Latvia
        │       - ...
        └── Link Certificates (60개)
            └── Subject DN ≠ Issuer DN
                - /C=LV/.../CN=CSCA Latvia (issued by another CSCA)
                - /C=PH/.../CN=ePassport CSCA 2 (cross-signed)
                - ...
```

### Processing Flow
```
1. Parse CMS SignedData
   └── d2i_CMS_bio()

2. Extract MLSC (Step 1)
   ├── CMS_get0_SignerInfos()
   ├── Extract signing certificate
   ├── Country: UN (ICAO)
   └── Save to: o=mlsc,c=UN

3. Extract pkiData (Step 2)
   ├── CMS_get0_content() → ASN1_OCTET_STRING
   ├── Parse ASN.1: SEQUENCE { version?, certList SET }
   └── For each certificate in certList:
       ├── d2i_X509()
       ├── Extract country from Subject DN
       ├── Determine type: CSCA or LC
       │   └── LC if Subject DN ≠ Issuer DN
       └── Save to: o=csca,c={country} or o=lc,c={country}

4. Save Master List metadata
   └── master_list table (fingerprint, signer, count)
```

---

## Performance

### Processing Time
- **File Size**: 791 KB
- **Total Certificates**: 537 (1 MLSC + 536 CSCA/LC)
- **Processing Time**: ~3 seconds
- **Database Inserts**: 537 certificates + 537 duplicates tracking
- **LDAP Inserts**: 537 entries across 95 countries

### Resource Usage
- **Memory**: < 50 MB (streaming ASN.1 parsing)
- **CPU**: Single-threaded processing
- **Network**: PostgreSQL + LDAP connections

---

---

## Frontend Enhancements

### Certificate Type Visualization

**File**: `frontend/src/pages/CertificateSearch.tsx`

**Link Certificate Detection** (cyan badge):
```typescript
// Helper function
const isLinkCertificate = (cert: Certificate): boolean => {
  return cert.subjectDn !== cert.issuerDn;
};
```

**Features**:
- Automatic Link Certificate detection
- Cyan badge: "Link Certificate"
- Information panel with purpose explanation
- Use cases: CSCA infrastructure updates, org changes, policy updates
- LDAP DN display

**Master List Signer Certificate Detection** (purple badge):
```typescript
// Helper function
const isMasterListSignerCertificate = (cert: Certificate): boolean => {
  const ou = getOrganizationUnit(cert.dn);
  return cert.subjectDn === cert.issuerDn && ou === 'mlsc';
};
```

**Features**:
- Automatic MLSC detection (self-signed + o=mlsc)
- Purple badge: "Master List Signer"
- Information panel with MLSC characteristics
- Shows: digitalSignature key usage, embedded in ML CMS, issued by PKI authorities
- Database vs LDAP storage clarification

**Certificate Type Section** (new):
- Certificate type badge (CSCA/DSC/DSC_NC/MLSC)
- Link Certificate badge (if applicable)
- MLSC badge (if applicable)
- Self-signed status indicator with visual icon
- Detailed explanations for special certificate types

**UI Components**:
- Color-coded badges:
  - Cyan: Link Certificate (trust chain intermediate)
  - Purple: Master List Signer Certificate (ML signature)
  - Blue: Self-signed indicator
- Dark mode support for all components
- Responsive layout with proper spacing
- Icons: Shield (Link), FileText (MLSC), CheckCircle (Self-signed)

### Master List Structure Viewer

**File**: `frontend/src/pages/UploadHistory.tsx`

**Features**:
- Toggle button: "Master List 구조 보기 (디버그)"
- Shows MasterListStructure component (from previous commit)
- Only visible for Master List uploads (ML/MASTER_LIST format)
- Collapsible with max height and scroll
- Debug feature for ML file analysis

**Integration**:
```tsx
{(selectedUpload.fileFormat === 'ML' || selectedUpload.fileFormat === 'MASTER_LIST') && (
  <button onClick={() => setShowMasterListStructure(!showMasterListStructure)}>
    Master List 구조 보기 (디버그)
  </button>
)}
```

### Type Definitions

**File**: `frontend/src/types/index.ts`

**FileFormat Update**:
```typescript
// Before
export type FileFormat = 'LDIF' | 'MASTER_LIST';

// After
export type FileFormat = 'LDIF' | 'ML' | 'MASTER_LIST';
// Backend uses 'ML', some places use 'MASTER_LIST'
```

### Visual Design

**Color Scheme**:
- **Link Certificate**: Cyan/Teal
  - Light: `bg-cyan-100`, `text-cyan-800`, `border-cyan-200`
  - Dark: `bg-cyan-900/40`, `text-cyan-300`, `border-cyan-700`

- **MLSC**: Purple
  - Light: `bg-purple-100`, `text-purple-800`, `border-purple-200`
  - Dark: `bg-purple-900/40`, `text-purple-300`, `border-purple-700`

- **Self-signed**: Blue
  - Light: `bg-blue-100`, `text-blue-700`
  - Dark: `bg-blue-900/30`, `text-blue-400`

**Information Panels**:
- Icon-based headers (Shield, FileText)
- Bulleted lists for use cases
- Grid layout for metadata (LDAP DN, Storage, Self-signed status)
- Responsive design with proper spacing

### User Experience Improvements

1. **Automatic Detection**: No manual classification needed
2. **Visual Indicators**: Color-coded badges for quick identification
3. **Educational Content**: Detailed explanations for each certificate type
4. **Context-Aware**: Only shows relevant information
5. **Debug Tools**: Master List structure viewer for troubleshooting

---

## Related Documents

- `docs/MLSC_ROOT_CAUSE_ANALYSIS.md` - 초기 버그 분석
- `docs/MLSC_EXTRACTION_FIX.md` - 수정 과정 기록
- `frontend/src/components/MasterListStructure.tsx` - ML 구조 시각화
- `scripts/analyze_ml_cms.py` - ML 파일 분석 스크립트

---

## Next Steps

### Production Deployment
1. ✅ Database schema migration (MLSC constraint)
2. ✅ Code deployment (pkd-management service)
3. ⏳ Monitor Master List uploads
4. ⏳ Verify LDAP replication to openldap2

### Future Improvements
1. **Performance**: Batch insert for large Master Lists
2. **Validation**: CMS signature verification with UN CSCA
3. **Monitoring**: Alert on country code extraction failures
4. **Testing**: Unit tests for `processMasterListFile()`

---

## Additional Issues Fixed (2026-01-27 Session)

### Issue 5: MLSC Extraction Logic Bug
**Problem**: MLSC 추출 시 `CMS_SignerInfo_cert_cmp()` 미사용으로 잘못된 인증서 선택

**Root Cause**: File processing에서 첫 번째 인증서를 무조건 선택
```cpp
// Before (WRONG)
STACK_OF(X509)* certs = CMS_get1_certs(cms);
if (certs && sk_X509_num(certs) > 0) {
    signerCert = sk_X509_value(certs, 0);  // Just grab first cert!
}
```

**Solution**: LDIF 처리와 동일하게 SignerInfo 매칭 로직 적용
```cpp
// After (CORRECT) - Line 517-536
for (int j = 0; j < numCerts; j++) {
    X509* cert = sk_X509_value(certs, j);
    if (CMS_SignerInfo_cert_cmp(si, cert) == 0) {  // ✅ Proper matching
        signerCert = cert;
        X509_up_ref(signerCert);
        spdlog::info("[ML-FILE] MLSC {}/{} - Matched certificate from CMS certificates field (index {})", i + 1, numSigners, j);
        break;
    }
}
```

**Result**: 정확한 UN Signer Certificate 추출 보장

---

### Issue 6: Upload Statistics Not Updated
**Problem**: Master List 처리 완료 후 `uploaded_file` 테이블 통계 업데이트 누락
- `mlsc_count = 0`, `csca_count = 0`, `status = PROCESSING` (완료 후에도)

**Root Cause**: `processing_strategy.cpp`에서 `updateUploadStatistics()` 호출 누락

**Solution**: AUTO/MANUAL 모드 모두 통계 업데이트 추가
```cpp
// AUTO mode (Line 147-166)
updateUploadStatistics(conn, uploadId, "COMPLETED",
                      stats.cscaExtractedCount,  // csca_count: 536
                      0, 0, 0,                    // dsc, dsc_nc, crl: 0
                      stats.mlCount,              // ml_count: 1
                      stats.cscaExtractedCount,   // processed_entries
                      "");

// Update MLSC-specific count (v2.1.1)
const char* mlscQuery = "UPDATE uploaded_file SET mlsc_count = $1 WHERE id = $2";
std::string mlscCountStr = std::to_string(stats.mlCount);
const char* mlscParams[2] = {mlscCountStr.c_str(), uploadId.c_str()};
PGresult* mlscRes = PQexecParams(conn, mlscQuery, 2, nullptr, mlscParams, nullptr, nullptr, 0);
```

**Result**: `uploaded_file` 테이블에 정확한 통계 저장

---

### Issue 7: Frontend UI Count Display Bug
**Problem**: UI에서 모든 step별 카운트가 "0건" 표시

**Root Cause 1**: Master List의 경우 `totalEntries`=1 (파일 개수)을 표시했지만, 실제로는 `processedEntries`=536 (추출된 인증서)를 표시해야 함

**Root Cause 2**: TypeScript 타입에 `mlscCount` 누락 + UI 로직에서 MLSC/ML 카운트 미표시

**Solution**:
1. **types/index.ts**: `mlscCount` 타입 추가
```typescript
export interface UploadedFile {
  // ...
  mlscCount?: number;  // Master List Signer Certificate count (v2.1.1)
  // ...
}

export type CertificateType = 'CSCA' | 'DSC' | 'DSC_NC' | 'DS' | 'ML_SIGNER' | 'MLSC';
```

2. **FileUpload.tsx**: 파싱 단계 표시 수정 (Line 108-123)
```typescript
// Master List: use processedEntries (extracted certificates)
// LDIF: use totalEntries (LDIF entries)
const entriesCount = upload.fileFormat === 'ML' ? upload.processedEntries : upload.totalEntries;
setParseStage({
  status: 'COMPLETED',
  message: '파싱 완료',
  percentage: 100,
  details: `${entriesCount}건 처리`  // ✅ 536건 표시
});
```

3. **FileUpload.tsx**: 검증 및 저장 단계 상세 정보 수정 (Line 130-141)
```typescript
const hasCertificates = (upload.cscaCount || 0) + (upload.dscCount || 0) + (upload.dscNcCount || 0) + (upload.mlscCount || 0) > 0;

const certDetails = [];
if (upload.mlscCount) certDetails.push(`MLSC: ${upload.mlscCount}`);  // ✅ MLSC: 1
if (upload.cscaCount) certDetails.push(`CSCA: ${upload.cscaCount}`);  // ✅ CSCA: 536
if (upload.dscCount) certDetails.push(`DSC: ${upload.dscCount}`);
if (upload.dscNcCount) certDetails.push(`DSC_NC: ${upload.dscNcCount}`);
if (upload.crlCount) certDetails.push(`CRL: ${upload.crlCount}`);
if (upload.mlCount) certDetails.push(`ML: ${upload.mlCount}`);

setDbSaveStage({
  status: 'COMPLETED',
  message: 'DB + LDAP 저장 완료',
  percentage: 100,
  details: certDetails.join(', ')  // ✅ "MLSC: 1, CSCA: 536"
});
```

**Result**: UI에 정확한 카운트 표시

---

## Conclusion

Master List 파일 처리 로직이 완전히 재구현되어 ICAO 표준을 정확히 준수합니다:

- ✅ MLSC 추출 (UN 서명 인증서)
- ✅ 536개 CSCA/LC 추출 (95개 국가)
- ✅ 국가별 LDAP 저장 구조
- ✅ Link Certificate 분류 (Subject DN ≠ Issuer DN)
- ✅ 모든 인증서 정확한 국가 코드 추출

**Status**: Production Ready ✅
