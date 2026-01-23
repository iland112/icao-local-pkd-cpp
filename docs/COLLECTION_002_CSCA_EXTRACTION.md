# Collection 002 Master List CSCA 추출 및 중복 처리

**작성일**: 2026-01-23
**버전**: 1.0.0
**상태**: Implementation Planned

---

## 개요

Collection 002 LDIF 파일의 Master List Entry에서 개별 CSCA 인증서를 추출하여 `o=csca` 브랜치에 저장하고, 원본 Master List는 `o=ml`에 백업하는 기능 구현.

### 배경

**문제**: DB와 LDAP의 CSCA 개수 불일치
- **PostgreSQL**: 536개 CSCA (ICAO Master List `.ml` 파일에서 추출)
- **LDAP**: 78개 (`o=csca`) + 27개 (`o=ml`) = 105개
- **불일치**: 431개 누락

**원인**: Collection 002 LDIF의 Master List Entry를 파싱은 하지만 개별 CSCA를 추출하지 않음

---

## Master List 파일 종류 및 처리 방식

### 1. ICAO Master List (`.ml` 파일)

**특징**:
- ICAO가 서명한 전체 회원국 CSCA 인증서
- Signed CMS (PKCS#7) 형식
- 파일 전체가 하나의 CMS 구조

**처리 방식** (현재 구현됨):
```cpp
// File: main.cpp, processMasterListFile()
1. BIO_new_mem_buf() → CMS 파일 전체 읽기
2. d2i_CMS_bio() → CMS 파싱
3. CMS_get1_certs() → 모든 CSCA 인증서 추출
4. 각 X509* 순회 → saveCertificate() 호출
5. LDAP 저장: o=csca,c={COUNTRY},dc=data,...
```

**저장 위치**:
- DB: `certificate` 테이블 (`certificate_type='CSCA'`)
- LDAP: `o=csca,c={COUNTRY},dc=data,dc=download,dc=pkd,...`

---

### 2. Collection 002 LDIF (`.ldif` 파일)

**특징**:
- ICAO PKD 다운로드 사이트 게시 파일
- LDIF 형식 (여러 Entry 포함)
- 각 Entry가 국가별 Master List (Signed CMS)
- Entry 속성: `pkdMasterListContent;binary` (Base64 인코딩된 CMS 데이터)

**기존 처리 방식** (문제):
```cpp
// File: main.cpp, parseMasterListEntry()
1. pkdMasterListContent;binary → Base64 디코딩
2. d2i_CMS_bio() → CMS 파싱
3. CMS_get1_certs() → 인증서 개수만 세기 (sk_X509_num)
4. saveMasterList() → DB에 메타데이터 저장
5. saveMasterListToLdap() → LDAP o=ml에 원본 CMS 저장
6. ❌ 개별 CSCA 추출 안 함
```

**새로운 처리 방식** (구현 예정):
```cpp
1. pkdMasterListContent;binary → Base64 디코딩
2. d2i_CMS_bio() → CMS 파싱
3. CMS_get1_certs() → 인증서 스택 획득
4. FOR EACH X509* in stack:
   a. i2d_X509() → DER 인코딩
   b. saveCertificateWithDuplicateCheck() → DB 저장 (중복 체크)
   c. IF NOT duplicate:
      - saveCertificateToLdap() → LDAP o=csca 저장
   d. ELSE:
      - incrementDuplicateCount()
   e. trackCertificateDuplicate() → 소스 추적
5. saveMasterList() → DB 메타데이터 저장 (백업)
6. saveMasterListToLdap() → LDAP o=ml 저장 (백업)
```

**저장 위치**:
- **Primary**: `o=csca,c={COUNTRY},dc=data,...` (통계 포함, 검색 대상)
- **Backup**: `o=ml,c={COUNTRY},dc=data,...` (통계 제외, 원본 보존)

---

## 중복 처리 전략

### 중복 판별 기준

**Primary Key**: `(certificate_type, fingerprint_sha256)`
- 인증서의 SHA-256 해시는 고유함
- 동일한 인증서는 동일한 fingerprint를 가짐

### 중복 시나리오

1. **ICAO ML 파일 vs Collection 002 LDIF**
   - `.ml` 파일에서 이미 추출된 CSCA
   - Collection 002의 국가별 Master List에 동일 CSCA 포함
   - 예상 중복률: 80-90%

2. **Collection 002 내부 중복**
   - 여러 국가의 Master List에 동일 CSCA 포함
   - Cross-certification 케이스
   - 예: EU 회원국 간 상호 인증

### 중복 처리 방식

**Option C: Separate Duplicate Tracking Table (선택)**

#### 장점
1. **완전한 이력 추적**: 각 인증서가 어느 파일/Entry에서 발견되었는지 모두 기록
2. **다중 소스 지원**: ML 파일, Collection 002, 001, 003 등 모든 소스 추적
3. **분석 용이**: 중복 패턴 분석, 국가별 통계 가능
4. **데이터 무결성**: 기존 `certificate` 테이블 구조 최소 변경

#### 단점
1. 추가 테이블 관리
2. INSERT 시 추가 쿼리 필요
3. 저장 공간 증가 (미미함)

---

## 데이터베이스 스키마 변경

### 1. `certificate` 테이블 컬럼 추가

```sql
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS duplicate_count INTEGER DEFAULT 0,
ADD COLUMN IF NOT EXISTS first_upload_id UUID REFERENCES uploaded_file(id),
ADD COLUMN IF NOT EXISTS last_seen_upload_id UUID,
ADD COLUMN IF NOT EXISTS last_seen_at TIMESTAMP;

COMMENT ON COLUMN certificate.duplicate_count IS 'Number of times this certificate was found in different uploads';
COMMENT ON COLUMN certificate.first_upload_id IS 'Upload ID where this certificate was first imported';
COMMENT ON COLUMN certificate.last_seen_upload_id IS 'Upload ID where this certificate was last seen';
COMMENT ON COLUMN certificate.last_seen_at IS 'Timestamp when this certificate was last seen in an upload';
```

### 2. `certificate_duplicates` 테이블 생성

```sql
CREATE TABLE IF NOT EXISTS certificate_duplicates (
    id SERIAL PRIMARY KEY,
    certificate_id UUID REFERENCES certificate(id) ON DELETE CASCADE,
    upload_id UUID REFERENCES uploaded_file(id) ON DELETE CASCADE,
    source_type VARCHAR(20) NOT NULL,
    source_country VARCHAR(10),
    source_entry_dn TEXT,
    detected_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT unique_cert_upload UNIQUE(certificate_id, upload_id)
);

CREATE INDEX idx_cert_dup_cert_id ON certificate_duplicates(certificate_id);
CREATE INDEX idx_cert_dup_upload_id ON certificate_duplicates(upload_id);
CREATE INDEX idx_cert_dup_source ON certificate_duplicates(source_type, source_country);

COMMENT ON TABLE certificate_duplicates IS 'Tracks all sources where each certificate was found';
COMMENT ON COLUMN certificate_duplicates.source_type IS 'ML_FILE, LDIF_001, LDIF_002, LDIF_003';
COMMENT ON COLUMN certificate_duplicates.source_country IS 'Country code from Master List Entry (for LDIF files)';
COMMENT ON COLUMN certificate_duplicates.source_entry_dn IS 'Original LDAP DN from LDIF entry';
```

**Source Type 값**:
- `ML_FILE`: ICAO Master List `.ml` 파일
- `LDIF_001`: Collection 001 (DSC/CRL)
- `LDIF_002`: Collection 002 (Master List)
- `LDIF_003`: Collection 003 (DSC_NC)

### 3. 중복 통계 뷰

```sql
CREATE OR REPLACE VIEW certificate_duplicate_stats AS
SELECT
  c.id,
  c.fingerprint_sha256,
  c.certificate_type,
  c.country_code,
  c.subject_dn,
  c.duplicate_count,
  COUNT(cd.id) as source_count,
  STRING_AGG(DISTINCT cd.source_type, ', ') as source_types,
  STRING_AGG(cd.source_type || '(' || COALESCE(cd.source_country, 'N/A') || ')', ', ') as sources
FROM certificate c
LEFT JOIN certificate_duplicates cd ON c.id = cd.certificate_id
GROUP BY c.id, c.fingerprint_sha256, c.certificate_type, c.country_code, c.subject_dn, c.duplicate_count
HAVING COUNT(cd.id) > 1
ORDER BY c.duplicate_count DESC;

COMMENT ON VIEW certificate_duplicate_stats IS 'Certificates found in multiple sources';
```

### 4. `uploaded_file` 테이블 컬럼 추가

```sql
ALTER TABLE uploaded_file
ADD COLUMN IF NOT EXISTS csca_extracted_from_ml INTEGER DEFAULT 0,
ADD COLUMN IF NOT EXISTS csca_duplicates INTEGER DEFAULT 0;

COMMENT ON COLUMN uploaded_file.csca_extracted_from_ml IS 'Number of CSCA certificates extracted from Master List entries (Collection 002)';
COMMENT ON COLUMN uploaded_file.csca_duplicates IS 'Number of duplicate CSCA certificates detected during import';
```

---

## 코드 구현

### 새로운 함수

#### 1. `saveCertificateWithDuplicateCheck()`

**파일**: `services/pkd-management/src/main.cpp`

**시그니처**:
```cpp
std::pair<std::string, bool> saveCertificateWithDuplicateCheck(
    PGconn* conn,
    const std::string& uploadId,
    const std::string& certType,
    const std::string& countryCode,
    const std::string& subjectDn,
    const std::string& issuerDn,
    const std::string& serialNumber,
    const std::string& fingerprint,
    const std::string& notBefore,
    const std::string& notAfter,
    const std::vector<uint8_t>& certData
);
```

**반환값**:
- `std::pair<std::string, bool>`: `{certificateId, isDuplicate}`
- `isDuplicate=true`: 기존 인증서와 fingerprint 일치
- `isDuplicate=false`: 새로운 인증서

**로직**:
1. Fingerprint로 중복 체크
2. 중복이면 기존 ID 반환
3. 신규면 INSERT 후 새 ID 반환

#### 2. `trackCertificateDuplicate()`

**시그니처**:
```cpp
void trackCertificateDuplicate(
    PGconn* conn,
    const std::string& certId,
    const std::string& uploadId,
    const std::string& sourceType,
    const std::string& sourceCountry,
    const std::string& sourceDn
);
```

**기능**: `certificate_duplicates` 테이블에 소스 기록

#### 3. `incrementDuplicateCount()`

**시그니처**:
```cpp
void incrementDuplicateCount(
    PGconn* conn,
    const std::string& certId,
    const std::string& uploadId
);
```

**기능**: `certificate` 테이블의 `duplicate_count`, `last_seen_*` 업데이트

---

### 수정할 함수

#### `parseMasterListEntry()` 대폭 수정

**파일**: `services/pkd-management/src/main.cpp` (Line 2892)

**변경 사항**:

1. **함수 시그니처 변경**:
```cpp
// BEFORE
bool parseMasterListEntry(PGconn* conn, LDAP* ld, const std::string& uploadId,
                          const LdifEntry& entry, int& mlCount, int& ldapMlStoredCount);

// AFTER
bool parseMasterListEntry(PGconn* conn, LDAP* ld, const std::string& uploadId,
                          const LdifEntry& entry, int& mlCount, int& ldapMlStoredCount,
                          int& cscaExtracted, int& cscaDuplicates);
```

2. **개별 CSCA 추출 로직 추가** (Line 2923 이후):
```cpp
// 기존: sk_X509_num(certs)로 개수만 세기
cscaCount = sk_X509_num(certs);

// 추가: 각 인증서 순회 및 저장
for (int i = 0; i < sk_X509_num(certs); i++) {
    X509* cert = sk_X509_value(certs, i);
    // DER 인코딩
    // 메타데이터 추출 (subject, issuer, serial, validity)
    // saveCertificateWithDuplicateCheck() 호출
    // LDAP 저장 (신규인 경우만)
    // trackCertificateDuplicate() 호출
}
```

3. **상세 로그 추가**:
```cpp
spdlog::info("  [{}] CSCA {}/{} - {} - fingerprint: {}, subject: {}",
            sourceCountry, i+1, sk_X509_num(certs),
            isDuplicate ? "DUPLICATE" : "NEW",
            fingerprint.substr(0, 16), subjectDn);
```

---

## 로그 형식

### 상세 처리 로그

```
[INFO] Master List Entry Processing: dn=cn=72ee...,o=ml,c=CA,dc=data,dc=download,...
[INFO]   Country: CA, CMS Size: 245678 bytes
[INFO]   Parsing CMS... Found 12 CSCA certificates
[INFO]   [CA] CSCA 1/12 - NEW - fingerprint: 3a2b1c4d5e6f..., subject: /C=CA/O=Government/CN=CSCA
[INFO]   [CA] CSCA 2/12 - DUPLICATE - fingerprint: 7f8e9d1a2b3c..., subject: /C=CA/O=MOI/CN=CSCA 2010
[INFO]   [CA] CSCA 3/12 - NEW - fingerprint: 1a2b3c4d5e6f..., subject: /C=LV/O=NSA/CN=CSCA Latvia
[INFO]   [CA] CSCA 4/12 - DUPLICATE - fingerprint: 9e8f7d6c5b4a..., subject: /C=US/O=DOS/CN=CSCA
...
[INFO]   Master List [CA] - Extracted: 8 new, 4 duplicates, Total: 12
[INFO]   Saved Master List backup to DB: id=uuid-..., country=CA
[INFO]   Saved Master List backup to LDAP: o=ml,c=CA,dc=data,...
```

### 요약 로그

```
[INFO] Collection 002 Processing Complete
[INFO]   Total Entries: 81
[INFO]   Master Lists: 27
[INFO]   CSCA Extracted: 453
[INFO]   CSCA Duplicates: 219
[INFO]   CSCA Total: 672
```

---

## PKD Relay Service 통계 수정

### LDAP 통계에서 `o=ml` 제외

**파일**: `services/pkd-relay-service/src/main.cpp` (Line 377-464)

**수정 내용**:

```cpp
// BEFORE
if (dnStr.find("o=csca,") != std::string::npos) {
    stats.cscaCount++;
} else if (dnStr.find("o=dsc,") != std::string::npos) {
    stats.dscCount++;
} else if (dnStr.find("o=crl,") != std::string::npos) {
    stats.crlCount++;
}

// AFTER
if (dnStr.find("o=csca,") != std::string::npos) {
    stats.cscaCount++;
} else if (dnStr.find("o=dsc,") != std::string::npos) {
    stats.dscCount++;
} else if (dnStr.find("o=crl,") != std::string::npos) {
    stats.crlCount++;
}
// o=ml은 백업 목적이므로 통계에서 제외
```

**이유**: `o=ml`의 Master List Entry는 백업/감사 목적이며, 실제 인증서는 `o=csca`에 있음

---

## 테스트 계획

### 1. 단위 테스트

```bash
# 1. 스키마 생성 테스트
psql -U pkd -d localpkd < docker/init-scripts/005_certificate_duplicates.sql

# 2. 중복 체크 함수 테스트
# - 신규 인증서 저장 → isDuplicate=false 확인
# - 동일 fingerprint 재저장 → isDuplicate=true 확인
# - certificate_duplicates 테이블 레코드 확인
```

### 2. 통합 테스트

```bash
# Collection 002 파일 재업로드
curl -X POST http://localhost:8080/api/upload/ldif \
  -F "file=@icaopkd-002-complete-000333.ldif" \
  -F "mode=AUTO"

# 결과 확인
# 1. DB 통계
SELECT
  csca_count,
  csca_extracted_from_ml,
  csca_duplicates
FROM uploaded_file
WHERE file_name = 'icaopkd-002-complete-000333.ldif';

# 2. LDAP 통계
curl http://localhost:8080/api/sync/status

# 3. 중복 통계
SELECT * FROM certificate_duplicate_stats LIMIT 10;
```

### 3. 성능 테스트

- Collection 002 (81 entries, ~500 CSCA) 처리 시간 측정
- 예상: 2-3분 (개별 CSCA 추출 및 중복 체크)

---

## 배포 계획

### Phase 1: 스키마 변경 (Low Risk)

```bash
# 1. DB 스키마 업데이트
docker exec -i icao-local-pkd-postgres psql -U pkd -d localpkd \
  < docker/init-scripts/005_certificate_duplicates.sql

# 2. 스키마 검증
docker exec icao-local-pkd-postgres psql -U pkd -d localpkd \
  -c "\d certificate" \
  -c "\d certificate_duplicates"
```

### Phase 2: 코드 배포 (Medium Risk)

```bash
# 1. 백엔드 빌드
docker compose -f docker/docker-compose.yaml build pkd-management

# 2. 서비스 재시작
docker compose -f docker/docker-compose.yaml restart pkd-management

# 3. 로그 확인
docker logs icao-local-pkd-management --tail 50
```

### Phase 3: 데이터 재처리 (High Risk)

```bash
# 1. 백업
./docker-backup.sh

# 2. 기존 데이터 정리 (선택)
# Option A: Collection 002 업로드 기록만 삭제
DELETE FROM uploaded_file WHERE collection_number = '002';

# Option B: 전체 CSCA 삭제 후 재업로드 (권장하지 않음)
# (ML 파일과 Collection 002를 모두 재업로드해야 함)

# 3. Collection 002 재업로드
curl -X POST http://localhost:8080/api/upload/ldif \
  -F "file=@icaopkd-002-complete-000333.ldif" \
  -F "mode=AUTO"

# 4. 검증
curl http://localhost:8080/api/sync/status
```

---

## 예상 결과

### 처리 전 (현재)
- **PostgreSQL CSCA**: 536개 (ML 파일만)
- **LDAP o=csca**: 78개
- **LDAP o=ml**: 27개 (Entry만, 내부 CSCA 미추출)
- **불일치**: 431개

### 처리 후 (예상)
- **PostgreSQL CSCA**: 536 + 450 (중복 제외) = **986개**
- **LDAP o=csca**: 78 + 450 = **528개**
- **LDAP o=ml**: 27개 (백업, 통계 제외)
- **중복 감지**: ~220개 (ML 파일과 Collection 002 간)
- **불일치**: 0개 ✅

---

## 중복 분석 쿼리

### 1. 중복이 많은 인증서 Top 10

```sql
SELECT
  fingerprint_sha256,
  subject_dn,
  duplicate_count,
  source_count,
  sources
FROM certificate_duplicate_stats
ORDER BY duplicate_count DESC
LIMIT 10;
```

### 2. Source Type별 중복 통계

```sql
SELECT
  cd.source_type,
  COUNT(DISTINCT cd.certificate_id) as unique_certs,
  COUNT(*) as total_occurrences
FROM certificate_duplicates cd
GROUP BY cd.source_type
ORDER BY source_type;
```

### 3. 국가별 중복 패턴

```sql
SELECT
  cd.source_country,
  COUNT(DISTINCT cd.certificate_id) as unique_certs,
  COUNT(*) as total_occurrences,
  AVG(c.duplicate_count) as avg_duplicate_count
FROM certificate_duplicates cd
JOIN certificate c ON cd.certificate_id = c.id
WHERE cd.source_type = 'LDIF_002'
GROUP BY cd.source_country
ORDER BY unique_certs DESC;
```

---

## 참고 자료

- **ICAO Doc 9303 Part 12**: PKI for MRTDs (Master List Specification)
- **RFC 5652**: Cryptographic Message Syntax (CMS)
- **OpenSSL CMS API**: `d2i_CMS_bio()`, `CMS_get1_certs()`
- **PostgreSQL ON CONFLICT**: https://www.postgresql.org/docs/15/sql-insert.html

---

## 변경 이력

| 날짜 | 버전 | 변경 내용 | 작성자 |
|------|------|----------|--------|
| 2026-01-23 | 1.0.0 | 초안 작성 | Claude |
