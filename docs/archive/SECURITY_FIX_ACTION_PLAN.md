# Security Fix Action Plan

**관련 보고서**: [SECURITY_AUDIT_REPORT.md](SECURITY_AUDIT_REPORT.md)

---

## 실행 요약

### 1차 감사 (2026-02-13 → 2026-02-15 완료)

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

### 2차 감사 (2026-03-01 완료)

| 구분 | 작업 | 상태 |
|------|------|------|
| Phase 5 | CORS 수정 (CRITICAL, 1건) | **완료** |
| Phase 6 | 예외 메시지 유출 + stoi 크래시 일괄 수정 (12개 파일) | **완료** |
| Phase 7 | LDAP DN 이스케이프 + X509 버퍼 + TOCTOU + regex 캐시 | **완료** |
| Phase 8 | AI Python 입력 검증 + 예외 유출 | **완료** |
| Phase 9 | ASN.1 바운드 + 환경변수 범위 검증 | **완료** |

**변경 파일**: 20개 (1개 신규), **변경량**: +353/-648 lines

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

---

## Phase 5: CORS 수정 (CRITICAL) — 완료 (2026-03-01)

### Task 5.1: AI 서비스 CORS 와일드카드 제거 [2C-01] — **완료**

**파일**: `services/ai-analysis/app/main.py`
**작업**: `allow_origins=["*"]` + `allow_credentials=True` → 명시적 origin 화이트리스트

```python
_cors_origins = [
    "http://localhost:13080", "http://localhost:3080",
    "https://pkd.smartcoreinc.com", "https://dev.pkd.smartcoreinc.com",
]
if _env_origins := os.getenv("CORS_ALLOWED_ORIGINS"):
    _cors_origins = [o.strip() for o in _env_origins.split(",") if o.strip()]
```

**결과**: RFC 6749 위반 해결, CSRF 공격 벡터 제거

---

## Phase 6: 예외 메시지 유출 + stoi 크래시 일괄 수정 — 완료 (2026-03-01)

### Task 6.0: 공유 유틸리티 헤더 생성 — **완료**

**파일**: `shared/lib/database/handler_utils.h` (신규, ~70줄)
**내용**: `common::handler` 네임스페이스

| 함수 | 용도 |
|------|------|
| `safeStoi(str, default, min, max)` | try-catch + `std::clamp`, stoi 대체 |
| `internalError(logContext, exception)` | spdlog 에러 로그 + "Internal server error" 응답 |
| `badRequest(publicMessage)` | 400 응답 헬퍼 |

---

### Task 6.1: PKD Management 핸들러 예외 유출 수정 (8개 파일) [2H-01] — **완료**

| 파일 | catch 수정 | stoi 수정 |
|------|----------:|----------:|
| `handlers/certificate_handler.cpp` | 17 | 2 |
| `handlers/upload_handler.cpp` | 12 | - |
| `handlers/upload_stats_handler.cpp` | 9 | 6 |
| `handlers/auth_handler.cpp` | 8 | 4 |
| `handlers/api_client_handler.cpp` | 7 | 3 |
| `handlers/code_master_handler.cpp` | 6 | - |
| `handlers/icao_handler.cpp` | 4 | - |
| `handlers/misc_handler.cpp` | 3 | - |

---

### Task 6.2: PA Service 핸들러 예외 유출 수정 [2H-01] — **완료**

**파일**: `services/pa-service/src/handlers/pa_handler.cpp`
- 5 catch 블록 → `internalError()` 적용
- JSON `isMember("sod")` 필드 체크 추가 [2M-07]

---

### Task 6.3: PKD Relay 핸들러 예외 유출 수정 [2H-01] — **완료**

**파일**: `services/pkd-relay-service/src/handlers/sync_handler.cpp` (4 catch)
**파일**: `services/pkd-relay-service/src/handlers/reconciliation_handler.cpp` (4 catch)
- 감사 로그 유지하면서 `internalError()` 적용

---

## Phase 7: 타겟 보안 수정 — 완료 (2026-03-01)

### Task 7.1: LDAP DN 이스케이프 일관성 [2H-03] — **완료**

**파일**: `services/pkd-management/src/services/ldap_storage_service.cpp`
- `buildCertificateDn()`: serialNumber, ou, countryCode → `escapeDnComponent()` 적용
- `buildCertificateDnV2()`: fingerprint, ou, countryCode → `escapeDnComponent()` 적용

---

### Task 7.2: X509_NAME_oneline 동적 할당 [2H-04] — **완료**

**파일**: `services/pa-service/src/repositories/ldap_certificate_repository.cpp`
- 3개소: `char buf[512]` → `X509_NAME_oneline(name, nullptr, 0)` + `OPENSSL_free()`

---

### Task 7.3: Upload Counter TOCTOU 수정 [2H-05] — **완료**

**파일**: `services/pkd-management/src/handlers/upload_handler.cpp`
- `processLdifFileAsync()`: atomic check를 `s_processingMutex` 내부로 이동
- `processMasterListFileAsync()`: 동일 수정

---

### Task 7.4: CIDR maskBits stoi 검증 [2M-02] — **완료**

