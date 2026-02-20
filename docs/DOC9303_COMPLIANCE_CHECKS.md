# Doc 9303 적합성 검증 항목 (PKD 업로드 시 적용)

**원본**: `docs/Checks_Against_Doc9303_Applied_to_PKD_uploads.pdf`
**작성일**: 2026-02-20
**참조 표준**: ICAO Doc 9303, RFC 3280 (X.509), RFC 3852 (CMS)

---

## 개요

ICAO PKD에 업로드되는 **인증서(CSCA/DSC)**, **CRL**, **마스터리스트**가 Doc 9303 및 관련 RFC 표준을 준수하는지 검증하는 항목을 정의한다. 검증 대상은 3가지 범주로 나뉜다:

1. **인증서 속성** — CSCA, DSC, 마스터리스트 서명자 인증서
2. **CRL 속성** — 인증서 폐지 목록
3. **CSCA 마스터리스트 속성** — CMS Signed Data 구조

---

## 1. 인증서 속성 검증 (CSCA / DSC / 마스터리스트 서명자)

### 1.1 기본 필드

| # | 항목 | RFC 3280 | Doc 9303 / Operator Terminal | 비고 |
|---|------|----------|------------------------------|------|
| 1.1 | **버전 (Version)** | V3 필수 | V3 필수 | |
| 1.2 | **시리얼 번호 (Serial Number)** | 양수, 2의 보수 인코딩, 최소 옥텟 수, 최대 20옥텟 | 동일 | |
| 1.3 | **서명 알고리즘 (Signature)** | OID가 signatureAlgorithm 필드(항목 2)와 일치 | 동일 | |
| 1.4 | **발행자 (Issuer)** | UTF8String 권장 (country, serialNumber은 PrintableString) | 국가코드 필수. DSC/ML서명자: subject country = issuer country | 실제로는 PrintableString도 허용. 국가코드 대문자 검증 미적용 |
| 1.5-1.7 | **유효기간 (Validity)** | UTCTime: 2050년 미만, 13바이트, "Z" 종료. GeneralizedTime: 2050년 이상, 15바이트, "Z" 종료 | 동일 | |
| 1.8 | **주체 (Subject)** | 자체서명이면 Issuer=Subject. CA+Issuer≠Subject이면 링크 인증서 추정 | 국가코드 필수. 링크 인증서: DN 마지막 2개 요소 동일 | DN 비교로 교차인증서와 구분 |
| 1.9 | **공개키 정보 (Subject Public Key Info)** | — | 존재 확인 | |
| 1.10 | **고유 식별자 (Unique Identifiers)** | 사용 비권장 | **사용 금지** (반드시 없어야 함) | |

### 1.2 확장 (Extensions)

| # | 확장 항목 | CSCA (자체서명/링크) | DSC | ML 서명자 | 비고 |
|---|-----------|---------------------|-----|-----------|------|
| 1.11.1 | **Authority Key Identifier** | Issuer≠Subject이면 필수, KeyIdentifier 최소 포함, **Non-critical** | 동일 | 동일 | |
| 1.11.2 | **Subject Key Identifier** | 자체서명/링크이면 필수, **Non-critical** | 무관 | 무관 | |
| 1.11.3 | **Key Usage** | **필수, Critical**. `keyCertSign` + `cRLSign`만 허용 | **필수, Critical**. `digitalSignature`만 허용 | `digitalSignature`만, **Critical** | 가장 엄격한 검증 항목 |
| 1.11.4 | **Private Key Usage Period** | **Non-critical**. CSCA에 존재 시 DSC 검증에 사용 | — | — | |
| 1.11.5 | **Certificate Policies** | 있으면 policy ID 필수, **Non-critical** | 동일 | 동일 | 전자여권 간 해석 불일치로 critical 불가 |
| 1.11.6 | **Policy Mappings** | **Non-critical**, 없어야 함 | 있으면 Non-critical | 있으면 Non-critical | |
| 1.11.7 | **Subject Alternative Name** | 없어야 함 | 있으면 **Non-critical** | 있으면 **Non-critical** | Doc9303상 Subject 필수이므로 |
| 1.11.8 | **Issuer Alternative Name** | 없어야 함 | 있으면 **Non-critical** | 있으면 **Non-critical** | |
| 1.11.9 | **Subject Directory Attributes** | 없어야 함 | 있으면 **Non-critical** | 있으면 **Non-critical** | |
| 1.11.10 | **Basic Constraints** | **필수, Critical**. CA=true, **pathLength=0** | CA 미설정 확인 | CA 미설정 확인 | |
| 1.11.11 | **Name Constraints** | 사용 금지 | 무관 | 무관 | pathLength=0이므로 무의미 |
| 1.11.12 | **Policy Constraints** | 사용 금지 | 무관 | 무관 | pathLength=0이므로 무의미 |
| 1.11.13 | **Extended Key Usage** | **사용 금지** | **사용 금지** | **필수, Critical** | ML 서명자만 EKU 필수 |
| 1.11.14 | **CRL Distribution Points** | 있으면 **Non-critical** | 있으면 **Non-critical** | 있으면 **Non-critical** | PKD 배포점 정의 미비로 검증 생략 |
| 1.11.15 | **Inhibit Any-Policy** | 없어야 함 | 무관 | 무관 | pathLength=0이므로 무의미 |
| 1.11.16 | **Freshest CRL** | 없어야 함 | 무관 | 무관 | |
| 1.11.17 | **Netscape Extensions** | **사용 금지** | **사용 금지** | **사용 금지** | EKU와 동일 효과이므로 완전 금지 |
| — | **Internet Certificate Extensions** | 사용 금지 | 무관 | 무관 | |
| — | **기타 사설 확장 (Other Private)** | **Non-critical** | 무관 | 무관 | |

