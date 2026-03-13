# BSI TR-03110 알고리즘 지원 가이드

**작성일**: 2026-03-13
**버전**: v1.0
**참조 표준**: BSI TR-03110 v2.20/2.21, ICAO Doc 9303 Part 11/12, RFC 5639 (Brainpool)

---

## 개요

본 문서는 ICAO Doc 9303과 BSI TR-03110의 암호 알고리즘 관계를 정리하고, ICAO Local PKD 시스템에서의 적용 방침을 기술한다.

ePassport에는 **두 개의 독립된 PKI 체계**가 존재한다:

| PKI | 인증서 형식 | 용도 | 배포 방식 | 본 시스템 관련 |
|-----|-----------|------|----------|--------------|
| **ICAO PKI** | X.509 v3 | Passive Authentication (SOD 서명 검증) | ICAO PKD (Master List, LDAP) | **직접 관리** |
| **EAC-PKI** | CVC (Card Verifiable Certificate) | Extended Access Control (지문/홍채) | 국가 간 양자 교환 | 관련 없음 |

본 시스템은 **ICAO PKI (X.509 CSCA/DSC)** 를 관리하므로, X.509 인증서에 등장하는 알고리즘만 검증 대상이다.

---

## BSI TR-03110 구조

| Part | 내용 | X.509 CSCA/DSC 관련 |
|------|------|-------------------|
| **Part 1** (v2.20) | PA, AA, TA v1, CA v1 | 관련 — Doc 9303과 동일 범위 |
| **Part 2** (v2.21) | PACE, CA v2/v3, TA v2, RI | 관련 없음 — 칩 프로토콜 |
| **Part 3** (v2.21) | ASN.1/APDU 명세, OID 정의, 표준 곡선 테이블 | 참조용 |
| **Part 4** (v2.21) | 적합성 테스트 | 관련 없음 |

---

## 알고리즘 분류 체계

본 시스템은 X.509 CSCA/DSC 인증서에 사용된 알고리즘을 다음 3단계로 분류한다:

| 분류 | 수준 | 의미 | 예시 |
|------|------|------|------|
| **PASS** | Doc 9303 Part 12 충족 | 완전 적합 | SHA-256, P-256, RSA 2048+ |
| **WARNING** | BSI TR-03110 지원 또는 deprecated | 기능적으로 유효하나 Doc 9303 Part 12 외 | Brainpool, SHA-224, SHA-1 |
| **FAIL** | 미인식 알고리즘 | 표준 목록에 없음 | MD5, 커스텀 곡선 |

---

## PKD 관련 알고리즘 상세

### 1. 서명 알고리즘 (X.509 인증서에 등장)

표준 X.509/PKCS OID를 사용하며, BSI OID(`0.4.0.127.0.7.*`)는 CVC 전용이므로 대상 아님.

| 알고리즘 | OID | 분류 | 근거 |
|---------|-----|------|------|
| sha256WithRSAEncryption | 1.2.840.113549.1.1.11 | PASS | Doc 9303 Part 12 Appendix A |
| sha384WithRSAEncryption | 1.2.840.113549.1.1.12 | PASS | Doc 9303 Part 12 Appendix A |
| sha512WithRSAEncryption | 1.2.840.113549.1.1.13 | PASS | Doc 9303 Part 12 Appendix A |
| RSASSA-PSS (rsassaPss) | 1.2.840.113549.1.1.10 | PASS | Doc 9303 Part 12 Appendix A |
| ecdsa-with-SHA256 | 1.2.840.10045.4.3.2 | PASS | Doc 9303 Part 12 Appendix A |
| ecdsa-with-SHA384 | 1.2.840.10045.4.3.3 | PASS | Doc 9303 Part 12 Appendix A |
| ecdsa-with-SHA512 | 1.2.840.10045.4.3.4 | PASS | Doc 9303 Part 12 Appendix A |
| sha224WithRSAEncryption | 1.2.840.113549.1.1.14 | WARNING | BSI TR-03110 지원, Doc 9303 Part 12 외 |
| ecdsa-with-SHA224 | 1.2.840.10045.4.3.1 | WARNING | BSI TR-03110 지원, Doc 9303 Part 12 외 |
| sha1WithRSAEncryption | 1.2.840.113549.1.1.5 | WARNING | ICAO NTWG deprecated, SHA-256+ 전환 권고 |
| ecdsa-with-SHA1 | 1.2.840.10045.4.1 | WARNING | ICAO NTWG deprecated, SHA-256+ 전환 권고 |

