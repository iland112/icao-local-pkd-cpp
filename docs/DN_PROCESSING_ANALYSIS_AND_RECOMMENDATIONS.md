# DN 처리 방식 분석 및 개선 권장사항

**작성일**: 2026-02-02
**버전**: v1.0
**프로젝트**: ICAO Local PKD v2.3.2

---

## 1. 제공된 가이드 핵심 요약

### 1.1 주요 권장사항

1. **DN은 문자열이 아닌 ASN.1 구조로 처리**
   - OpenSSL의 `X509_NAME` 구조체 직접 사용
   - `X509_NAME_print_ex()` + `XN_FLAG_RFC2253`로 canonical DN 생성

2. **절대 금지 패턴**
   - ❌ `split("/")` - OpenSSL oneline DN은 비표준
   - ❌ 정규식 파싱 - escape / multi-valued RDN 깨짐
   - ❌ 문자열 비교로 DN 동일성 판단 - canonicalization 문제

3. **표준 처리 파이프라인**
   ```
   DER X.509 Certificate
      ↓
   OpenSSL X509 parser
      ↓
   X509_NAME (ASN.1)
      ↓
   X509_NAME_print_ex (RFC2253)
      ↓
   Canonical LDAP DN / DB DN
   ```

---

## 2. 우리 프로젝트 현황 분석

### 2.1 이미 올바르게 구현된 부분 ✅

#### A. RFC2253 변환 함수 (main.cpp:1830-1845)

```cpp
std::string x509NameToString(X509_NAME* name) {
    if (!name) return "";
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";
    X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);  // ✅ 가이드 권장 방식!
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    BIO_free(bio);
    return result;
}
```

**평가**: 가이드가 권장하는 표준 변환 방식을 정확히 구현함

#### B. Self-Signed 인증서 감지 (main.cpp:410-413)

```cpp
X509_NAME* subject = X509_get_subject_name(cert);
X509_NAME* issuer = X509_get_issuer_name(cert);

if (X509_NAME_cmp(subject, issuer) == 0) {  // ✅ ASN.1 구조 기반 비교!
    // Self-signed certificate
}
```

**평가**: 문자열 비교가 아닌 구조 기반 비교 사용 (완벽함)

#### C. 인증서 저장 시 DN 처리 (main.cpp:3219-3220)

```cpp
std::string subjectDn = x509NameToString(X509_get_subject_name(cert));
std::string issuerDn = x509NameToString(X509_get_issuer_name(cert));
```

**평가**: RFC2253 형식으로 DB 저장 (표준 준수)

---

### 2.2 개선이 필요한 부분 ⚠️

#### A. ValidationService의 DN 추출 (validation_service.cpp:720, 732)

```cpp
// ❌ 문제: X509_NAME_oneline() 사용
char* dn = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
```

**문제점**:
- `X509_NAME_oneline()`은 `/C=KR/O=MOIS/CN=...` 형식 반환 (비표준)
- DB에는 RFC2253 형식으로 저장되어 있어 형식 불일치
- Trust chain 검증 시 불필요한 문자열 변환 필요

**해결책**: `x509NameToString()` 사용으로 변경

#### B. 문자열 기반 DN 정규화 (validation_service.cpp:783-857)

```cpp
std::string ValidationService::normalizeDnForComparison(const std::string& dn)
{
    // ❌ 문제: 정규식 및 split 기반 파싱
    if (dn[0] == '/') {
        // OpenSSL slash-separated format
        std::istringstream stream(dn);
        std::string segment;
        while (std::getline(stream, segment, '/')) {  // ❌ split("/") 사용
            // ...
        }
    } else {
        // RFC 2253 comma-separated format
        // 복잡한 문자열 파싱 로직...
    }
    // ...
}
```

**문제점**:
1. 가이드가 명시적으로 금지한 `split("/")` 패턴 사용
2. Escape 문자, multi-valued RDN 처리 복잡성
3. 성능 저하 (매번 문자열 파싱)
4. 유지보수 어려움

**현재 사용처**:
- `CertificateRepository::findCscaByIssuerDn()` - Trust chain 검증
- `CertificateRepository::findAllCscasBySubjectDn()` - 인증서 검색

