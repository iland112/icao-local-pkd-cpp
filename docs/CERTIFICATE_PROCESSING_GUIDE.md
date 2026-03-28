# FASTpass® SPKD — Certificate Processing Guide

**Version**: v2.41.0 | **Last Updated**: 2026-03-27
> Updated for v2.41.0 서비스 기능 재배치 (Sync↔Upload 교차 이동)

---

## Part 1: Certificate Source Management

---

### 1. 개요

ICAO Local PKD 시스템은 다양한 경로를 통해 인증서를 수집한다. 본 문서는 각 출처(source)의 특성, DB/LDAP 저장 정책, 그리고 출처가 다른 인증서가 동일한 LDAP DIT 경로에 저장되는 이유를 설명한다.

#### 핵심 원칙

> **인증서의 LDAP DIT 위치는 인증서 유형(type)과 국가(country)에 의해 결정되며, 출처(source)에 의해 결정되지 않는다.**
> 출처 추적은 DB 레벨의 `source_type` 컬럼으로 관리한다.

---

### 2. 인증서 출처 유형

| source_type | 설명 | 수집 경로 |
|-------------|------|----------|
| `LDIF_PARSED` | ICAO PKD LDIF 파일에서 파싱 | `icaopkd-001` (CSCA, DSC, CRL), `icaopkd-003` (DSC_NC) |
| `ML_PARSED` | Master List 파일에서 추출 | `icaopkd-002` (Master List 내 CSCA/MLSC) |
| `FILE_UPLOAD` | 개별 인증서 파일 직접 업로드 | PEM, DER, P7B, DL, CRL 파일 업로드 |
| `PA_EXTRACTED` | PA 검증 시 SOD에서 DSC 추출 | `POST /api/pa/verify` 요청 시 자동 등록 |
| `DL_PARSED` | Deviation List에서 파싱 | DL 파일 업로드 |

---

### 3. LDAP DIT 저장 정책

#### 3.1 DIT 구조 (인증서 유형 기반)

```
dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
├── dc=data                                    ← 모든 출처의 준수 인증서
│   └── c={COUNTRY}
│       ├── o=csca   (CSCA)
│       ├── o=dsc    (DSC)        ← LDIF_PARSED, FILE_UPLOAD, PA_EXTRACTED 모두 동일 경로
│       ├── o=lc     (Link Certificate)
│       ├── o=crl    (CRL)
│       ├── o=mlsc   (MLSC)
│       └── o=ml     (Master List)
└── dc=nc-data                                 ← ICAO 비준수 인증서 (deprecated 2021)
    └── c={COUNTRY}
        └── o=dsc    (DSC_NC)
```

#### 3.2 DN 구성 규칙

모든 인증서의 DN은 출처와 무관하게 동일한 규칙으로 구성된다:

```
cn={FINGERPRINT_SHA256},o={TYPE},c={COUNTRY},{CONTAINER},{BASE_DN}
```

예시:
| 인증서 | source_type | LDAP DN |
|--------|-------------|---------|
| 한국 DSC (ICAO PKD) | `LDIF_PARSED` | `cn=abc123...,o=dsc,c=KR,dc=data,...` |
| 한국 DSC (PA 추출) | `PA_EXTRACTED` | `cn=5fed6d...,o=dsc,c=KR,dc=data,...` |
| 한국 DSC (직접 업로드) | `FILE_UPLOAD` | `cn=def456...,o=dsc,c=KR,dc=data,...` |

**Fingerprint(SHA-256)가 동일하면 동일 인증서**이므로, 출처가 달라도 LDAP에 중복 저장되지 않는다.

#### 3.3 출처별 분리하지 않는 이유

ICAO 표준, 상용 레퍼런스 구현(Keyfactor NPKD), 오픈소스 구현(JMRTD) 모두 출처별 DIT 분리를 하지 않는다:

| 근거 | 설명 |
|------|------|
| **ICAO 중앙 PKD** | DIT 분리 기준은 **적합성**(dc=data vs dc=nc-data)이지 소스가 아님. nc-data는 2021년 폐지 |
| **Keyfactor NPKD** (상용) | DB의 `importedFrom` 컬럼으로 출처 추적. LDAP 게시는 동일 DN 템플릿 사용 |
| **JMRTD** (오픈소스) | ICAO PKD, BSI, 커뮤니티 등 다양한 출처 인증서를 동일 DIT에 로드 |
| **ICAO PKD White Paper (2020)** | Local PKD 내부 구조는 각 국가 구현에 위임. 출처별 분리 요구사항 없음 |

**기술적 이유**:

1. **인증서 동일성은 소스와 무관** — DSC는 fingerprint(SHA-256)로 고유 식별됨. ICAO PKD에서 받든, PA 검증에서 추출하든, 바이너리 내용과 fingerprint가 동일하면 같은 인증서
2. **검사 시스템은 type + country로 조회** — 출입국 시스템이 `o=dsc,c=KR,dc=data,...`에서 DSC를 검색할 때 출처를 구분하지 않음
3. **LDAP 스키마 호환성** — pkdDownload, cRLDistributionPoint 등 표준 objectClass에 출처 속성이 없음. 커스텀 속성 추가는 상호운용성을 저해

