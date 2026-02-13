# Plan: 개별 인증서 파일 업로드 + PGconn 레거시 코드 완전 마이그레이션

## Context

두 가지 작업을 통합 진행:

**작업 A**: 고객 요청 - 개별 인증서 파일(PEM, DER, CER, P7B, CRL) 업로드 기능 추가
**작업 B**: Multi-DBMS 준수 - main.cpp 외 전체 PGconn 직접 SQL 코드를 QueryExecutor/Repository 패턴으로 마이그레이션

**현재 문제점:**
- 업로드: LDIF, ML 2가지 형식만 지원
- PGconn 레거시: main.cpp에 132건, lc_validator.cpp에 10건, crl_validator.cpp에 16건, processing_strategy.cpp에 8건, icao_version_repository.cpp에 41건의 직접 PGconn 사용 → Oracle에서 작동 불가
- SQL Injection 위험: `checkDuplicateFile()`, `saveUploadRecord()`, `updateUploadStatus()`, upload detail 엔드포인트 등에서 SQL 문자열 결합 사용

---

## Part A: 개별 인증서 파일 업로드 기능

### Design Decisions

1. **새 엔드포인트 `POST /api/upload/certificate`** - 기존 LDIF/ML과 분리
2. **AUTO 모드 전용 (동기 처리)** - 파일 크기 소규모 (1KB~1MB), SSE 불필요
3. **DB file_format**: 감지된 포맷 문자열 (`'PEM'`, `'DER'`, `'CER'`, `'P7B'`, `'CRL'`)
4. **CRL 포함** - 기존 CRL 처리 로직 재사용
5. **ProcessingStrategy 확장 안함** - UploadService에 직접 구현

### 재사용 기존 라이브러리

| 라이브러리 | 위치 | 용도 |
|-----------|------|------|
| `FileDetector` | `shared/lib/certificate-parser/` | 포맷 감지 (P7B, CRL 추가 필요) |
| `PemParser` | `shared/lib/certificate-parser/src/pem_parser.cpp` | PEM 다중 인증서 파싱 |
| `DerParser` | `shared/lib/certificate-parser/src/der_parser.cpp` | DER 단일 인증서 파싱 |
| `extractCertificatesFromCms()` | `services/common-lib/src/x509/certificate_parser.cpp` | PKCS#7 인증서 추출 |
| `CertTypeDetector` | `shared/lib/certificate-parser/src/cert_type_detector.cpp` | CSCA/DSC/MLSC 자동 분류 |
| `CertificateRepository` | `services/pkd-management/src/repositories/certificate_repository.h` | DB 저장 + 중복 감지 |

### Implementation

#### A-1: FileDetector 확장

**`shared/lib/certificate-parser/include/file_detector.h`** - FileFormat enum에 `P7B`, `CRL` 추가

**`shared/lib/certificate-parser/src/file_detector.cpp`**
- `detectByExtension()`: `.p7b`, `.p7c` → P7B, `.crl` → CRL
- `detectByContent()`: 기존 DVL/ML OID 검사 후 일반 PKCS7 SignedData → P7B
- `formatToString()`, `stringToFormat()`: 케이스 추가

#### A-2: DB 스키마 업데이트

**`docker/init-scripts/01-core-schema.sql`** (PostgreSQL line 56)
**`docker/db-oracle/init/03-core-schema.sql`** (Oracle line 73)
```sql
CHECK (file_format IN ('LDIF', 'ML', 'PEM', 'DER', 'CER', 'P7B', 'CRL'))
```

#### A-3: Backend - UploadService

**`services/pkd-management/src/services/upload_service.h`** - `CertificateUploadResult` 구조체 + `uploadCertificate()` 선언

**`services/pkd-management/src/services/upload_service.cpp`** - `uploadCertificate()` 구현
1. SHA-256 해시 → 중복 파일 확인
2. `FileDetector::detectFormat()` → 포맷 감지
3. 포맷별 파싱: PEM→PemParser, DER/CER→DerParser, P7B→extractCertificatesFromCms, CRL→d2i_X509_CRL
4. 각 인증서: `CertTypeDetector::detectType()` → DB 저장 → LDAP 저장
5. 통계 업데이트, COMPLETED