### 1.3 서명 알고리즘 (항목 2)

- `signatureAlgorithm` 필드는 TBSCertificate의 `signature` 필드(항목 1.3)와 반드시 일치해야 한다.

---

## 2. CRL (인증서 폐지 목록) 속성 검증

### 2.1 기본 필드

| # | 항목 | RFC 3280 | Operator Terminal | 비고 |
|---|------|----------|-------------------|------|
| 1.1 | **버전 (Version)** | v2 필수 | v2 필수 | |
| 1.2 | **서명 알고리즘 (Signature)** | signatureAlgorithm(항목 2)과 일치 | 필수 | |
| 1.3 | **발행자 (Issuer)** | UTF8String, 국가코드 ISO 3166 대문자 | 국가코드 필수 | UTF8 미강제 (PrintableString 허용) |
| 1.4 | **This Update** | — | 필수 | UTCTime/GeneralizedTime 규칙 동일 |
| 1.5 | **Next Update** | — | 필수 | |
| 1.6 | **폐지 인증서 목록 (Revoked Certificates)** | 폐지 인증서 없으면 목록 자체가 없어야 함 | 있으면 비어있으면 안 됨 | |

### 2.2 CRL 확장 (Extensions)

| # | 확장 항목 | 검증 내용 | 비고 |
|---|-----------|-----------|------|
| 1.7.1 | **Authority Key Identifier** | **필수**, KeyIdentifier 최소 포함 | |
| 1.7.2 | **Issuer Alternative Name** | 있으면 **Non-critical** | |
| 1.7.3 | **CRL Number** | **필수**, 최대 20옥텟, 2의 보수 인코딩 | |
| 1.7.4 | **Delta CRL Indicator** | **사용 금지** | 전자여권 PKD는 전체 CRL만 사용 |
| 1.7.5 | **Issuing Distribution Point** | **사용 금지** | |
| 1.7.6 | **Freshest CRL** | **사용 금지** | |

### 2.3 CRL 엔트리 확장 (CRL Entry Extensions)

| # | 항목 | 검증 내용 | 비고 |
|---|------|-----------|------|
| 1.8.1 | **Reason Code** | 있으면 **Non-critical** | Doc9303은 금지하지만 실무에서 허용 |
| 1.8.2 | **Hold Instruction Code** | 있으면 **Non-critical** | |
| 1.8.3 | **Invalidity Date** | 있으면 **Non-critical**, "Z" 종료 | |
| 1.8.4 | **Certificate Issuer** | **사용 금지** | |

### 2.4 서명

- `signatureAlgorithm` (항목 2)은 TBS CertList의 `signature` (항목 1.2)와 반드시 일치해야 한다.

---

## 3. CSCA 마스터리스트 (CMS Signed Data) 속성 검증

### 3.1 Signed Data 구조