---

### 4. PA_EXTRACTED DSC 처리 상세

#### 4.1 저장 흐름

```
PA 검증 요청 (POST /api/pa/verify)
  │
  ├─ SOD에서 DSC 추출
  │
  ├─ Fingerprint(SHA-256) 계산
  │
  ├─ DB 중복 검사 (fingerprint 기반)
  │   ├─ 이미 존재 → 저장하지 않음 (대부분의 경우)
  │   └─ 미존재 → DB INSERT (source_type='PA_EXTRACTED', stored_in_ldap=FALSE)
  │
  └─ PA 검증 결과 반환 (dscAutoRegistration 필드 포함)

                    ↓ (비동기, PKD Management Reconciliation)

PKD Management Reconciliation
  │
  ├─ stored_in_ldap=FALSE 인증서 조회
  │
  ├─ buildDn() → cn={FINGERPRINT},o=dsc,c={COUNTRY},dc=data,...
  │
  ├─ LDAP에 추가 (addCertificate)
  │
  └─ stored_in_ldap=TRUE 업데이트
```

#### 4.2 중복 방지 메커니즘

PA 검증에서 추출한 DSC는 대부분 ICAO PKD에서 이미 수신한 인증서와 동일하다.

| 단계 | 중복 방지 방법 |
|------|---------------|
| **DB 저장** | SHA-256 fingerprint 인메모리 캐시 + DB SELECT 폴백 |
| **LDAP 저장** | Reconciliation이 stored_in_ldap=FALSE만 처리 (이미 LDAP에 있으면 TRUE) |

**운영 데이터 증거** (31,428건 중):

| source_type | 건수 | 비율 | 비고 |
|-------------|------|------|------|
| `LDIF_PARSED` | 31,427 | 99.997% | ICAO PKD LDIF 파일에서 파싱 |
| `PA_EXTRACTED` | 1 | 0.003% | PA 검증에서 추출한 신규 DSC |
| `ML_PARSED` | 0 | — | ML 컬렉션은 LDIF 형식으로 배포 |
| `FILE_UPLOAD` | 0 | — | 개별 업로드 없음 |

> **참고**: ICAO PKD 컬렉션 002(Master List)도 LDIF 형식으로 배포되어 `ldif_processor.cpp`를 통해 처리되므로 `LDIF_PARSED`로 분류된다. `.ml` 파일 직접 업로드 시에만 `ML_PARSED`가 사용된다.

PA 검증으로 중복 저장된 인증서는 **0건**. 유일한 PA_EXTRACTED 1건은 ICAO PKD LDIF에 포함되지 않았던 신규 DSC.

#### 4.3 PA_EXTRACTED DSC가 LDAP에 저장되는 경우

PA_EXTRACTED DSC가 LDAP에 저장되는 것은 **ICAO PKD에 없는 신규 DSC**인 경우에만 해당:

- ICAO PKD에 미참여 국가가 발급한 DSC
- 최근 발급되어 ICAO PKD에 아직 미반영된 DSC
- 양자간(bilateral) 교환으로만 배포된 DSC

이런 DSC를 LDAP에 저장하면 다른 검사 시스템에서도 PA 검증 시 해당 DSC를 조회할 수 있다.

---

### 5. 출처 추적 (DB 레벨)

#### 5.1 certificate 테이블

```sql
SELECT source_type, COUNT(*) as count
FROM certificate
GROUP BY source_type;
```

| 컬럼 | 설명 |
|------|------|
| `source_type` | 인증서 출처 (LDIF_PARSED, ML_PARSED, FILE_UPLOAD, PA_EXTRACTED, DL_PARSED) |
| `upload_id` | 출처 업로드 ID (PA_EXTRACTED는 NULL) |
| `stored_in_ldap` | LDAP 동기화 상태 |
| `ldap_dn` | LDAP DN (업로드 시 설정, Reconciliation은 미설정) |

#### 5.2 source_type 저장 구조

인증서 저장 시 `source_type`은 호출 경로에 따라 자동 설정된다:

| 처리 모듈 | 소스 파일 | source_type |
|-----------|----------|-------------|
| LDIF 파싱 (PKD Relay) | `ldif_processor.cpp` | `LDIF_PARSED` |
| Master List 파싱 (PKD Relay) | `masterlist_processor.cpp` | `ML_PARSED` |
| Master List 핸들러 직접 저장 (PKD Relay) | `upload_handler.cpp` | `ML_PARSED` |
| 개별 인증서 업로드 (PKD Management) | `upload_service.cpp` | `FILE_UPLOAD` |
| PA 검증 DSC 추출 | `dsc_auto_registration_service.cpp` | `PA_EXTRACTED` |

