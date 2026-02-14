# 전자여권 Trust Chain 기술 가이드

**ICAO Doc 9303 기반 전자여권 PKI 신뢰 체인 분석**

| 항목 | 내용 |
|------|------|
| 기준 문서 | ICAO Doc 9303, Eighth Edition (2021) |
| 관련 파트 | Part 10 (LDS), Part 11 (보안 메커니즘), Part 12 (PKI) |
| 보조 표준 | RFC 5280 (X.509 PKI), RFC 5652 (CMS), BSI TR-03105/03110 |
| 작성일 | 2026-02-14 |

---

## 목차

1. [PKI 계층 구조 개요](#1-pki-계층-구조-개요)
2. [인증서 유형별 상세](#2-인증서-유형별-상세)
3. [Trust Chain 아키텍처](#3-trust-chain-아키텍처)
4. [Passive Authentication (PA) 절차](#4-passive-authentication-pa-절차)
5. [Active Authentication vs Passive Authentication](#5-active-authentication-vs-passive-authentication)
6. [Link Certificate와 CSCA 키 전환](#6-link-certificate와-csca-키-전환)
7. [Master List 구조와 처리](#7-master-list-구조와-처리)
8. [CRL (인증서 폐지 목록)](#8-crl-인증서-폐지-목록)
9. [SOD (Security Object Document) 구조](#9-sod-security-object-document-구조)
10. [ICAO PKD (Public Key Directory)](#10-icao-pkd-public-key-directory)
11. [암호화 요구사항](#11-암호화-요구사항)
12. [실제 Use Case 시나리오](#12-실제-use-case-시나리오)
13. [예외 처리 및 Edge Case](#13-예외-처리-및-edge-case)
14. [구현 시 주의사항 (Pitfalls)](#14-구현-시-주의사항-pitfalls)
15. [참조 표준 및 문서](#15-참조-표준-및-문서)

---

## 1. PKI 계층 구조 개요

### 1.1 전자여권 PKI 4계층 구조

ICAO Doc 9303 Part 12에서 정의하는 전자여권 PKI는 **국가별 독립 신뢰 루트** 모델을 사용한다.

```
┌─────────────────────────────────────────────────────────┐
│                    ICAO PKD (글로벌)                      │
│  Master List (MLSC 서명) → CSCA 인증서 배포               │
└───────────────────────┬─────────────────────────────────┘
                        │ 배포
┌───────────────────────▼─────────────────────────────────┐
│              CSCA (Country Signing CA)                   │
│  각 국가의 신뢰 루트 (Self-signed Root CA)                │
│  유효기간: 15~20년 | Key: RSA 3072+ / ECDSA P-384+       │
└───────────────────────┬─────────────────────────────────┘
                        │ 서명 발급
┌───────────────────────▼─────────────────────────────────┐
│              DSC (Document Signer Certificate)           │
│  여권 서명용 인증서 (End-entity)                          │
│  유효기간: 3~5년 | Key: RSA 2048+ / ECDSA P-256+         │
└───────────────────────┬─────────────────────────────────┘
                        │ SOD 서명
┌───────────────────────▼─────────────────────────────────┐
│              SOD (Security Object Document)              │
│  여권 칩 내 저장된 CMS SignedData                         │
│  DataGroup 해시값 + DSC 전자서명                          │
└───────────────────────┬─────────────────────────────────┘
                        │ 해시 검증
┌───────────────────────▼─────────────────────────────────┐
│              Data Groups (DG1~DG16)                      │
│  DG1: MRZ | DG2: 얼굴 이미지 | DG3: 지문 | ...          │
└─────────────────────────────────────────────────────────┘
```

### 1.2 핵심 설계 원칙

| 원칙 | 설명 |
|------|------|
| **국가별 독립 신뢰 루트** | 각 국가가 자체 CSCA 운영, 중앙 CA 없음 |
| **글로벌 상호 운용성** | ICAO PKD를 통한 CSCA 인증서 교환 |
| **오프라인 검증** | Local PKD 캐시로 네트워크 없이 검증 가능 |
| **키 전환 지원** | Link Certificate로 CSCA 키 교체 시 연속성 보장 |
| **계층적 보안** | PA + AA/CA + TA로 다층 방어 구현 |

---

## 2. 인증서 유형별 상세

### 2.1 CSCA (Country Signing Certificate Authority)

**참조**: ICAO Doc 9303 Part 12, Section 7.1

CSCA는 각 국가의 **신뢰 루트(Root CA)**로, 모든 전자여권 보안의 기반이 된다.

**인증서 프로파일 요구사항**:

| 필드 | 요구사항 |
|------|----------|
| Version | v3 (value 2) |
| Serial Number | 발급 CA 내 고유값 |
| Issuer | `C=` (국가 코드) 포함, Self-signed: Issuer == Subject |
| Validity | **15~20년** (DSC 유효기간 + 여권 유효기간 이상) |
| Basic Constraints | **CRITICAL**, `CA=TRUE`, pathLenConstraint 권장 (통상 0) |
| Key Usage | **CRITICAL**, `keyCertSign` (bit 5) + `cRLSign` (bit 6) 필수 |
| Subject Key Identifier | 필수 |
| Authority Key Identifier | 권장 (Self-signed의 경우 자기 참조) |
| CRL Distribution Points | 권장 |

**분류 알고리즘** (cert_type_detector.cpp):
```
IF BasicConstraints.CA == TRUE
   AND KeyUsage.keyCertSign == TRUE
   AND Issuer DN == Subject DN (Self-signed):
   → Type = CSCA
```

### 2.2 DSC (Document Signer Certificate)

**참조**: ICAO Doc 9303 Part 12, Section 7.2

DSC는 여권 칩의 SOD에 서명하는 **End-entity 인증서**이다.

**인증서 프로파일 요구사항**:

| 필드 | 요구사항 |
|------|----------|
| Version | v3 (value 2) |
| Issuer | CSCA의 Subject DN과 일치해야 함 |
| Validity | **3~5년** (CSCA 유효기간 내, 여권 유효기간 이상) |
| Basic Constraints | 없거나 `CA=FALSE` (CA여서는 안 됨) |
| Key Usage | **CRITICAL**, `digitalSignature` (bit 0) 필수, `keyCertSign` 불가 |
| Extended Key Usage | 선택. OID `2.23.136.1.1.1` (documentSigning) 가능 |
| Authority Key Identifier | 필수 (CSCA 참조) |

**Conformant vs Non-Conformant**:
- **DSC (적합)**: LDAP `dc=data` 브랜치 저장, ICAO 9303 완전 준수
- **DSC_NC (비적합)**: LDAP `dc=nc-data` 브랜치 저장. ICAO 요구사항 일부 미충족이나 운용 중. ICAO는 2021년 nc-data 브랜치 폐지(deprecated)

### 2.3 MLSC (Master List Signer Certificate)

**참조**: ICAO Doc 9303 Part 12, Annex G

MLSC는 ICAO Master List에 서명하는 인증서이다.

| 필드 | 요구사항 |
|------|----------|
| Extended Key Usage | OID `2.23.136.1.1.9` (`id-icao-mrtd-security-masterListSigner`) |
| Key Usage | `digitalSignature` |
| Subject | 통상 `/C=UN/O=United Nations/OU=Master List Signers` |

**분류 알고리즘**:
```
IF hasExtendedKeyUsage("2.23.136.1.1.9"):
   → Type = MLSC
```

### 2.4 Link Certificate (CSCA 키 전환용)

**참조**: ICAO Doc 9303 Part 12, Section 7.1.2

Link Certificate는 **CSCA 키 전환(rollover)** 시 신뢰 연속성을 보장하는 중간 CA 인증서이다.

| 필드 | 요구사항 |
|------|----------|
| Issuer | **이전 CSCA**의 Subject DN |
| Subject | **새 CSCA**의 Subject DN |
| Self-Signed | **아니오** (Issuer != Subject) |
| Basic Constraints | **CRITICAL**, `CA=TRUE` |
| Key Usage | **CRITICAL**, `keyCertSign` + `cRLSign` |

**분류 알고리즘**:
```
IF BasicConstraints.CA == TRUE
   AND KeyUsage.keyCertSign == TRUE
   AND Issuer DN != Subject DN (Non-self-signed):
   → Type = LINK_CERT
```

### 2.5 Deviation List Signer Certificate

**참조**: ICAO Doc 9303 Part 12

Deviation List(편차 목록)에 서명하는 인증서.

| 필드 | 요구사항 |
|------|----------|
| Extended Key Usage | OID `2.23.136.1.1.10` (`id-icao-mrtd-security-deviationListSigner`) |

### 2.6 ICAO 전용 OID 체계

| OID | 이름 | 용도 |
|-----|------|------|
| `2.23.136.1.1.1` | `id-icao-mrtd-security-documentSigning` | DSC 서명 |
| `2.23.136.1.1.6` | `id-icao-mrtd-security-aaProtocol` | Active Authentication |
| `2.23.136.1.1.9` | `id-icao-mrtd-security-masterListSigner` | Master List 서명 |
| `2.23.136.1.1.10` | `id-icao-mrtd-security-deviationListSigner` | Deviation List 서명 |

---

## 3. Trust Chain 아키텍처

### 3.1 기본 Trust Chain (2단계)

가장 일반적인 형태: 여권 칩 → DSC → CSCA

```
여권 칩 (eMRTD)
    │
    ├── Data Group 1 (MRZ)         ─┐
    ├── Data Group 2 (얼굴 이미지)   ├─ 해시값이 SOD에 저장
    ├── Data Group 14 (보안 옵션)    │
    └── Data Group N               ─┘
    │
    └── SOD (Security Object Document)
         │
         ├── LDSSecurityObject
         │    ├── Hash Algorithm (SHA-256)
         │    └── DataGroup Hashes (DG1..DG16)
         │
         └── CMS SignedData
              │
              ├── SignerInfo
              │    ├── DSC Certificate (내장)
              │    └── 전자서명 (DG 해시에 대한)
              │
              └── Signature Algorithm (SHA256withRSA)
                   │
                   ▼
              DSC (Document Signer Certificate)
                   │
                   └── CSCA 공개키로 서명 검증
                   │
                   ▼
              CSCA (Country Signing CA)
                   │
                   └── Self-signed (신뢰 루트)
                   │
                   ▼
              [Master List 또는 양자 교환으로 획득]
```

### 3.2 Link Certificate 포함 Trust Chain (3단계)

CSCA 키 전환 시 확장된 체인:

```
DSC (새 CSCA가 서명)
    │
    ▼
CSCA_new (새 Country Signing CA)
    │
    └── Link Certificate에 의해 서명
    │
    ▼
Link Certificate (LC)
    │
    └── CSCA_old에 의해 서명
    │
    ▼
CSCA_old (기존 Country Signing CA, 기존 시스템에서 신뢰)
```

### 3.3 검증 알고리즘

구현체의 검증 흐름 (certificate_validation_service.cpp):

```
1. DSC에서 Subject DN, Issuer DN 추출
2. DSC 유효기간 확인 (X509_cmp_time)
3. DSC Issuer DN에서 국가 코드 추출 (C= 컴포넌트)
4. LDAP에서 DSC Issuer DN과 매칭되는 CSCA 조회
   (RFC 5280 기준 대소문자 무시 DN 비교)
5. CSCA 발견 시:
   a. CSCA 공개키 추출 (X509_get_pubkey)
   b. DSC 서명 검증: X509_verify(dsc, csca_pubkey)
   c. CSCA 유효기간 확인
   d. CRL 상태 확인
   e. 종합 판정: signatureVerified AND NOT revoked
6. CSCA 미발견 시:
   → INVALID ("CSCA not found for issuer: {dn}")
```

---

## 4. Passive Authentication (PA) 절차

### 4.1 개요

**참조**: ICAO Doc 9303 Part 11, Section 5

Passive Authentication은 전자여권의 **필수(MANDATORY)** 보안 메커니즘이다. 여권 칩의 데이터가 발급 이후 변조되지 않았음을 검증한다. "수동(Passive)"인 이유는 칩이 별도의 암호 연산을 수행하지 않기 때문이다 -- 검사 시스템이 칩에서 데이터를 읽어 PKI를 사용해 검증한다.

### 4.2 8단계 검증 프로세스

본 시스템의 PA 검증은 8단계로 구성된다:

| 단계 | 명칭 | 작업 | ICAO 참조 |
|------|------|------|-----------|
| 1 | **SOD 파싱** | CMS SignedData 파싱, 해시 알고리즘 및 DG 해시 추출 | Part 10, Sec 4 |
| 2 | **DSC 추출** | SOD SignerInfo에서 DSC 인증서 추출 | Part 11, Sec 5 |
| 3 | **Trust Chain 검증** | DSC 서명을 CSCA 공개키로 검증 | Part 12, Sec 7-8 |
| 4 | **CSCA 조회** | Local PKD (LDAP/DB)에서 CSCA 검색 (Link Certificate 포함) | Part 12, Sec 7 |
| 5 | **SOD 서명 검증** | CMS 서명 무결성 검증 (`CMS_verify()`) | Part 10, Sec 4 |
| 6 | **DG 해시 검증** | 제공된 DG들의 해시 계산 후 SOD 값과 비교 | Part 10, Sec 4 |
| 7 | **CRL 확인** | CRL 만료 확인 후 DSC 폐지 상태 확인 | Part 11, Sec 5; RFC 5280 |
| 8 | **DSC 자동 등록** | 신규 발견된 DSC를 Local PKD에 자동 등록 | 구현 특화 |

### 4.3 단계별 상세

#### Step 1: SOD 파싱

```
입력: SOD 바이너리 데이터

처리:
  1. ICAO SOD 태그(0x77) 래퍼 감지 및 제거
  2. CMS ContentInfo 파싱: d2i_CMS_bio()
  3. digestAlgorithms 추출 (SHA-256, SHA-384 등)
  4. encapContentInfo에서 LDSSecurityObject 추출
  5. DataGroupHash 시퀀스 파싱

성공 기준: CMS 유효, LDSSecurityObject 파싱 가능, DG 해시 1개 이상
실패: "INVALID_SOD" - CMS 구조 오류 또는 ASN.1 손상
```

#### Step 2: DSC 추출

```
입력: 파싱된 CMS 구조

처리:
  1. CMS에서 인증서 셋 추출: CMS_get1_certs(cms)
  2. 첫 번째 인증서를 DSC로 사용
  3. 독립 사용을 위해 복제: X509_dup()

성공 기준: CMS 내 인증서 1개 이상 존재
실패: "DSC_EXTRACTION_FAILED" - 일부 국가는 SOD에 DSC를 포함하지 않음
       이 경우 PKD에서 일련번호/발급자 매칭으로 DSC 획득 필요
```

#### Step 3: Trust Chain 검증

```
입력: DSC 인증서, 국가 코드

처리:
  1. DSC의 Issuer DN 추출
  2. Issuer DN에서 국가 코드 추출 (/C=XX)
  3. LDAP에서 Issuer DN ↔ CSCA Subject DN 매칭으로 CSCA 검색
  4. DSC 서명 검증: X509_verify(dsc, csca_pubkey)
  5. DSC 유효기간 확인: notAfter와 현재 시각 비교
  6. CSCA 유효기간 확인

성공 기준: X509_verify() 반환값 1, CSCA가 신뢰 저장소에 존재
실패 사유:
  - CSCA_NOT_FOUND: PKD에 매칭 CSCA 없음
  - TRUST_CHAIN_INVALID: 서명 검증 실패 (DSC가 이 CSCA로 서명되지 않음)
  - CERTIFICATE_EXPIRED: DSC 또는 CSCA 유효기간 초과
```

#### Step 4: CSCA 조회

```
신뢰 소스 (우선순위 순):
  1. ICAO PKD: 공식 LDAP 기반 디렉토리
  2. 양자 교환: 정부 간 직접 CSCA 교환
  3. Master List: ICAO/참가국이 배포하는 CMS 서명 CSCA 모음

LDAP 검색 경로:
  o=csca, c={COUNTRY}, dc=data, dc=download, dc=pkd, ...
  필터: (objectClass=pkdDownload)
  Fallback: o=lc (Link Certificate 검색)
```

#### Step 5: SOD 서명 검증

```
입력: SOD 바이너리, DSC 인증서

처리:
  1. SOD에서 CMS 파싱 (ICAO 래퍼 제거 후)
  2. DSC 인증서로 X509_STORE 생성
  3. CMS_verify() 호출
     - CMS_NO_SIGNER_CERT_VERIFY: CMS 내 체인 검증 생략 (Step 3에서 별도 수행)
     - CMS_NO_ATTR_VERIFY: 서명 속성 검증 생략

성공 기준: CMS_verify() 반환값 1
실패: "SOD_SIGNATURE_INVALID" - 서명 불일치, 데이터 변조 의심
```

#### Step 6: DG 해시 검증

```
입력: 여권 리더에서 읽은 Data Group들, SOD의 해시값

처리:
  각 Data Group에 대해:
    expected_hash = SOD.LDSSecurityObject.dataGroupHashes[dgNumber]
    actual_hash   = HashAlgorithm(DG_raw_bytes)

    IF expected_hash == actual_hash:
      → DG 인증됨 (발급 이후 변조 없음)
    ELSE:
      → DG 변조됨

성공 기준: 제공된 모든 DG 해시가 SOD 값과 일치
실패: "DG_HASH_MISMATCH" - 데이터 변조 또는 NFC 읽기 오류
```

#### Step 7: CRL 확인

```
입력: DSC 인증서, 국가 코드

처리:
  1. LDAP/DB에서 해당 국가 CRL 조회
  2. CRL 미발견 시: CRL_UNAVAILABLE (Fail-open 정책)
  3. CRL 날짜 추출:
     - thisUpdate: X509_CRL_get0_lastUpdate()
     - nextUpdate: X509_CRL_get0_nextUpdate()
  4. CRL 만료 확인 (nextUpdate < 현재시각)
     - 만료 시: CRL_EXPIRED (경고)
  5. DSC 일련번호가 폐지 목록에 있는지 확인
     - 있으면: REVOKED (치명적 실패)
  6. 없으면: VALID
```

### 4.4 최종 판정 로직

```
PA 결과 = certificateChainValid AND sodSignatureValid AND dataGroupsValid

상세 판정:
  VALID         = 서명 검증 OK + 미폐지 + 미만료 + DG 해시 일치
  EXPIRED_VALID = 서명 검증 OK + 미폐지 + 만료됨 (Point-in-Time Validation)
  INVALID       = 서명 실패 또는 폐지됨 또는 DG 해시 불일치
  PENDING       = CSCA 미발견 (검증 보류)
```

### 4.5 Point-in-Time Validation

**참조**: ICAO Doc 9303 Part 11 (권고사항)

ICAO는 DSC/CSCA가 **현재 시점에서 만료**되었더라도, **여권 발급 시점에 유효**했다면 여전히 유효한 것으로 간주할 수 있도록 권고한다. 이를 **Point-in-Time Validation**이라 한다.

이는 여권의 물리적 유효기간(통상 10년)이 DSC 유효기간(3~5년)보다 길기 때문에 발생하는 현실적 문제를 해결한다.

구현체에서는 `EXPIRED_VALID` 상태로 이를 구분한다.

---

## 5. Active Authentication vs Passive Authentication

### 5.1 비교

**참조**: ICAO Doc 9303 Part 11, Section 5-6

| 구분 | Passive Authentication (PA) | Active Authentication (AA) |
|------|---------------------------|---------------------------|
| **목적** | 데이터 무결성 검증 (변조 여부) | 칩 진품 검증 (복제 여부) |
| **ICAO 상태** | **필수(MANDATORY)** | **선택(OPTIONAL)** |
| **칩 연산** | 없음 (칩은 읽기 전용) | 칩이 RSA/ECDSA 서명 수행 |
| **필요 데이터** | SOD + Data Groups | DG15 (AA 공개키) + Challenge |
| **검증 대상** | 발급 이후 데이터 변조 여부 | 물리적 칩의 진품 여부 |
| **PKI 사용** | CSCA → DSC → SOD → DG 해시 | DG15에 내장된 공개키 |
| **온라인 필요** | 불필요 (Local CSCA 캐시) | 불필요 |
| **취약점** | 칩 복제 감지 불가 | 데이터 변조 감지 불가 |
| **조합 효과** | PA + AA로 완전한 보안 제공 | -- |

### 5.2 Active Authentication 프로토콜

```
검사 시스템                              여권 칩
    │                                       │
    │   1. DG15 읽기 (AA 공개키)              │
    │◄──────────────────────────────────────│
    │                                       │
    │   2. 랜덤 Challenge 생성 (8 bytes)      │
    │──────────────────────────────────────►│
    │   INTERNAL AUTHENTICATE 명령           │
    │                                       │
    │   3. 칩이 AA 개인키로 Challenge 서명     │
    │                                       │
    │   4. 서명 반환                          │
    │◄──────────────────────────────────────│
    │                                       │
    │   5. DG15 공개키로 서명 검증             │
    │                                       │
```

### 5.3 Chip Authentication (CA) vs Active Authentication

| 특성 | Active Authentication (AA) | Chip Authentication (CA) |
|------|---------------------------|--------------------------|
| **프로토콜** | Challenge-response | Diffie-Hellman 키 합의 |
| **키 저장소** | DG15 | DG14 |
| **보안 채널** | 미생성 (진품 증명만) | 생성 (암호화 채널 수립) |
| **복제 보호** | 기본 (이론적 재생 공격 가능) | 강력 (세션 키 수립) |
| **폐지 상태** | CA에 의해 대체 중 | **신규 구현 권장** |

### 5.4 보안 메커니즘 조합표

```
                    데이터 무결성    칩 진품성    생체정보 접근
PA (필수)                O             X             X
AA (선택)                X             O             X
CA (선택)                O             O             X
TA (선택)                X             X             O
PACE (선택)              X             X          보안 채널
```

### 5.5 접근 제어 메커니즘

| 메커니즘 | 참조 | 용도 |
|----------|------|------|
| **BAC** (Basic Access Control) | Part 11, Sec 4.3 | NFC 통신 보호 (MRZ 기반 3DES), 레거시 |
| **PACE** (Password Authenticated Connection Establishment) | Part 11, SAC | BAC의 강화 대체 (DH 기반), 신규 권장 |
| **EAC** (Extended Access Control) | Part 11, Sec 7 | 민감 생체정보(DG3/DG4) 접근, TA+CA 조합 |

---

## 6. Link Certificate와 CSCA 키 전환

### 6.1 키 전환의 필요성

CSCA 인증서는 장기간(15~20년) 유효하지만, 다음 상황에서 키 교체가 필요하다:
- 키 유효기간 만료
- 암호 알고리즘 마이그레이션 (SHA-1→SHA-256, RSA-1024→RSA-2048 등)
- 보안 사고 (키 유출 의심)
- 정책 변경

### 6.2 Link Certificate의 역할

Link Certificate 없이는 새 CSCA 발급 시 전 세계 모든 출입국 시스템이 즉시 새 CSCA를 신뢰해야 한다 -- 물리적으로 불가능한 작업이다. Link Certificate는 기존 CSCA를 신뢰하는 시스템이 **추이적으로(transitively)** 새 CSCA를 신뢰할 수 있게 한다.

### 6.3 Link Certificate의 두 가지 유형

**Forward Link Certificate (순방향)**:
```
발급자(Issuer) = 이전 CSCA
주체(Subject)  = 새 CSCA
→ 이전 CSCA 개인키로 새 CSCA 공개키를 서명
→ 이전 CSCA를 신뢰하는 시스템이 새 CSCA도 신뢰 가능
```

**Backward/Reverse Link Certificate (역방향)**:
```
발급자(Issuer) = 새 CSCA
주체(Subject)  = 이전 CSCA
→ 새 CSCA 개인키로 이전 CSCA 공개키를 서명
→ 새 CSCA를 신뢰 루트로 사용해도 이전 DSC 검증 가능
```

### 6.4 Link Certificate 검증 절차

구현체의 LC 검증 (lc_validator.cpp):

```
Step 1:  LC 바이너리 파싱 (DER 형식)
Step 2:  메타데이터 추출 (Subject DN, Issuer DN, Serial)
Step 3:  이전 CSCA 찾기 (LC의 Issuer DN 매칭)
Step 4:  LC 서명 검증 (이전 CSCA 공개키 사용)
         X509_verify(linkCert, oldCscaPubKey)
Step 5:  새 CSCA 찾기 (LC의 Subject DN 매칭, forward lookup)
Step 6:  새 CSCA 서명 검증 (LC 공개키 사용)
         X509_verify(newCsca, linkCertPubKey)
Step 7:  LC 유효기간 확인 (notBefore <= NOW <= notAfter)
Step 8:  인증서 확장 검증:
         - BasicConstraints: CA:TRUE 필수
         - KeyUsage: keyCertSign 필수
         - pathlen: 0이 일반적 (end-entity만 서명 가능)
Step 9:  CRL 폐지 상태 확인
Step 10: 종합 결과:
         trustChainValid = oldCscaSigValid AND newCscaSigValid
                          AND validityOK AND extensionsOK
                          AND NOT revoked
```

### 6.5 실제 데이터

ICAO Master List (2025년 12월) 분석 결과:

| 지표 | 수량 | 비율 |
|------|------|------|
| Master List 전체 인증서 | 537 | 100% |
| Self-signed CSCA | 476 | 88.6% |
| Link Certificate | 60 | 11.2% |
| MLSC (UN 서명자) | 1 | 0.2% |
| LC 보유 국가 | 25+ | -- |

**LC 다수 보유 국가**: 라트비아(7), 필리핀(6), 에스토니아(4)

---

## 7. Master List 구조와 처리

### 7.1 Master List 개요

**참조**: ICAO Doc 9303 Part 12, Annex G

ICAO Master List는 **CMS SignedData** (PKCS#7) 형식으로, 참가 회원국의 CSCA 인증서를 포함한다.

### 7.2 ASN.1 구조

```
MasterList CMS SignedData
├── version: INTEGER (3)
├── digestAlgorithms: SET OF AlgorithmIdentifier
│   └── algorithm: sha256 (2.16.840.1.101.3.4.2.1)
├── encapContentInfo: ContentInfo
│   ├── contentType: id-data (1.2.840.113549.1.7.1)
│   └── content: OCTET STRING
│       └── MasterList ::= SEQUENCE {
│               version    INTEGER OPTIONAL (v0=0),
│               certList   SET OF Certificate  (536개 인증서)
│           }
├── certificates: [0] IMPLICIT CertificateSet (ICAO ML에서는 비어 있음!)
└── signerInfos: SET OF SignerInfo
    └── SignerInfo (1개)
        ├── sid: issuer=/C=UN/O=United Nations/OU=Master List Signers
        ├── digestAlgorithm: sha256
        ├── signatureAlgorithm: rsaEncryption
        └── signature: 256 bytes
```

### 7.3 핵심 구현 사항: 2단계 인증서 추출

**중요**: ICAO Master List는 인증서를 **두 개의 서로 다른 위치**에 저장한다:

| 위치 | 내용 | 접근 방법 |
|------|------|-----------|
| **SignerInfo** (외부 CMS) | MLSC 인증서 (1개) | `CMS_get0_SignerInfos()` |
| **encapContentInfo** (내부 콘텐츠) | CSCA + LC 인증서 (536개) | `CMS_get0_content()` + ASN.1 파싱 |

**주의**: 외부 CMS의 `certificates` 필드는 **비어 있다**! `CMS_get1_certs()`를 사용하면 NULL이 반환된다. 이것은 가장 흔한 구현 실수 중 하나이다.

### 7.4 Master List 사양

| 속성 | 값 |
|------|-----|
| 파일 확장자 | `.ml` (바이너리 CMS) |
| 일반적 크기 | ~800 KB |
| Content Type | `application/pkcs7-mime` |
| 서명 알고리즘 | RSA with SHA-256 |
| 인증서 수 | 500~600 (발행 시점에 따라 변동) |
| 표현 국가 | 95+ |
| 발행 주기 | 주기적 (ICAO가 새 버전 발행) |

---

## 8. CRL (인증서 폐지 목록)

### 8.1 역할

**참조**: RFC 5280, Section 5; ICAO Doc 9303 Part 11

CRL은 각 국가의 CSCA가 발급하는 **DSC 폐지 목록**이다. 유출되었거나 더 이상 신뢰하면 안 되는 DSC를 폐지한다.

### 8.2 CRL 필드

| 필드 | 설명 |
|------|------|
| `issuer` | 발급 CSCA의 DN |
| `thisUpdate` | CRL 발행 시각 |
| `nextUpdate` | 다음 CRL 예상 발행 시각 |
| `revokedCertificates` | 폐지된 인증서 일련번호 목록 |
| `signature` | CSCA의 CRL 서명 |

### 8.3 CRL 검증 프로세스

```
1. 해당 국가의 CRL을 LDAP/DB에서 조회
2. CRL 미발견 시: CRL_UNAVAILABLE 반환 (Fail-open)
3. CRL 날짜 추출:
   - thisUpdate: X509_CRL_get0_lastUpdate(crl)
   - nextUpdate: X509_CRL_get0_nextUpdate(crl)
4. CRL 만료 확인 (nextUpdate < 현재시각)
   - 만료 시: CRL_EXPIRED 반환 (경고)
5. DSC 일련번호가 폐지 목록에 있는지 확인
   - 있으면: REVOKED 반환 (치명적 실패)
6. 없으면: VALID 반환
```

### 8.4 CRL 상태 유형

| 상태 | 설명 | 심각도 | PA 결과 |
|------|------|--------|---------|
| `VALID` | 인증서 미폐지, CRL 유효 | INFO | 통과 |
| `REVOKED` | 인증서가 CRL에 존재 | CRITICAL | **실패** |
| `CRL_UNAVAILABLE` | 해당 국가 CRL 없음 | WARNING | 통과 (Fail-open) |
| `CRL_EXPIRED` | CRL `nextUpdate` 경과 | WARNING | 통과 (경고 포함) |
| `CRL_INVALID` | CRL 서명 검증 실패 | CRITICAL | 통과 (경고 포함) |
| `NOT_CHECKED` | CRL 확인 생략 | INFO | 통과 |

### 8.5 Fail-Open 정책

ICAO Doc 9303 Part 11에 따르면 CRL 확인은 **SHOULD (권고)**이지 **MUST (필수)**가 아니다:
- CRL이 없으면 검증은 경고와 함께 통과
- CRL이 만료되었으면 검증은 경고와 함께 통과
- 실제 폐지 항목만 PA 실패를 유발

이는 모든 국가가 CRL을 일관성 있게 발행하지 않는 현실을 반영한다.

### 8.6 폐지 사유 코드 (RFC 5280)

| 코드 | 사유 | 설명 |
|------|------|------|
| 0 | `UNSPECIFIED` | 사유 미지정 |
| 1 | `KEY_COMPROMISE` | 개인키 유출 |
| 2 | `CA_COMPROMISE` | CA 키 유출 |
| 3 | `AFFILIATION_CHANGED` | 소속 변경 |
| 4 | `SUPERSEDED` | 새 인증서로 대체 |
| 5 | `CESSATION_OF_OPERATION` | 운영 중단 |
| 6 | `CERTIFICATE_HOLD` | 임시 보류 |

---

## 9. SOD (Security Object Document) 구조

### 9.1 개요

SOD는 전자여권 칩(EF.SOD)에 저장되는 **CMS SignedData** 구조로, Data Group 해시값에 대한 DSC 전자서명을 포함한다.

### 9.2 ASN.1 구조 상세

```asn1
-- 여권 칩에 저장된 SOD (Tag 0x77 래퍼)
EF.SOD ::= [APPLICATION 23] IMPLICIT SignedData

-- RFC 5652 CMS SignedData
SignedData ::= SEQUENCE {
    version           CMSVersion,              -- v3 권장
    digestAlgorithms  DigestAlgorithmIdentifiers,
    encapContentInfo  EncapsulatedContentInfo,  -- LDSSecurityObject 포함
    certificates      [0] IMPLICIT CertificateSet OPTIONAL,  -- DSC 인증서
    crls              [1] IMPLICIT RevocationInfoChoices OPTIONAL,
    signerInfos       SignerInfos              -- DSC 서명
}

-- ICAO 전용 콘텐츠
EncapsulatedContentInfo ::= SEQUENCE {
    eContentType  ContentType,    -- 2.23.136.1.1.1 (id-icao-ldsSecurityObject)
    eContent      [0] EXPLICIT OCTET STRING  -- DER 인코딩된 LDSSecurityObject
}
```

### 9.3 LDS Security Object 형식

```asn1
-- Doc 9303 Part 10, Section 4.6.2.2
LDSSecurityObject ::= SEQUENCE {
    version                LDSSecurityObjectVersion,  -- v0(0) 또는 v1(1)
    hashAlgorithm          DigestAlgorithmIdentifier,  -- SHA-256 등
    dataGroupHashValues    SEQUENCE OF DataGroupHash,
    ldsVersionInfo         LDSVersionInfo OPTIONAL     -- LDS 1.8+
}

DataGroupHash ::= SEQUENCE {
    dataGroupNumber  DataGroupNumber,   -- INTEGER (1..16)
    dataGroupHashValue  OCTET STRING    -- Data Group의 해시값
}
```

### 9.4 Data Group 참조표

| DG | ASN.1 Tag | 내용 | 필수 여부 | PA 시 필요 |
|----|-----------|------|----------|-----------|
| DG1 | 0x61 | MRZ (Machine Readable Zone) | 필수 | **권장** |
| DG2 | 0x75 | 얼굴 이미지 | 필수 | **권장** |
| DG3 | 0x63 | 지문 | EAC 전용 | 선택 |
| DG4 | 0x76 | 홍채 | EAC 전용 | 선택 |
| DG5 | 0x65 | 표시된 초상화 | 선택 | 선택 |
| DG7 | 0x67 | 표시된 서명 | 선택 | 선택 |
| DG11 | 0x6B | 추가 개인 정보 | 선택 | 선택 |
| DG12 | 0x6C | 추가 문서 정보 | 선택 | 선택 |
| DG14 | 0x6E | 보안 옵션 (CA/PACE) | CA/PACE 사용 시 | 선택 |
| DG15 | 0x6F | AA 공개키 | AA 사용 시 | 선택 |

> PA 검증에 필요한 최소 데이터는 **SOD + 1개 이상의 DG**이다. DG1(MRZ)과 DG2(얼굴)를 포함하면 여권 정보와 생체 이미지를 함께 확인할 수 있다.

### 9.5 LDS 버전 차이

| 특성 | LDS 1.7 (v0) | LDS 1.8 (v1) |
|------|--------------|--------------|
| `LDSSecurityObject` version | v0 (0) | v1 (1) |
| `ldsVersionInfo` 필드 | 없음 | 있음 (선택적) |
| Unicode 버전 | 미지정 | 지정 |
| 하위 호환성 | -- | v0 지원 필수 |

---

## 10. ICAO PKD (Public Key Directory)

### 10.1 목적

**참조**: ICAO Doc 9303 Part 12, Section 10

ICAO PKD는 전자여권 PKI 자료를 배포하는 **글로벌 인프라**이다. ICAO가 운영하며, 회원국들이 인증서와 CRL을 공유할 수 있게 한다.

### 10.2 PKD Collection

| Collection | 내용 | LDIF 경로 |
|-----------|------|-----------|
| **Collection 001** | DSC 인증서 (적합) | `o=dsc,c={country},dc=data,...` |
| **Collection 002** | CSCA 인증서 + Master List | `o=csca,c={country},...` + `o=ml,...` |
| **Collection 003** | CRL (인증서 폐지 목록) | `o=crl,c={country},...` |

### 10.3 PKD LDAP DIT (Directory Information Tree)

```
dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
├── dc=data                          (적합 인증서)
│   ├── c=KR (대한민국)
│   │   ├── o=csca                   (CSCA 인증서)
│   │   │   └── cn={SHA256_fingerprint}
│   │   ├── o=mlsc                   (Master List Signer 인증서)
│   │   ├── o=dsc                    (DSC 인증서)
│   │   │   └── cn={SHA256_fingerprint}
│   │   ├── o=crl                    (CRL)
│   │   │   └── cn={SHA256_fingerprint}
│   │   └── o=ml                     (Master List)
│   ├── c=US (미국)
│   │   └── ...
│   └── c={...} (95+ 국가)
└── dc=nc-data                       (비적합, 2021년 폐지)
    └── c={country}
        └── o=dsc                    (비적합 DSC)
```

### 10.4 LDAP ObjectClass

| ObjectClass | 용도 | 주요 속성 |
|-------------|------|-----------|
| `pkdDownload` | 인증서 (CSCA, DSC, MLSC) | `userCertificate;binary`, `cACertificate;binary` |
| `cRLDistributionPoint` | CRL | `certificateRevocationList;binary` |

### 10.5 Local PKD 아키텍처

Local PKD(본 프로젝트)는 ICAO PKD 데이터를 로컬에 캐싱하여:
1. 오프라인 운영 가능
2. 저지연 검증
3. ICAO 인프라 의존도 감소
4. 맞춤 검증 정책 적용

```
ICAO PKD (글로벌)
    │ LDIF/Master List 다운로드
    ▼
Local PKD
    ├── PostgreSQL (메타데이터, 검색, 통계)
    ├── OpenLDAP (DIT 구조, 바이너리 인증서)
    └── PA Service (실시간 검증)
```

---

## 11. 암호화 요구사항

### 11.1 승인된 서명 알고리즘

**참조**: ICAO Doc 9303 Part 12, Section 6

| 알고리즘 | OID | 키 유형 | 상태 |
|----------|-----|---------|------|
| SHA256withRSA | `1.2.840.113549.1.1.11` | RSA | **권장** |
| SHA384withRSA | `1.2.840.113549.1.1.12` | RSA | 승인 |
| SHA512withRSA | `1.2.840.113549.1.1.13` | RSA | 승인 |
| SHA256withECDSA | `1.2.840.10045.4.3.2` | ECDSA | **권장** |
| SHA384withECDSA | `1.2.840.10045.4.3.3` | ECDSA | 승인 |
| SHA512withECDSA | `1.2.840.10045.4.3.4` | ECDSA | 승인 |
| SHA1withRSA | `1.2.840.113549.1.1.5` | RSA | **폐지** |
| SHA224withRSA | `1.2.840.113549.1.1.14` | RSA | **폐지** |
| SHA224withECDSA | `1.2.840.10045.4.3.1` | ECDSA | **폐지** |

### 11.2 해시 알고리즘

| 해시 알고리즘 | OID | 출력 크기 | 상태 |
|--------------|-----|----------|------|
| SHA-1 | `1.3.14.3.2.26` | 160 bits | **폐지** (하위 호환 검증만) |
| SHA-256 | `2.16.840.1.101.3.4.2.1` | 256 bits | **권장** |
| SHA-384 | `2.16.840.1.101.3.4.2.2` | 384 bits | 승인 |
| SHA-512 | `2.16.840.1.101.3.4.2.3` | 512 bits | 승인 |

### 11.3 키 크기 요구사항

**RSA**:
| 인증서 유형 | 최소 | 권장 |
|------------|------|------|
| CSCA (15~20년) | 2048 bits | **3072+ bits** (4096 권장) |
| DSC (3~5년) | 2048 bits | 3072 bits |

**ECDSA**:
| 인증서 유형 | 최소 곡선 | 권장 곡선 |
|------------|----------|----------|
| CSCA | P-256 (secp256r1) | **P-384** (secp384r1) 또는 P-521 |
| DSC | P-256 (secp256r1) | P-256 이상 |

승인 곡선: P-256, P-384, P-521, brainpoolP256r1, brainpoolP384r1, brainpoolP512r1

### 11.4 알고리즘 전환 이력

1. **SHA-1 폐지**: 7판에서 폐지. 신규 eMRTD는 SHA-1 사용 금지. 단, 검사국은 유통 중인 이전 문서를 위해 SHA-1 검증을 지원해야 함.
2. **RSA-1024 폐지**: 최소 2048 bits로 상향.
3. **SHA-224 폐지**: 보안 마진 부족으로 비권장.
4. **마이그레이션**: ECDSA P-256+ 또는 RSA-3072+로의 전환 권장.

---

## 12. 실제 Use Case 시나리오

### 12.1 시나리오 1: 정상적인 PA 검증

```
상황: 대한민국 여권, 유효기간 내, 최신 CSCA 등록됨

처리 흐름:
  1. SOD 파싱 → SHA-256 해시 알고리즘, DG1/DG2 해시 추출
  2. DSC 추출 → /C=KR/O=Ministry of Foreign Affairs/CN=DSC KR 01
  3. CSCA 조회 → LDAP에서 /C=KR/.../CN=CSCA KR 발견
  4. Trust Chain 검증 → X509_verify(DSC, CSCA_pubkey) = SUCCESS
  5. SOD 서명 검증 → CMS_verify() = SUCCESS
  6. DG 해시 검증 → DG1, DG2 해시 모두 일치
  7. CRL 확인 → DSC 일련번호 미폐지

결과: VALID
신뢰 체인 경로: DSC → CSCA KR (깊이: 2)
```

### 12.2 시나리오 2: CSCA 키 전환 (Link Certificate)

```
상황: 독일이 CSCA 키를 교체, Link Certificate 발급

처리 흐름:
  1. DSC가 새 CSCA(CSCA_DE_2025)로 서명됨
  2. LDAP에서 CSCA_DE_2025 검색 → 직접 신뢰되지 않음
  3. Link Certificate 검색 → LC 발견
     - LC.Issuer = CSCA_DE_2020 (이전 CSCA, 신뢰됨)
     - LC.Subject = CSCA_DE_2025 (새 CSCA)
  4. LC 검증: X509_verify(LC, CSCA_DE_2020_pubkey) = SUCCESS
  5. 새 CSCA 검증: X509_verify(CSCA_DE_2025, LC_pubkey) = SUCCESS
  6. DSC 검증: X509_verify(DSC, CSCA_DE_2025_pubkey) = SUCCESS

결과: VALID
신뢰 체인 경로: DSC → CSCA_DE_2025 → LC → CSCA_DE_2020 (깊이: 4)
```

### 12.3 시나리오 3: 만료된 인증서 (Point-in-Time Validation)

```
상황: DSC 유효기간 2023년 만료, 여권 유효기간 2028년까지

처리 흐름:
  1. SOD 파싱 → 정상
  2. DSC 추출 → notAfter: 2023-12-31 (만료됨)
  3. Trust Chain 검증 → 서명은 유효
  4. 유효기간 확인 → DSC 만료 감지
  5. Point-in-Time 검증 → 발급 시점에는 유효했음

결과: EXPIRED_VALID
사유: "서명 유효, DSC 만료 (발급 시점 유효)"
정책: 국가별 정책에 따라 VALID로 취급 가능
```

### 12.4 시나리오 4: CSCA 미등록 국가

```
상황: 투발루(TV) 여권, CSCA가 Local PKD에 등록되지 않음

처리 흐름:
  1. SOD 파싱 → 정상
  2. DSC 추출 → /C=TV/CN=DSC TV 01
  3. CSCA 조회 → LDAP에서 c=TV 검색
     - o=csca 검색: 없음
     - o=lc 검색: 없음
  4. 결과: CSCA_NOT_FOUND

결과: PENDING (검증 보류)
사유: "CSCA not found for issuer: /C=TV/..."
조치: 해당 국가의 Master List 또는 CSCA 인증서 수동 등록 필요
```

### 12.5 시나리오 5: DSC 폐지 (CRL Revocation)

```
상황: 프랑스 DSC가 개인키 유출로 폐지됨

처리 흐름:
  1. SOD 파싱 → 정상
  2. DSC 추출 → /C=FR/CN=DSC FR 03, Serial: 1A2B3C
  3. Trust Chain 검증 → 서명 유효
  4. CRL 조회 → 프랑스 CRL 발견
  5. CRL 만료 확인 → nextUpdate 미경과 (유효)
  6. 폐지 확인 → Serial 1A2B3C가 CRL에 존재
     - 폐지 사유: KEY_COMPROMISE (코드 1)

결과: INVALID
사유: "인증서 폐지됨 (사유: KEY_COMPROMISE)"
```

### 12.6 시나리오 6: 데이터 변조 감지

```
상황: 여권 칩의 DG1(MRZ) 데이터가 변조됨

처리 흐름:
  1. SOD 파싱 → DG1 해시: abc123... (원본)
  2. DSC/CSCA 검증 → 모두 유효
  3. SOD 서명 검증 → 유효 (SOD 자체는 변조되지 않음)
  4. DG 해시 검증:
     - SOD의 DG1 해시: abc123...
     - 실제 DG1 해시: def456... (변조됨!)
     - 불일치!

결과: INVALID
사유: "DG_HASH_MISMATCH - DG1 해시 불일치"
의미: DG1 데이터가 발급 이후 변조됨
```

### 12.7 시나리오 7: PA 검증을 통한 DSC 자동 등록

```
상황: Local PKD에 없는 DSC가 PA 검증 과정에서 발견됨

처리 흐름:
  1. SOD에서 DSC 추출
  2. SHA-256 fingerprint 계산: abcdef...
  3. 기존 certificate 테이블에서 fingerprint 검색 → 미등록
  4. Trust Chain 검증 → VALID
  5. DSC 자동 등록:
     - source_type: 'PA_EXTRACTED'
     - stored_in_ldap: false
     - X.509 메타데이터 전체 추출 (22 필드)
  6. PKD Relay reconciliation으로 LDAP 동기화 예정

결과: VALID + DSC 자동 등록 완료
응답: dscAutoRegistration.newlyRegistered = true
```

---

## 13. 예외 처리 및 Edge Case

### 13.1 인증서 만료 관련

| 상황 | 처리 방법 | ICAO 지침 |
|------|----------|-----------|
| DSC 만료 | `EXPIRED_VALID` 상태 부여 | Point-in-Time Validation 적용 권고 |
| CSCA 만료 | 경고 포함 보고 | 여권 발급 시점 유효 여부 확인 |
| DSC + CSCA 모두 만료 | 여전히 서명 검증 수행 | 서명 유효 시 EXPIRED_VALID |
| DSC 미래 유효기간 (notBefore > now) | `NOT_YET_VALID` 상태 | 시스템 시계 오차 가능성 확인 |

### 13.2 CRL 관련 Edge Case

| 상황 | 처리 | 근거 |
|------|------|------|
| CRL 미존재 | Fail-open (경고와 함께 통과) | ICAO: CRL 확인은 SHOULD |
| CRL 만료 (nextUpdate 경과) | `CRL_EXPIRED` (경고와 함께 통과) | 만료 CRL로 폐지 상태 확인 불가 |
| Delta CRL | 미지원 (Full CRL만) | 대부분의 국가가 Full CRL 사용 |
| DSC 폐지 후 여권 계속 유통 | `REVOKED` → INVALID | 물리 여권 회수는 별도 프로세스 |
| CRL 서명 검증 실패 | `CRL_INVALID` (경고) | CRL 자체의 무결성 의심 |

### 13.3 Trust Chain 실패 사례

| 상황 | 원인 | 해결 |
|------|------|------|
| **CSCA 미발견** | 해당 국가 미등록 또는 PKD 미참가 | Master List 업로드, 양자 교환 |
| **서명 검증 실패** | DSC가 해당 CSCA로 서명되지 않음 | DN 매칭 로직 확인, LC 검색 |
| **다중 CSCA** | 한 국가에 여러 CSCA 활성 | 모든 CSCA 대상 매칭 시도 |
| **DN 형식 불일치** | OpenSSL `/C=X` vs LDAP `C=X,O=Y` | 형식 독립적 DN 비교 사용 |
| **중간 인증서 누락** | LC 없이 새 CSCA만 등록 | LC 포함 Master List 재업로드 |
| **알고리즘 미지원** | 시스템이 서명 알고리즘 미지원 | OpenSSL 업데이트, 알고리즘 추가 |

### 13.4 Self-signed 인증서 처리

- **정상**: CSCA는 반드시 self-signed (Issuer == Subject)
- **비정상**: DSC가 self-signed → CSCA로 잘못 분류될 수 있음
- **검증 방법**: BasicConstraints(CA:TRUE) + KeyUsage(keyCertSign) + Self-signed 조합으로 정확한 분류

### 13.5 시간대 및 유효기간 Edge Case

| 상황 | 주의사항 |
|------|----------|
| UTC vs 로컬 시간 | 모든 인증서 시각은 UTC 기준 |
| GeneralizedTime vs UTCTime | 2050년 이후는 GeneralizedTime 사용 |
| 윤초 | OpenSSL이 내부적으로 처리 |
| 유효기간 경계 (정확히 만료 시각) | `notAfter`는 포함(inclusive) |

### 13.6 Deviation List (편차 목록)

**목적**: ICAO 9303 규격을 완전히 준수하지 않지만 운용 중인 eMRTD의 알려진 편차를 기록

**주요 편차 유형**:
- DG 인코딩 오류 (BER vs DER)
- 잘못된 ASN.1 태그 사용
- 비표준 서명 알고리즘
- 누락된 필수 확장

**처리 방법**: 편차 목록과 대조하여 알려진 편차인 경우 허용 처리

---

## 14. 구현 시 주의사항 (Pitfalls)

본 프로젝트 구현 경험에서 도출된 주요 함정들:

### 14.1 Master List 파싱

**함정 1: `CMS_get1_certs()`로 Master List 인증서 추출**

`CMS_get1_certs()`는 CMS의 `SignedData.certificates` 필드만 반환한다. ICAO Master List에서 이 필드는 **비어 있다**! CSCA 인증서들은 `encapContentInfo`(pkiData)에 저장되어 있다.

**해결**: `CMS_get0_SignerInfos()`(MLSC) + `CMS_get0_content()`(CSCA) 2단계 추출

**함정 2: MLSC 누락**

MLSC는 SignerInfo에**만** 존재한다. pkiData에서만 추출하면 MLSC를 놓친다.

### 14.2 DN 처리

**함정 3: DN 형식 불일치**

```
OpenSSL oneline: /C=LV/O=Ministry/CN=CSCA LV
LDAP 형식:       C=LV, O=Ministry, CN=CSCA LV
RFC 2253 형식:   CN=CSCA LV,O=Ministry,C=LV
```

**해결**: 모든 형식을 지원하는 DN 파싱 및 정규화 함수 사용

**함정 4: 대소문자 비교**

RFC 5280은 DN 비교 시 **대소문자 무시**를 요구한다. `LOWER(subject_dn) = LOWER(issuer_dn)` 사용.

### 14.3 인증서 유형 분류

**함정 5: Link Certificate 오분류**

LC와 CSCA 모두 `CA:TRUE` + `keyCertSign`을 가진다. **Self-signed 여부**가 유일한 차별점이다.

```
CA:TRUE + keyCertSign + Self-signed     → CSCA
CA:TRUE + keyCertSign + Non-self-signed → Link Certificate
```

### 14.4 국가 코드 추출

**함정 6: 잘못된 Fallback**

Master List에서 모든 인증서에 "UN"을 fallback 국가 코드로 사용하면, 모든 CSCA가 `c=UN` 아래 저장된다.

**해결**: Subject DN → Issuer DN → Entry DN → "XX" fallback 체인 사용

### 14.5 바이너리 데이터 처리

**함정 7: PostgreSQL BYTEA 인코딩**

`E''` (escape string literal)에 `\x` 접두사를 사용하면 PostgreSQL이 `\x`를 이스케이프 시퀀스로 해석하여 인증서 바이너리 데이터가 손상된다. 모든 Trust Chain 검증이 실패하게 된다.

**해결**: 표준 따옴표 사용 (`E''` 대신 `''`로 bytea hex 값 저장)

### 14.6 OpenSSL 메모리 관리

**함정 8: 메모리 누수**

OpenSSL 구조체(X509, CMS_ContentInfo, EVP_PKEY 등)는 명시적으로 해제해야 한다.

**해결**: RAII 패턴 적용, 모든 코드 경로에서 일관된 정리

### 14.7 SOD 래핑

**함정 9: ICAO 외부 태그**

일부 SOD에는 CMS 콘텐츠를 래핑하는 ICAO 전용 외부 태그(0x77)가 있다. CMS 파싱 전에 이 태그를 감지하고 제거해야 한다.

### 14.8 Oracle 호환성

**함정 10: Multi-DBMS 인증서 저장**

- Oracle은 빈 문자열을 NULL로 처리 (PostgreSQL과 다름)
- Oracle은 컬럼명을 UPPERCASE로 반환
- Oracle은 `NUMBER(1)`로 BOOLEAN 표현
- Oracle은 `OFFSET/FETCH`로 페이지네이션

**해결**: Query Executor 추상화 계층으로 데이터베이스별 처리

### 14.9 Trust Chain 깊이

**함정 11: 다중 레벨 Trust Chain**

```
일반적: DSC → CSCA (깊이 2)
키 전환: DSC → LC → CSCA (깊이 3)
다중 LC: DSC → LC_n → LC_n-1 → ... → CSCA_old (깊이 N)
```

**해결**: 깊이 제한이 있는 반복적 체인 빌딩으로 무한 루프 방지

### 14.10 CRL 처리 순서

**함정 12: CRL 만료 확인 순서**

CRL 만료 확인(`nextUpdate`)을 인증서 폐지 확인 **전에** 수행해야 한다. 만료된 CRL로는 폐지 상태를 신뢰할 수 없다.

---

## 15. 참조 표준 및 문서

### 15.1 ICAO 문서

| 문서 | 제목 | 주요 내용 |
|------|------|-----------|
| **Doc 9303 Part 10** | LDS for Storage of Biometrics and Other Data in the Contactless IC | SOD 구조, DG 형식, LDS 버전 |
| **Doc 9303 Part 11** | Security Mechanisms for MRTDs | PA, AA, CA, TA, PACE |
| **Doc 9303 Part 12** | Public Key Infrastructure for MRTDs | CSCA, DSC, LC, Master List, PKD 운영, CRL 관리 |

### 15.2 RFC 표준

| RFC | 제목 | 관련성 |
|-----|------|--------|
| **RFC 5280** | Internet X.509 PKI Certificate and CRL Profile | 인증서 구조, 검증 규칙, CRL 처리 |
| **RFC 5652** | Cryptographic Message Syntax (CMS) | SOD 및 Master List SignedData 형식 |
| **RFC 4511** | LDAP Protocol | PKD LDAP 운영 |
| **RFC 4515** | LDAP String Representation of Search Filters | LDAP 필터 이스케이프 |
| **RFC 3279** | Algorithms and Identifiers for PKIX | 알고리즘 OID 참조 |
| **RFC 4055** | Additional Algorithms for RSA Cryptography in PKIX | RSA-PSS 등 |
| **RFC 5480** | ECC Subject Public Key Information | ECDSA 곡선 정의 |

### 15.3 BSI 기술 지침

| 문서 | 제목 | 관련성 |
|------|------|--------|
| **BSI TR-03105 Part 5** | Test Plan for eMRTD Application Protocol and LDS | SOD/DG/PA 적합성 테스트 |
| **BSI TR-03110** | Advanced Security Mechanisms for Machine Readable Travel Documents | EAC, PACE 프로토콜 상세 |
| **BSI TR-03111** | Elliptic Curve Cryptography | 전자여권 ECC 사양 |

### 15.4 본 시스템 구현 파일 매핑

| ICAO 표준 섹션 | 구현 파일 |
|---------------|-----------|
| Part 12 Sec 7 (인증서 프로파일) | `shared/lib/certificate-parser/src/cert_type_detector.cpp` |
| Part 12 Sec 6 (알고리즘) | `shared/lib/icao9303/sod_parser.cpp` (OID 매핑) |
| Part 10 Sec 4.6 (SOD/LDS) | `shared/lib/icao9303/sod_parser.cpp` |
| Part 11 Sec 5 (Passive Auth) | `services/pa-service/src/services/pa_verification_service.cpp` |
| Part 11 Sec 5 (Trust Chain) | `services/pa-service/src/services/certificate_validation_service.cpp` |
| Part 11 Sec 5.4 (CRL Check) | `services/pa-service/src/services/certificate_validation_service.cpp` |
| Part 12 (Master List) | `services/pkd-management/src/common/masterlist_processor.cpp` |
| Part 12 Sec 7.1.2 (Link Cert) | `services/pkd-management/src/common/lc_validator.cpp` |
| Part 11 (CRL Validation) | `services/pkd-management/src/common/crl_validator.cpp` |
| Part 11 (PA Domain Model) | `services/pa-service/src/domain/models/certificate_chain_validation.h` |

---

## 부록 A: 본 시스템 운영 데이터

| 인증서 유형 | 수량 | LDAP | 적용률 |
|------------|------|------|--------|
| CSCA | 845 | 845 | 100% |
| MLSC | 27 | 27 | 100% |
| DSC | 29,838 | 29,838 | 100% |
| DSC_NC | 502 | 502 | 100% |
| CRL | 69 | 69 | 100% |
| **합계** | **31,212** | **31,212** | **100%** |

---

## 부록 B: 에러 코드 참조

| 코드 | 심각도 | 설명 |
|------|--------|------|
| `INVALID_REQUEST` | CRITICAL | 잘못된 요청 형식 |
| `MISSING_SOD` | CRITICAL | SOD 데이터 누락 |
| `INVALID_SOD` | CRITICAL | SOD 파싱 실패 (CMS 구조 오류) |
| `DSC_EXTRACTION_FAILED` | CRITICAL | SOD에서 DSC 인증서 추출 실패 |
| `CSCA_NOT_FOUND` | CRITICAL | LDAP에서 CSCA 인증서를 찾을 수 없음 |
| `TRUST_CHAIN_INVALID` | HIGH | DSC → CSCA 신뢰 체인 검증 실패 |
| `SOD_SIGNATURE_INVALID` | HIGH | SOD 서명 검증 실패 |
| `DG_HASH_MISMATCH` | HIGH | Data Group 해시 불일치 |
| `CERTIFICATE_EXPIRED` | MEDIUM | 인증서 유효기간 만료 |
| `CRL_EXPIRED` | MEDIUM | CRL 유효기간 만료 (nextUpdate 경과) |
| `CERTIFICATE_REVOKED` | HIGH | 인증서 CRL에 의해 폐지됨 |

---

**Copyright 2026 SmartCore Inc. All rights reserved.**