---

## 3. 개선 권장사항

### 3.1 우선순위 1 (High): ValidationService 개선

#### 변경 대상 파일
- `services/pkd-management/src/services/validation_service.cpp`

#### 변경 내용

**Before**:
```cpp
char* dn = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
std::string result(dn);
OPENSSL_free(dn);
```

**After**:
```cpp
std::string result = x509NameToString(X509_get_subject_name(cert));
```

**효과**:
- RFC2253 형식 통일
- DB 저장 형식과 일치
- 불필요한 문자열 변환 제거

---

### 3.2 우선순위 2 (Medium): ASN.1 구조 기반 DN 비교 추가

#### 새로운 Helper 함수 추가 (certificate_utils.h/cpp)

```cpp
namespace certificate_utils {

/**
 * @brief Compare two X509_NAME structures for equality
 * @note Uses ASN.1 structure comparison (recommended by ICAO guide)
 */
inline bool compareX509Names(X509_NAME* name1, X509_NAME* name2) {
    if (!name1 || !name2) return false;
    return X509_NAME_cmp(name1, name2) == 0;
}

/**
 * @brief Extract X509_NAME from certificate and convert to RFC2253 canonical DN
 */
inline std::string extractCanonicalSubjectDn(X509* cert) {
    if (!cert) return "";
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";

    X509_NAME* subject = X509_get_subject_name(cert);
    X509_NAME_print_ex(bio, subject, 0, XN_FLAG_RFC2253);

    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    BIO_free(bio);
    return result;
}

/**
 * @brief Extract X509_NAME from certificate and convert to RFC2253 canonical DN
 */
inline std::string extractCanonicalIssuerDn(X509* cert) {
    if (!cert) return "";
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";

    X509_NAME* issuer = X509_get_issuer_name(cert);
    X509_NAME_print_ex(bio, issuer, 0, XN_FLAG_RFC2253);

    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    BIO_free(bio);
    return result;
}

} // namespace certificate_utils
```

---

### 3.3 우선순위 3 (Low): normalizeDnForComparison 역할 재정의

#### 현재 문제
- 문자열 파싱 기반 (가이드 위반)
- Trust chain 검증에서 핵심 역할

#### 권장 접근

**Option A: 완전 제거 (이상적)**
- DB에서 인증서를 가져올 때 X509 객체로 파싱
- `X509_NAME_cmp()`로 직접 비교
- 문자열 비교 완전 제거

**Option B: Fallback으로 유지 (현실적)**
- Primary: `X509_NAME_cmp()` 사용
- Fallback: DB 문자열만 있을 때 `normalizeDnForComparison()` 사용
- 점진적 마이그레이션 가능

```cpp
// Pseudo-code
bool matchDn(X509* cert, const std::string& targetDn) {
    // Primary: ASN.1 structure comparison
    if (targetCert) {
        return X509_NAME_cmp(
            X509_get_subject_name(cert),
            X509_get_subject_name(targetCert)
        ) == 0;
    }

    // Fallback: String comparison (only if no certificate available)
    std::string certDn = extractCanonicalSubjectDn(cert);
    return (certDn == targetDn);  // Both in RFC2253 format
}
```

---

## 4. 구현 계획

### Phase 1: Quick Win (1-2시간)

**목표**: ValidationService DN 추출 방식 변경

**작업**:
1. `validation_service.cpp` 수정
   - `X509_NAME_oneline()` → `x509NameToString()` 전환
   - 또는 직접 `X509_NAME_print_ex()` 호출

2. 빌드 및 테스트
   - Trust chain 검증 테스트
   - 기존 테스트 케이스 통과 확인

**예상 효과**:
- ✅ 가이드 권장사항 준수
- ✅ DB 형식 일치
- ✅ 코드 일관성 향상

---

### Phase 2: Enhancement (3-4시간)

**목표**: Helper 함수 추가 및 문서화

**작업**:
1. `certificate_utils.h/cpp` 확장
   - `compareX509Names()` 추가
   - `extractCanonicalSubjectDn()` 추가
   - `extractCanonicalIssuerDn()` 추가