**`services/pkd-management/src/main.cpp`** - `POST /api/upload/certificate` 핸들러 등록

**`services/pkd-management/src/middleware/auth_middleware.cpp`** - public endpoints에 추가

#### A-4: Frontend

**`frontend/src/types/index.ts`** - FileFormat 타입 확장
```typescript
export type FileFormat = 'LDIF' | 'ML' | 'MASTER_LIST' | 'PEM' | 'DER' | 'CER' | 'P7B' | 'CRL';
```

**`frontend/src/services/relayApi.ts`** - `uploadCertificate()` API 메서드 추가

**`frontend/src/pages/FileUpload.tsx`**
- `isValidFileType()`: .pem, .crt, .der, .cer, .p7b, .p7c, .crl 추가
- `handleUpload()`: 확장자별 API 라우팅
- 인증서 파일은 동기 처리 → SSE 건너뛰기, 즉시 COMPLETED
- processingMode UI: 인증서 파일일 때 숨김

---

## Part B: PGconn 레거시 코드 완전 마이그레이션

### PGconn 사용 현황 (총 22개 블록)

#### main.cpp 헬퍼 함수 (6개)

| # | Line | 함수 | SQL 안전? | 대체 |
|---|------|------|---------|------|
| 1 | 1222 | `certificateExistsByFingerprint()` | ✓ Param | `CertificateRepository::existsByFingerprint()` |
| 2 | 1240 | `checkDuplicateFile()` | **✗ Concat** | `UploadRepository::findByFileHash()` (이미 존재) |
| 3 | 1409 | `escapeSqlString()` | - | **제거** (호출자 제거 시 불필요) |
| 4 | 1420 | `saveUploadRecord()` | **✗ Concat** | `UploadRepository::insert()` (이미 존재) |
| 5 | 1451 | `updateUploadStatus()` | **✗ Concat** | `UploadRepository::updateStatus()` (이미 존재) |
| 6 | 1745 | `escapeBytea()` | - | **제거** (PostgreSQL 전용) |

#### main.cpp 엔드포인트 핸들러 (8개)

| # | Line | 엔드포인트 | SQL 안전? | 대체 |
|---|------|----------|---------|------|
| 7 | 4483 | `POST /api/upload/{id}/parse` | ✓ Param | `UploadRepository::findById()` |
| 8 | 5756 | `GET /api/upload/detail/{id}` (LDAP 통계) | **✗ Concat** | `CertificateRepository::countLdapStatus()` 신규 |
| 9 | 5840 | `GET /api/upload/{id}/masterlist-structure` | ✓ Param | `UploadRepository::findById()` |
| 10 | 5965 | `GET /api/upload/changes` | **✗ Concat** | `UploadRepository::getChangeHistory()` 신규 |
| 11 | 7267 | `GET /api/link-certs/validate` | ✓ (LcValidator) | LcValidator → QueryExecutor |
| 12 | 7379 | `GET /api/link-certs/search` | ✓ Param | `LinkCertificateRepository::search()` 신규 |
| 13 | 7498 | `GET /api/link-certs/{id}` | ✓ Param | `LinkCertificateRepository::findById()` 신규 |
| 14 | 7662 | `GET /api/certificates/export/{format}` | ✓ Param | `CertificateRepository::findForExport()` 신규 |

#### 외부 파일 (8개)

| # | File | 함수 | 대체 |
|---|------|------|------|
| 15 | `lc_validator.cpp:267` | `storeLinkCertificate()` | QueryExecutor 또는 LinkCertRepository |
| 16 | `lc_validator.cpp:376` | `findCscaBySubjectDn()` | `CertificateRepository::findCscaBySubjectDn()` |
| 17 | `lc_validator.cpp:411` | `findCscaByIssuerDn()` | `CertificateRepository::findCscaByIssuerDn()` |
| 18 | `crl_validator.cpp:43` | `checkRevocation()` | QueryExecutor 또는 CrlRepository |
| 19 | `crl_validator.cpp:231` | `isCrlExpired()` | QueryExecutor |
| 20 | `crl_validator.cpp:249` | `getLatestCrlMetadata()` | QueryExecutor 또는 CrlRepository |
| 21 | `crl_validator.cpp:283` | `logRevocationCheck()` | QueryExecutor |
| 22 | `processing_strategy.cpp:422,545` | ML 업데이트, 업로드 삭제 | QueryExecutor/Repository |

