# Security Fix Action Plan

**관련 보고서**: [SECURITY_AUDIT_REPORT.md](SECURITY_AUDIT_REPORT.md)
**작성일**: 2026-02-13
**완료일**: 2026-02-15
**대상 버전**: v2.9.1 → **적용 브랜치**: `feat/security-fixes`
**상태**: **전체 완료 (Phase 1-4 + 추가 강화)**

---

## 실행 요약

| 구분 | 작업 | 상태 |
|------|------|------|
| Phase 1 | CRITICAL 차단 (3개) | **완료** |
| Phase 2 | Command/SQL Injection 제거 (4개) | **완료** |
| Phase 3 | Crash Protection (5개) | **완료** |
| Phase 4 | 추가 강화 (6개) | **완료** |
| 추가 | Null Pointer 전수검사 (24개소) | **완료** |
| 추가 | Memory Leak 전수검사 | **완료 (누수 없음)** |
| 추가 | Frontend OWASP 보안 감사 | **완료** |

**변경 파일**: 23개, **변경량**: +410/-277 lines

---

## Phase 1: 즉시 조치 (CRITICAL 차단) — 완료

### Task 1.1: 업로드 엔드포인트 인증 복원 [C-01] — **완료**

**파일**: `services/pkd-management/src/middleware/auth_middleware.cpp`
**작업**: TEMPORARY 섹션 (업로드 public 엔드포인트) 삭제
**결과**: `/api/upload/ldif`, `/api/upload/masterlist`, `/api/upload/certificate` → 인증 필요

---

### Task 1.2: PKD Management LDAP Health Check 안전화 [C-03] — **완료**

**파일**: `services/pkd-management/src/main.cpp`
**작업**: `system(ldapsearch)` → LDAP C API (`ldap_initialize` + `ldap_sasl_bind_s`) 직접 호출
**결과**: Command Injection 벡터 제거, 동일한 헬스체크 기능 유지

---

### Task 1.3: PA Service LDAP Health Check 안전화 [C-04] — **완료**

**파일**: `services/pa-service/src/main.cpp`
**작업**: `popen(ldapsearch)` → LDAP C API 직접 호출
**결과**: Command Injection 벡터 제거

---

## Phase 2: 긴급 조치 (Command Injection + SQL Injection 제거) — 완료

### Task 2.1: EmailSender 안전화 [C-02] — **완료**

**파일**: `services/pkd-relay-service/src/relay/icao/infrastructure/notification/email_sender.cpp`
**작업**: `system()` 호출 3개소 제거, spdlog 로그 전용으로 전환
**결과**: Command Injection 벡터 완전 제거. TODO: SMTP via libcurl 구현 시 대체

---

### Task 2.2: ASN.1 Parser popen() 제거 [H-04] — **완료**

**파일**: `services/pkd-management/src/common/asn1_parser.cpp`
**작업**: `popen("openssl asn1parse")` → OpenSSL C API (`ASN1_parse_dump()`) 사용
**결과**: 외부 프로세스 호출 제거, 메모리 내 직접 파싱

---

### Task 2.3: ORDER BY 화이트리스트 검증 [H-03] — **완료**

**파일**: `services/pkd-management/src/repositories/upload_repository.cpp`
**작업**: `sortBy`, `direction` 파라미터 화이트리스트 검증 (허용 컬럼 외 → 기본값 fallback)
**결과**: SQL Injection 벡터 제거

---

### Task 2.4: LIKE 절 Parameterized Query 전환 [H-01, H-02] — **완료**

**파일**: `services/pkd-management/src/repositories/certificate_repository.cpp`
**작업**: `findCscaByIssuerDn()`, `findAllCscasBySubjectDn()` — LIKE wildcard 이스케이프 + parameterized query
**결과**: SQL Injection 벡터 제거, `escapeSqlWildcards()` 유틸리티 추가

---

## Phase 3: 안정성 강화 (Crash Protection) — 완료

### Task 3.1: SOD Parser 경계 검사 추가 [C-05] — **완료**

**파일**: `shared/lib/icao9303/sod_parser.cpp`
**작업**: 모든 ASN.1 수동 파싱에 `end` 포인터 경계 검사 추가 (`unwrapIcaoSod`, `parseDataGroupHashesRaw`)
**결과**: Buffer overread 방지, 악성 SOD 입력에 대한 안전한 early return

---

### Task 3.2: PA Service BIO Null 체크 [H-05] — **완료**

