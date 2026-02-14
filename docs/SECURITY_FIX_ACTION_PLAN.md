# Security Fix Action Plan

**관련 보고서**: [SECURITY_AUDIT_REPORT.md](SECURITY_AUDIT_REPORT.md)
**작성일**: 2026-02-13
**대상 버전**: v2.9.1

---

## Phase 1: 즉시 조치 (CRITICAL 차단)

### Task 1.1: 업로드 엔드포인트 인증 복원 [C-01]

**파일**: `services/pkd-management/src/middleware/auth_middleware.cpp`
**작업**: Lines 38-45 삭제 (TEMPORARY 섹션 전체)

```diff
- // ========================================================================
- // File Upload (TEMPORARY for Oracle testing - REMOVE after testing)
- // ========================================================================
- "^/api/upload/ldif$",
- "^/api/upload/masterlist$",
- "^/api/upload/certificate$",
- "^/api/upload/certificate/preview$",
- "^/api/progress.*",
```

**검증**: 인증 없이 `POST /api/upload/ldif` → 401 Unauthorized 반환 확인

---

### Task 1.2: PKD Management LDAP Health Check 안전화 [C-03]

**파일**: `services/pkd-management/src/main.cpp` (Line ~3712)
**작업**: `system(ldapsearch)` → LDAP C API 직접 호출

**변경 방법**:
```cpp
// Before: system("ldapsearch -x -H ldap://... 2>/dev/null | grep -q namingContexts");

// After: Use LDAP C API
LDAP* ld = nullptr;
auto start = std::chrono::steady_clock::now();
int rc = ldap_initialize(&ld, ("ldap://" + appConfig.ldapHost + ":" +
         std::to_string(appConfig.ldapPort)).c_str());
if (rc == LDAP_SUCCESS) {
    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
    struct timeval tv = {3, 0};  // 3 second timeout
    ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &tv);
    rc = ldap_simple_bind_s(ld, nullptr, nullptr);  // Anonymous bind
    ldap_unbind_ext_s(ld, nullptr, nullptr);
}
auto elapsed = std::chrono::steady_clock::now() - start;
bool isUp = (rc == LDAP_SUCCESS);
```

**검증**: `/api/health` 엔드포인트에서 LDAP 상태 정상 표시 확인

---

### Task 1.3: PA Service LDAP Health Check 안전화 [C-04]

**파일**: `services/pa-service/src/main.cpp` (Line ~541)
**작업**: `popen(ldapsearch)` → 기존 `LdapConnectionPool` 활용

**변경 방법**: 기존 PA Service의 LDAP pool에서 connection acquire → rootDSE search → release. system ldapsearch 대신 `ldap_search_ext_s()` 사용.

**검증**: PA Service 헬스체크에서 LDAP 상태 정상 확인

---

## Phase 2: 긴급 조치 (Command Injection + SQL Injection 제거)

### Task 2.1: EmailSender 안전화 [C-02]

**파일**: `services/pkd-relay-service/src/relay/icao/infrastructure/notification/email_sender.cpp`
**작업**: `system()` 호출 제거, 로그 전용으로 전환

**변경 방법**:
```cpp
bool EmailSender::sendViaSystemMail(const EmailMessage& message) {
    // Log email content instead of system() execution
    spdlog::info("[EmailSender] Email notification (log only):");
    spdlog::info("[EmailSender] To: {}", /* join addresses */);
    spdlog::info("[EmailSender] Subject: {}", message.subject);
    spdlog::info("[EmailSender] Body: {}", message.body);
    // TODO: Implement proper SMTP via libcurl when email sending is required
    return true;
}
```

**근거**: 현재 Docker 환경에서 `mail` 명령어 미설치 상태로 실제 이메일 전송되지 않음. 안전한 로그 기록으로 대체.

---

### Task 2.2: ASN.1 Parser popen() 제거 [H-04]

**파일**: `services/pkd-management/src/common/asn1_parser.cpp`
**작업**: `popen(openssl asn1parse)` → OpenSSL C API 사용

**변경 방법**: `d2i_ASN1_SEQUENCE_ANY()` 또는 OpenSSL의 `ASN1_parse_dump()` API를 사용하여 메모리 버퍼에서 직접 ASN.1 구조 파싱.

```cpp
// Instead of popen("openssl asn1parse -inform DER -i -in file"):
BIO* bio = BIO_new_mem_buf(derData.data(), static_cast<int>(derData.size()));
if (!bio) return "";
BIO* out = BIO_new(BIO_s_mem());
ASN1_parse_dump(out, derData.data(), derData.size(), 0, 0);
// Read from out BIO
BUF_MEM* bptr;
BIO_get_mem_ptr(out, &bptr);
std::string result(bptr->data, bptr->length);
BIO_free(out);
BIO_free(bio);
```

---

### Task 2.3: ORDER BY 화이트리스트 검증 [H-03]

**파일**: `services/pkd-management/src/repositories/upload_repository.cpp`
**작업**: `sortBy`와 `direction` 파라미터 화이트리스트 검증

```cpp
// Whitelist for sortBy
static const std::set<std::string> allowedSortColumns = {
    "upload_timestamp", "completed_timestamp", "file_name",
    "status", "file_size", "total_entries"
};
if (allowedSortColumns.find(dbSortBy) == allowedSortColumns.end()) {
    dbSortBy = "upload_timestamp";  // Safe default
}

// Whitelist for direction
if (direction != "ASC" && direction != "DESC" &&
    direction != "asc" && direction != "desc") {
    direction = "DESC";  // Safe default
}
```

---

### Task 2.4: LIKE 절 Parameterized Query 전환 [H-01, H-02]