**파일**: `services/pkd-management/src/middleware/auth_middleware.cpp`
- `std::stoi(maskStr)` → `common::handler::safeStoi(maskStr, 32, 0, 32)`

---

### Task 7.5: Regex 캐시 [2M-01] — **완료**

**파일**: `services/pkd-management/src/middleware/auth_middleware.cpp`
- `static std::unordered_map<std::string, std::regex> s_regexCache` + `std::mutex`
- 요청당 컴파일 → 1회 컴파일 + 캐시 재사용

---

## Phase 8: AI Python 서비스 보안 — 완료 (2026-03-01)

### Task 8.1: 예외 메시지 유출 수정 [2M-03] — **완료**

**파일**: `services/ai-analysis/app/routers/analysis.py`
- `str(e)` → `"Analysis failed. Check server logs for details."` 고정 메시지

---

### Task 8.2: 입력 검증 추가 [2M-04] — **완료**

**파일**: `services/ai-analysis/app/routers/analysis.py`, `reports.py`

| 파라미터 | 검증 패턴 | 적용 엔드포인트 |
|----------|----------|----------------|
| fingerprint | `^[a-fA-F0-9]{64}$` | certificate/{fp}, certificate/{fp}/forensic |
| country | `^[A-Z]{2,3}$` | anomalies, country/{code}, extension-anomalies |
| cert_type | `^(CSCA\|DSC\|DSC_NC\|MLSC\|LC)$` | anomalies, extension-anomalies |
| risk_level | `^(LOW\|MEDIUM\|HIGH\|CRITICAL)$` | anomalies |
| anomaly_label | `^(NORMAL\|SUSPICIOUS\|ANOMALOUS)$` | anomalies |

---

## Phase 9: 나머지 보안 수정 — 완료 (2026-03-01)

### Task 9.1: ASN.1 바운드 체크 [2M-05] — **완료**

**파일**: `services/pkd-management/src/handlers/upload_handler.cpp`
- `ASN1_get_object()` 후 `seqLen > remaining` 검증 3개소 추가
- 위반 시 에러 로그 + `false` 반환 (안전한 early return)

---

### Task 9.2: 환경변수 범위 검증 [2M-06] — **완료**

**파일**: `services/pkd-management/src/infrastructure/app_config.h`
- `envStoi()` static 메서드 추가 (try-catch + clamp + spdlog 경고)
- 모든 `std::stoi()` → `envStoi()` 전환 (DB_PORT, LDAP_PORT, SERVER_PORT, THREAD_NUM 등 10개)

**파일**: `services/pa-service/src/infrastructure/app_config.h`
- 동일 `envStoi()` 패턴 적용 (DB_PORT, LDAP_PORT, SERVER_PORT, THREAD_NUM, MAX_BODY_SIZE_MB)

---

## 2차 감사 변경 파일 목록 (20개)

| 파일 | 변경 내용 |
|------|-----------|
| `shared/lib/database/handler_utils.h` | **신규** — safeStoi, internalError, badRequest |
| `services/ai-analysis/app/main.py` | CORS 와일드카드 → origin 화이트리스트 |
| `services/ai-analysis/app/routers/analysis.py` | 예외 유출 + 입력 검증 5개 패턴 |
| `services/ai-analysis/app/routers/reports.py` | 입력 검증 (country, cert_type) |
| `services/pkd-management/src/handlers/certificate_handler.cpp` | internalError 17개소 + safeStoi 2 |
| `services/pkd-management/src/handlers/upload_handler.cpp` | internalError 12 + TOCTOU + ASN.1 바운드 |
| `services/pkd-management/src/handlers/upload_stats_handler.cpp` | internalError 9 + safeStoi 6 |
| `services/pkd-management/src/handlers/auth_handler.cpp` | internalError 8 + safeStoi 4 |
| `services/pkd-management/src/handlers/api_client_handler.cpp` | internalError 7 + safeStoi 3 |
| `services/pkd-management/src/handlers/code_master_handler.cpp` | internalError 6 |
| `services/pkd-management/src/handlers/icao_handler.cpp` | internalError 4 |
| `services/pkd-management/src/handlers/misc_handler.cpp` | internalError 3 |
| `services/pkd-management/src/middleware/auth_middleware.cpp` | regex 캐시 + CIDR safeStoi |
| `services/pkd-management/src/services/ldap_storage_service.cpp` | DN escapeDnComponent 2개 함수 |
| `services/pkd-management/src/infrastructure/app_config.h` | envStoi 10개 환경변수 |
| `services/pa-service/src/handlers/pa_handler.cpp` | internalError 5 + isMember 체크 |
| `services/pa-service/src/repositories/ldap_certificate_repository.cpp` | X509_NAME 동적 할당 3개소 |
| `services/pa-service/src/infrastructure/app_config.h` | envStoi 5개 환경변수 |
| `services/pkd-relay-service/src/handlers/sync_handler.cpp` | internalError 4 |
| `services/pkd-relay-service/src/handlers/reconciliation_handler.cpp` | internalError 4 |

---

*1차 Plan created: 2026-02-13, Completed: 2026-02-15*
*2차 Plan created & completed: 2026-03-01*
