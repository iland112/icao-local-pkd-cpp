# PKD Management Service

**Port**: 8081 (via API Gateway :8080/api)
**Language**: C++20, Drogon Framework
**Role**: 인증서 업로드, 검색, ICAO 동기화, 인증, API 클라이언트 관리

---

## API Endpoints

### Upload Management
- `POST /api/upload/ldif` — LDIF 업로드
- `POST /api/upload/masterlist` — Master List 업로드
- `POST /api/upload/certificate` — 개별 인증서 업로드 (PEM, DER, P7B, DL, CRL)
- `POST /api/upload/certificate/preview` — 미리보기 (파싱만, 저장 안함)
- `GET /api/upload/history` — 업로드 이력 (paginated)
- `GET /api/upload/detail/{id}` — 업로드 상세
- `POST /api/upload/{id}/retry` — 실패 업로드 재시도 (FAILED only, resume mode)
- `DELETE /api/upload/{id}` — 업로드 삭제
- `GET /api/upload/statistics` — 업로드 통계 (validation.icao, certValidCount/certExpiredCount 포함)
- `GET /api/upload/statistics/icao-noncompliant?category=keyUsage|algorithm|keySize|validityPeriod|extensions` — ICAO 미준수 인증서 목록
- `GET /api/upload/countries` — 국가 통계
- `GET /api/upload/countries/detailed` — 국가별 상세
- `GET /api/upload/changes` — 최근 변경
- `GET /api/upload/{id}/validations` — Trust Chain 검증 결과
- `GET /api/upload/{id}/issues` — 중복 인증서
- `GET /api/upload/{id}/ldif-structure` — LDIF/ASN.1 구조
- `GET /api/progress/{id}` — SSE 진행 상황 스트림

### Certificate Search & Validation
- `GET /api/certificates/search` — 인증서 검색 (country/type/status 필터)
- `GET /api/certificates/validation` — 핑거프린트별 검증 결과
- `POST /api/certificates/pa-lookup` — PA 간편 조회 (subject DN 또는 fingerprint)
- `GET /api/certificates/dsc-nc/report` — DSC_NC 부적합 보고서
- `GET /api/certificates/crl/report` — CRL 보고서
- `GET /api/certificates/crl/{id}` — CRL 상세 (폐기 인증서 목록)
- `GET /api/certificates/crl/{id}/download` — CRL 파일 다운로드 (.crl)
- `GET /api/certificates/doc9303-checklist` — Doc 9303 준수 체크리스트
- `GET /api/certificates/pending-dsc` — DSC 등록 대기 목록
- `POST /api/certificates/pending-dsc/{id}/approve` — DSC 승인 (JWT)
- `POST /api/certificates/pending-dsc/{id}/reject` — DSC 거부 (JWT)
- `GET /api/certificates/export/{format}` — 인증서 내보내기
- `GET /api/certificates/export/all` — 전체 LDAP 데이터 DIT ZIP 내보내기

### Authentication & User Management
- `POST /api/auth/login` / `logout` / `refresh` — 인증
- `GET /api/auth/me` — 현재 사용자 정보
- `GET /api/auth/users` — 사용자 관리 (admin)
- `GET /api/auth/audit-log` — 인증 감사 로그

### API Client Management (admin, JWT)
- CRUD: `POST/GET/PUT/DELETE /api/auth/api-clients`
- `POST /api/auth/api-clients/{id}/regenerate` — API Key 재생성
- `GET /api/auth/api-clients/{id}/usage` — 사용량 통계
- `GET /api/auth/internal/check` — nginx auth_request 전용

### Other
- `GET/POST/PUT/DELETE /api/code-master` — 코드 마스터 CRUD
- `POST/GET/DELETE /api/csr` — CSR 관리 (RSA-2048, SHA256withRSA)
- `GET /api/audit/operations` — 운영 감사 로그
- `GET /api/icao/status` — ICAO PKD 버전 상태

### Public vs Protected
Public endpoints: [auth_middleware.cpp](src/middleware/auth_middleware.cpp) lines 10-93
Protected: 파일 업로드, 사용자 관리, 감사 로그, Code Master CUD, CSR 전체

---

## 코드 구조

```
src/
├── auth/           # API Key, JWT, Bcrypt, AES-256-GCM PII 암호화
├── handlers/       # 10개 핸들러 (Upload, Certificate, Auth, CSR 등)
├── repositories/   # 15개 리포지토리 (Certificate, CRL, User, Audit 등)
├── services/       # 9개 서비스 (Certificate, CSR, Validation, Upload 등)
├── middleware/      # Auth, Rate Limiter, Permission Filter
├── common/         # ASN.1 Parser, Doc 9303 Checklist, X.509 Extractor, Progress Manager
├── adapters/       # DB CSCA/CRL Provider (icao::validation 인터페이스 구현)
├── infrastructure/ # ServiceContainer (DI), HTTP Client
└── domain/models/  # Certificate, ValidationResult, User 등
```

## Oracle 호환성 참고

| 이슈 | PostgreSQL | Oracle | 해결 |
|------|-----------|--------|------|
| 컬럼명 | lowercase | UPPERCASE | OracleQueryExecutor 자동 소문자 변환 |
| Boolean | TRUE/FALSE | 1/0 | `common::db::boolLiteral()` |
| 페이지네이션 | LIMIT/OFFSET | OFFSET ROWS FETCH NEXT | `common::db::paginationClause()` |
| 대소문자 검색 | ILIKE | UPPER() LIKE UPPER() | `common::db::ilikeCond()` |
| 빈 문자열 | '' (empty) | NULL | IS NOT NULL 필터 |
| CLOB 혼합 조회 | TEXT (무관) | OCI 1행만 반환 | `TO_CHAR(clob)`, `RAWTOHEX(blob)` |
| BLOB 절단 | bytea (무관) | LOB locator 33-89 bytes | `RAWTOHEX(DBMS_LOB.SUBSTR(...))` |
