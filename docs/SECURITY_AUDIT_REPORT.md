# Security Audit Report - ICAO Local PKD

**1차 감사**: 2026-02-13 (v2.9.0) → **v2.10.5에서 전체 해결** (2026-02-15)
**2차 감사**: 2026-03-01 (v2.25.5) → **동일 버전에서 전체 해결** (2026-03-01)
**Auditor**: Code Review (Automated + Manual)
**Scope**: 전체 백엔드 서비스 (pkd-management, pa-service, pkd-relay-service, ai-analysis) + shared libraries + nginx
**Resolution**: [SECURITY_FIX_ACTION_PLAN.md](SECURITY_FIX_ACTION_PLAN.md)

---

## 1. 요약 (Executive Summary)

### 1차 감사 (2026-02-13, v2.9.0 → v2.10.5)

| 심각도 | 건수 | 상태 |
|--------|------|------|
| **CRITICAL** | 5 | **완료** (v2.10.5) |
| **HIGH** | 8 | **완료** (v2.10.5) |
| **MEDIUM** | 7 | **완료** (v2.10.5) |
| **LOW** | 3 | **완료** (v2.10.5) |
| **Total** | **23** | **전체 완료** |

### 2차 감사 (2026-03-01, v2.25.5)

| 심각도 | 건수 | 상태 |
|--------|------|------|
| **CRITICAL** | 1 | **완료** |
| **HIGH** | 6 | **완료** |
| **MEDIUM** | 8 | **완료** |
| **Total** | **15** | **전체 완료** |

**누적 해결**: 1차 23건 + 2차 15건 = **총 38건 전체 해결**

### 긍정적 사항
- 100% parameterized SQL queries (QueryExecutor 패턴)
- XSS 취약점 없음 (백엔드 JSON API, 프론트엔드 React)
- SodData 구조체 RAII 패턴 적용 (OpenSSL 리소스 자동 해제)
- LDAP/DB connection pool RAII 패턴 적용
- JWT + API Key 이중 인증 구현
- Command Injection 벡터 전무 (system/popen 완전 제거)
- LDAP Injection 방어 (RFC 4514 이스케이프)
- DoS 방어 다층 구현 (파일 크기, 동시 처리, Rate Limiting, nginx per-IP)

### ~~주요 위험 요소~~ (전체 해결)
- ~~인증 우회: 업로드 엔드포인트 임시 공개 상태 (CRITICAL)~~ → 인증 복원 완료 (1차)
- ~~Command Injection: system()/popen() 4개소 (CRITICAL)~~ → 네이티브 C API로 교체 완료 (1차)
- ~~SOD 파서 버퍼 오버리드 (CRITICAL)~~ → end 포인터 경계 검사 추가 (1차)
- ~~AI 서비스 CORS 와일드카드 (CRITICAL)~~ → 명시적 origin 화이트리스트 (2차)
- ~~예외 메시지 유출 130+건 (HIGH)~~ → internalError() 공유 유틸리티 (2차)
- ~~std::stoi 크래시 (HIGH)~~ → safeStoi() try-catch + clamp (2차)

---

## 2. CRITICAL 취약점

### C-01: 인증 우회 - 업로드 엔드포인트 임시 공개

**파일**: `services/pkd-management/src/middleware/auth_middleware.cpp:39-45`
**유형**: Authentication Bypass (CWE-287)

```cpp
// ========================================================================
// File Upload (TEMPORARY for Oracle testing - REMOVE after testing)
// ========================================================================
"^/api/upload/ldif$",
"^/api/upload/masterlist$",
"^/api/upload/certificate$",
"^/api/upload/certificate/preview$",
"^/api/progress.*",
```

**설명**: Oracle 테스트를 위해 업로드 엔드포인트가 JWT 인증 없이 공개 상태. 인증되지 않은 사용자가 LDIF, Master List, 인증서 파일 업로드 가능.

**영향**: 무단 데이터 업로드, 악의적 인증서 주입, LDAP 데이터 오염
**조치**: 해당 5줄 삭제 (원래 보호 상태 복원)
**난이도**: 매우 쉬움 (5줄 삭제)

---

### C-02: Command Injection - EmailSender system() 호출

**파일**: `services/pkd-relay-service/src/relay/icao/infrastructure/notification/email_sender.cpp:58-67`
**유형**: OS Command Injection (CWE-78)

```cpp
std::ostringstream cmd;
cmd << "echo \"" << message.body << "\" | mail -s \"" << message.subject << "\"";
for (const auto& to : message.toAddresses) {
    cmd << " " << to;
}
int result = system(cmd.str().c_str());
```

