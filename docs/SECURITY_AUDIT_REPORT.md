# Security Audit Report - ICAO Local PKD

**Date**: 2026-02-13
**Version**: v2.9.0
**Auditor**: Code Review (Automated + Manual)
**Scope**: 전체 C++ 백엔드 서비스 (pkd-management, pa-service, pkd-relay-service) + shared libraries

---

## 1. 요약 (Executive Summary)

| 심각도 | 건수 | 상태 |
|--------|------|------|
| **CRITICAL** | 5 | 미조치 |
| **HIGH** | 8 | 미조치 |
| **MEDIUM** | 7 | 미조치 |
| **LOW** | 3 | 미조치 |
| **Total** | **23** | |

### 긍정적 사항
- 대부분의 SQL 쿼리가 parameterized queries (QueryExecutor) 사용
- XSS 취약점 없음 (백엔드 JSON API, 프론트엔드 React)
- SodData 구조체 RAII 패턴 적용 (OpenSSL 리소스 자동 해제)
- LDAP connection pool RAII 패턴 적용
- JWT 인증 구현 완료

### 주요 위험 요소
- 인증 우회: 업로드 엔드포인트 임시 공개 상태 (CRITICAL)
- Command Injection: system()/popen() 4개소 (CRITICAL)
- SOD 파서 버퍼 오버리드: ASN.1 수동 파싱 경계 검사 부재 (CRITICAL)
- SQL Injection: LIKE 절 문자열 연결 2개소 (HIGH)

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

*Report generated: 2026-02-13*