| # | 항목 | RFC 3852 | Operator Terminal | 비고 |
|---|------|----------|-------------------|------|
| 1.1 | **버전 (Version)** | 콘텐츠에 따라 결정 | **v3 필수** | |
| 1.2 | **Digest Algorithm** | digest algorithm 컬렉션 | 필수 | |
| 1.3.1 | **eContent Type** | — | `id-icao-cscaMasterList` OID | ICAO 전용 OID |
| 1.3.2 | **eContent** | — | ML 서명자에 대응하는 CSCA 포함 | |
| 1.4 | **인증서 (Certificates)** | 루트→서명자 경로에 충분한 인증서 포함 | ML 서명자 인증서 포함 필수. EKU 존재 시 **Critical** + OID `2.23.136.1.1.3` | |
| 1.5 | **CRL** | — | **포함 금지** | |
| 1.6 | **Signer Infos** | — | 필수 | |

### 3.2 Signer Info 구조

| # | 항목 | 검증 내용 |
|---|------|-----------|
| 1.7.1 | **버전 (Version)** | issuer&serialNumber 사용 시 v1, subjectKeyIdentifier 사용 시 v3 |
| 1.7.2 | **SID** | 버전에 따라 issuer&serialNumber 또는 subjectKeyIdentifier 참조 |
| 1.7.3 | **Digest Algorithm** | Signed Data의 digest algorithm 목록에 포함되어야 함 |
| 1.7.4 | **Signed Attributes** | **필수**, signing time 포함 |
| 1.7.5 | **Signature Algorithm** | 필수 |
| 1.7.6 | **Signature** | 필수 |
| 1.7.7 | **Unsigned Attributes** | 무시 |

---

## 핵심 설계 원칙 요약

### pathLength=0 의 의미

CSCA의 Basic Constraints에서 `pathLength=0`으로 설정되므로, 다음 확장들은 실질적 의미가 없다:
- Name Constraints (1.11.11)
- Policy Constraints (1.11.12)
- Inhibit Any-Policy (1.11.15)

이는 CSCA가 직접 DSC만 서명할 수 있고, 중간 CA를 허용하지 않음을 의미한다.

### 링크 인증서 (Link Certificate) 식별

- CA 비트가 설정되고 Issuer≠Subject인 인증서는 링크 인증서 또는 교차 인증서일 수 있다.
- **구분 방법**: DN의 마지막 2개 요소가 동일하면 링크 인증서, 다르면 교차 인증서.
- 링크 인증서는 CSCA 키 갱신(key rollover) 시 사용된다.

### Key Usage 엄격 적용

| 인증서 유형 | 허용되는 Key Usage | Critical |
|-------------|-------------------|----------|
| CSCA (자체서명/링크) | `keyCertSign` + `cRLSign` | 필수 |
| DSC | `digitalSignature` | 필수 |

### Extended Key Usage (EKU) 규칙

| 인증서 유형 | EKU 규칙 |
|-------------|----------|
| CSCA | **사용 금지** |
| DSC | **사용 금지** |
| 마스터리스트 서명자 | **필수, Critical**, OID: `2.23.136.1.1.3` (`id-icao-mrtd-security-masterListSigningKey`) |

### Delta CRL 금지

전자여권 PKD에서는 **전체 CRL만 사용**한다. Delta CRL Indicator, Issuing Distribution Point, Freshest CRL은 모두 금지된다.

### Netscape Extensions 완전 금지

Netscape Extensions는 Extended Key Usage와 동일한 효과를 가지므로, 전자여권 인증서에서 **완전히 금지**된다.

---

## 프로젝트 내 구현 매핑

이 검증 항목들은 프로젝트 내 다음 모듈에서 구현된다:

| 검증 범주 | 구현 위치 |
|-----------|-----------|
| 인증서 버전/필드 검증 | `shared/lib/icao-validation/extension_validator` |
| Key Usage 검증 | `shared/lib/icao-validation/extension_validator` |
| 알고리즘 적합성 | `shared/lib/icao-validation/algorithm_compliance` |
| Trust Chain (링크 인증서 포함) | `shared/lib/icao-validation/trust_chain_builder` |
| CRL 검증 | `shared/lib/icao-validation/crl_checker` |
| 인증서 파싱/메타데이터 추출 | `shared/lib/certificate-parser/` |
| 마스터리스트 파싱 | `shared/lib/icao9303/` |
| ICAO 적합성 수준 판정 | `icao_compliance_level` (COMPLIANT / WARNING / NON_COMPLIANT) |