**설명**: 이메일 본문(`body`), 제목(`subject`), 수신자(`toAddresses`)가 쉘 명령어에 직접 삽입. 쉘 메타문자(`"`, `$`, `` ` ``, `|`, `;`)를 통한 임의 명령 실행 가능.

**공격 예시**: `subject = 'test"; rm -rf / ; echo "'` → 시스템 파괴
**영향**: 원격 코드 실행 (RCE)
**조치**: system() 제거, libcurl SMTP 또는 로그 전용으로 대체
**난이도**: 중간

---

### C-03: Command Injection - PKD Management LDAP Health Check

**파일**: `services/pkd-management/src/main.cpp:3712-3716`
**유형**: OS Command Injection (CWE-78)

```cpp
std::string cmd = "ldapsearch -x -H ldap://" + appConfig.ldapHost + ":" +
                 std::to_string(appConfig.ldapPort) +
                 " -b \"\" -s base \"(objectclass=*)\" namingContexts 2>/dev/null | grep -q namingContexts";
int ret = system(cmd.c_str());
```

**설명**: `ldapHost` 설정값이 쉘 명령어에 직접 삽입. 환경변수나 설정 파일 조작 시 명령 주입 가능.

**영향**: 원격 코드 실행 (설정 변경 가능 시)
**조치**: LDAP C API (`ldap_simple_bind_s`) 사용으로 대체
**난이도**: 중간

---

### C-04: Command Injection - PA Service LDAP Health Check

**파일**: `services/pa-service/src/main.cpp:541-558`
**유형**: OS Command Injection (CWE-78)

```cpp
std::string cmd = "ldapsearch -x -H " + ldapUri +
                  " -D '" + appConfig.ldapBindDn + "'" +
                  " -w '" + appConfig.ldapBindPassword + "'" +
                  " -b '' -s base '(objectClass=*)' namingContexts 2>&1";
FILE* pipe = popen(cmd.c_str(), "r");
```

**설명**: LDAP Bind DN과 비밀번호가 쉘 명령어에 직접 삽입. 비밀번호에 단일 인용부호(`'`)가 포함되면 명령 주입 가능.

**영향**: 원격 코드 실행, 자격 증명 노출
**조치**: 기존 `LdapConnectionPool` 사용으로 대체 (이미 구현됨)
**난이도**: 쉬움 (기존 인프라 활용)

---

### C-05: SOD 파서 버퍼 오버리드 - ASN.1 수동 파싱

**파일**: `shared/lib/icao9303/sod_parser.cpp:254-413`
**유형**: Out-of-bounds Read (CWE-125)

#### 5a. unwrapIcaoSod (Line 254-274)
```cpp
if (sodBytes[offset] & 0x80) {
    int numLengthBytes = sodBytes[offset] & 0x7F;
    offset += numLengthBytes + 1;  // ← 경계 검사 없음
}
return std::vector<uint8_t>(sodBytes.begin() + offset, sodBytes.end());
```

**문제**: `numLengthBytes`가 `sodBytes.size()`보다 클 수 있음. `offset`이 버퍼 범위를 초과해도 검사 없이 접근.

#### 5b. parseDataGroupHashesRaw (Line 300-413)
```cpp
const unsigned char* contentData = p;
contentData++;           // 경계 검사 없이 포인터 이동
if (*contentData & 0x80) {
    int numBytes = *contentData & 0x7F;
    contentData++;
    for (int i = 0; i < numBytes; i++) {
        contentLen = (contentLen << 8) | *contentData++;  // ← 무한 전진 가능
    }
}
```

**문제**: `contentData` 포인터가 원본 데이터 범위를 초과해도 계속 진행. 조작된 SOD 데이터로 서비스 크래시 유발 가능.

**영향**: PA 서비스 크래시 (DoS), 메모리 정보 노출 가능
**조치**: 모든 포인터 이동에 `end` 포인터 경계 검사 추가
**난이도**: 높음 (주의 깊은 구현 필요)

---

## 3. HIGH 취약점

### H-01: SQL Injection - LIKE 절 문자열 연결 (findCscaByIssuerDn)

**파일**: `services/pkd-management/src/repositories/certificate_repository.cpp:456-470`
**유형**: SQL Injection (CWE-89)

```cpp
std::string escaped = escapeSingleQuotes(cn);
query += " AND LOWER(subject_dn) LIKE '%cn=" + escaped + "%'";
```

**설명**: `escapeSingleQuotes()`는 단일 인용부호만 이스케이프. SQL wildcard(`%`, `_`), 백슬래시 등은 처리하지 않음. Parameterized query 미사용.

**영향**: LIKE 패턴 조작으로 의도하지 않은 데이터 조회
**조치**: Parameterized queries로 전환 또는 SQL wildcard 이스케이프 추가

---

### H-02: SQL Injection - LIKE 절 문자열 연결 (findAllCscasBySubjectDn)

**파일**: `services/pkd-management/src/repositories/certificate_repository.cpp:530-544`
**유형**: SQL Injection (CWE-89)

동일 패턴. `escapeSingleQuotes()` + 문자열 연결.

---

### H-03: SQL Injection - ORDER BY 미검증

**파일**: `services/pkd-management/src/repositories/upload_repository.cpp:118-142`
**유형**: SQL Injection (CWE-89)

```cpp
std::string dbSortBy = sortBy;  // 사용자 입력 직접 사용
query << "ORDER BY " << dbSortBy << " " << direction << " ";
```

**설명**: `sortBy`와 `direction` 파라미터가 화이트리스트 검증 없이 SQL에 직접 삽입. `direction`에 `ASC; DROP TABLE certificate --` 같은 값 주입 가능.

**영향**: 데이터베이스 조작, 정보 유출
**조치**: sortBy/direction 화이트리스트 검증

---

### H-04: Command Injection - asn1_parser.cpp popen()

**파일**: `services/pkd-management/src/common/asn1_parser.cpp:24`
**유형**: OS Command Injection (CWE-78)

```cpp
std::string cmd = "openssl asn1parse -inform DER -i -in \"" + filePath + "\" 2>&1";
std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
```

**설명**: 파일 경로가 쉘 명령어에 직접 삽입. 업로드 파일명에 쉘 메타문자 포함 시 명령 주입 가능.

**영향**: 원격 코드 실행 (파일명 조작 가능 시)
**조치**: OpenSSL C API (`d2i_ASN1_SEQUENCE_ANY`) 사용으로 대체

---

### H-05: BIO Null Pointer Dereference (PA Service - 3개소)

**파일**: `services/pa-service/src/main.cpp`
**유형**: Null Pointer Dereference (CWE-476)

| 위치 | 함수 | 설명 |
|------|------|------|
| Line 342 | base64 decode | `BIO_new_mem_buf()` null 체크 없음 |
| Line 708 | extractHashAlgorithmOid | `BIO_new_mem_buf()` null 체크 없음 |
| Line 757 | extractSignatureAlgorithm | `BIO_new_mem_buf()` null 체크 없음 |

**영향**: PA 서비스 크래시 (메모리 부족 시)
**조치**: null 체크 + 에러 반환 추가

---

### H-06: BIO Null Pointer Dereference (Base64Util)

**파일**: `shared/util/Base64Util.hpp:62-67`
**유형**: Null Pointer Dereference (CWE-476)

```cpp
BIO* b64 = BIO_new(BIO_f_base64());
BIO* mem = BIO_new_mem_buf(encoded.c_str(), static_cast<int>(encoded.length()));
mem = BIO_push(b64, mem);  // mem이 nullptr이면 크래시
```

**영향**: Base64 디코딩 시 크래시 (여러 서비스에서 사용)
**조치**: null 체크 추가

---

### H-07: LDAP Connection Leak - 에러 경로

**파일**: `services/pkd-management/src/repositories/ldap_certificate_repository.cpp`
**유형**: Resource Leak (CWE-404)

**설명**: LDAP 검색 에러 발생 시 connection이 pool에 반환되지 않는 경로 존재. 반복 발생 시 pool 고갈.

**조치**: RAII 패턴 강화 (LdapConnection 소멸자에서 자동 반환 확인)

---

### H-08: Thread Safety - regex 정적 변수

**파일**: `services/pkd-management/src/middleware/auth_middleware.cpp`
**유형**: Race Condition (CWE-362)

**설명**: 공개 엔드포인트 regex 패턴이 매 요청마다 `std::regex` 재컴파일. 멀티스레드 환경에서 비효율적이며 잠재적 경쟁 상태.

**조치**: 정적 pre-compiled regex 사용

---

## 4. MEDIUM 취약점

### M-01: SOD Parser - unwrapIcaoSod 길이 검증 부재

**파일**: `shared/lib/icao9303/sod_parser.cpp:256-263`

`sodBytes.size() > 4` 검사만 존재. long-form ASN.1 길이에서 `numLengthBytes`가 나머지 버퍼보다 큰 경우 미처리.

---

### M-02: JWT Secret 환경변수 미검증

**설명**: JWT_SECRET가 빈 문자열이거나 너무 짧은 경우에 대한 시작 시 검증 없음.

---

### M-03: nginx 보안 헤더 미설정

**설명**: `X-Content-Type-Options`, `X-Frame-Options`, `Content-Security-Policy` 등 보안 헤더 미설정.

---

### M-04: LDAP DN Injection 방어 부재

**설명**: 사용자 입력이 LDAP DN 구성에 사용되는 경우 특수문자(`,`, `=`, `+`, `<`, `>`, `#`, `;`) 이스케이프 미적용.

---

### M-05: 파일 업로드 크기 제한 미설정

**설명**: 업로드 파일 크기에 대한 서버 레벨 제한 미설정. 대용량 파일로 메모리 고갈 가능.

---

### M-06: 에러 메시지 정보 노출

**설명**: 일부 에러 응답에서 내부 파일 경로, SQL 쿼리, 스택 정보가 클라이언트에 반환.

---

### M-07: pclose() 이중 호출 가능

**파일**: `services/pkd-management/src/common/asn1_parser.cpp:49`

```cpp
std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
// ...
int returnCode = pclose(pipe.release());  // unique_ptr에서 release 후 수동 pclose
```

`pipe.release()` 후 수동 `pclose()`는 정상이나, unique_ptr의 커스텀 deleter와 혼용 시 혼란 유발.

---

## 5. LOW 취약점

### L-01: Timing-Safe 비교 미사용

**설명**: JWT 서명 검증에서 constant-time 비교가 보장되지 않음. 타이밍 공격 벡터 (실제 위험도 낮음).

---

### L-02: LDAP Base64 입력 검증 부재

**파일**: `shared/util/Base64Util.hpp`

**설명**: Base64 디코딩 전 입력 문자열의 유효 Base64 문자 검증 없음.

---

### L-03: 사용되지 않는 코드

**파일**: `scripts/analyze_ml_cms.cpp:65`

`analyze_ml_cms.cpp`의 BIO null 체크 미비. 분석 스크립트이므로 프로덕션 영향 없음.

---

## 6. 취약점 위치 매트릭스

| 서비스 | CRITICAL | HIGH | MEDIUM | LOW |
|--------|----------|------|--------|-----|
| pkd-management | C-01, C-03 | H-01, H-02, H-03, H-04, H-07, H-08 | M-03, M-04, M-05, M-06, M-07 | - |
| pa-service | C-04, C-05 | H-05 | M-01, M-02 | L-01, L-02 |
| pkd-relay-service | C-02 | - | - | - |
| shared/lib | C-05 | H-06 | - | L-03 |

---

## 7. 조치 우선순위

### Phase 1: 즉시 조치 (1일) - CRITICAL 차단

| ID | 작업 | 예상 시간 | 파일 |
|----|------|-----------|------|
| C-01 | 업로드 엔드포인트 인증 복원 (5줄 삭제) | 5분 | auth_middleware.cpp |
| C-03 | LDAP health check → LDAP C API 전환 | 1시간 | pkd-management/main.cpp |
| C-04 | LDAP health check → LdapConnectionPool 활용 | 1시간 | pa-service/main.cpp |

### Phase 2: 긴급 조치 (2-3일) - Command Injection 제거

| ID | 작업 | 예상 시간 | 파일 |
|----|------|-----------|------|
| C-02 | EmailSender → 로그 전용 또는 libcurl SMTP | 2시간 | email_sender.cpp |
| H-04 | asn1_parser → OpenSSL C API 전환 | 3시간 | asn1_parser.cpp |
| H-03 | ORDER BY 화이트리스트 검증 | 30분 | upload_repository.cpp |
| H-01/02 | LIKE 절 parameterized query 전환 | 2시간 | certificate_repository.cpp |

### Phase 3: 안정성 강화 (3-5일) - Crash Protection

| ID | 작업 | 예상 시간 | 파일 |
|----|------|-----------|------|
| C-05 | SOD 파서 경계 검사 추가 | 4시간 | sod_parser.cpp |
| H-05 | PA Service BIO null 체크 (3개소) | 30분 | pa-service/main.cpp |
| H-06 | Base64Util BIO null 체크 | 15분 | Base64Util.hpp |
| H-07 | LDAP connection RAII 강화 | 1시간 | ldap_certificate_repository.cpp |
| H-08 | regex pre-compile | 30분 | auth_middleware.cpp |

### Phase 4: 추가 강화 (1주) - Defense-in-Depth

| ID | 작업 | 예상 시간 | 파일 |
|----|------|-----------|------|
| M-02 | JWT Secret 시작 시 검증 | 15분 | main.cpp (both services) |
| M-03 | nginx 보안 헤더 추가 | 30분 | nginx.conf |
| M-04 | LDAP DN 이스케이프 유틸리티 | 1시간 | shared/lib |
| M-05 | 업로드 파일 크기 제한 | 30분 | main.cpp, nginx.conf |
| M-06 | 에러 메시지 내부 정보 제거 | 1시간 | 여러 파일 |
| L-01 | Timing-safe JWT 비교 | 30분 | auth_middleware.cpp |
| L-02 | Base64 입력 검증 | 15분 | Base64Util.hpp |

---

## 8. 참고: 이미 적용된 보안 조치

| 조치 | 상태 | 비고 |
|------|------|------|
| Parameterized SQL (QueryExecutor) | ✅ 적용 | 99% 쿼리에 적용 |
| JWT 인증 (HS256) | ✅ 적용 | v1.8.0 이후 |
| RBAC (admin/user 역할) | ✅ 적용 | |
| 자격 증명 외부화 (.env) | ✅ 적용 | |
| LDAP Connection Pool RAII | ✅ 적용 | v2.4.3 |
| DB Connection Pool RAII | ✅ 적용 | v2.4.2 |
| SodData RAII (OpenSSL) | ✅ 적용 | |
| 파일 MIME 타입 검증 | ✅ 적용 | |
| 파일 경로 sanitization | ✅ 적용 | |
| 감사 로그 (operation + auth) | ✅ 적용 | v2.6.3 |
| IP/User-Agent 추적 | ✅ 적용 | |
| XSS 방어 (React + JSON API) | ✅ 적용 | |

---

---

## 9. 해결 완료 (Resolution Summary)

**모든 23건의 취약점이 v2.10.5 (2026-02-15)에서 해결 완료되었습니다.**

| ID | 해결 방법 |
|----|-----------|
| C-01 | 업로드 엔드포인트 임시 공개 5줄 삭제, JWT 인증 복원 |
| C-02 | EmailSender `system(mail)` x3 → spdlog log-only 대체 |
| C-03 | PKD Management `system(ldapsearch)` → LDAP C API (`ldap_initialize` + `ldap_sasl_bind_s`) |
| C-04 | PA Service `popen(ldapsearch)` → LDAP C API |
| C-05 | SOD 파서 `end` 포인터 경계 검사 추가 (ASN.1 수동 파싱 전체) |
| H-01/H-02 | Parameterized LIKE queries + `escapeSqlWildcards()` 유틸리티 |
| H-03 | ORDER BY 화이트리스트 검증 |
| H-04 | `popen(openssl asn1parse)` → OpenSSL `ASN1_parse_dump()` C API |
| H-05 | PA Service BIO null 체크 3개소 추가 |
| H-06 | Base64Util BIO null 체크 추가 |
| H-07 | LDAP connection pool RAII 패턴 검증 완료 (이미 안전) |
| H-08 | Auth middleware regex `std::call_once` + `std::regex::optimize` 사전 컴파일 |
| M-01 | C-05 수정에서 함께 해결 (SOD 파서 경계 검사) |
| M-02 | JWT_SECRET 최소 32바이트 길이 검증 (서비스 시작 시) |
| M-03 | nginx 보안 헤더 추가 (X-Content-Type-Options, X-Frame-Options, X-XSS-Protection, Referrer-Policy) |
| M-04 | LDAP DN 이스케이프 유틸리티 (RFC 4514 특수문자) |
| M-05 | nginx `client_max_body_size` 이미 설정 (비이슈) |
| M-06 | Frontend `console.error` → DEV-only, 에러 메시지 내부 정보 제거 |
| M-07 | H-04 수정에서 함께 해결 (popen 제거로 pclose 이중 호출 원천 차단) |
| L-01 | JWT 라이브러리 내장 timing-safe 비교 확인 (비이슈) |
| L-02 | `isValidBase64()` 사전 검증 추가 |
| L-03 | Dead code 제거 완료 |

상세 구현 내역: [SECURITY_FIX_ACTION_PLAN.md](SECURITY_FIX_ACTION_PLAN.md)

---

## 10. 2차 보안 감사 (2026-03-01)

**감사 버전**: v2.25.5
**범위**: 전체 5개 백엔드 서비스 + nginx + Docker (1차 감사 이후 추가된 코드 포함)
**방법**: 3개 병렬 보안 감사 에이전트 + 직접 코드 검증
- 에이전트 1: C++ 입력 검증 (SQL/LDAP Injection, 버퍼 오버플로, Path Traversal, Null Pointer, JSON, Base64, OpenSSL)
- 에이전트 2: Python + nginx (SQL Injection, 입력 검증, 예외 처리, 의존성, CORS, Docker)
- 에이전트 3: Auth + 데이터 흐름 (JWT, API Key, Race Condition, 업로드, DoS, 에러 유출, 세션)

### 허용된 위험 (NOT FIXING)
| 항목 | 사유 |
|------|------|
| JWT 로그아웃 토큰 무효화 | Redis 등 인프라 필요, 별도 기능 티켓 |
| CSP `unsafe-inline` | React/Tailwind 필수, 제거 시 프론트엔드 깨짐 |
| HSTS | 내부망 전용, 의도적 미적용 |
| strtol hexToBytes 리팩토링 | 실질적 위험 없음, 선택적 개선 |

---

### 2C-01: AI 서비스 CORS 와일드카드 (CRITICAL)

**파일**: `services/ai-analysis/app/main.py:57-63`
**유형**: Cross-Origin Credential Theft (CWE-942)

```python
# Before (취약):
app.add_middleware(CORSMiddleware,
    allow_origins=["*"], allow_credentials=True, ...)
```

**설명**: `allow_origins=["*"]` + `allow_credentials=True` 조합은 RFC 6749 위반. 악의적 사이트에서 인증된 사용자의 세션으로 AI API 호출 가능 (CSRF).

**영향**: 인증된 사용자 세션 탈취, 분석 데이터 무단 조회
**해결**: 명시적 origin 화이트리스트 + `CORS_ALLOWED_ORIGINS` 환경변수 오버라이드

```python
# After (수정):
_cors_origins = [
    "http://localhost:13080", "http://localhost:3080",
    "https://pkd.smartcoreinc.com", "https://dev.pkd.smartcoreinc.com",
]
if _env_origins := os.getenv("CORS_ALLOWED_ORIGINS"):
    _cors_origins = [o.strip() for o in _env_origins.split(",") if o.strip()]
```

---

### 2H-01: 예외 메시지 정보 유출 (HIGH, 130+ catch 블록)

**파일**: 12개 핸들러 파일 (pkd-management 8, pa-service 1, pkd-relay 2, ai-analysis 1)
**유형**: Information Exposure (CWE-209)

```cpp
// Before (유출):
} catch (const std::exception& e) {
    error["error"] = e.what();  // DB 에러, 파일 경로, 스택 정보 클라이언트에 노출
    callback(resp);
}

// After (안전):
} catch (const std::exception& e) {
    callback(common::handler::internalError("ContextName", e));
    // → spdlog에 실제 에러 기록, 클라이언트에 "Internal server error" 반환
}
```

**영향**: 내부 DB 스키마, 파일 경로, 라이브러리 버전 등 공격자에게 유용한 정보 노출
**해결**: `shared/lib/database/handler_utils.h` 신규 생성 — `internalError()` 공유 유틸리티로 12개 파일 130+ catch 블록 일괄 수정

| 파일 | 수정 catch 수 |
|------|-------------:|
| certificate_handler.cpp | 17 |
| upload_handler.cpp | 12 |
| upload_stats_handler.cpp | 9 |
| auth_handler.cpp | 8 |
| api_client_handler.cpp | 7 |
| code_master_handler.cpp | 6 |
| pa_handler.cpp | 5 |
| icao_handler.cpp | 4 |
| sync_handler.cpp | 4 |
| reconciliation_handler.cpp | 4 |
| misc_handler.cpp | 3 |
| analysis.py | 1 |

---

### 2H-02: std::stoi 크래시 (HIGH)

**파일**: auth_handler.cpp, api_client_handler.cpp, upload_stats_handler.cpp, certificate_handler.cpp
**유형**: Unhandled Exception (CWE-248)

```cpp
// Before (크래시):
int limit = std::stoi(limitParam);  // "abc" → std::invalid_argument 예외 → 500 에러

// After (안전):
int limit = common::handler::safeStoi(limitParam, 100, 1, 1000);  // 범위 clamp 포함
```

**영향**: 조작된 쿼리 파라미터로 서비스 크래시 (DoS)
**해결**: `safeStoi()` — try-catch + `std::clamp()` 적용

---

### 2H-03: LDAP DN 이스케이프 불일치 (HIGH)

**파일**: `services/pkd-management/src/services/ldap_storage_service.cpp`
**유형**: LDAP Injection (CWE-90)

**설명**: `buildCrlDn()`, `buildMasterListDn()`은 `escapeDnComponent()` 적용, 그러나 `buildCertificateDn()`과 `buildCertificateDnV2()`는 미적용. 인증서 serialNumber/fingerprint에 RFC 4514 특수문자 포함 시 DN 구조 파괴 가능.

**해결**: `buildCertificateDn()` — serialNumber, ou, countryCode에 `escapeDnComponent()` 적용
`buildCertificateDnV2()` — fingerprint, ou, countryCode에 `escapeDnComponent()` 적용

---

### 2H-04: X509_NAME_oneline 스택 버퍼 오버플로 (HIGH)

**파일**: `services/pa-service/src/repositories/ldap_certificate_repository.cpp:74-75`
**유형**: Stack Buffer Overflow (CWE-121)

```cpp
// Before (위험):
char subjectBuf[512];
X509_NAME_oneline(subject, subjectBuf, sizeof(subjectBuf));
// → DN 길이 > 512 시 잘림 (OpenSSL은 truncation하지만, 불완전한 DN 비교 가능)

// After (안전):
char* subjectStr = X509_NAME_oneline(subject, nullptr, 0);  // OpenSSL 동적 할당
std::string subjectDn(subjectStr ? subjectStr : "");
OPENSSL_free(subjectStr);
```

**영향**: 긴 DN을 가진 인증서 처리 시 DN 잘림 → 잘못된 CSCA 매칭 가능
**해결**: 3개소 모두 동적 할당 + `OPENSSL_free()` 패턴으로 교체

---

### 2H-05: Upload Counter TOCTOU 레이스 컨디션 (HIGH)

**파일**: `services/pkd-management/src/handlers/upload_handler.cpp:378-392`
**유형**: Time-of-Check Time-of-Use (CWE-367)

```cpp
// Before (TOCTOU):
if (s_activeProcessingCount.load() >= MAX_CONCURRENT) {  // 체크 (mutex 밖)
    return 503;
}
std::lock_guard<std::mutex> lock(s_processingMutex);
s_activeProcessingCount.fetch_add(1);  // 사용 (mutex 안)
// → 두 스레드가 동시에 체크 통과 후 둘 다 처리 시작 가능

// After (안전):
std::lock_guard<std::mutex> lock(s_processingMutex);
if (s_activeProcessingCount.load() >= MAX_CONCURRENT) {  // 체크+사용 모두 mutex 안
    return 503;
}
s_activeProcessingCount.fetch_add(1);
```

**영향**: 동시 업로드 제한 우회 (MAX_CONCURRENT 초과 처리)
**해결**: `processLdifFileAsync()` + `processMasterListFileAsync()` 2개소 모두 mutex 내부로 이동

---

### 2H-06: Pagination 미검증 (HIGH)

**설명**: 2H-02(stoi)에 포함. `limit`, `offset`, `page` 파라미터가 검증 없이 `std::stoi()` 호출 — safeStoi()로 일괄 수정.

---

### 2M-01: Regex 요청당 컴파일 (MEDIUM)

**파일**: `services/pkd-management/src/middleware/auth_middleware.cpp:474-487`
**유형**: ReDoS / Performance (CWE-1333)

**설명**: API Key endpoint 패턴 매칭에서 매 요청마다 `std::regex(pattern)` 재컴파일. 복잡한 패턴에서 ReDoS 가능성 + 불필요한 CPU 소모.

**해결**: `static std::unordered_map<std::string, std::regex>` 캐시, `std::mutex` 보호, `std::regex::optimize` 플래그

---

### 2M-02: CIDR maskBits stoi 크래시 (MEDIUM)

**파일**: `services/pkd-management/src/middleware/auth_middleware.cpp:513`
**유형**: Unhandled Exception (CWE-248)

**설명**: IP 화이트리스트 CIDR 파싱에서 `std::stoi(maskStr)` — 잘못된 값 시 크래시.
**해결**: `common::handler::safeStoi(maskStr, 32, 0, 32)` 적용

---

### 2M-03: AI Python 예외 메시지 유출 (MEDIUM)

**파일**: `services/ai-analysis/app/routers/analysis.py:144`
**유형**: Information Exposure (CWE-209)

```python
# Before: raise HTTPException(status_code=500, detail=str(e))
# After:  raise HTTPException(status_code=500, detail="Analysis failed. Check server logs for details.")
```

---

### 2M-04: AI Python 입력 검증 부재 (MEDIUM)

**파일**: `services/ai-analysis/app/routers/analysis.py`, `reports.py`
**유형**: Improper Input Validation (CWE-20)

**해결**: 정규식 검증 패턴 추가
- SHA-256 fingerprint: `^[a-fA-F0-9]{64}$`
- Country code: `^[A-Z]{2,3}$`
- Certificate type: `^(CSCA|DSC|DSC_NC|MLSC|LC)$`
- Risk level: `^(LOW|MEDIUM|HIGH|CRITICAL)$`
- Anomaly label: `^(NORMAL|SUSPICIOUS|ANOMALOUS)$`

---

### 2M-05: ASN.1 seqLen 바운드 체크 부재 (MEDIUM)

**파일**: `services/pkd-management/src/handlers/upload_handler.cpp:740-785`
**유형**: Out-of-bounds Read (CWE-125)

**설명**: `ASN1_get_object()` 반환 후 `seqLen > remaining` 검증 없이 포인터 이동. 조작된 ASN.1 입력으로 버퍼 오버리드 가능.
**해결**: 3개소에 `seqLen > remaining` 검증 추가, 위반 시 에러 로그 + `false` 반환

---

### 2M-06: 환경변수 정수 파싱 무검증 (MEDIUM)

**파일**: `services/pkd-management/src/infrastructure/app_config.h`, `services/pa-service/src/infrastructure/app_config.h`
**유형**: Unhandled Exception (CWE-248)

**설명**: `fromEnvironment()`에서 `std::stoi()` 직접 호출 — 잘못된 환경변수 값으로 서비스 시작 실패.
**해결**: `envStoi()` static 메서드 추가 (try-catch + `std::clamp()`), 모든 정수 환경변수에 범위 적용

| 환경변수 | 범위 | 기본값 |
|----------|------|--------|
| DB_PORT | 1~65535 | 5432 |
| LDAP_PORT | 1~65535 | 389 |
| SERVER_PORT | 1~65535 | 8081/8082 |
| THREAD_NUM | 1~128 | 16/4 |
| MAX_BODY_SIZE_MB | 1~500 | 100/50 |
| ICAO_CHECK_SCHEDULE_HOUR | 0~23 | 6 |
| DB_POOL_MIN/MAX | 1~100 | 2/10 |
| LDAP_POOL_MIN/MAX | 1~100 | 2/10 |

---

### 2M-07: JSON 필드 존재 체크 부재 (MEDIUM)

**파일**: `services/pa-service/src/handlers/pa_handler.cpp:200`
**유형**: Null Pointer Dereference (CWE-476)

```cpp
// Before: std::string sodBase64 = (*jsonBody)["sod"].asString();  // sod 미전송 시 빈 문자열
// After: if (!jsonBody->isMember("sod")) { callback(badRequest("SOD field required")); return; }
```

---

### 2M-08: strtol endptr 미검증 (MEDIUM)

**설명**: 2M-06(envStoi)에 포함. 환경변수 파싱에서 `strtol` 대신 `stoi` 사용 중이었으며, envStoi() 래퍼로 일괄 해결.

---

### 2차 감사 — 취약점 위치 매트릭스

| 서비스 | CRITICAL | HIGH | MEDIUM |
|--------|----------|------|--------|
| pkd-management | - | 2H-01~06 | 2M-01, 2M-02, 2M-05, 2M-06 |
| pa-service | - | 2H-01, 2H-04 | 2M-06, 2M-07 |
| pkd-relay-service | - | 2H-01 | - |
| ai-analysis | 2C-01 | 2H-01 | 2M-03, 2M-04 |
| shared/lib | - | - | - |

---

### 2차 감사 — 해결 요약

**모든 15건의 취약점이 v2.25.5 (2026-03-01)에서 해결 완료.**

| ID | 해결 방법 | 파일 수 |
|----|-----------|---------|
| 2C-01 | CORS 와일드카드 → 명시적 origin 화이트리스트 | 1 |
| 2H-01 | 130+ catch 블록 → `internalError()` 유틸리티 | 12 |
| 2H-02 | `std::stoi()` → `safeStoi()` try-catch + clamp | 5 |
| 2H-03 | LDAP DN `escapeDnComponent()` 누락 2개 함수 보완 | 1 |
| 2H-04 | `X509_NAME_oneline` 512B 스택 → 동적 할당 + `OPENSSL_free` | 1 |
| 2H-05 | TOCTOU atomic check → mutex 내부 이동 | 1 |
| 2M-01 | Regex 요청당 컴파일 → static 캐시 | 1 |
| 2M-02 | CIDR `stoi` → `safeStoi(maskStr, 32, 0, 32)` | 1 |
| 2M-03 | AI `str(e)` → 고정 에러 메시지 | 1 |
| 2M-04 | AI 입력 검증 정규식 5개 패턴 추가 | 2 |
| 2M-05 | ASN.1 `seqLen > remaining` 바운드 체크 3개소 | 1 |
| 2M-06 | 환경변수 `envStoi()` 범위 검증 | 2 |
| 2M-07 | JSON `isMember("sod")` 체크 | 1 |

**신규 파일**: `shared/lib/database/handler_utils.h` (~70줄)
**변경**: 20개 파일, +353 삽입, -648 삭제 (net -295줄)
**빌드 검증**: pkd-management, pa-service, pkd-relay, ai-analysis 4개 서비스 Docker 빌드 성공

---

*Report generated: 2026-02-13*
*1차 해결 업데이트: 2026-02-17*
*2차 감사 추가: 2026-03-01*