**구현 상세**: `saveCertificateWithDuplicateCheck()` 함수의 `sourceType` 파라미터 (기본값: `"FILE_UPLOAD"`)로 INSERT 시 `source_type` 컬럼에 저장된다.

> **v2.29.0 수정**: v2.28.2 이전에는 INSERT 쿼리에 `source_type` 컬럼이 누락되어 모든 인증서가 DB DEFAULT `FILE_UPLOAD`로 저장되었다. v2.29.0에서 `sourceType` 파라미터를 추가하고 각 호출부에서 올바른 타입을 전달하도록 수정.

#### 5.3 프론트엔드 활용

- **Dashboard**: "인증서 출처별 현황" 카드 (source_type별 가로 바 차트)
- **Certificate Search**: source 필터 (드롭다운)
- **Upload Statistics API**: `bySource` 필드 (GROUP BY source_type)

---

### 6. 참고 자료 (Certificate Source Management)

#### ICAO 문서

- ICAO Doc 9303 Part 12: Public Key Infrastructure (PKI)
- ICAO PKD White Paper (July 2020)
- ICAO PKD Regulations (July 2020)
- ICAO PKD Participant Guide (2021): nc-data deprecation notice

#### 레퍼런스 구현

- [Keyfactor NPKD Documentation](https://docs.keyfactor.com/npkd/latest/) — 상용 National PKD 솔루션
- [JMRTD Project](https://jmrtd.org/certificates.shtml) — 오픈소스 Java MRTD 구현

#### 관련 내부 문서

- [DSC_NC_HANDLING.md](DSC_NC_HANDLING.md) — DSC_NC 비준수 인증서 처리 가이드
- [PA_API_GUIDE.md](PA_API_GUIDE.md) — PA Service API 가이드 (DSC Auto-Registration 포함)
- [LDAP_QUERY_GUIDE.md](LDAP_QUERY_GUIDE.md) — LDAP 조회 가이드
- [SOFTWARE_ARCHITECTURE.md](SOFTWARE_ARCHITECTURE.md) — 시스템 아키텍처

---

## Part 2: Master List Processing

---

### 1. Overview

#### What is a Master List?

ICAO Master List는 **전 세계 국가의 CSCA 인증서를 포함하는 CMS SignedData 파일**입니다. UN(ICAO)이 서명하여 배포하며, 각국의 여권 발급 인증서(CSCA)와 Link Certificate를 포함합니다.

#### Purpose

- **CSCA 배포**: 각국의 Country Signing CA (CSCA) 인증서 배포
- **Trust Chain**: Link Certificate를 통한 CSCA 키 교체 및 조직 변경 지원
- **검증 기준**: UN 서명으로 Master List의 무결성 보장

#### File Types

| Type | Source | Format | Certificate Count |
|------|--------|--------|-------------------|
| **Direct File** | ICAO Portal | `.ml` (binary CMS) | 537 (1 MLSC + 536 CSCA/LC) |
| **LDIF Entry** | Collection 002 | LDIF (base64 CMS) | 27 entries x ~186 certs/entry |

---

### 2. Master List File Format

#### CMS SignedData Structure

Master List는 **PKCS#7 CMS SignedData** 형식입니다:

```
ICAO_ml_December2025.ml (CMS SignedData)
├── version: INTEGER
├── digestAlgorithms: SET OF AlgorithmIdentifier
├── encapContentInfo: ContentInfo
│   ├── contentType: id-data (1.2.840.113549.1.7.1)
│   └── content: OCTET STRING
│       └── MasterList ::= SEQUENCE {
│               version    INTEGER OPTIONAL,
│               certList   SET OF Certificate  (536개)
│           }
├── certificates: [0] IMPLICIT CertificateSet OPTIONAL  (비어있음!)
└── signerInfos: SET OF SignerInfo
    └── SignerInfo (1개)
        ├── version: INTEGER
        ├── sid: SignerIdentifier
        ├── digestAlgorithm: AlgorithmIdentifier
        ├── signedAttrs: [0] IMPLICIT Attributes OPTIONAL
        ├── signatureAlgorithm: AlgorithmIdentifier
        ├── signature: OCTET STRING
        └── unsignedAttrs: [1] IMPLICIT Attributes OPTIONAL
```

#### Key Insight: Two-Level Certificate Storage

**중요**: ICAO Master List는 인증서를 **2곳**에 저장합니다:

1. **SignerInfo**: MLSC (Master List Signer Certificate) - UN이 서명한 인증서
2. **encapContentInfo (pkiData)**: CSCA + Link Certificate (536개)

#### ASN.1 Structure

```asn1
-- Master List Structure (ICAO Doc 9303 Part 12)
MasterList ::= SEQUENCE {
    version    [0] INTEGER OPTIONAL,  -- v0(0)
    certList   SET OF Certificate
}

-- Certificate는 표준 X.509 Certificate 구조
Certificate ::= SEQUENCE {
    tbsCertificate       TBSCertificate,
    signatureAlgorithm   AlgorithmIdentifier,
    signature            BIT STRING
}
```

#### File Size & Structure

| Component | Size | Description |
|-----------|------|-------------|
| **Total File** | ~800 KB | CMS SignedData wrapper |
| **CMS Header** | ~3 KB | SignedData structure metadata |
| **SignerInfo** | ~10 KB | 1 MLSC certificate + signature |
| **pkiData** | ~790 KB | 536 CSCA/LC certificates |

---

### 3. Processing Architecture

#### Overview

Master List 처리는 **2단계 추출 방식**을 사용합니다:

```
┌─────────────────────────────────────────────────────────┐
│ Master List File (.ml or LDIF entry)                    │
└──────────────────┬──────────────────────────────────────┘
                   │
                   ▼
         ┌─────────────────┐
         │ Parse CMS       │
         │ d2i_CMS_bio()   │
         └────────┬────────┘
                  │
        ┌─────────┴─────────┐
        │                   │
        ▼                   ▼
┌───────────────┐   ┌──────────────────┐
│ Step 1:       │   │ Step 2:          │
│ SignerInfo    │   │ pkiData          │
│ (MLSC 1개)    │   │ (CSCA/LC 536개)  │
└───────┬───────┘   └────────┬─────────┘
        │                    │
        ▼                    ▼
┌───────────────┐   ┌──────────────────┐
│ o=mlsc,c=UN   │   │ o=csca,c={국가}  │
│ (LDAP)        │   │ o=lc,c={국가}    │
└───────────────┘   └──────────────────┘
```

#### Step 1: Extract MLSC from SignerInfo

```cpp
// Get SignerInfo entries
STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);

for (int i = 0; i < sk_CMS_SignerInfo_num(signerInfos); i++) {
    CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, i);

    // Extract signer certificate
    X509* signerCert = nullptr;
    CMS_SignerInfo_get0_algs(si, nullptr, &signerCert, nullptr, nullptr);

    // signerCert is the MLSC (Master List Signer Certificate)
    // Subject: /C=UN/O=United Nations/OU=Master List Signers/CN=ICAO Master List Signer
    // Issuer: Same (self-signed)

    // Save to: o=mlsc,c=UN
}
```

**Result**: 1 MLSC certificate

#### Step 2: Extract CSCA/LC from pkiData

```cpp
// Get encapsulated content
ASN1_OCTET_STRING** contentPtr = CMS_get0_content(cms);
const unsigned char* contentData = ASN1_STRING_get0_data(*contentPtr);
int contentLen = ASN1_STRING_length(*contentPtr);

// Parse MasterList ASN.1 structure
const unsigned char* p = contentData;

// 1. Parse outer SEQUENCE
int tag, xclass;
long seqLen;
ASN1_get_object(&p, &seqLen, &tag, &xclass, remaining);

// 2. Skip version (INTEGER) if present
if (tag == V_ASN1_INTEGER) {
    p += elemLen;
    ASN1_get_object(&p, &elemLen, &tag, &xclass, seqEnd - p);
}

// 3. Parse certList (SET)
if (tag == V_ASN1_SET) {
    const unsigned char* certPtr = p;
    const unsigned char* certSetEnd = p + elemLen;

    // 4. Extract each certificate
    while (certPtr < certSetEnd) {
        X509* cert = d2i_X509(nullptr, &certPtr, certSetEnd - certPtr);

        // Classify: CSCA or Link Certificate
        bool isLinkCert = (subjectDn != issuerDn);

        if (isLinkCert) {
            // Save to: o=lc,c={country}
        } else {
            // Save to: o=csca,c={country}
        }
    }
}
```

**Result**: 536 certificates (476 CSCA + 60 Link Certificates)

#### Country Code Extraction

**Challenge**: X509_NAME_oneline() returns slash-separated DN format (`/C=LV/O=...`), not comma-separated (`C=LV, O=...`).

**Solution**: Regex pattern supporting both formats

```cpp
// Before (comma only) - WRONG
static const std::regex countryRegex(R"((?:^|,\s*)C=([A-Z]{2,3})(?:,|$))", ...);

// After (slash + comma) - CORRECT
static const std::regex countryRegex(R"((?:^|[/,]\s*)C=([A-Z]{2,3})(?:[/,\s]|$))", ...);
```

**Fallback Order**:
1. Subject DN의 C= 추출
2. Issuer DN의 C= 추출 (Link Certificate용)
3. LDAP Entry DN의 c= 추출 (LDIF 처리 시)
4. 실패 시 "XX" 반환

---

### 4. Implementation Details

#### File Locations

| Component | File | Lines |
|-----------|------|-------|
| **Master List Processor** | `services/pkd-relay-service/src/common/masterlist_processor.cpp` | (v2.41.0 — relay로 이동) |
| **Header** | `services/pkd-relay-service/src/common/masterlist_processor.h` | (v2.41.0 — relay로 이동) |
| **Certificate Utils** | `services/pkd-relay-service/src/common/certificate_utils.cpp` | (v2.41.0 — relay로 이동) |

#### Function Signatures

```cpp
// Direct file processing
bool processMasterListFile(
    PGconn* conn,
    LDAP* ld,
    const std::string& uploadId,
    const std::vector<uint8_t>& content,
    MasterListStats& stats
);

// LDIF entry processing
bool parseMasterListEntryV2(
    PGconn* conn,
    LDAP* ld,
    const std::string& uploadId,
    const LdifEntry& entry,
    MasterListStats& stats
);
```

#### Database Schema

```sql
-- Certificate table
CREATE TABLE certificate (
    id UUID PRIMARY KEY,
    certificate_type VARCHAR(20) CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC')),
    country_code VARCHAR(3) NOT NULL,
    subject_dn TEXT NOT NULL,
    issuer_dn TEXT NOT NULL,
    serial_number VARCHAR(100) NOT NULL,
    fingerprint_sha256 VARCHAR(64) NOT NULL UNIQUE,
    not_before TIMESTAMP NOT NULL,
    not_after TIMESTAMP NOT NULL,
    certificate_data BYTEA NOT NULL,
    stored_in_ldap BOOLEAN DEFAULT FALSE,
    ldap_dn TEXT,
    upload_id UUID REFERENCES uploaded_file(id),
    ...
);

-- Master List metadata
CREATE TABLE master_list (
    id UUID PRIMARY KEY,
    upload_id UUID REFERENCES uploaded_file(id),
    country_code VARCHAR(3) NOT NULL,
    signer_dn TEXT NOT NULL,
    fingerprint_sha256 VARCHAR(64) NOT NULL,
    csca_count INTEGER NOT NULL,
    master_list_data BYTEA NOT NULL,
    stored_in_ldap BOOLEAN DEFAULT FALSE,
    ldap_dn TEXT,
    ...
);
```

#### LDAP Structure

```
dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
├── c=UN
│   └── o=mlsc
│       └── cn={fingerprint}  (MLSC, 1 entry)
│           ├── objectClass: pkdDownload
│           ├── userCertificate;binary: {DER}
│           └── description: Self-signed Master List Signer
│
├── c=CN (China)
│   └── o=csca
│       ├── cn={fingerprint1}  (CSCA)
│       ├── cn={fingerprint2}  (CSCA)
│       └── ...  (34 entries)
│
├── c=LV (Latvia)
│   ├── o=csca
│   │   ├── cn={fingerprint1}  (CSCA)
│   │   └── ...  (9 entries)
│   └── o=lc
│       ├── cn={fingerprint2}  (Link Cert)
│       └── ...  (7 entries)
│
└── ... (95 countries total)
```

**DN Format**:
- Fingerprint-based: `cn={sha256_hex},o={type},c={country},dc=data,...`
- Example: `cn=64b542ae9f8c...,o=csca,c=LV,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com`

---

### 5. Common Pitfalls & Solutions

#### Pitfall 1: Using CMS_get1_certs()

**Problem**: `CMS_get1_certs(cms)` only returns `SignedData.certificates` field, which is **empty** in ICAO Master Lists.

```cpp
// WRONG - Only returns 0-2 certificates
STACK_OF(X509)* certs = CMS_get1_certs(cms);
// certs is NULL or empty!
```

**Why**: ICAO Master List stores certificates in pkiData (encapContentInfo), not in the certificates field.

**Solution**: Use two-step extraction (SignerInfo + pkiData)

#### Pitfall 2: Ignoring SignerInfo

**Problem**: MLSC (Master List Signer Certificate) is **only** in SignerInfo, not in pkiData.

**Solution**: Always extract SignerInfo first

```cpp
STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
// Extract MLSC from SignerInfo
```

#### Pitfall 3: Wrong Country Code Regex

**Problem**: Regex `(?:^|,\s*)C=([A-Z]{2,3})(?:,|$)` only matches comma-separated DN (`C=LV, O=...`), but OpenSSL returns slash-separated DN (`/C=LV/O=...`).

**Symptoms**:
- All certificates stored with country_code='XX'
- LDAP entries created at wrong locations

**Solution**: Update regex to support both formats

```cpp
// Supports: /C=LV/O=... AND C=LV, O=...
static const std::regex countryRegex(
    R"((?:^|[/,]\s*)C=([A-Z]{2,3})(?:[/,\s]|$))",
    std::regex::icase
);
```

#### Pitfall 4: Wrong Fallback for Country Code

**Problem**: Using UN as fallback country code for all CSCA/LC

```cpp
// WRONG
if (certCountryCode == "XX") {
    certCountryCode = countryCode;  // countryCode = "UN" from Master List
}
// Result: All certificates stored at c=UN instead of their own countries
```

**Solution**: Use Subject DN -> Issuer DN -> Entry DN -> "XX" fallback chain

```cpp
// CORRECT
std::string certCountryCode = extractCountryCode(meta.subjectDn);
if (certCountryCode == "XX") {
    // Try issuer DN (for link certificates)
    certCountryCode = extractCountryCode(meta.issuerDn);
    if (certCountryCode == "XX") {
        // Try LDAP entry DN (LDIF processing only)
        certCountryCode = extractCountryCodeFromDn(entry.dn);
        // Keep as "XX" if still not found - do NOT use "UN"
    }
}
```

#### Pitfall 5: Missing MLSC Constraint

**Problem**: Database rejects MLSC certificate type

```
ERROR: new row violates check constraint "chk_certificate_type"
DETAIL: Failing row contains (..., MLSC, UN, ...)
```

**Solution**: Add MLSC to constraint

```sql
ALTER TABLE certificate DROP CONSTRAINT IF EXISTS chk_certificate_type;
ALTER TABLE certificate ADD CONSTRAINT chk_certificate_type
    CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC'));
```

#### Pitfall 6: Memory Leaks in OpenSSL

**Problem**: Not freeing OpenSSL structures causes memory leaks

**Solution**: Always free structures

```cpp
CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
// ... processing ...

// Cleanup
CMS_ContentInfo_free(cms);  // Frees cms and internal structures
X509_free(cert);            // Free individual certificates
sk_X509_pop_free(certs, X509_free);  // Free certificate stack
```

#### Pitfall 7: ASN.1 Parsing Errors

**Problem**: Incorrect ASN.1 tag checking or pointer arithmetic

**Common Issues**:
- Not checking return value of `ASN1_get_object()`
- Wrong pointer advancement
- Buffer overrun

**Solution**: Always validate ASN.1 parsing

```cpp
int ret = ASN1_get_object(&p, &seqLen, &tag, &xclass, remaining);
if (ret == 0x80) {
    // Error: indefinite length or invalid
    spdlog::error("ASN.1 parsing error");
    return false;
}

if (tag != V_ASN1_SEQUENCE) {
    spdlog::error("Expected SEQUENCE, got tag {}", tag);
    return false;
}

// Verify buffer bounds
if (p + seqLen > bufferEnd) {
    spdlog::error("Buffer overrun");
    return false;
}
```

---

### 6. Testing & Verification

#### Test Files

| File | Source | Size | Certificates |
|------|--------|------|--------------|
| `ICAO_ml_December2025.ml` | ICAO Portal | 791 KB | 537 (1 MLSC + 536 CSCA/LC) |
| `icaopkd-002-complete-000333.ldif` | Collection 002 | 10.5 MB | 27 ML entries x ~186 certs |

#### Verification Checklist

##### Certificate Extraction

```bash
# Check uploaded file statistics
docker compose -f docker/docker-compose.yaml exec -T postgres psql -U pkd -d localpkd -c "
SELECT
    file_name,
    status,
    ml_count,
    csca_count,
    csca_extracted_from_ml,
    csca_duplicates
FROM uploaded_file
WHERE file_name LIKE '%ml%'
ORDER BY upload_timestamp DESC
LIMIT 5;
"
```

**Expected**:
- Direct file: `ml_count=1`, `csca_count=0`, `csca_extracted_from_ml=536`
- LDIF file: `ml_count=27`, `csca_count=309`, `csca_extracted_from_ml=5017`

##### Certificate Types

```sql
SELECT certificate_type, COUNT(*)
FROM certificate
WHERE upload_id = '{upload_id}'
GROUP BY certificate_type;
```

**Expected**:
- CSCA: 476-536
- MLSC: 1-27 (depending on ML count)

##### Country Distribution

```sql
SELECT country_code, COUNT(*) as count
FROM certificate
WHERE certificate_type IN ('CSCA', 'MLSC')
GROUP BY country_code
ORDER BY count DESC
LIMIT 10;
```

**Expected Top Countries**:
- CN (China): 34
- HU (Hungary): 21
- LV (Latvia): 16
- NL (Netherlands): 15
- NZ (New Zealand): 13

##### LDAP Storage

```bash
# Count MLSC
ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -b "o=mlsc,c=UN,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(objectClass=pkdDownload)" dn | grep -c "^dn:"

# Count CSCA for specific country
ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -b "o=csca,c=CN,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(objectClass=pkdDownload)" dn | grep -c "^dn:"
```

**Expected**:
- MLSC at c=UN: 1 entry
- CSCA at c=CN: 34 entries
- LC at c=LV: 7 entries

##### No XX Country Codes

```sql
SELECT COUNT(*) FROM certificate WHERE country_code = 'XX';
```

**Expected**: 0 (no unknown country codes)

##### Link Certificate Classification

```sql
SELECT
    CASE
        WHEN subject_dn = issuer_dn THEN 'Self-signed'
        ELSE 'Link Certificate'
    END as cert_class,
    COUNT(*)
FROM certificate
WHERE certificate_type = 'CSCA'
  AND upload_id = '{upload_id}'
GROUP BY cert_class;
```

**Expected**:
- Self-signed: ~476
- Link Certificate: ~60

#### Log Verification

##### Successful Processing Logs

```
[ML-FILE] Processing Master List file: 810009 bytes
[ML-FILE] CMS SignedData parsed successfully
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
AUTO mode: Master List processing completed - 1 MLSC, 536 CSCA/LC extracted
```

##### Error Patterns to Watch

```
[ML-FILE] Failed to parse Master List as CMS SignedData
→ File is not a valid CMS structure

[ML-FILE] MLSC 1/1 - Failed to save to DB, reason: Database operation failed
→ Check database constraint (MLSC type allowed?)

[ML-FILE] Certificate 123 - Could not extract country from Subject or Issuer DN
→ Certificate has non-standard DN format

[ML-FILE] Certificate 456 - Failed to save to LDAP, reason: LDAP operation failed
→ Check LDAP connection and parent DN existence
```

---

### 7. Troubleshooting

#### Issue: Only 2 Certificates Extracted

**Symptoms**:
- Upload shows `csca_extracted_from_ml=2`
- Logs show "Master List contains 2 CSCAs"

**Cause**: Using `CMS_get1_certs()` instead of two-step extraction

**Fix**: Update to use `CMS_get0_SignerInfos()` + `CMS_get0_content()`

#### Issue: All Certificates at c=XX

**Symptoms**:
- LDAP shows all entries under `c=XX`
- Database has `country_code='XX'` for all

**Cause**: Regex pattern not matching slash-separated DN

**Fix**: Update country code extraction regex:
```cpp
static const std::regex countryRegex(R"((?:^|[/,]\s*)C=([A-Z]{2,3})(?:[/,\s]|$))", ...);
```

#### Issue: All Certificates at c=UN

**Symptoms**:
- LDAP shows CSCA/LC under `c=UN` instead of their countries
- Database has `country_code='UN'` for CSCA

**Cause**: Using UN as fallback for all certificates

**Fix**: Remove UN fallback for CSCA/LC, only use for MLSC

#### Issue: MLSC Not Saved

**Symptoms**:
```
ERROR: new row violates check constraint "chk_certificate_type"
```

**Cause**: Database constraint doesn't allow MLSC type

**Fix**:
```sql
ALTER TABLE certificate DROP CONSTRAINT IF EXISTS chk_certificate_type;
ALTER TABLE certificate ADD CONSTRAINT chk_certificate_type
    CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC'));
```

#### Issue: Memory Leaks

**Symptoms**:
- Service memory grows over time
- OOM errors after processing many files

**Cause**: Not freeing OpenSSL structures

**Fix**: Always call cleanup functions:
```cpp
CMS_ContentInfo_free(cms);
X509_free(cert);
sk_X509_pop_free(certs, X509_free);
```

#### Issue: Buffer Overrun in ASN.1 Parsing

**Symptoms**:
- Segmentation fault during processing
- Random data corruption

**Cause**: Pointer arithmetic error in ASN.1 parsing

**Fix**: Always check buffer bounds:
```cpp
while (certPtr < certSetEnd) {
    if (certPtr + 4 > certSetEnd) {
        // Not enough data for next certificate
        break;
    }
    X509* cert = d2i_X509(nullptr, &certPtr, certSetEnd - certPtr);
    if (!cert) break;
}
```

---

### 8. Performance Considerations

#### Processing Time

| File Type | Size | Certificates | Time | Rate |
|-----------|------|-------------|------|------|
| Direct ML | 791 KB | 537 | ~3s | 179 certs/s |
| LDIF ML | 11 MB | 5,017 | ~30s | 167 certs/s |

#### Optimization Opportunities

1. **Batch Insert**: Currently inserts certificates one by one
   - **Current**: 537 x (1 DB INSERT + 1 LDAP ADD)
   - **Optimized**: 1 DB COPY + batch LDAP ADD
   - **Improvement**: 5-10x faster

2. **Parallel Processing**: LDIF entries can be processed in parallel
   - **Current**: Sequential processing of 27 ML entries
   - **Optimized**: Thread pool with 4-8 workers
   - **Improvement**: 3-4x faster

3. **Connection Pooling**: Reuse DB/LDAP connections
   - **Current**: New connection per entry
   - **Optimized**: Connection pool
   - **Improvement**: 20-30% faster

#### Memory Usage

- **Peak Memory**: ~50 MB per Master List
- **Breakdown**:
  - CMS structure: ~1 MB
  - Certificate parsing: ~10 MB
  - OpenSSL internal: ~20 MB
  - Application data: ~20 MB

---

### 9. References (Master List Processing)

#### ICAO Standards

- **ICAO Doc 9303**: Machine Readable Travel Documents
  - Part 12: Public Key Infrastructure for MRTDs
  - Annex G: Master List Format

#### OpenSSL Documentation

- [CMS Functions](https://www.openssl.org/docs/man3.0/man3/CMS_get0_SignerInfos.html)
- [ASN.1 Parsing](https://www.openssl.org/docs/man3.0/man3/ASN1_get_object.html)
- [X509 Certificate](https://www.openssl.org/docs/man3.0/man3/X509_get_subject_name.html)

#### Internal Documents

- [ML_FILE_PROCESSING_COMPLETION.md](ML_FILE_PROCESSING_COMPLETION.md) - Initial implementation completion
- [MLSC_ROOT_CAUSE_ANALYSIS.md](archive/MLSC_ROOT_CAUSE_ANALYSIS.md) - Bug analysis
- [MLSC_EXTRACTION_FIX.md](archive/MLSC_EXTRACTION_FIX.md) - Fix implementation
- [SPRINT3_COMPLETION_SUMMARY.md](SPRINT3_COMPLETION_SUMMARY.md) - Sprint 3 summary

#### Code References

- [masterlist_processor.cpp](../services/pkd-relay-service/src/common/masterlist_processor.cpp) - Main implementation (v2.41.0 — relay로 이동)
- [certificate_utils.cpp](../services/pkd-relay-service/src/common/certificate_utils.cpp) - Database operations (v2.41.0 — relay로 이동)

---

### Appendix A: Test Master List Structure

#### ICAO_ml_December2025.ml Analysis

```bash
# Analyze with OpenSSL
openssl cms -in ICAO_ml_December2025.ml -inform DER -cmsout -print

# Output (abbreviated):
CMS_ContentInfo:
  contentType: pkcs7-signedData (1.2.840.113549.1.7.2)
  d.signedData:
    version: 3
    digestAlgorithms:
      algorithm: sha256 (2.16.840.1.101.3.4.2.1)
    encapContentInfo:
      eContentType: pkcs7-data (1.2.840.113549.1.7.1)
      eContent:
        <OCTET STRING> (806391 bytes)
    signerInfos:
      version: 3
      d.issuerAndSerialNumber:
        issuer: C=UN, O=United Nations, OU=Master List Signers, CN=ICAO Master List Signer
        serialNumber: 0x1234567890ABCDEF
      digestAlgorithm:
        algorithm: sha256
      signatureAlgorithm:
        algorithm: rsaEncryption
      signature:
        <OCTET STRING> (256 bytes)
```

#### Certificate Distribution by Type

```
Total: 537 certificates
├── MLSC: 1 (0.2%)
│   └── UN: 1
├── Self-signed CSCA: 476 (88.6%)
│   ├── CN: 30
│   ├── HU: 18
│   ├── LV: 9
│   └── ... (92 more countries)
└── Link Certificates: 60 (11.2%)
    ├── LV: 7
    ├── PH: 6
    ├── EE: 4
    └── ... (25 more countries)
```

---

### Appendix B: Quick Reference Commands

#### Development

```bash
# Rebuild service (LDIF/ML processing is now in PKD Relay, v2.41.0)
docker compose -f docker/docker-compose.yaml build pkd-relay

# Restart service
docker compose -f docker/docker-compose.yaml restart pkd-relay

# View logs
docker logs icao-local-pkd-relay -f --tail 100 | grep "ML-FILE\|ML-LDIF"
```

#### Testing

```bash
# Upload Master List file
curl -X POST http://localhost:8080/api/upload/masterlist \
  -F "file=@data/uploads/ICAO_ml_December2025.ml" \
  -F "processingMode=AUTO"

# Upload LDIF file
curl -X POST http://localhost:8080/api/upload/ldif \
  -F "file=@data/uploads/icaopkd-002-complete-000333.ldif" \
  -F "processingMode=AUTO"

# Check upload status
curl http://localhost:8080/api/upload/history | jq '.data.uploads[0]'
```

#### Database Queries

```bash
# Connect to database
docker compose -f docker/docker-compose.yaml exec -T postgres psql -U pkd -d localpkd

# Count certificates by type
SELECT certificate_type, COUNT(*) FROM certificate GROUP BY certificate_type;

# Count certificates by country (Top 10)
SELECT country_code, COUNT(*) FROM certificate
WHERE certificate_type IN ('CSCA', 'MLSC')
GROUP BY country_code ORDER BY COUNT(*) DESC LIMIT 10;

# Check LDAP storage rate
SELECT
    COUNT(*) as total,
    SUM(CASE WHEN stored_in_ldap THEN 1 ELSE 0 END) as stored,
    ROUND(100.0 * SUM(CASE WHEN stored_in_ldap THEN 1 ELSE 0 END) / COUNT(*), 2) as percentage
FROM certificate
WHERE certificate_type IN ('CSCA', 'MLSC');
```

#### LDAP Queries

```bash
# Source helper functions
source scripts/ldap-helpers.sh

# Count all certificates
ldap_count_all

# Count by country
ldap_search_country CN

# Count MLSC
ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -b "o=mlsc,c=UN,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(objectClass=pkdDownload)" dn | grep -c "^dn:"
```

---

**Copyright 2026 SMARTCORE Inc. All rights reserved.**