### 2. ECDSA 곡선

| 곡선 | OID | 키 크기 | 분류 | 근거 |
|------|-----|--------|------|------|
| P-256 (prime256v1/secp256r1) | 1.2.840.10045.3.1.7 | 256bit | PASS | Doc 9303 Part 12 |
| P-384 (secp384r1) | 1.3.132.0.34 | 384bit | PASS | Doc 9303 Part 12 |
| P-521 (secp521r1) | 1.3.132.0.35 | 521bit | PASS | Doc 9303 Part 12 |
| brainpoolP256r1 | 1.3.36.3.3.2.8.1.1.7 | 256bit | WARNING | BSI TR-03110, RFC 5639 |
| brainpoolP384r1 | 1.3.36.3.3.2.8.1.1.11 | 384bit | WARNING | BSI TR-03110, RFC 5639 |
| brainpoolP512r1 | 1.3.36.3.3.2.8.1.1.13 | 512bit | WARNING | BSI TR-03110, RFC 5639 |

> **참고**: ICAO Doc 9303 Part 12는 CSCA/DSC에 **explicit EC parameters** (named curve OID 대신 p, a, b, G, n, h 전체)를 의무화한다. Brainpool 사용 국가(독일 등)의 인증서는 explicit parameters로 인코딩되며, OpenSSL `EC_GROUP_get_curve_name()`이 named curve로 매핑하지 못할 수 있다.

### 3. RSA 키 크기

| 키 크기 | 분류 | 근거 |
|--------|------|------|
| 2048bit 이상 | PASS | Doc 9303 Part 12 최소 요구사항 |
| 3072bit 이상 | PASS (권고 충족) | Doc 9303 Part 12 권장 |
| 2048bit 미만 | FAIL | Doc 9303 Part 12 최소 미달 |
| 4096bit 초과 | WARNING | 권장 최대 초과 (실무 제한 아님) |

---

## PKD와 무관한 알고리즘 (CVC/칩 프로토콜 전용)

BSI OID(`0.4.0.127.0.7.*`)를 사용하는 다음 항목들은 X.509 CSCA/DSC에 **등장하지 않으므로** 본 시스템에서 처리하지 않는다:

| 프로토콜 | BSI OID | 용도 |
|---------|---------|------|
| id-PACE-* | 0.4.0.127.0.7.2.2.4.* | PACE 세션 키 설정 (BAC 대체) |
| id-CA-* | 0.4.0.127.0.7.2.2.3.* | Chip Authentication 키 합의 |
| id-TA-* | 0.4.0.127.0.7.2.2.2.* | Terminal Authentication CVC 서명 |
| id-RI-* | 0.4.0.127.0.7.2.2.5.* | Restricted Identification |

이들은 EAC-PKI(CVC 기반)에서 사용되며, ICAO PKD와는 별도의 인프라이다.

---

## Brainpool 곡선 상세

### 배경

- **정의**: RFC 5639 (Elliptic Curve Cryptography Brainpool Standard Curves and Curve Generation)
- **개발**: 독일 BSI 주도, ECC Brainpool 워킹그룹
- **특징**: NIST 곡선과 달리 곡선 파라미터 생성 과정이 완전 공개 (verifiably random)

### 실제 사용 국가

독일(DE)을 포함한 유럽 국가들이 CSCA/DSC 인증서에 Brainpool 곡선을 사용한다. 이들은 ICAO PKD에 정상 등록되어 있으며, Passive Authentication에서 유효하게 검증된다.

- **독일 CSCA**: `C=DE, O=bund, OU=bsi, CN=csca-germany` — BSI 운영, Brainpool 곡선 사용
- BSI TR-03116 Part 3 Chapter 4.1에 따라 Brainpool 권장