**파일**: `services/pa-service/src/main.cpp`
**작업**: `BIO_new_mem_buf()` 후 null 체크 추가 (base64Decode, extractHashAlgorithmOid 등 5개소)
**결과**: OpenSSL 메모리 할당 실패 시 안전한 에러 처리

---

### Task 3.3: Base64Util BIO Null 체크 [H-06] — **완료**

**파일**: `shared/util/Base64Util.hpp`
**작업**: `encode()` BIO 할당 null 체크 + `decode()` Base64 문자셋 사전 검증(`isValidBase64()`)
**결과**: 잘못된 입력에 대한 안전한 예외 처리

---

### Task 3.4: LDAP Connection RAII 강화 [H-07] — **완료**

**파일**: `services/pkd-management/src/repositories/ldap_certificate_repository.cpp`
**작업**: 에러 경로에서 connection pool 반환 보장 (검증 결과 기존 RAII 패턴 정상)
**결과**: 기존 코드 안전성 확인 완료

---

### Task 3.5: Auth Middleware Regex Pre-compile [H-08] — **완료**

**파일**: `services/pkd-management/src/middleware/auth_middleware.cpp`, `auth_middleware.h`
**작업**: `std::once_flag` + `std::call_once`로 정적 regex 사전 컴파일, 요청마다 재컴파일 제거
**결과**: 성능 최적화 + ReDoS 위험 경감

---

## Phase 4: 추가 강화 — 완료

### Task 4.1: JWT Secret 검증 [M-02] — **완료**

**파일**: `services/pkd-management/src/main.cpp`
**작업**: 서비스 시작 시 JWT_SECRET 최소 길이(32바이트) 검증
**결과**: 약한 시크릿 사용 방지

---

### Task 4.2: nginx 보안 헤더 [M-03] — **완료**

**파일**: `nginx/api-gateway.conf`
**작업**: 4개 보안 헤더 추가
- `X-Content-Type-Options: nosniff`
- `X-Frame-Options: DENY`
- `X-XSS-Protection: 1; mode=block`
- `Referrer-Policy: strict-origin-when-cross-origin`

---

### Task 4.3: LDAP DN 이스케이프 [M-04] — **완료**

**파일**: `services/pkd-management/src/main.cpp`
**작업**: RFC 4514 DN 특수문자 이스케이프 유틸리티 함수 추가
**결과**: LDAP Injection 방지

---

### Task 4.4: 업로드 파일 크기 제한 [M-05] — **완료**

**파일**: `nginx/api-gateway.conf`
**작업**: `client_max_body_size 50m` (기존 설정 확인 — 이미 적용됨)
**결과**: 대용량 업로드 DoS 방지

---

### Task 4.5: 에러 메시지 정보 노출 제거 [M-06] — **완료**

**파일**: `services/pkd-management/src/main.cpp`
**작업**: 프로덕션 모드에서 상세 에러 정보 숨김
**결과**: 내부 구현 정보 노출 방지

---

### Task 4.6: Base64 입력 검증 [L-02] — **완료**

**파일**: `shared/util/Base64Util.hpp`
**작업**: `isValidBase64()` 함수로 유효 Base64 문자셋 사전 검증
**결과**: 잘못된 입력에 대한 명확한 에러 메시지

---

## 추가 강화 작업 (Action Plan 외)

### Null Pointer 전수검사 — **완료 (24개소 수정)**

OpenSSL 할당 함수 (`BIO_new`, `EVP_MD_CTX_new`, `X509_STORE_new`, `sk_X509_new_null`, `ASN1_INTEGER_to_BN`, `BN_bn2hex`) 반환값 null 체크 전수검사.

| 파일 | 수정 개소 |
|------|-----------|
| `services/pa-service/src/main.cpp` | 8개 (getX509SubjectDn, getX509IssuerDn, getX509SerialNumber, EVP_MD_CTX_new, X509_STORE_new, sk_X509_new_null, parseDataGroupHashes bounds) |
| `shared/lib/icao9303/sod_parser.cpp` | 8개 (BIO×5, X509_STORE, sk_X509, notBefore/notAfter BIO) |
| `shared/util/Base64Util.hpp` | 2개 (encode BIO×2) |
| `services/pkd-management/src/main.cpp` | 4개 (verifyCms BIO, computeFileHash EVP, asn1IntegerToHex BN, ML BIO) |
| `shared/lib/icao9303/dg_parser.cpp` | 2개 (base64Encode BIO×2) |
| `services/pkd-management/src/common/masterlist_processor.cpp` | 2개 (notBefore/notAfter BIO) |
| `services/pkd-management/src/common/lc_validator.cpp` | 4개 (extractSubjectDn, extractIssuerDn, asn1TimeToIso8601 BIO, BN_bn2hex) |
| `services/pkd-management/src/services/certificate_service.cpp` | 2개 (derCertToPem, derCrlToPem BIO) |