**파일**: `services/pkd-management/src/repositories/certificate_repository.cpp`
**작업**: `findCscaByIssuerDn()`, `findAllCscasBySubjectDn()` - LIKE 절을 parameterized query로 전환

**변경 방법**: SQL wildcard 문자(`%`, `_`) 이스케이프 함수 추가 + parameterized query 사용

```cpp
// escapeSqlWildcards: LIKE 패턴 주입 방지
std::string escapeSqlWildcards(const std::string& str) {
    std::string escaped;
    for (char c : str) {
        if (c == '%' || c == '_' || c == '\\') escaped += '\\';
        escaped += c;
    }
    return escaped;
}

// Parameterized query 사용
std::string query = "SELECT certificate_data, subject_dn FROM certificate "
                   "WHERE certificate_type = 'CSCA'"
                   " AND LOWER(subject_dn) LIKE $1"
                   " LIMIT 20";
std::string param = "%cn=" + escapeSqlWildcards(cn) + "%";
Json::Value result = queryExecutor_->executeQuery(query, {param});
```

---

## Phase 3: 안정성 강화 (Crash Protection)

### Task 3.1: SOD Parser 경계 검사 추가 [C-05]

**파일**: `shared/lib/icao9303/sod_parser.cpp`
**작업**: 모든 ASN.1 수동 파싱에 `end` 포인터 경계 검사 추가

**핵심 변경**:
```cpp
// unwrapIcaoSod: 경계 검사 추가
if (sodBytes[offset] & 0x80) {
    int numLengthBytes = sodBytes[offset] & 0x7F;
    if (offset + numLengthBytes + 1 > sodBytes.size()) {
        spdlog::error("SOD unwrap: length bytes exceed buffer");
        return sodBytes;  // Return as-is on parse error
    }
    offset += numLengthBytes + 1;
}
if (offset >= sodBytes.size()) {
    return sodBytes;
}

// parseDataGroupHashesRaw: end 포인터 기반 검사
const unsigned char* end = p + ASN1_STRING_length(*contentPtr);
// 모든 contentData++ 앞에:
if (contentData >= end) { CMS_ContentInfo_free(cms); return result; }
```

---

### Task 3.2: PA Service BIO Null 체크 [H-05]

**파일**: `services/pa-service/src/main.cpp`
**작업**: Line 342, 708, 757 - `BIO_new_mem_buf()` 후 null 체크 추가

```cpp
BIO* bio = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
if (!bio) {
    spdlog::error("Failed to create BIO");
    return /* appropriate error */;
}
```

---

### Task 3.3: Base64Util BIO Null 체크 [H-06]

**파일**: `shared/util/Base64Util.hpp`
**작업**: Line 62-67 - BIO null 체크 추가

---

### Task 3.4: LDAP Connection RAII 강화 [H-07]

**파일**: `services/pkd-management/src/repositories/ldap_certificate_repository.cpp`
**작업**: 에러 경로에서 connection pool 반환 보장

---

### Task 3.5: Auth Middleware Regex Pre-compile [H-08]

**파일**: `services/pkd-management/src/middleware/auth_middleware.cpp`
**작업**: 정적 초기화 시 regex 사전 컴파일

```cpp
// Static pre-compiled regex patterns (thread-safe after initialization)
static std::vector<std::regex> compiledPatterns_;
static std::once_flag initFlag_;

static void initPatterns() {
    for (const auto& pattern : publicEndpoints_) {
        compiledPatterns_.emplace_back(pattern, std::regex::optimize);
    }
}
```

---

## Phase 4: 추가 강화

### Task 4.1: JWT Secret 검증 [M-02]
- 서비스 시작 시 JWT_SECRET 최소 길이(32바이트) 검증

### Task 4.2: nginx 보안 헤더 [M-03]
- `X-Content-Type-Options: nosniff`
- `X-Frame-Options: DENY`
- `X-XSS-Protection: 1; mode=block`
- `Referrer-Policy: strict-origin-when-cross-origin`

### Task 4.3: LDAP DN 이스케이프 [M-04]
- RFC 4514 DN 특수문자 이스케이프 유틸리티 함수

### Task 4.4: 업로드 파일 크기 제한 [M-05]
- nginx: `client_max_body_size 50m`
- 서버: Drogon 업로드 크기 제한

### Task 4.5: 에러 메시지 정보 노출 제거 [M-06]
- 프로덕션 모드에서 상세 에러 정보 숨김

### Task 4.6: Base64 입력 검증 [L-02]
- 유효 Base64 문자셋 사전 검증

---

## 일정 요약

| Phase | 기간 | 작업 수 | 심각도 커버 |
|-------|------|---------|-------------|
| Phase 1 | 1일 | 3개 | CRITICAL ×3 |
| Phase 2 | 2-3일 | 4개 | CRITICAL ×1, HIGH ×3 |
| Phase 3 | 2-3일 | 5개 | CRITICAL ×1, HIGH ×4 |
| Phase 4 | 3-5일 | 6개 | MEDIUM ×5, LOW ×1 |

**총 소요**: ~2주 (Phase 1-3 우선, Phase 4 병행)

---

## 빌드 및 검증

각 Phase 완료 후:

```bash
# 영향받는 서비스 빌드
./scripts/build/rebuild-pkd-management.sh
./scripts/build/rebuild-pa-service.sh
./scripts/build/rebuild-pkd-relay.sh

# 기능 검증
curl -s http://localhost:8080/api/health | jq .
curl -s -X POST http://localhost:8080/api/upload/ldif  # 401 확인 (C-01)
curl -s http://localhost:8080/api/certificates/search?country=KR | jq .

# PA 검증
curl -s http://localhost:8080/api/pa/statistics | jq .
```

---

*Plan created: 2026-02-13*