### Migration 전략

**원칙:**
- 기존 Repository 메서드가 있으면 재사용 (`UploadRepository::findByFileHash()` 등)
- 없으면 기존 Repository에 메서드 추가 (새 Repository 생성 최소화)
- `LcValidator`, `CrlValidator` → PGconn* 대신 `IQueryExecutor*` 주입받도록 리팩토링
- `IcaoVersionRepository` → 이미 Repository 클래스이므로 QueryExecutor 사용으로 내부만 변경

### B-1: main.cpp 헬퍼 함수 제거 (Phase 1 - 최우선)

**SQL Injection 위험 제거 + 레거시 헬퍼 제거**

1. `checkDuplicateFile()` (line 1240) → 호출부를 `::uploadRepository->findByFileHash()` 직접 호출로 변경
2. `saveUploadRecord()` (line 1420) → 호출부를 `::uploadRepository->insert()` 직접 호출로 변경
3. `updateUploadStatus()` (line 1451) → 호출부를 `::uploadRepository->updateStatus()` 직접 호출로 변경
4. `certificateExistsByFingerprint()` (line 1222) → `::certificateRepository` 메서드로 대체
5. `escapeSqlString()` (line 1409) → 호출자 제거 후 삭제
6. `escapeBytea()` (line 1745) → 호출자 제거 후 삭제

### B-2: main.cpp 엔드포인트 마이그레이션 (Phase 2)

각 엔드포인트에서 `PQconnectdb()` → `::queryExecutor` 또는 기존 Repository 사용:

1. **`POST /api/upload/{id}/parse`** (line 4483) → `::uploadRepository->findById()`
2. **`GET /api/upload/detail/{id}`** (line 5756) → `::queryExecutor->executeQuery()` + `CertificateRepository`에 `countLdapStatusByUploadId()` 추가
3. **`GET /api/upload/{id}/masterlist-structure`** (line 5840) → `::uploadRepository->findById()`
4. **`GET /api/upload/changes`** (line 5965) → `UploadRepository::getChangeHistory(limit)` 신규 메서드 추가
5. **`GET /api/link-certs/search`** (line 7379) → `::queryExecutor->executeQuery()` 직접 사용 (동적 WHERE)
6. **`GET /api/link-certs/{id}`** (line 7498) → `::queryExecutor->executeQuery()`
7. **`GET /api/certificates/export/{format}`** (line 7662) → `::queryExecutor->executeQuery()` + 기존 CertificateRepository 확장

### B-3: Validator 클래스 리팩토링 (Phase 3)

**`services/pkd-management/src/common/lc_validator.h/cpp`**
- 생성자: `LcValidator(PGconn* conn)` → `LcValidator(IQueryExecutor* executor)`
- 내부 PQexecParams → `executor->executeQuery()` / `executor->executeCommand()`
- Oracle 호환: `NOW()` → `SYSTIMESTAMP` (DB타입별 분기 또는 `executor->getDatabaseType()`)

**`services/pkd-management/src/common/crl_validator.h/cpp`**
- 생성자: `CrlValidator(PGconn* conn)` → `CrlValidator(IQueryExecutor* executor)`
- 4개 메서드 모두 QueryExecutor로 변환

### B-4: processing_strategy.cpp 잔여 PGconn 제거 (Phase 4)

- line 422: `PGconn* conn = nullptr` → 제거, Repository/QueryExecutor 사용
- line 517: `PQexec(conn, mlUpdateQuery)` → `::queryExecutor->executeCommand()`
- line 545-588: 업로드 삭제 로직 → `::uploadRepository->deleteWithCascade(uploadId)` 신규 메서드