### NIST vs Brainpool 비교

| 속성 | NIST (P-256/384/521) | Brainpool (P256r1/P384r1/P512r1) |
|------|---------------------|----------------------------------|
| 표준 | FIPS 186-4, SEC 2 | RFC 5639 |
| 파라미터 생성 | 비공개 seed | Verifiably random (공개) |
| Doc 9303 Part 12 | 명시적 포함 | 미포함 (BSI TR-03110 참조) |
| ICAO PKD 등록 | 지원 | 지원 (실제 등록 인증서 존재) |
| 보안 수준 비교 | P-256 ≈ 128bit | brainpoolP256r1 ≈ 128bit |

### 본 시스템 적용 방침

Brainpool 곡선은 `keySizeCompliant = false`(FAIL)가 아닌 **WARNING** 수준으로 분류한다:

1. ICAO PKD에 실제 등록된 유효 인증서들이 사용
2. BSI TR-03110 Part 1에서 Passive Authentication에 사용 가능
3. Doc 9303 Part 11이 BSI TR-03110을 참조
4. 보안 수준이 NIST 곡선과 동등

---

## SHA-1 Deprecated 처리

### ICAO NTWG 권고

- SHA-1은 ICAO NTWG(New Technologies Working Group)에 의해 단계적 폐지 권고
- 2017년 SHA-1 충돌 공격(SHAttered) 이후 마이그레이션 가속
- 기존 SHA-1 인증서는 여전히 ICAO PKD에 존재 (레거시)

### 본 시스템 적용 방침

- **SHA-1 서명 인증서**: WARNING (FAIL이 아님)
  - 이유: 기존 발급 인증서 유효기간 내 폐기 불가, 기능적으로 서명 검증 가능
  - 메시지: "SHA-1 지원 중단 예정 (ICAO NTWG 권고, SHA-256+ 전환 필요)"
- **SHA-224**: WARNING
  - BSI TR-03110에서 지원하나 Doc 9303 Part 12 기본 목록에 미포함
  - 메시지: "SHA-224 BSI TR-03110 지원 (Doc 9303 Part 12 외)"

---

## 구현 파일 매핑

| 파일 | 역할 |
|------|------|
| `shared/lib/icao-validation/src/algorithm_compliance.cpp` | PA 검증 시 서명 알고리즘 + 곡선 검사 (icao::validation 라이브러리) |
| `shared/lib/icao-validation/include/icao/validation/algorithm_compliance.h` | 알고리즘 검증 인터페이스 |
| `services/pkd-management/src/common/progress_manager.cpp` | 업로드 시 ICAO 적합성 검사 (6개 카테고리) |
| `services/pkd-management/src/common/doc9303_checklist.cpp` | Doc 9303 체크리스트 (~28개 항목) |
| `frontend/src/i18n/locales/ko/certificate.json` | 위반 메시지 한국어 번역 |
| `frontend/src/i18n/locales/en/certificate.json` | 위반 메시지 영어 번역 |
| `frontend/src/pages/CertificateQualityReport.tsx` | 품질 보고서 위반 메시지 번역 패턴 |
| `frontend/src/components/IcaoViolationDetailDialog.tsx` | 위반 상세 다이얼로그 번역 패턴 |

---

## 참고 문헌

1. **ICAO Doc 9303 Part 12** — PKI for Machine Readable Travel Documents
2. **ICAO Doc 9303 Part 11** — Security Mechanisms for MRTDs
3. **BSI TR-03110 Part 1** (v2.20) — eMRTDs and Similar Applications
4. **BSI TR-03110 Part 3** (v2.21) — Common Specifications (OID, ASN.1, domain parameters)
5. **RFC 5639** — Elliptic Curve Cryptography (ECC) Brainpool Standard Curves and Curve Generation
6. **BSI TR-03116 Part 3** — ePassport Cryptographic Mechanisms (독일 CSCA 운영 기준)
7. **RFC 3279** — Algorithms and Identifiers for X.509 PKI
8. **RFC 4055** — Additional Algorithms for RSA-PSS and RSA-OAEP