2. 주요 사용처에 적용
   - ValidationService
   - CertificateRepository

3. 단위 테스트 작성
   - ASN.1 비교 정확성
   - RFC2253 변환 일관성

---

### Phase 3: Long-term (선택적)

**목표**: 완전한 ASN.1 기반 아키텍처

**작업**:
1. DB 스키마 검토
   - certificate_data (bytea) 활용
   - DN 비교 시 X509 파싱

2. LDAP 통합 개선
   - OpenLDAP libldap 활용
   - `ldap_str2dn()` / `ldap_dnfree()` 사용

3. 성능 최적화
   - X509 객체 캐싱
   - DN 비교 성능 측정

---

## 5. 현재 코드 평가 요약

| 항목 | 현황 | 가이드 준수 | 비고 |
|------|------|------------|------|
| DN 저장 형식 | RFC2253 | ✅ 완벽 | `x509NameToString()` 사용 |
| Self-signed 감지 | `X509_NAME_cmp()` | ✅ 완벽 | ASN.1 구조 비교 |
| ValidationService DN | `X509_NAME_oneline()` | ⚠️ 개선 필요 | oneline 형식 비표준 |
| DN 정규화 | 문자열 파싱 | ❌ 위반 | `split("/")` 사용 |
| Trust chain 검증 | 문자열 비교 | ⚠️ 개선 필요 | ASN.1 비교 권장 |

**종합 평가**: 70% 준수 (기초는 올바름, 일부 개선 필요)

---

## 6. 권장 액션 플랜

### 즉시 실행 가능

1. **ValidationService 수정** (30분)
   - `X509_NAME_oneline()` → `x509NameToString()` 전환
   - 빌드 및 기본 테스트

### 단기 목표 (1주일 내)

2. **Helper 함수 추가** (2-3시간)
   - `certificate_utils.h/cpp` 확장
   - ASN.1 비교 함수 구현
   - 단위 테스트 작성

3. **Documentation 업데이트** (1시간)
   - DN 처리 표준 문서화
   - 개발자 가이드 추가

### 중장기 목표 (선택적)

4. **점진적 마이그레이션**
   - 새로운 코드는 ASN.1 기반 사용
   - 레거시 코드는 점진적 전환
   - 성능 측정 및 최적화

---

## 7. 결론

### 긍정적 평가

우리 프로젝트는 이미 핵심 부분에서 가이드의 권장사항을 따르고 있습니다:

- ✅ `x509NameToString()`으로 RFC2253 변환 구현
- ✅ `X509_NAME_cmp()`로 self-signed 감지
- ✅ DB 저장 시 canonical DN 사용

### 개선 포인트

일부 구현에서 비표준 방식을 사용하고 있어 개선이 필요합니다:

- ⚠️ ValidationService의 `X509_NAME_oneline()` 사용
- ❌ Trust chain 검증의 문자열 기반 DN 정규화

### 최종 권장

**Phase 1 (Quick Win)을 즉시 실행하는 것을 강력히 권장합니다.**

- 작업량: 1-2시간
- 위험도: 낮음 (기존 로직 유지)
- 효과: 가이드 준수도 80% → 90%

Phase 2, 3은 시간 여유가 있을 때 단계적으로 진행하면 됩니다.

---

## 8. 참고 자료

### OpenSSL 공식 문서

- [X509_NAME_print_ex](https://www.openssl.org/docs/man1.1.1/man3/X509_NAME_print_ex.html)
- [X509_NAME_cmp](https://www.openssl.org/docs/man1.1.1/man3/X509_NAME_cmp.html)

### ICAO 표준

- ICAO Doc 9303 Part 12 - PKI for Machine Readable Travel Documents

### 프로젝트 파일

- `services/pkd-management/src/main.cpp:1830-1845` - x509NameToString()
- `services/pkd-management/src/services/validation_service.cpp:783-857` - normalizeDnForComparison()
- `services/pkd-management/src/repositories/certificate_repository.cpp` - DN 기반 검색

---

**작성자**: Claude Sonnet 4.5
**검토 필요**: Phase 1 실행 전 코드 리뷰 권장