### B-5: IcaoVersionRepository 마이그레이션 (Phase 5)

**`services/pkd-management/src/repositories/icao_version_repository.cpp`**
- `PQconnectdb(connInfo_)` → `IQueryExecutor*` 멤버 사용
- 5개 메서드 모두 `executeQuery()` / `executeCommand()` 변환
- Oracle 호환: `ON CONFLICT DO NOTHING` → `MERGE` 또는 PL/SQL, `CURRENT_TIMESTAMP` → `SYSTIMESTAMP`

---

## Files to Modify (전체)

### Part A: 개별 인증서 업로드 (11개)

| # | File | Change |
|---|------|--------|
| 1 | `shared/lib/certificate-parser/include/file_detector.h` | FileFormat enum P7B, CRL 추가 |
| 2 | `shared/lib/certificate-parser/src/file_detector.cpp` | 감지 로직 추가 |
| 3 | `docker/init-scripts/01-core-schema.sql` | file_format 제약 확장 (PostgreSQL) |
| 4 | `docker/db-oracle/init/03-core-schema.sql` | file_format 제약 확장 (Oracle) |
| 5 | `services/pkd-management/src/services/upload_service.h` | CertificateUploadResult + uploadCertificate() |
| 6 | `services/pkd-management/src/services/upload_service.cpp` | uploadCertificate() 구현 |
| 7 | `services/pkd-management/src/main.cpp` | POST /api/upload/certificate 엔드포인트 |
| 8 | `services/pkd-management/src/middleware/auth_middleware.cpp` | public endpoints 추가 |
| 9 | `frontend/src/types/index.ts` | FileFormat 타입 확장 |
| 10 | `frontend/src/services/relayApi.ts` | uploadCertificate() API |
| 11 | `frontend/src/pages/FileUpload.tsx` | 파일 타입 + 라우팅 + UI |

### Part B: PGconn 마이그레이션 (8개)

| # | File | Change |
|---|------|--------|
| 12 | `services/pkd-management/src/main.cpp` | 6개 헬퍼 제거 + 8개 엔드포인트 QueryExecutor 전환 |
| 13 | `services/pkd-management/src/common/lc_validator.h` | PGconn* → IQueryExecutor* |
| 14 | `services/pkd-management/src/common/lc_validator.cpp` | 4개 메서드 QueryExecutor 전환 |
| 15 | `services/pkd-management/src/common/crl_validator.h` | PGconn* → IQueryExecutor* |
| 16 | `services/pkd-management/src/common/crl_validator.cpp` | 4개 메서드 QueryExecutor 전환 |
| 17 | `services/pkd-management/src/processing_strategy.cpp` | PGconn 잔여 코드 Repository 전환 |
| 18 | `services/pkd-management/src/repositories/icao_version_repository.cpp` | PQconnectdb → QueryExecutor |
| 19 | `services/pkd-management/src/repositories/certificate_repository.h/cpp` | countLdapStatus(), findForExport() 추가 |

---

## Verification

### Part A 검증
1. `docker compose build pkd-management` → 빌드 성공
2. 포맷별 테스트: `.pem`, `.der`, `.cer`, `.p7b`, `.crl` 업로드
3. 중복 감지, LDAP 저장, 히스토리 페이지 확인

### Part B 검증
1. `grep -r "PGconn\|PQexec\|PQconnect" services/pkd-management/src/` → **0건** (common.h 선언 제외)
2. PostgreSQL 모드: 전체 API 엔드포인트 동작 확인
3. Oracle 모드: 전체 API 엔드포인트 동작 확인
4. 기존 기능 회귀 테스트:
   - LDIF 업로드 → 성공
   - Master List 업로드 → 성공
   - 인증서 검색 → 성공
   - Link Certificate 검증 → 성공
   - CRL 검증 → 성공
   - 업로드 히스토리 → 성공
   - 업로드 변경 이력 → 성공
   - 인증서 내보내기 → 성공