---

### Memory Leak 전수검사 — **완료 (누수 없음)**

모든 OpenSSL/LDAP 리소스 관리 코드 감사 완료. `SodData` RAII 패턴 (destructor + copy/move) 정상 확인.

---

### Frontend OWASP 보안 감사 — **완료**

| 항목 | 결과 | 조치 |
|------|------|------|
| XSS (dangerouslySetInnerHTML, eval, innerHTML) | 취약점 없음 | - |
| CSRF | 안전 (JWT Bearer token, 쿠키 미사용) | - |
| Prototype Pollution | 취약점 없음 | - |
| 기본 계정 노출 (Login.tsx) | MEDIUM | `import.meta.env.DEV` 가드 적용 |
| console.error API 정보 노출 (6개 서비스) | MEDIUM | 전체 DEV-only 전환 |
| 수동 토큰 관리 (UserManagement, AuditLog) | MEDIUM | `createAuthenticatedClient` 중앙화 리팩토링 |
| localStorage JWT 저장 | LOW (XSS 벡터 없음) | 수용 |

**수정 파일**:
- `frontend/src/pages/Login.tsx` — DEV-only 기본 계정, console.error
- `frontend/src/services/authApi.ts` — DEV-only console.error
- `frontend/src/services/api.ts` — DEV-only console.error (×2)
- `frontend/src/services/auditApi.ts` — DEV-only console.error
- `frontend/src/services/relayApi.ts` — DEV-only console.error, JWT 토큰 인터셉터 추가
- `frontend/src/services/pkdApi.ts` — DEV-only console.error
- `frontend/src/pages/UserManagement.tsx` — `createAuthenticatedClient` 리팩토링 (fetch 5개소 제거)
- `frontend/src/pages/AuditLog.tsx` — `createAuthenticatedClient` 리팩토링 (fetch 2개소 제거)

---

## 전체 변경 파일 목록 (23개)

### Backend (15개)
| 파일 | 변경 내용 |
|------|-----------|
| `services/pkd-management/src/middleware/auth_middleware.cpp` | TEMPORARY 엔드포인트 삭제, regex 사전 컴파일 |
| `services/pkd-management/src/middleware/auth_middleware.h` | static compiledPatterns_, patternsInitFlag_ |
| `services/pkd-management/src/main.cpp` | LDAP C API, JWT 검증, DN 이스케이프, null 체크 |
| `services/pkd-management/src/common/asn1_parser.cpp` | popen() → OpenSSL C API |
| `services/pkd-management/src/common/lc_validator.cpp` | BIO/BN null 체크 |
| `services/pkd-management/src/common/masterlist_processor.cpp` | BIO null 체크 |
| `services/pkd-management/src/repositories/certificate_repository.cpp` | Parameterized LIKE |
| `services/pkd-management/src/repositories/upload_repository.cpp` | ORDER BY 화이트리스트 |
| `services/pkd-management/src/services/certificate_service.cpp` | BIO null 체크 |
| `services/pa-service/src/main.cpp` | LDAP C API, BIO null, bounds check, X509 null |
| `services/pkd-relay-service/src/.../email_sender.cpp` | system() 제거 |
| `shared/lib/icao9303/sod_parser.cpp` | 경계 검사, BIO/X509_STORE null |
| `shared/lib/icao9303/dg_parser.cpp` | BIO null 체크 |
| `shared/util/Base64Util.hpp` | BIO null, isValidBase64() |
| `nginx/api-gateway.conf` | 보안 헤더 4개 |

### Frontend (8개)
| 파일 | 변경 내용 |
|------|-----------|
| `frontend/src/pages/Login.tsx` | DEV-only 기본 계정, console.error |
| `frontend/src/pages/UserManagement.tsx` | createAuthenticatedClient 리팩토링 |
| `frontend/src/pages/AuditLog.tsx` | createAuthenticatedClient 리팩토링 |
| `frontend/src/services/authApi.ts` | DEV-only console.error |
| `frontend/src/services/api.ts` | DEV-only console.error (×2) |
| `frontend/src/services/auditApi.ts` | DEV-only console.error |
| `frontend/src/services/relayApi.ts` | DEV-only console.error, JWT 인터셉터 |
| `frontend/src/services/pkdApi.ts` | DEV-only console.error |

---

*Plan created: 2026-02-13*
*Completed: 2026-02-15*
