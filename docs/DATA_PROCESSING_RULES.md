# ICAO Local PKD - 데이터 처리 규칙

**버전**: 2.0.0
**최종 수정**: 2026-01-23
**상태**: Active

---

## 개요

ICAO Local PKD 시스템의 파일 업로드 및 데이터 처리 규칙을 정의합니다.

---

## 파일 형식별 처리 규칙

### 1. ICAO Master List 파일 (`.ml`)

**파일 특징**:
- ICAO가 서명한 전체 회원국 CSCA 인증서
- Signed CMS (PKCS#7) 형식
- 파일 전체가 하나의 CMS 구조
- 파일명 예: `ICAO_ml_December2025.ml`

**처리 절차**:
1. **CMS 파싱**:
   ```cpp
   BIO* bio = BIO_new_mem_buf(fileContent, fileSize);
   CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
   STACK_OF(X509)* certs = CMS_get1_certs(cms);
   ```

2. **인증서 추출 및 저장**:
   ```cpp
   for (int i = 0; i < sk_X509_num(certs); i++) {
       X509* cert = sk_X509_value(certs, i);
       // DER 인코딩
       // 메타데이터 추출 (subject, issuer, serial, validity)
       // DB 저장: certificate 테이블
       // LDAP 저장: o=csca,c={COUNTRY},dc=data,dc=download,dc=pkd,...
   }
   ```

3. **인증서 분류**:
   - **CSCA**: Self-signed 인증서 (Issuer DN == Subject DN)
   - **CSCA**: Cross-signed 인증서 (CSCA간 상호 인증)
   - 원칙: Master List의 모든 인증서는 CSCA로 분류

**저장 위치**:
- **DB**: `certificate` 테이블 (`certificate_type='CSCA'`)
- **LDAP**: `o=csca,c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com`

**통계**:
- `uploaded_file.csca_count`: 추출된 CSCA 개수
- LDAP 통계에 포함: ✅

---

### 2. Collection 001 - DSC/CRL (`.ldif`)

**파일 특징**:
- ICAO PKD Collection 001 (DSC + CRL)
- LDIF 형식 (여러 Entry)
- 파일명: `icaopkd-001-complete-009668.ldif`, `icaopkd-001-delta-009665.ldif`

**Entry 종류**:

#### 2.1 DSC Entry
**속성**: `userCertificate;binary`

**처리**:
1. Base64 디코딩 → DER 바이너리
2. d2i_X509() → X509 구조체
3. 메타데이터 추출
4. Trust Chain 검증 (CSCA lookup)
5. DB + LDAP 저장

**저장 위치**:
- **DB**: `certificate` 테이블 (`certificate_type='DSC'`)
- **LDAP**: `o=dsc,c={COUNTRY},dc=data,dc=download,dc=pkd,...`

#### 2.2 CRL Entry
**속성**: `certificateRevocationList;binary`

**처리**:
1. Base64 디코딩 → DER 바이너리
2. d2i_X509_CRL() → X509_CRL 구조체
3. 메타데이터 추출 (issuer, thisUpdate, nextUpdate)
4. DB + LDAP 저장

**저장 위치**:
- **DB**: `crl` 테이블
- **LDAP**: `o=crl,c={COUNTRY},dc=data,dc=download,dc=pkd,...`

---

### 3. Collection 002 - Master List (`.ldif`) ⭐ **NEW**

**파일 특징**:
- ICAO PKD Collection 002 (국가별 Master List)
- LDIF 형식 (여러 Entry)
- 각 Entry가 국가별 Master List Signed CMS
- 파일명: `icaopkd-002-complete-000333.ldif`

**Entry 구조**:
```ldif
dn: cn={HASH},o=ml,c={COUNTRY},dc=data,dc=download,dc=pkd,...
objectClass: pkdMasterList
objectClass: pkdDownload
cn: {HASH}
sn: 1
pkdMasterListContent:: {BASE64_ENCODED_CMS}
```

**처리 절차** (v2.0.0):

#### Step 1: CMS 파싱 및 CSCA 추출
```cpp
// 1. Base64 디코딩
std::vector<uint8_t> cmsBytes = base64Decode(pkdMasterListContent);

// 2. CMS 파싱
BIO* bio = BIO_new_mem_buf(cmsBytes.data(), cmsBytes.size());
CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
STACK_OF(X509)* certs = CMS_get1_certs(cms);

// 3. 각 CSCA 추출 및 저장
for (int i = 0; i < sk_X509_num(certs); i++) {
    X509* cert = sk_X509_value(certs, i);

    // DER 인코딩
    unsigned char* certDer = nullptr;
    int certDerLen = i2d_X509(cert, &certDer);

    // 메타데이터 추출
    char subjectBuf[512], issuerBuf[512], serialBuf[128];
    X509_NAME_oneline(X509_get_subject_name(cert), subjectBuf, sizeof(subjectBuf));
    // ... (issuer, serial, validity 추출)

    // 중복 체크 및 저장
    auto [certId, isDuplicate] = saveCertificateWithDuplicateCheck(
        conn, uploadId, "CSCA", sourceCountry, subjectBuf, issuerBuf, ...
    );

    if (isDuplicate) {
        // 중복 통계 업데이트
        incrementDuplicateCount(conn, certId, uploadId);
    } else {
        // LDAP 저장 (신규만)
        saveCertificateToLdap(ld, "CSCA", sourceCountry, ...);
    }

    // 소스 추적 (모든 경우)
    trackCertificateDuplicate(conn, certId, uploadId, "LDIF_002", sourceCountry, entry.dn);
}
```

#### Step 2: 원본 Master List 백업
```cpp
// DB 저장 (메타데이터)
saveMasterList(conn, uploadId, sourceCountry, signerDn, fingerprint, cscaCount, cmsBytes);

// LDAP 저장 (원본 CMS)
saveMasterListToLdap(ld, sourceCountry, signerDn, fingerprint, cmsBytes);
```

**저장 위치**:

| 데이터 | 위치 | 용도 | 통계 포함 |
|-------|------|------|----------|
| **개별 CSCA** | `o=csca,c={COUNTRY},dc=data,...` | Primary 인증서 저장소, 검색 대상 | ✅ |
| **원본 Master List** | `o=ml,c={COUNTRY},dc=data,...` | 백업, 감사 추적, 원본 보존 | ❌ |

**중복 처리**:
- `.ml` 파일에서 이미 추출된 CSCA와 중복 가능
- `certificate_duplicates` 테이블에 모든 소스 기록
- `duplicate_count` 증가
- LDAP에는 중복 저장 안 함 (첫 번째만)

**통계**:
- `uploaded_file.ml_count`: Master List Entry 개수 (27개)
- `uploaded_file.csca_extracted_from_ml`: 추출된 CSCA 개수 (신규 + 중복)
- `uploaded_file.csca_duplicates`: 중복 감지된 CSCA 개수
- LDAP 통계:
  - `o=csca` 브랜치: 통계 포함 ✅
  - `o=ml` 브랜치: 통계 제외 ❌ (백업 목적)

---

### 4. Collection 003 - DSC_NC (`.ldif`)

**파일 특징**:
- ICAO PKD Collection 003 (Non-Conformant DSC)
- LDIF 형식 (여러 Entry)
- 파일명: `icaopkd-003-complete-000090.ldif`

**Entry 구조**:
```ldif
dn: cn={HASH},o=dsc,c={COUNTRY},dc=nc-data,dc=download,dc=pkd,...
objectClass: pkdDownload
userCertificate;binary: {BASE64_ENCODED_DER}
```

**처리**:
- Collection 001 DSC와 동일
- 단, 저장 위치가 `dc=nc-data` 브랜치

**저장 위치**:
- **DB**: `certificate` 테이블 (`certificate_type='DSC_NC'`)
- **LDAP**: `o=dsc,c={COUNTRY},dc=nc-data,dc=download,dc=pkd,...`

---

## LDAP DIT 구조

```
dc=ldap,dc=smartcoreinc,dc=com
└── dc=pkd
    └── dc=download
        ├── dc=data                         # 표준 인증서 (통계 포함)
        │   └── c={COUNTRY}
        │       ├── o=csca                  # ✅ CSCA (ML 파일 + Collection 002)
        │       ├── o=dsc                   # ✅ DSC (Collection 001)
        │       ├── o=crl                   # ✅ CRL (Collection 001)
        │       └── o=ml                    # ❌ Master List 백업 (통계 제외)
        └── dc=nc-data                      # 비표준 인증서
            └── c={COUNTRY}
                └── o=dsc                   # ✅ DSC_NC (Collection 003)
```

**통계 집계 규칙** (PKD Relay Service):
- ✅ **포함**: `dc=data` 하위의 `o=csca`, `o=dsc`, `o=crl`
- ✅ **포함**: `dc=nc-data` 하위의 `o=dsc` (DSC_NC)
- ❌ **제외**: `dc=data` 하위의 `o=ml` (백업 목적)

---

## 중복 처리 규칙

### 중복 판별 기준

**Primary Key**: `(certificate_type, fingerprint_sha256)`

### 중복 시나리오

1. **ML 파일 vs Collection 002**:
   - ML 파일에서 추출한 CSCA
   - Collection 002의 국가별 Master List에 동일 CSCA 포함
   - 예상 중복률: 80-90%

2. **Collection 002 내부 중복**:
   - 여러 국가의 Master List에 동일 CSCA 포함
   - Cross-certification 케이스

### 중복 처리 동작

1. **중복 감지**:
   ```sql
   SELECT id FROM certificate
   WHERE certificate_type = 'CSCA'
     AND fingerprint_sha256 = '{HASH}'
   ```

2. **중복인 경우**:
   - DB: `duplicate_count` 증가, `last_seen_*` 업데이트
   - LDAP: 저장 안 함 (첫 번째만 유지)
   - 추적: `certificate_duplicates` 테이블에 소스 기록

3. **신규인 경우**:
   - DB: INSERT with `duplicate_count=0`, `first_upload_id` 설정
   - LDAP: `o=csca` 브랜치에 저장
   - 추적: `certificate_duplicates` 테이블에 소스 기록

---

## Trust Chain 검증 규칙

### DSC → CSCA 검증

```cpp
// 1. DSC의 Issuer DN 추출
std::string issuerDn = X509_get_issuer_name(dsc);

// 2. CSCA 검색 (Case-insensitive)
SELECT id, certificate_data
FROM certificate
WHERE certificate_type = 'CSCA'
  AND LOWER(subject_dn) = LOWER('{ISSUER_DN}');

// 3. 서명 검증
X509* csca = d2i_X509(...);
EVP_PKEY* cscaPubKey = X509_get_pubkey(csca);
int verifyResult = X509_verify(dsc, cscaPubKey);

// 4. 검증 결과 저장
updateCertificateValidation(conn, dscId, verifyResult == 1, ...);
```

### 검증 통계

- `uploaded_file.validation_valid_count`: 검증 성공한 DSC 개수
- `uploaded_file.validation_invalid_count`: 검증 실패한 DSC 개수
- `uploaded_file.trust_chain_valid_count`: Trust Chain 유효한 DSC 개수
- `uploaded_file.trust_chain_invalid_count`: Trust Chain 무효한 DSC 개수

---

## 처리 모드

### AUTO 모드

**특징**:
- 파일 업로드 → 자동 파싱 → DB 저장 → LDAP 저장 → 검증
- 사용자 개입 없이 전체 처리 완료

**적용 대상**:
- ICAO Master List (`.ml`)
- Collection 001, 002, 003 LDIF (완전 파일)

### MANUAL 모드

**특징**:
- Stage 1: 파싱 → Temp 파일 저장 → 대기
- Stage 2: 사용자 승인 → DB 저장 → 대기
- Stage 3: 사용자 승인 → LDAP 업로드

**적용 대상**:
- Delta LDIF 파일 (검토 필요)
- 테스트 파일

---

## 예외 처리

### 파일 크기 제한

- **Nginx**: 100MB (`client_max_body_size`)
- **Backend**: 100MB (Drogon `setClientMaxBodySize`)

### 타임아웃

- **Nginx**: 5분 (300초)
- **LDAP**: 60초 (search timeout)
- **PostgreSQL**: 5초 (connection timeout)

### 중복 파일 업로드

- SHA-256 해시로 중복 감지
- HTTP 409 Conflict 응답
- 기존 업로드 정보 반환

---

## 변경 이력

| 날짜 | 버전 | 변경 내용 |
|------|------|----------|
| 2026-01-23 | 2.0.0 | Collection 002 처리 규칙 대폭 수정 (CSCA 추출, 중복 처리) |
| 2026-01-21 | 1.1.0 | MANUAL 모드 추가, Trust Chain 검증 상세화 |
| 2026-01-15 | 1.0.0 | 초안 작성 |

---

## 참고 문서

- [COLLECTION_002_CSCA_EXTRACTION.md](COLLECTION_002_CSCA_EXTRACTION.md) - Collection 002 상세 구현 계획
- [ICAO Doc 9303 Part 12](https://www.icao.int/publications/Documents/9303_p12_cons_en.pdf) - PKI for MRTDs
- [RFC 5652](https://datatracker.ietf.org/doc/html/rfc5652) - Cryptographic Message Syntax
