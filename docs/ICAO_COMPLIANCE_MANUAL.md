# FASTpass® SPKD — ICAO Compliance Manual

**Version**: v2.39.0 | **Last Updated**: 2026-03-22

---

## 목차

- [Part 1: ePassport PKI Trust Chain](#part-1-epassport-pki-trust-chain)
- [Part 2: Doc 9303 Compliance Checks](#part-2-doc-9303-compliance-checks)
- [Part 3: Cryptographic Algorithm Support](#part-3-cryptographic-algorithm-support)
- [Part 4: PKD Certificate Compliance Analysis](#part-4-pkd-certificate-compliance-analysis)
- [Part 5: Non-Conformant DSC Handling](#part-5-non-conformant-dsc-handling)
- [Part 6: Data Protection & PII Encryption](#part-6-data-protection--pii-encryption)

---

# Part 1: ePassport PKI Trust Chain

**ICAO Doc 9303 기반 전자여권 PKI 신뢰 체인 분석**

| 항목 | 내용 |
|------|------|
| 기준 문서 | ICAO Doc 9303, Eighth Edition (2021) |
| 관련 파트 | Part 10 (LDS), Part 11 (보안 메커니즘), Part 12 (PKI) |
| 보조 표준 | RFC 5280 (X.509 PKI), RFC 5652 (CMS), BSI TR-03105/03110 |

---

## 1.1 PKI 계층 구조 개요

### 1.1.1 전자여권 PKI 4계층 구조

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

### 1.1.2 핵심 설계 원칙

| 원칙 | 설명 |
|------|------|
| **국가별 독립 신뢰 루트** | 각 국가가 자체 CSCA 운영, 중앙 CA 없음 |
| **글로벌 상호 운용성** | ICAO PKD를 통한 CSCA 인증서 교환 |
| **오프라인 검증** | Local PKD 캐시로 네트워크 없이 검증 가능 |
| **키 전환 지원** | Link Certificate로 CSCA 키 교체 시 연속성 보장 |
| **계층적 보안** | PA + AA/CA + TA로 다층 방어 구현 |

---

## 1.2 인증서 유형별 상세

### 1.2.1 CSCA (Country Signing Certificate Authority)

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

### 1.2.2 DSC (Document Signer Certificate)

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

### 1.2.3 MLSC (Master List Signer Certificate)

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

### 1.2.4 Link Certificate (CSCA 키 전환용)

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

### 1.2.5 Deviation List Signer Certificate

**참조**: ICAO Doc 9303 Part 12

Deviation List(편차 목록)에 서명하는 인증서.

| 필드 | 요구사항 |
|------|----------|
| Extended Key Usage | OID `2.23.136.1.1.10` (`id-icao-mrtd-security-deviationListSigner`) |

### 1.2.6 ICAO 전용 OID 체계

| OID | 이름 | 용도 |
|-----|------|------|
| `2.23.136.1.1.1` | `id-icao-mrtd-security-documentSigning` | DSC 서명 |
| `2.23.136.1.1.6` | `id-icao-mrtd-security-aaProtocol` | Active Authentication |
| `2.23.136.1.1.9` | `id-icao-mrtd-security-masterListSigner` | Master List 서명 |
| `2.23.136.1.1.10` | `id-icao-mrtd-security-deviationListSigner` | Deviation List 서명 |

---

## 1.3 Trust Chain 아키텍처

### 1.3.1 기본 Trust Chain (2단계)

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

### 1.3.2 Link Certificate 포함 Trust Chain (3단계)

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

### 1.3.3 검증 알고리즘

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

## 1.4 Passive Authentication (PA) 절차

### 1.4.1 개요

**참조**: ICAO Doc 9303 Part 11, Section 5

Passive Authentication은 전자여권의 **필수(MANDATORY)** 보안 메커니즘이다. 여권 칩의 데이터가 발급 이후 변조되지 않았음을 검증한다. "수동(Passive)"인 이유는 칩이 별도의 암호 연산을 수행하지 않기 때문이다 -- 검사 시스템이 칩에서 데이터를 읽어 PKI를 사용해 검증한다.

### 1.4.2 8단계 검증 프로세스

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

### 1.4.3 단계별 상세

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

### 1.4.4 최종 판정 로직

```
PA 결과 = certificateChainValid AND sodSignatureValid AND dataGroupsValid

상세 판정:
  VALID         = 서명 검증 OK + 미폐지 + 미만료 + DG 해시 일치
  EXPIRED_VALID = 서명 검증 OK + 미폐지 + 만료됨 (Point-in-Time Validation)
  INVALID       = 서명 실패 또는 폐지됨 또는 DG 해시 불일치
  PENDING       = CSCA 미발견 (검증 보류)
```

### 1.4.5 Point-in-Time Validation

**참조**: ICAO Doc 9303 Part 11 (권고사항)

ICAO는 DSC/CSCA가 **현재 시점에서 만료**되었더라도, **여권 발급 시점에 유효**했다면 여전히 유효한 것으로 간주할 수 있도록 권고한다. 이를 **Point-in-Time Validation**이라 한다.

이는 여권의 물리적 유효기간(통상 10년)이 DSC 유효기간(3~5년)보다 길기 때문에 발생하는 현실적 문제를 해결한다.

구현체에서는 `EXPIRED_VALID` 상태로 이를 구분한다.

---

## 1.5 Active Authentication vs Passive Authentication

### 1.5.1 비교

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

### 1.5.2 Active Authentication 프로토콜

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

### 1.5.3 Chip Authentication (CA) vs Active Authentication

| 특성 | Active Authentication (AA) | Chip Authentication (CA) |
|------|---------------------------|--------------------------|
| **프로토콜** | Challenge-response | Diffie-Hellman 키 합의 |
| **키 저장소** | DG15 | DG14 |
| **보안 채널** | 미생성 (진품 증명만) | 생성 (암호화 채널 수립) |
| **복제 보호** | 기본 (이론적 재생 공격 가능) | 강력 (세션 키 수립) |
| **폐지 상태** | CA에 의해 대체 중 | **신규 구현 권장** |

### 1.5.4 보안 메커니즘 조합표

```
                    데이터 무결성    칩 진품성    생체정보 접근
PA (필수)                O             X             X
AA (선택)                X             O             X
CA (선택)                O             O             X
TA (선택)                X             X             O
PACE (선택)              X             X          보안 채널
```

### 1.5.5 접근 제어 메커니즘

| 메커니즘 | 참조 | 용도 |
|----------|------|------|
| **BAC** (Basic Access Control) | Part 11, Sec 4.3 | NFC 통신 보호 (MRZ 기반 3DES), 레거시 |
| **PACE** (Password Authenticated Connection Establishment) | Part 11, SAC | BAC의 강화 대체 (DH 기반), 신규 권장 |
| **EAC** (Extended Access Control) | Part 11, Sec 7 | 민감 생체정보(DG3/DG4) 접근, TA+CA 조합 |

---

## 1.6 Link Certificate와 CSCA 키 전환

### 1.6.1 키 전환의 필요성

CSCA 인증서는 장기간(15~20년) 유효하지만, 다음 상황에서 키 교체가 필요하다:
- 키 유효기간 만료
- 암호 알고리즘 마이그레이션 (SHA-1→SHA-256, RSA-1024→RSA-2048 등)
- 보안 사고 (키 유출 의심)
- 정책 변경

### 1.6.2 Link Certificate의 역할

Link Certificate 없이는 새 CSCA 발급 시 전 세계 모든 출입국 시스템이 즉시 새 CSCA를 신뢰해야 한다 -- 물리적으로 불가능한 작업이다. Link Certificate는 기존 CSCA를 신뢰하는 시스템이 **추이적으로(transitively)** 새 CSCA를 신뢰할 수 있게 한다.

### 1.6.3 Link Certificate의 두 가지 유형

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

### 1.6.4 Link Certificate 검증 절차

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

### 1.6.5 실제 데이터

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

## 1.7 Master List 구조와 처리

### 1.7.1 Master List 개요

**참조**: ICAO Doc 9303 Part 12, Annex G

ICAO Master List는 **CMS SignedData** (PKCS#7) 형식으로, 참가 회원국의 CSCA 인증서를 포함한다.

### 1.7.2 ASN.1 구조

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

### 1.7.3 핵심 구현 사항: 2단계 인증서 추출

**중요**: ICAO Master List는 인증서를 **두 개의 서로 다른 위치**에 저장한다:

| 위치 | 내용 | 접근 방법 |
|------|------|-----------|
| **SignerInfo** (외부 CMS) | MLSC 인증서 (1개) | `CMS_get0_SignerInfos()` |
| **encapContentInfo** (내부 콘텐츠) | CSCA + LC 인증서 (536개) | `CMS_get0_content()` + ASN.1 파싱 |

**주의**: 외부 CMS의 `certificates` 필드는 **비어 있다**! `CMS_get1_certs()`를 사용하면 NULL이 반환된다. 이것은 가장 흔한 구현 실수 중 하나이다.

### 1.7.4 Master List 사양

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

## 1.8 CRL (인증서 폐지 목록)

### 1.8.1 역할

**참조**: RFC 5280, Section 5; ICAO Doc 9303 Part 11

CRL은 각 국가의 CSCA가 발급하는 **DSC 폐지 목록**이다. 유출되었거나 더 이상 신뢰하면 안 되는 DSC를 폐지한다.

### 1.8.2 CRL 필드

| 필드 | 설명 |
|------|------|
| `issuer` | 발급 CSCA의 DN |
| `thisUpdate` | CRL 발행 시각 |
| `nextUpdate` | 다음 CRL 예상 발행 시각 |
| `revokedCertificates` | 폐지된 인증서 일련번호 목록 |
| `signature` | CSCA의 CRL 서명 |

### 1.8.3 CRL 검증 프로세스

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

### 1.8.4 CRL 상태 유형

| 상태 | 설명 | 심각도 | PA 결과 |
|------|------|--------|---------|
| `VALID` | 인증서 미폐지, CRL 유효 | INFO | 통과 |
| `REVOKED` | 인증서가 CRL에 존재 | CRITICAL | **실패** |
| `CRL_UNAVAILABLE` | 해당 국가 CRL 없음 | WARNING | 통과 (Fail-open) |
| `CRL_EXPIRED` | CRL `nextUpdate` 경과 | WARNING | 통과 (경고 포함) |
| `CRL_INVALID` | CRL 서명 검증 실패 | CRITICAL | 통과 (경고 포함) |
| `NOT_CHECKED` | CRL 확인 생략 | INFO | 통과 |

### 1.8.5 Fail-Open 정책

ICAO Doc 9303 Part 11에 따르면 CRL 확인은 **SHOULD (권고)**이지 **MUST (필수)**가 아니다:
- CRL이 없으면 검증은 경고와 함께 통과
- CRL이 만료되었으면 검증은 경고와 함께 통과
- 실제 폐지 항목만 PA 실패를 유발

이는 모든 국가가 CRL을 일관성 있게 발행하지 않는 현실을 반영한다.

### 1.8.6 폐지 사유 코드 (RFC 5280)

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

## 1.9 SOD (Security Object Document) 구조

### 1.9.1 개요

SOD는 전자여권 칩(EF.SOD)에 저장되는 **CMS SignedData** 구조로, Data Group 해시값에 대한 DSC 전자서명을 포함한다.

### 1.9.2 ASN.1 구조 상세

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

### 1.9.3 LDS Security Object 형식

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

### 1.9.4 Data Group 참조표

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

### 1.9.5 LDS 버전 차이

| 특성 | LDS 1.7 (v0) | LDS 1.8 (v1) |
|------|--------------|--------------|
| `LDSSecurityObject` version | v0 (0) | v1 (1) |
| `ldsVersionInfo` 필드 | 없음 | 있음 (선택적) |
| Unicode 버전 | 미지정 | 지정 |
| 하위 호환성 | -- | v0 지원 필수 |

---

## 1.10 ICAO PKD (Public Key Directory)

### 1.10.1 목적

**참조**: ICAO Doc 9303 Part 12, Section 10

ICAO PKD는 전자여권 PKI 자료를 배포하는 **글로벌 인프라**이다. ICAO가 운영하며, 회원국들이 인증서와 CRL을 공유할 수 있게 한다.

### 1.10.2 PKD Collection

| Collection | 내용 | LDIF 경로 |
|-----------|------|-----------|
| **Collection 001** | DSC 인증서 (적합) | `o=dsc,c={country},dc=data,...` |
| **Collection 002** | CSCA 인증서 + Master List | `o=csca,c={country},...` + `o=ml,...` |
| **Collection 003** | CRL (인증서 폐지 목록) | `o=crl,c={country},...` |

### 1.10.3 PKD LDAP DIT (Directory Information Tree)

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

### 1.10.4 LDAP ObjectClass

| ObjectClass | 용도 | 주요 속성 |
|-------------|------|-----------|
| `pkdDownload` | 인증서 (CSCA, DSC, MLSC) | `userCertificate;binary`, `cACertificate;binary` |
| `cRLDistributionPoint` | CRL | `certificateRevocationList;binary` |

### 1.10.5 Local PKD 아키텍처

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

## 1.11 암호화 요구사항

### 1.11.1 승인된 서명 알고리즘

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

### 1.11.2 해시 알고리즘

| 해시 알고리즘 | OID | 출력 크기 | 상태 |
|--------------|-----|----------|------|
| SHA-1 | `1.3.14.3.2.26` | 160 bits | **폐지** (하위 호환 검증만) |
| SHA-256 | `2.16.840.1.101.3.4.2.1` | 256 bits | **권장** |
| SHA-384 | `2.16.840.1.101.3.4.2.2` | 384 bits | 승인 |
| SHA-512 | `2.16.840.1.101.3.4.2.3` | 512 bits | 승인 |

### 1.11.3 키 크기 요구사항

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

### 1.11.4 알고리즘 전환 이력

1. **SHA-1 폐지**: 7판에서 폐지. 신규 eMRTD는 SHA-1 사용 금지. 단, 검사국은 유통 중인 이전 문서를 위해 SHA-1 검증을 지원해야 함.
2. **RSA-1024 폐지**: 최소 2048 bits로 상향.
3. **SHA-224 폐지**: 보안 마진 부족으로 비권장.
4. **마이그레이션**: ECDSA P-256+ 또는 RSA-3072+로의 전환 권장.

---

## 1.12 실제 Use Case 시나리오

### 1.12.1 시나리오 1: 정상적인 PA 검증

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

### 1.12.2 시나리오 2: CSCA 키 전환 (Link Certificate)

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

### 1.12.3 시나리오 3: 만료된 인증서 (Point-in-Time Validation)

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

### 1.12.4 시나리오 4: CSCA 미등록 국가

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

### 1.12.5 시나리오 5: DSC 폐지 (CRL Revocation)

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

### 1.12.6 시나리오 6: 데이터 변조 감지

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

### 1.12.7 시나리오 7: PA 검증을 통한 DSC 자동 등록

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

## 1.13 예외 처리 및 Edge Case

### 1.13.1 인증서 만료 관련

| 상황 | 처리 방법 | ICAO 지침 |
|------|----------|-----------|
| DSC 만료 | `EXPIRED_VALID` 상태 부여 | Point-in-Time Validation 적용 권고 |
| CSCA 만료 | 경고 포함 보고 | 여권 발급 시점 유효 여부 확인 |
| DSC + CSCA 모두 만료 | 여전히 서명 검증 수행 | 서명 유효 시 EXPIRED_VALID |
| DSC 미래 유효기간 (notBefore > now) | `NOT_YET_VALID` 상태 | 시스템 시계 오차 가능성 확인 |

### 1.13.2 CRL 관련 Edge Case

| 상황 | 처리 | 근거 |
|------|------|------|
| CRL 미존재 | Fail-open (경고와 함께 통과) | ICAO: CRL 확인은 SHOULD |
| CRL 만료 (nextUpdate 경과) | `CRL_EXPIRED` (경고와 함께 통과) | 만료 CRL로 폐지 상태 확인 불가 |
| Delta CRL | 미지원 (Full CRL만) | 대부분의 국가가 Full CRL 사용 |
| DSC 폐지 후 여권 계속 유통 | `REVOKED` → INVALID | 물리 여권 회수는 별도 프로세스 |
| CRL 서명 검증 실패 | `CRL_INVALID` (경고) | CRL 자체의 무결성 의심 |

### 1.13.3 Trust Chain 실패 사례

| 상황 | 원인 | 해결 |
|------|------|------|
| **CSCA 미발견** | 해당 국가 미등록 또는 PKD 미참가 | Master List 업로드, 양자 교환 |
| **서명 검증 실패** | DSC가 해당 CSCA로 서명되지 않음 | DN 매칭 로직 확인, LC 검색 |
| **다중 CSCA** | 한 국가에 여러 CSCA 활성 | 모든 CSCA 대상 매칭 시도 |
| **DN 형식 불일치** | OpenSSL `/C=X` vs LDAP `C=X,O=Y` | 형식 독립적 DN 비교 사용 |
| **중간 인증서 누락** | LC 없이 새 CSCA만 등록 | LC 포함 Master List 재업로드 |
| **알고리즘 미지원** | 시스템이 서명 알고리즘 미지원 | OpenSSL 업데이트, 알고리즘 추가 |

### 1.13.4 Self-signed 인증서 처리

- **정상**: CSCA는 반드시 self-signed (Issuer == Subject)
- **비정상**: DSC가 self-signed → CSCA로 잘못 분류될 수 있음
- **검증 방법**: BasicConstraints(CA:TRUE) + KeyUsage(keyCertSign) + Self-signed 조합으로 정확한 분류

### 1.13.5 시간대 및 유효기간 Edge Case

| 상황 | 주의사항 |
|------|----------|
| UTC vs 로컬 시간 | 모든 인증서 시각은 UTC 기준 |
| GeneralizedTime vs UTCTime | 2050년 이후는 GeneralizedTime 사용 |
| 윤초 | OpenSSL이 내부적으로 처리 |
| 유효기간 경계 (정확히 만료 시각) | `notAfter`는 포함(inclusive) |

### 1.13.6 Deviation List (편차 목록)

**목적**: ICAO 9303 규격을 완전히 준수하지 않지만 운용 중인 eMRTD의 알려진 편차를 기록

**주요 편차 유형**:
- DG 인코딩 오류 (BER vs DER)
- 잘못된 ASN.1 태그 사용
- 비표준 서명 알고리즘
- 누락된 필수 확장

**처리 방법**: 편차 목록과 대조하여 알려진 편차인 경우 허용 처리

---

## 1.14 구현 시 주의사항 (Pitfalls)

본 프로젝트 구현 경험에서 도출된 주요 함정들:

### 1.14.1 Master List 파싱

**함정 1: `CMS_get1_certs()`로 Master List 인증서 추출**

`CMS_get1_certs()`는 CMS의 `SignedData.certificates` 필드만 반환한다. ICAO Master List에서 이 필드는 **비어 있다**! CSCA 인증서들은 `encapContentInfo`(pkiData)에 저장되어 있다.

**해결**: `CMS_get0_SignerInfos()`(MLSC) + `CMS_get0_content()`(CSCA) 2단계 추출

**함정 2: MLSC 누락**

MLSC는 SignerInfo에**만** 존재한다. pkiData에서만 추출하면 MLSC를 놓친다.

### 1.14.2 DN 처리

**함정 3: DN 형식 불일치**

```
OpenSSL oneline: /C=LV/O=Ministry/CN=CSCA LV
LDAP 형식:       C=LV, O=Ministry, CN=CSCA LV
RFC 2253 형식:   CN=CSCA LV,O=Ministry,C=LV
```

**해결**: 모든 형식을 지원하는 DN 파싱 및 정규화 함수 사용

**함정 4: 대소문자 비교**

RFC 5280은 DN 비교 시 **대소문자 무시**를 요구한다. `LOWER(subject_dn) = LOWER(issuer_dn)` 사용.

### 1.14.3 인증서 유형 분류

**함정 5: Link Certificate 오분류**

LC와 CSCA 모두 `CA:TRUE` + `keyCertSign`을 가진다. **Self-signed 여부**가 유일한 차별점이다.

```
CA:TRUE + keyCertSign + Self-signed     → CSCA
CA:TRUE + keyCertSign + Non-self-signed → Link Certificate
```

### 1.14.4 국가 코드 추출

**함정 6: 잘못된 Fallback**

Master List에서 모든 인증서에 "UN"을 fallback 국가 코드로 사용하면, 모든 CSCA가 `c=UN` 아래 저장된다.

**해결**: Subject DN → Issuer DN → Entry DN → "XX" fallback 체인 사용

### 1.14.5 바이너리 데이터 처리

**함정 7: PostgreSQL BYTEA 인코딩**

`E''` (escape string literal)에 `\x` 접두사를 사용하면 PostgreSQL이 `\x`를 이스케이프 시퀀스로 해석하여 인증서 바이너리 데이터가 손상된다. 모든 Trust Chain 검증이 실패하게 된다.

**해결**: 표준 따옴표 사용 (`E''` 대신 `''`로 bytea hex 값 저장)

### 1.14.6 OpenSSL 메모리 관리

**함정 8: 메모리 누수**

OpenSSL 구조체(X509, CMS_ContentInfo, EVP_PKEY 등)는 명시적으로 해제해야 한다.

**해결**: RAII 패턴 적용, 모든 코드 경로에서 일관된 정리

### 1.14.7 SOD 래핑

**함정 9: ICAO 외부 태그**

일부 SOD에는 CMS 콘텐츠를 래핑하는 ICAO 전용 외부 태그(0x77)가 있다. CMS 파싱 전에 이 태그를 감지하고 제거해야 한다.

### 1.14.8 Oracle 호환성

**함정 10: Multi-DBMS 인증서 저장**

- Oracle은 빈 문자열을 NULL로 처리 (PostgreSQL과 다름)
- Oracle은 컬럼명을 UPPERCASE로 반환
- Oracle은 `NUMBER(1)`로 BOOLEAN 표현
- Oracle은 `OFFSET/FETCH`로 페이지네이션

**해결**: Query Executor 추상화 계층으로 데이터베이스별 처리

### 1.14.9 Trust Chain 깊이

**함정 11: 다중 레벨 Trust Chain**

```
일반적: DSC → CSCA (깊이 2)
키 전환: DSC → LC → CSCA (깊이 3)
다중 LC: DSC → LC_n → LC_n-1 → ... → CSCA_old (깊이 N)
```

**해결**: 깊이 제한이 있는 반복적 체인 빌딩으로 무한 루프 방지

### 1.14.10 CRL 처리 순서

**함정 12: CRL 만료 확인 순서**

CRL 만료 확인(`nextUpdate`)을 인증서 폐지 확인 **전에** 수행해야 한다. 만료된 CRL로는 폐지 상태를 신뢰할 수 없다.

---

## 1.15 참조 표준 및 문서

### 1.15.1 ICAO 문서

| 문서 | 제목 | 주요 내용 |
|------|------|-----------|
| **Doc 9303 Part 10** | LDS for Storage of Biometrics and Other Data in the Contactless IC | SOD 구조, DG 형식, LDS 버전 |
| **Doc 9303 Part 11** | Security Mechanisms for MRTDs | PA, AA, CA, TA, PACE |
| **Doc 9303 Part 12** | Public Key Infrastructure for MRTDs | CSCA, DSC, LC, Master List, PKD 운영, CRL 관리 |

### 1.15.2 RFC 표준

| RFC | 제목 | 관련성 |
|-----|------|--------|
| **RFC 5280** | Internet X.509 PKI Certificate and CRL Profile | 인증서 구조, 검증 규칙, CRL 처리 |
| **RFC 5652** | Cryptographic Message Syntax (CMS) | SOD 및 Master List SignedData 형식 |
| **RFC 4511** | LDAP Protocol | PKD LDAP 운영 |
| **RFC 4515** | LDAP String Representation of Search Filters | LDAP 필터 이스케이프 |
| **RFC 3279** | Algorithms and Identifiers for PKIX | 알고리즘 OID 참조 |
| **RFC 4055** | Additional Algorithms for RSA Cryptography in PKIX | RSA-PSS 등 |
| **RFC 5480** | ECC Subject Public Key Information | ECDSA 곡선 정의 |

### 1.15.3 BSI 기술 지침

| 문서 | 제목 | 관련성 |
|------|------|--------|
| **BSI TR-03105 Part 5** | Test Plan for eMRTD Application Protocol and LDS | SOD/DG/PA 적합성 테스트 |
| **BSI TR-03110** | Advanced Security Mechanisms for Machine Readable Travel Documents | EAC, PACE 프로토콜 상세 |
| **BSI TR-03111** | Elliptic Curve Cryptography | 전자여권 ECC 사양 |

### 1.15.4 구현 파일 매핑

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

# Part 2: Doc 9303 Compliance Checks

**원본**: `docs/Checks_Against_Doc9303_Applied_to_PKD_uploads.pdf`
**참조 표준**: ICAO Doc 9303, RFC 3280 (X.509), RFC 3852 (CMS), BSI TR-03110 (Brainpool/SHA-224)

---

## 2.1 개요

ICAO PKD에 업로드되는 **인증서(CSCA/DSC)**, **CRL**, **마스터리스트**가 Doc 9303 및 관련 RFC 표준을 준수하는지 검증하는 항목을 정의한다. 검증 대상은 3가지 범주로 나뉜다:

1. **인증서 속성** -- CSCA, DSC, 마스터리스트 서명자 인증서
2. **CRL 속성** -- 인증서 폐지 목록
3. **CSCA 마스터리스트 속성** -- CMS Signed Data 구조

---

## 2.2 인증서 속성 검증 (CSCA / DSC / 마스터리스트 서명자)

### 2.2.1 기본 필드

| # | 항목 | RFC 3280 | Doc 9303 / Operator Terminal | 비고 |
|---|------|----------|------------------------------|------|
| 1.1 | **버전 (Version)** | V3 필수 | V3 필수 | |
| 1.2 | **시리얼 번호 (Serial Number)** | 양수, 2의 보수 인코딩, 최소 옥텟 수, 최대 20옥텟 | 동일 | |
| 1.3 | **서명 알고리즘 (Signature)** | OID가 signatureAlgorithm 필드(항목 2)와 일치 | 동일 | |
| 1.4 | **발행자 (Issuer)** | UTF8String 권장 (country, serialNumber은 PrintableString) | 국가코드 필수. DSC/ML서명자: subject country = issuer country | 실제로는 PrintableString도 허용. 국가코드 대문자 검증 미적용 |
| 1.5-1.7 | **유효기간 (Validity)** | UTCTime: 2050년 미만, 13바이트, "Z" 종료. GeneralizedTime: 2050년 이상, 15바이트, "Z" 종료 | 동일 | |
| 1.8 | **주체 (Subject)** | 자체서명이면 Issuer=Subject. CA+Issuer!=Subject이면 링크 인증서 추정 | 국가코드 필수. 링크 인증서: DN 마지막 2개 요소 동일 | DN 비교로 교차인증서와 구분 |
| 1.9 | **공개키 정보 (Subject Public Key Info)** | -- | 존재 확인 | |
| 1.10 | **고유 식별자 (Unique Identifiers)** | 사용 비권장 | **사용 금지** (반드시 없어야 함) | |

### 2.2.2 확장 (Extensions)

| # | 확장 항목 | CSCA (자체서명/링크) | DSC | ML 서명자 | 비고 |
|---|-----------|---------------------|-----|-----------|------|
| 1.11.1 | **Authority Key Identifier** | Issuer!=Subject이면 필수, KeyIdentifier 최소 포함, **Non-critical** | 동일 | 동일 | |
| 1.11.2 | **Subject Key Identifier** | 자체서명/링크이면 필수, **Non-critical** | 무관 | 무관 | |
| 1.11.3 | **Key Usage** | **필수, Critical**. `keyCertSign` + `cRLSign`만 허용 | **필수, Critical**. `digitalSignature`만 허용 | `digitalSignature`만, **Critical** | 가장 엄격한 검증 항목 |
| 1.11.4 | **Private Key Usage Period** | **Non-critical**. CSCA에 존재 시 DSC 검증에 사용 | -- | -- | |
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
| -- | **Internet Certificate Extensions** | 사용 금지 | 무관 | 무관 | |
| -- | **기타 사설 확장 (Other Private)** | **Non-critical** | 무관 | 무관 | |

### 2.2.3 서명 알고리즘 (항목 2)

- `signatureAlgorithm` 필드는 TBSCertificate의 `signature` 필드(항목 1.3)와 반드시 일치해야 한다.

---

## 2.3 CRL (인증서 폐지 목록) 속성 검증

### 2.3.1 기본 필드

| # | 항목 | RFC 3280 | Operator Terminal | 비고 |
|---|------|----------|-------------------|------|
| 1.1 | **버전 (Version)** | v2 필수 | v2 필수 | |
| 1.2 | **서명 알고리즘 (Signature)** | signatureAlgorithm(항목 2)과 일치 | 필수 | |
| 1.3 | **발행자 (Issuer)** | UTF8String, 국가코드 ISO 3166 대문자 | 국가코드 필수 | UTF8 미강제 (PrintableString 허용) |
| 1.4 | **This Update** | -- | 필수 | UTCTime/GeneralizedTime 규칙 동일 |
| 1.5 | **Next Update** | -- | 필수 | |
| 1.6 | **폐지 인증서 목록 (Revoked Certificates)** | 폐지 인증서 없으면 목록 자체가 없어야 함 | 있으면 비어있으면 안 됨 | |

### 2.3.2 CRL 확장 (Extensions)

| # | 확장 항목 | 검증 내용 | 비고 |
|---|-----------|-----------|------|
| 1.7.1 | **Authority Key Identifier** | **필수**, KeyIdentifier 최소 포함 | |
| 1.7.2 | **Issuer Alternative Name** | 있으면 **Non-critical** | |
| 1.7.3 | **CRL Number** | **필수**, 최대 20옥텟, 2의 보수 인코딩 | |
| 1.7.4 | **Delta CRL Indicator** | **사용 금지** | 전자여권 PKD는 전체 CRL만 사용 |
| 1.7.5 | **Issuing Distribution Point** | **사용 금지** | |
| 1.7.6 | **Freshest CRL** | **사용 금지** | |

### 2.3.3 CRL 엔트리 확장 (CRL Entry Extensions)

| # | 항목 | 검증 내용 | 비고 |
|---|------|-----------|------|
| 1.8.1 | **Reason Code** | 있으면 **Non-critical** | Doc9303은 금지하지만 실무에서 허용 |
| 1.8.2 | **Hold Instruction Code** | 있으면 **Non-critical** | |
| 1.8.3 | **Invalidity Date** | 있으면 **Non-critical**, "Z" 종료 | |
| 1.8.4 | **Certificate Issuer** | **사용 금지** | |

### 2.3.4 서명

- `signatureAlgorithm` (항목 2)은 TBS CertList의 `signature` (항목 1.2)와 반드시 일치해야 한다.

---

## 2.4 CSCA 마스터리스트 (CMS Signed Data) 속성 검증

### 2.4.1 Signed Data 구조

| # | 항목 | RFC 3852 | Operator Terminal | 비고 |
|---|------|----------|-------------------|------|
| 1.1 | **버전 (Version)** | 콘텐츠에 따라 결정 | **v3 필수** | |
| 1.2 | **Digest Algorithm** | digest algorithm 컬렉션 | 필수 | |
| 1.3.1 | **eContent Type** | -- | `id-icao-cscaMasterList` OID | ICAO 전용 OID |
| 1.3.2 | **eContent** | -- | ML 서명자에 대응하는 CSCA 포함 | |
| 1.4 | **인증서 (Certificates)** | 루트→서명자 경로에 충분한 인증서 포함 | ML 서명자 인증서 포함 필수. EKU 존재 시 **Critical** + OID `2.23.136.1.1.3` | |
| 1.5 | **CRL** | -- | **포함 금지** | |
| 1.6 | **Signer Infos** | -- | 필수 | |

### 2.4.2 Signer Info 구조

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

## 2.5 핵심 설계 원칙 요약

### pathLength=0 의 의미

CSCA의 Basic Constraints에서 `pathLength=0`으로 설정되므로, 다음 확장들은 실질적 의미가 없다:
- Name Constraints (1.11.11)
- Policy Constraints (1.11.12)
- Inhibit Any-Policy (1.11.15)

이는 CSCA가 직접 DSC만 서명할 수 있고, 중간 CA를 허용하지 않음을 의미한다.

### 링크 인증서 (Link Certificate) 식별

- CA 비트가 설정되고 Issuer!=Subject인 인증서는 링크 인증서 또는 교차 인증서일 수 있다.
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

## 2.6 프로젝트 내 구현 매핑

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

---

# Part 3: Cryptographic Algorithm Support

**참조 표준**: BSI TR-03110 v2.20/2.21, ICAO Doc 9303 Part 11/12, RFC 5639 (Brainpool), RFC 4055/8017 (RSA-PSS)

---

## 3.1 개요

본 장은 ICAO Doc 9303과 BSI TR-03110의 암호 알고리즘 관계를 정리하고, ICAO Local PKD 시스템에서의 적용 방침을 기술한다.

ePassport에는 **두 개의 독립된 PKI 체계**가 존재한다:

| PKI | 인증서 형식 | 용도 | 배포 방식 | 본 시스템 관련 |
|-----|-----------|------|----------|--------------|
| **ICAO PKI** | X.509 v3 | Passive Authentication (SOD 서명 검증) | ICAO PKD (Master List, LDAP) | **직접 관리** |
| **EAC-PKI** | CVC (Card Verifiable Certificate) | Extended Access Control (지문/홍채) | 국가 간 양자 교환 | 관련 없음 |

본 시스템은 **ICAO PKI (X.509 CSCA/DSC)** 를 관리하므로, X.509 인증서에 등장하는 알고리즘만 검증 대상이다.

---

## 3.2 BSI TR-03110 구조

| Part | 내용 | X.509 CSCA/DSC 관련 |
|------|------|-------------------|
| **Part 1** (v2.20) | PA, AA, TA v1, CA v1 | 관련 -- Doc 9303과 동일 범위 |
| **Part 2** (v2.21) | PACE, CA v2/v3, TA v2, RI | 관련 없음 -- 칩 프로토콜 |
| **Part 3** (v2.21) | ASN.1/APDU 명세, OID 정의, 표준 곡선 테이블 | 참조용 |
| **Part 4** (v2.21) | 적합성 테스트 | 관련 없음 |

---

## 3.3 알고리즘 분류 체계

본 시스템은 X.509 CSCA/DSC 인증서에 사용된 알고리즘을 다음 3단계로 분류한다:

| 분류 | 수준 | 의미 | 예시 |
|------|------|------|------|
| **PASS** | Doc 9303 Part 12 충족 | 완전 적합 | SHA-256, P-256, RSA 2048+ |
| **WARNING** | BSI TR-03110 지원 또는 deprecated | 기능적으로 유효하나 Doc 9303 Part 12 외 | Brainpool, SHA-224, SHA-1 |
| **FAIL** | 미인식 알고리즘 | 표준 목록에 없음 | MD5, 커스텀 곡선 |

---

## 3.4 PKD 관련 알고리즘 상세

### 3.4.1 서명 알고리즘 (X.509 인증서에 등장)

표준 X.509/PKCS OID를 사용하며, BSI OID(`0.4.0.127.0.7.*`)는 CVC 전용이므로 대상 아님.

| 알고리즘 | OID | 분류 | 근거 |
|---------|-----|------|------|
| sha256WithRSAEncryption | 1.2.840.113549.1.1.11 | PASS | Doc 9303 Part 12 Appendix A |
| sha384WithRSAEncryption | 1.2.840.113549.1.1.12 | PASS | Doc 9303 Part 12 Appendix A |
| sha512WithRSAEncryption | 1.2.840.113549.1.1.13 | PASS | Doc 9303 Part 12 Appendix A |
| RSASSA-PSS (rsassaPss) | 1.2.840.113549.1.1.10 | PASS | Doc 9303 Part 12 Appendix A, 해시는 PSS 파라미터에서 추출 |
| ecdsa-with-SHA256 | 1.2.840.10045.4.3.2 | PASS | Doc 9303 Part 12 Appendix A |
| ecdsa-with-SHA384 | 1.2.840.10045.4.3.3 | PASS | Doc 9303 Part 12 Appendix A |
| ecdsa-with-SHA512 | 1.2.840.10045.4.3.4 | PASS | Doc 9303 Part 12 Appendix A |
| sha224WithRSAEncryption | 1.2.840.113549.1.1.14 | WARNING | BSI TR-03110 지원, Doc 9303 Part 12 외 |
| ecdsa-with-SHA224 | 1.2.840.10045.4.3.1 | WARNING | BSI TR-03110 지원, Doc 9303 Part 12 외 |
| sha1WithRSAEncryption | 1.2.840.113549.1.1.5 | WARNING | ICAO NTWG deprecated, SHA-256+ 전환 권고 |
| ecdsa-with-SHA1 | 1.2.840.10045.4.1 | WARNING | ICAO NTWG deprecated, SHA-256+ 전환 권고 |

### 3.4.2 ECDSA 곡선

| 곡선 | OID | 키 크기 | 분류 | 근거 |
|------|-----|--------|------|------|
| P-256 (prime256v1/secp256r1) | 1.2.840.10045.3.1.7 | 256bit | PASS | Doc 9303 Part 12 |
| P-384 (secp384r1) | 1.3.132.0.34 | 384bit | PASS | Doc 9303 Part 12 |
| P-521 (secp521r1) | 1.3.132.0.35 | 521bit | PASS | Doc 9303 Part 12 |
| brainpoolP256r1 | 1.3.36.3.3.2.8.1.1.7 | 256bit | WARNING | BSI TR-03110, RFC 5639 |
| brainpoolP384r1 | 1.3.36.3.3.2.8.1.1.11 | 384bit | WARNING | BSI TR-03110, RFC 5639 |
| brainpoolP512r1 | 1.3.36.3.3.2.8.1.1.13 | 512bit | WARNING | BSI TR-03110, RFC 5639 |

> **참고**: ICAO Doc 9303 Part 12는 CSCA/DSC에 **explicit EC parameters** (named curve OID 대신 p, a, b, G, n, h 전체)를 의무화한다. Brainpool 사용 국가(독일 등)의 인증서는 explicit parameters로 인코딩되며, OpenSSL `EC_GROUP_get_curve_name()`이 named curve로 매핑하지 못할 수 있다.

### 3.4.3 RSA-PSS 해시 알고리즘 추출

RSA-PSS(`1.2.840.113549.1.1.10` / `NID_rsassaPss`)는 기존 RSA/ECDSA와 달리 해시 알고리즘이 **서명 알고리즘 OID에 내장되지 않고**, PSS 파라미터(ASN.1 SEQUENCE) 안에 별도 지정된다:

```
RSASSA-PSS-params ::= SEQUENCE {
    hashAlgorithm      [0] HashAlgorithm    DEFAULT sha1,
    maskGenAlgorithm   [1] MaskGenAlgorithm DEFAULT mgf1SHA1,
    saltLength         [2] INTEGER          DEFAULT 20,
    trailerField       [3] INTEGER          DEFAULT 1
}
```

**ePassport에서 사용되는 RSA-PSS 해시:**

| PSS 파라미터 내 해시 | Hash OID | OpenSSL NID | Doc 9303 분류 |
|---|---|---|---|
| SHA-256 | 2.16.840.1.101.3.4.2.1 | `NID_sha256` | PASS |
| SHA-384 | 2.16.840.1.101.3.4.2.2 | `NID_sha384` | PASS |
| SHA-512 | 2.16.840.1.101.3.4.2.3 | `NID_sha512` | PASS |
| SHA-224 | 2.16.840.1.101.3.4.2.4 | `NID_sha224` | WARNING (BSI TR-03110) |
| SHA-1 | 1.3.14.3.2.26 | `NID_sha1` | WARNING (deprecated, RFC 8017 기본값) |

**OpenSSL C/C++ 추출 방법:**
1. `X509_get0_tbs_sigalg(cert)` → `X509_ALGOR*`
2. `X509_ALGOR_get0()` → `V_ASN1_SEQUENCE` 파라미터 획득
3. `d2i_RSA_PSS_PARAMS()` → `RSA_PSS_PARAMS*` 디코딩
4. `pss->hashAlgorithm->algorithm` → `OBJ_obj2nid()` → 해시 NID 확인
5. `hashAlgorithm` 필드가 없으면 기본값 SHA-1 (RFC 8017)
6. `RSA_PSS_PARAMS_free()` 해제 필수

> **참고**: 본 시스템의 SE/PH/CN 등 국가 CSCA 인증서 3,019개가 RSA-PSS를 사용하며, 대부분 SHA-256 또는 SHA-512를 PSS 해시로 지정한다.

### 3.4.4 RSA 키 크기

| 키 크기 | 분류 | 근거 |
|--------|------|------|
| 2048bit 이상 | PASS | Doc 9303 Part 12 최소 요구사항 |
| 3072bit 이상 | PASS (권고 충족) | Doc 9303 Part 12 권장 |
| 2048bit 미만 | FAIL | Doc 9303 Part 12 최소 미달 |
| 4096bit 초과 | WARNING | 권장 최대 초과 (실무 제한 아님) |

---

## 3.5 PKD와 무관한 알고리즘 (CVC/칩 프로토콜 전용)

BSI OID(`0.4.0.127.0.7.*`)를 사용하는 다음 항목들은 X.509 CSCA/DSC에 **등장하지 않으므로** 본 시스템에서 처리하지 않는다:

| 프로토콜 | BSI OID | 용도 |
|---------|---------|------|
| id-PACE-* | 0.4.0.127.0.7.2.2.4.* | PACE 세션 키 설정 (BAC 대체) |
| id-CA-* | 0.4.0.127.0.7.2.2.3.* | Chip Authentication 키 합의 |
| id-TA-* | 0.4.0.127.0.7.2.2.2.* | Terminal Authentication CVC 서명 |
| id-RI-* | 0.4.0.127.0.7.2.2.5.* | Restricted Identification |

이들은 EAC-PKI(CVC 기반)에서 사용되며, ICAO PKD와는 별도의 인프라이다.

---

## 3.6 Brainpool 곡선 상세

### 3.6.1 배경

- **정의**: RFC 5639 (Elliptic Curve Cryptography Brainpool Standard Curves and Curve Generation)
- **개발**: 독일 BSI 주도, ECC Brainpool 워킹그룹
- **특징**: NIST 곡선과 달리 곡선 파라미터 생성 과정이 완전 공개 (verifiably random)

### 3.6.2 실제 사용 국가

독일(DE)을 포함한 유럽 국가들이 CSCA/DSC 인증서에 Brainpool 곡선을 사용한다. 이들은 ICAO PKD에 정상 등록되어 있으며, Passive Authentication에서 유효하게 검증된다.

- **독일 CSCA**: `C=DE, O=bund, OU=bsi, CN=csca-germany` -- BSI 운영, Brainpool 곡선 사용
- BSI TR-03116 Part 3 Chapter 4.1에 따라 Brainpool 권장

### 3.6.3 NIST vs Brainpool 비교

| 속성 | NIST (P-256/384/521) | Brainpool (P256r1/P384r1/P512r1) |
|------|---------------------|----------------------------------|
| 표준 | FIPS 186-4, SEC 2 | RFC 5639 |
| 파라미터 생성 | 비공개 seed | Verifiably random (공개) |
| Doc 9303 Part 12 | 명시적 포함 | 미포함 (BSI TR-03110 참조) |
| ICAO PKD 등록 | 지원 | 지원 (실제 등록 인증서 존재) |
| 보안 수준 비교 | P-256 ~ 128bit | brainpoolP256r1 ~ 128bit |

### 3.6.4 본 시스템 적용 방침

Brainpool 곡선은 `keySizeCompliant = false`(FAIL)가 아닌 **WARNING** 수준으로 분류한다:

1. ICAO PKD에 실제 등록된 유효 인증서들이 사용
2. BSI TR-03110 Part 1에서 Passive Authentication에 사용 가능
3. Doc 9303 Part 11이 BSI TR-03110을 참조
4. 보안 수준이 NIST 곡선과 동등

---

## 3.7 SHA-1 Deprecated 처리

### 3.7.1 ICAO NTWG 권고

- SHA-1은 ICAO NTWG(New Technologies Working Group)에 의해 단계적 폐지 권고
- 2017년 SHA-1 충돌 공격(SHAttered) 이후 마이그레이션 가속
- 기존 SHA-1 인증서는 여전히 ICAO PKD에 존재 (레거시)

### 3.7.2 본 시스템 적용 방침

- **SHA-1 서명 인증서**: WARNING (FAIL이 아님)
  - 이유: 기존 발급 인증서 유효기간 내 폐기 불가, 기능적으로 서명 검증 가능
  - 메시지: "SHA-1 지원 중단 예정 (ICAO NTWG 권고, SHA-256+ 전환 필요)"
- **SHA-224**: WARNING
  - BSI TR-03110에서 지원하나 Doc 9303 Part 12 기본 목록에 미포함
  - 메시지: "SHA-224 BSI TR-03110 지원 (Doc 9303 Part 12 외)"

---

## 3.8 구현 파일 매핑

### 3.8.1 해시 알고리즘 추출 (서명 알고리즘 → 해시)

| 파일 | 방식 | RSA-PSS | SHA-224 | SHA-1 (old OID) |
|------|------|---------|---------|-----------------|
| `services/common-lib/src/x509/metadata_extractor.cpp` | NID switch/case | PSS 파라미터 파싱 | 지원 | `NID_sha1WithRSA` 지원 |
| `services/pkd-management/src/common/x509_metadata_extractor.cpp` | 문자열 매칭 + PSS 폴백 | PSS 파라미터 파싱 | 지원 | 문자열 "sha1" 지원 |

### 3.8.2 ICAO 준수 검사

| 파일 | 역할 | RSA-PSS 처리 |
|------|------|-------------|
| `shared/lib/icao-validation/src/algorithm_compliance.cpp` | PA 검증 시 서명 알고리즘 + 곡선 검사 | NID 레벨 승인 (PSS 내 해시 미확인) |
| `services/pkd-management/src/common/progress_manager.cpp` | 업로드 시 ICAO 적합성 검사 (6개 카테고리) | 해시 문자열 레벨 검사 (PSS 해시 정확 판정) |
| `services/pkd-management/src/common/doc9303_checklist.cpp` | Doc 9303 체크리스트 (~28개 항목) | 해시 문자열 레벨 검사 (PSS 해시 정확 판정) |

### 3.8.3 프론트엔드

| 파일 | 역할 |
|------|------|
| `frontend/src/i18n/locales/ko/certificate.json` | 위반 메시지 한국어 번역 |
| `frontend/src/i18n/locales/en/certificate.json` | 위반 메시지 영어 번역 |
| `frontend/src/components/IcaoViolationDetailDialog.tsx` | 위반 상세 다이얼로그 번역 패턴 |

---

## 3.9 참고 문헌

1. **ICAO Doc 9303 Part 12** -- PKI for Machine Readable Travel Documents
2. **ICAO Doc 9303 Part 11** -- Security Mechanisms for MRTDs
3. **BSI TR-03110 Part 1** (v2.20) -- eMRTDs and Similar Applications
4. **BSI TR-03110 Part 3** (v2.21) -- Common Specifications (OID, ASN.1, domain parameters)
5. **RFC 5639** -- Elliptic Curve Cryptography (ECC) Brainpool Standard Curves and Curve Generation
6. **BSI TR-03116 Part 3** -- ePassport Cryptographic Mechanisms (독일 CSCA 운영 기준)
7. **RFC 3279** -- Algorithms and Identifiers for X.509 PKI
8. **RFC 4055** -- Additional Algorithms for RSA-PSS and RSA-OAEP

---

# Part 4: PKD Certificate Compliance Analysis

**분석 대상**: ICAO PKD Download collection-003 (31,212 인증서)

---

## 4.1 개요

ICAO PKD에서 공식 배포하는 인증서들을 업로드 후 Doc 9303 Part 12 기술 규격 준수 여부를 검사한 결과, 상당수 인증서에서 미준수 항목이 발견되었다. 이 장은 그 원인을 분석하고 운영 시 고려사항을 정리한다.

## 4.2 분석 결과 요약

| 검증 카테고리 | 위반 유형 | 심각도 | 주요 원인 |
|---|---|---|---|
| 알고리즘 | SHA-1 해시 알고리즘 사용 | HIGH | 2010년 이전 발급 인증서 |
| 키 크기 | RSA 1024/1536비트 | HIGH | 초기 ePassport 시기 인증서 |
| Key Usage | digitalSignature 누락(CSCA) | MEDIUM | Doc 9303 해석 차이 |
| Basic Constraints | 확장 필드 누락 | MEDIUM | 일부 국가 PKI 구현 차이 |
| 유효기간 | CSCA 15년/DSC 3년 초과 | LOW | SHOULD 권고 미준수 |
| DN 형식 | Country(C) 속성 누락 | LOW | 초기 인증서 형식 |

## 4.3 SHALL vs SHOULD 구분

Doc 9303 Part 12는 RFC 2119 용어를 사용하여 요구사항 수준을 구분한다:

### 4.3.1 SHALL (필수) 위반 -- 보안 위험

- **SHA-1 해시 알고리즘**: Doc 9303은 SHA-224 이상을 **SHALL** 로 요구. SHA-1은 충돌 공격 가능성이 입증되어 보안 위험 존재
- **RSA 1024비트 키**: 최소 2048비트 **SHALL** 요구. 1024비트는 현재 기술로 인수분해 가능
- **Key Usage 확장**: CSCA는 `keyCertSign` + `cRLSign`, DSC는 `digitalSignature` **SHALL** 포함

### 4.3.2 SHOULD (권고) 미준수 -- 운영 참고

- **CSCA 유효기간 15년 초과**: Doc 9303 Section 7.1.1에서 SHOULD로 권고. 일부 국가는 20~30년 유효기간 사용
- **DSC 유효기간 3년 초과**: Section 7.1.2에서 SHOULD로 권고. 실제 5~10년 사용 사례 존재
- **Basic Constraints 확장**: CSCA에서 CA=TRUE, pathLenConstraint=0이 SHOULD. 일부 초기 인증서에서 누락

## 4.4 ICAO PKD의 역할과 한계

### 4.4.1 PKD는 배포 플랫폼

ICAO PKD는 각국이 제출한 인증서를 **있는 그대로 배포**하는 플랫폼이다. PKD가 인증서의 기술 준수 여부를 검증하여 거부하지 않는다.

> "The PKD is a distribution mechanism, not a regulatory enforcement tool."

### 4.4.2 dc=data vs dc=nc-data

- **dc=data**: 정규 인증서 저장소. 기본적으로 적합한 인증서로 분류되나, 기술 세부사항에서 Doc 9303 권고와 차이가 있을 수 있음
- **dc=nc-data**: ICAO가 **명시적으로** 표준 미준수로 분류한 DSC 인증서 (502건). `pkdConformanceCode`로 사유 기록

### 4.4.3 2021년 정책 변경

ICAO는 2021년부터 nc-data 분류를 폐기하는 방향으로 정책 변경:
- PKD를 규제 도구가 아닌 배포 플랫폼으로 재정의
- 인증서 적합성 판단은 **검증 시스템(Inspection System)**의 책임으로 이관
- 실제로 nc-data에 새로운 인증서가 추가되지 않고 있음

## 4.5 미준수 발생 원인

### 4.5.1 레거시 인증서

ePassport 시스템은 2004년부터 운영되었으나, Doc 9303 Part 12의 기술 프로파일은 이후 여러 차례 개정되었다. 초기 발급 인증서는 당시 기준에는 적합했으나 현재 기준에서는 미준수로 판정될 수 있다.

- **SHA-1 인증서**: 2015년 이전 발급된 CSCA/DSC에서 주로 발견
- **RSA 1024비트**: 2010년 이전 발급 인증서
- **RSA 1536비트**: 과도기(2008-2012) 인증서

### 4.5.2 국가별 PKI 구현 차이

195+ 국가가 독립적으로 PKI를 운영하며, Doc 9303 해석과 구현에 차이가 존재한다:

- **Key Usage 해석**: 일부 국가는 CSCA에 `keyCertSign`만 설정하고 `cRLSign` 누락
- **Basic Constraints**: DSC에 확장 자체를 포함하지 않는 국가 존재 (Doc 9303은 CA=FALSE 권고)
- **유효기간**: 국가 보안 정책에 따라 ICAO 권고보다 긴 유효기간 채택

### 4.5.3 SHOULD 해석의 유연성

Doc 9303의 SHOULD 항목은 강제가 아닌 권고이므로, 미준수가 곧 "잘못된 인증서"를 의미하지 않는다. 각국의 보안 정책과 운영 환경에 따라 합리적인 편차가 존재한다.

## 4.6 운영 시 권장사항

### 4.6.1 검증 시스템에서의 처리

| 위반 유형 | 권장 처리 |
|---|---|
| SHA-1 알고리즘 | WARNING -- 보안 위험 경고, 거부하지는 않음 |
| RSA < 2048비트 | WARNING -- 키 강도 부족 경고 |
| Key Usage 누락 | WARNING -- 확장 필드 미비 |
| 유효기간 초과 | INFO -- 참고 정보 (SHOULD 항목) |
| Basic Constraints 누락 | INFO -- 참고 정보 |

### 4.6.2 Passive Authentication에서의 영향

- SHA-1/RSA-1024 인증서라도 PA 검증 자체는 가능 (서명 알고리즘만 지원하면 됨)
- PA 검증 결과에 알고리즘 강도 경고를 추가 정보로 포함
- 최종 판단은 검증 시스템 운영 정책에 따름

## 4.7 검증 카테고리별 세부 체크 항목

### A. 알고리즘 검증
- 서명 해시 알고리즘: SHA-224, SHA-256, SHA-384, SHA-512 (ICAO 승인)
- 공개키 알고리즘: RSA, ECDSA, RSA-PSS (ICAO 승인)

### B. 키 크기 검증
- RSA: 최소 2048비트 (SHALL), 최대 4096비트 (권고)
- ECDSA: 최소 224비트, 승인 곡선 (P-256, P-384, P-521, brainpool)

### C. Key Usage 검증
- CSCA: keyCertSign + cRLSign (SHALL)
- DSC: digitalSignature (SHALL)
- MLSC: digitalSignature (SHALL)

### D. 확장 필드 검증
- Basic Constraints 존재 여부
- Key Usage 확장 존재 여부
- CSCA: CA=TRUE (SHOULD)
- DSC: CA=FALSE (SHOULD)

### E. 유효기간 검증
- CSCA: 15년 이하 (SHOULD)
- DSC: 3년 이하 (SHOULD)

### F. DN 형식 검증
- Subject DN 존재 여부
- Country(C) 속성 포함 여부

---

# Part 5: Non-Conformant DSC Handling

---

## 5.1 개요

ICAO PKD는 Document Signer Certificate(DSC)를 **준수(Conformant)**와 **비준수(Non-Conformant)**로 분류한다. 비준수 DSC(DSC_NC)는 ICAO Doc 9303 기술 사양을 완전히 충족하지 않지만, 실제 여권 서명에 사용된 유효한 인증서이다.

### 핵심 사실

- DSC_NC는 기술적 비준수이지, 보안적으로 무효한 것이 아니다
- ICAO는 2021년에 nc-data 브랜치를 deprecated (신규 업로드 중단)
- 기존 ~502개 DSC_NC는 여전히 유효하며, 실제 유통 중인 여권에 사용됨
- Receiving State(수신국)가 nc-data 임포트 여부를 결정

---

## 5.2 ICAO PKD 컬렉션 구조

| 컬렉션 | 내용 | LDAP 경로 | 상태 |
|--------|------|-----------|------|
| icaopkd-001 | CSCA, DSC, CRL (준수) | `dc=data` | Active |
| icaopkd-002 | Master Lists | `dc=data` | Active |
| icaopkd-003 | DSC_NC (비준수) | `dc=nc-data` | Deprecated (2021) |

### LDAP DIT 구조

```
dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
├── dc=data                          <- icaopkd-001, 002
│   └── c={COUNTRY}
│       ├── o=csca   (CSCA 인증서)
│       ├── o=dsc    (준수 DSC)
│       ├── o=crl    (CRL)
│       ├── o=mlsc   (Master List Signer)
│       └── o=ml     (Master Lists)
└── dc=nc-data                       <- icaopkd-003 (deprecated)
    └── c={COUNTRY}
        └── o=dsc    (비준수 DSC_NC)
```

### DSC_NC LDAP 속성

| 속성 | 설명 | 예시 |
|------|------|------|
| `pkdConformanceCode` | 비준수 사유 코드 | `ERR:CSCA.CDP.14` |
| `pkdConformanceText` | 비준수 사유 설명 | `The Subject Public Key Info field does not contain an rsaEncryption OID` |
| `pkdVersion` | PKD 버전 | `90` |
| `userCertificate;binary` | 인증서 바이너리 (DER) | - |

---

## 5.3 비준수 사유 분류

ICAO PKD가 정의하는 주요 비준수 사유:

| 코드 패턴 | 분류 | 설명 |
|-----------|------|------|
| `ERR:CSCA.*` | CSCA 관련 | CSCA 인증서 형식/알고리즘 비준수 |
| `ERR:DSC.*` | DSC 자체 | DSC 인증서 형식/알고리즘 비준수 |
| `ERR:*.CDP.*` | CRL 배포점 | CRL Distribution Point 관련 이슈 |
| `ERR:*.SIG.*` | 서명 알고리즘 | 서명 알고리즘이 ICAO 권장 목록에 없음 |
| `ERR:*.KEY.*` | 공개키 | 공개키 형식/크기 비준수 |

---

## 5.4 다른 NPKD 구현체의 처리 방식

### Keyfactor NPKD (상용 솔루션)

- **conformant + non-conformant 모두 다운로드**
- Receiving State가 nc-data 임포트 여부를 결정하도록 설계
- 인증서 분류 표시: "Conformance" 상태 필드 제공

### German BSI (연방정보보안청)

- CSCA/DSC/CRL만 필수 다운로드
- nc-data는 선택적 임포트
- PA 검증 시 nc-data 포함 여부는 정책 기반

### 일반 권장 사항 (ICAO 커뮤니티)

- DSC_NC는 실제 여권에 사용된 인증서이므로 **수집은 권장**
- PA 검증 시 DSC_NC도 신뢰 체인 검증 대상에 포함해야 함
- 비준수 상태는 정보성으로 표시 (검증 통과/실패와 별개)

---

## 5.5 현 시스템의 DSC_NC 처리 아키텍처

### 5.5.1 데이터 저장

| 저장소 | 준수 DSC | 비준수 DSC_NC |
|--------|---------|--------------|
| DB `certificate` 테이블 | `certificate_type = 'DSC'` | `certificate_type = 'DSC_NC'` |
| LDAP `dc=data` | `o=dsc,c={country},dc=data,...` | - |
| LDAP `dc=nc-data` | - | `o=dsc,c={country},dc=nc-data,...` |

- DB 복합 유니크 인덱스: `(certificate_type, fingerprint_sha256)` → 동일 fingerprint로 DSC와 DSC_NC 별도 저장 가능
- LDAP은 DIT 경로로 자연스럽게 분리

### 5.5.2 PA Lookup (PKD Management - DB 기반)

```
POST /api/certificates/pa-lookup
-> ValidationRepository.findBySubjectDn() / findByFingerprint()
  -> DB 조회 (DSC, DSC_NC 모두 포함)
  -> certificateType이 "DSC_NC"이면 enrichWithConformanceData() 호출
    -> LDAP nc-data에서 pkdConformanceCode/Text/Version 조회
  -> 응답에 conformance 정보 포함
```

### 5.5.3 PA Verify (PA Service - LDAP 기반)

```
POST /api/pa/verify
-> SOD에서 DSC 추출 (LDAP 조회 아님)
-> CSCA 조회: dc=data (o=csca + o=lc) <- CSCAs는 항상 dc=data에만 존재
-> Trust Chain 검증 (DSC 서명 -> CSCA 공개키)
-> CRL 검사: dc=data (o=crl)
-> DSC Conformance 체크: checkDscConformance()
  -> DSC fingerprint 계산 (SHA-256)
  -> LDAP nc-data에서 해당 fingerprint 검색
  -> 존재하면 dscNonConformant=true + conformance 데이터
-> 응답에 conformance 정보 포함 (certificateChainValidation 내)
```

### 5.5.4 DSC LDAP 검색 (fallback)

```
LdapCertificateRepository.findDscBySubjectDn()
-> dc=data 검색 (conformant DSC)
-> 없으면 dc=nc-data 검색 (non-conformant DSC_NC)
-> isNonConformant 출력 파라미터로 구분
```

---

## 5.6 중요 설계 결정

### Q: PA Verify에서 DSC_NC가 검증에 실패하면?

**A**: DSC가 non-conformant라는 것은 ICAO 기술 사양 비준수를 의미하지, 인증서 자체가 무효함을 의미하지 않는다. PA 검증 결과(VALID/INVALID)는 trust chain, 서명 검증, CRL 상태에 따라 결정되며, conformance 상태와 독립적이다.

### Q: dc=data와 dc=nc-data에 같은 DSC가 존재할 수 있는가?

**A**: ICAO PKD 운영상 동일 인증서가 001(conformant)과 003(non-conformant)에 동시 등록되지 않는다. 단, PA auto-registration(`PA_EXTRACTED` source)으로 SOD에서 추출한 DSC가 DB에 별도 등록될 수 있으며, 이 경우 DB에는 같은 fingerprint로 DSC(PA_EXTRACTED)와 DSC_NC(LDIF_PARSED)가 공존한다.

### Q: Reconciliation(DB-LDAP 동기화)에서 DSC_NC는?

**A**: ICAO가 2021년에 nc-data를 deprecated했으므로, Reconciliation 범위에서 DSC_NC는 제외한다. DSC_NC의 LDAP 동기화 상태(`stored_in_ldap`)는 업로드 시 설정되며, 이후 변경하지 않는다.

---

## 5.7 운영 데이터 현황

| 항목 | 수량 |
|------|------|
| 전체 DSC_NC | 502 |
| DSC_NC 국가 수 | ~30개국 |
| LDAP nc-data 저장 | 502 (100%) |
| DB certificate 저장 | 502 (certificate_type='DSC_NC') |

---

## 5.8 관련 파일

### Backend (PA Service)
- `services/pa-service/src/repositories/ldap_certificate_repository.h` - `checkDscConformance()`, `DscConformanceInfo`
- `services/pa-service/src/repositories/ldap_certificate_repository.cpp` - nc-data LDAP 조회 구현
- `services/pa-service/src/domain/models/certificate_chain_validation.h` - `dscNonConformant`, `pkdConformanceCode/Text`
- `services/pa-service/src/services/certificate_validation_service.cpp` - conformance 체크 호출

### Backend (PKD Management)
- `services/pkd-management/src/repositories/validation_repository.h` - `enrichWithConformanceData()`
- `services/pkd-management/src/repositories/validation_repository.cpp` - PA Lookup conformance LDAP 보조 조회

### Frontend
- `frontend/src/pages/PAVerify.tsx` - PA Verify/Lookup 결과에 Non-Conformant 경고 표시

---

# Part 6: Data Protection & PII Encryption

---

## 6.1 법적 근거

### 6.1.1 개인정보 보호법 (법률 제19234호)

#### 제24조 (고유식별정보의 처리 제한)

> 개인정보처리자가 고유식별정보를 처리하는 경우에는 그 고유식별정보가 분실 도난 유출 위조 변조 또는 훼손되지 아니하도록 대통령령으로 정하는 바에 따라 **암호화 등 안전성 확보에 필요한 조치**를 하여야 한다. (제3항)

**고유식별정보의 범위** (시행령 제19조):

| 정보 유형 | 고유식별정보 여부 | 근거 |
|-----------|:---:|------|
| 주민등록번호 | **O** | 시행령 제19조 제1호 |
| **여권번호** | **O** | **시행령 제19조 제2호** |
| 운전면허번호 | **O** | 시행령 제19조 제3호 |
| 외국인등록번호 | **O** | 시행령 제19조 제4호 |

> **핵심**: 본 프로젝트에서 PA 검증 시 처리되는 **여권번호(document_number)**는 고유식별정보에 해당하며, **저장 및 전송 시 암호화가 법적 의무**임.

#### 제29조 (안전조치의무)

> 개인정보처리자는 개인정보가 분실 도난 유출 위조 변조 또는 훼손되지 아니하도록 내부 관리계획 수립, 접근 통제 및 접근권한 제한 조치, **개인정보의 암호화**, 접속기록의 보관 및 위조 변조 방지 조치, 보안프로그램의 설치 및 갱신 등 대통령령으로 정하는 바에 따라 안전성 확보에 필요한 조치를 하여야 한다.

### 6.1.2 개인정보 보호법 시행령

#### 시행령 제21조 (고유식별정보의 안전성 확보 조치)

고유식별정보(여권번호 포함)를 처리하는 경우:
- **정보통신망을 통하여 송수신할 때**: 암호화 필수
- **저장할 때**: 암호화 필수 (encryption at rest)

#### 시행령 제30조 (개인정보의 안전성 확보 조치)

개인정보처리자가 제29조에 따라 취해야 할 안전성 확보 조치의 세부 기준:
1. 내부 관리계획의 수립 시행
2. **접근 통제 및 접근권한 제한 조치**
3. **개인정보의 암호화**
4. 접속기록의 보관 및 점검
5. 보안프로그램의 설치 갱신

### 6.1.3 개인정보의 안전성 확보조치 기준 (고시 제2021-2호)

#### 고시 제7조 (개인정보의 암호화)

| 대상 | 저장 시 (at rest) | 전송 시 (in transit) |
|------|:---:|:---:|
| 비밀번호 | 일방향 해시 (SHA-256+) | - |
| **여권번호** (고유식별정보) | **암호화 필수** | **암호화 필수** (TLS) |
| 생체인식정보 (민감정보) | **암호화 필수** | **암호화 필수** |
| 일반 개인정보 (성명, IP 등) | 권고 | **TLS 권고** |

#### 허용 암호 알고리즘 (KISA 기준)

| 용도 | 알고리즘 | 최소 키 길이 |
|------|----------|:---:|
| 대칭키 암호화 | **AES**, ARIA, SEED, LEA | 128비트 |
| 해시 (비밀번호) | **SHA-256**, SHA-384, SHA-512 | - |
| 공개키 암호화 | RSA | 2048비트 |
| 전송 구간 보호 | **TLS 1.2** 이상 | - |

> **사용 금지**: DES, 3DES, RC4, MD5, SHA-1 (취약 알고리즘)

---

## 6.2 프로젝트 내 개인정보 현황

### 6.2.1 개인정보 분류

| 데이터 | 위치 | 분류 | 암호화 의무 | 적용 상태 |
|--------|------|------|:---:|:---:|
| **여권번호** (`document_number`) | `pa_verification` | **고유식별정보** | **필수** | **적용 완료** |
| 요청자 성명 (`requester_name`) | `api_client_requests` | 일반 개인정보 | 권고 | **적용 완료** |
| 요청자 소속 (`requester_org`) | `api_client_requests` | 일반 개인정보 | 권고 | **적용 완료** |
| 요청자 이메일 (`requester_contact_email`) | `api_client_requests` | 일반 개인정보 | 권고 | **적용 완료** |
| 요청자 전화번호 (`requester_contact_phone`) | `api_client_requests` | 일반 개인정보 | 권고 | **적용 완료** |
| 클라이언트 IP (`client_ip`) | `pa_verification` | 일반 개인정보 | 권고 | **적용 완료** |
| User-Agent (`user_agent`) | `pa_verification` | 일반 개인정보 | 권고 | **적용 완료** |
| 비밀번호 (`password_hash`) | `users` | - | 일방향 해시 | **적용 완료** (bcrypt) |
| API Key (`api_key_hash`) | `api_clients` | - | 일방향 해시 | **적용 완료** (SHA-256) |

### 6.2.2 암호화 미적용 (의무 아님)

| 데이터 | 위치 | 사유 |
|--------|------|------|
| 인증서 데이터 (CSCA/DSC/CRL) | `certificate`, LDAP | 공개키 기반 **공개 데이터** -- 개인정보 비해당 |
| 인증서 Subject DN | `certificate` | 기관/국가 DN -- 일반적으로 개인 식별 불가 |
| 감사 로그 IP/User-Agent | `auth_audit_log`, `operation_audit_log` | **접속기록** -- 고시 제8조에 따라 보관 의무 (암호화 의무 아님) |
| 국가 코드 | `pa_verification` | 단독으로 개인 식별 불가 |

---

## 6.3 기술 구현

### 6.3.1 암호화 방식

**AES-256-GCM** (Authenticated Encryption with Associated Data)

| 항목 | 값 |
|------|-----|
| 알고리즘 | AES-256 (KISA 승인, 고시 제7조 충족) |
| 모드 | GCM (Galois/Counter Mode) |
| 키 길이 | 256비트 (32바이트) |
| IV (초기화 벡터) | 96비트 (12바이트) -- NIST SP 800-38D 권고 |
| 인증 태그 | 128비트 (16바이트) |
| IV 생성 | `RAND_bytes()` -- 매 암호화마다 랜덤 생성 |

**AES-256-GCM을 선택한 이유**:
- **기밀성 + 무결성 동시 보장**: GCM 모드는 암호화와 동시에 인증 태그를 생성하여 데이터 변조 감지 가능
- **KISA 승인 알고리즘**: 암호 알고리즘 및 키 길이 이용 안내서에서 AES-256 권장
- **NIST 표준**: SP 800-38D에서 GCM 모드 표준화
- **성능**: CTR 기반으로 병렬 처리 가능, CBC 대비 우수한 성능

### 6.3.2 저장 형식

```
ENC:<hex(IV[12bytes] + ciphertext[N bytes] + tag[16bytes])>
```

- **접두사** `ENC:` -- 암호화된 값과 평문을 구분 (하위 호환성)
- **IV**: 12바이트 랜덤 값 (hex 24자)
- **Ciphertext**: 평문과 동일 길이
- **Tag**: GCM 인증 태그 16바이트 (hex 32자)
- **총 오버헤드**: `4 + (12 + N + 16) * 2` = `60 + 2N` 문자

**예시**: "홍길동" (9바이트 UTF-8) → `ENC:` + 74자 hex = 총 78자

### 6.3.3 키 관리

```bash
# 키 생성 (32바이트 = 64 hex 문자)
openssl rand -hex 32

# .env 파일에 설정
PII_ENCRYPTION_KEY=9ac3dec3d78b290bd9074ad7b908325087c953057ebeaf10db39193b2989a9d8
```

| 항목 | 설명 |
|------|------|
| 키 형식 | 64자 hex 문자열 (= 32바이트 = 256비트) |
| 환경변수 | `PII_ENCRYPTION_KEY` |
| 키 미설정 시 | 암호화 비활성화 -- 평문 저장 (개발/테스트용) |
| 키 로딩 | `std::call_once` -- 프로세스 시작 시 1회 로드 |
| 키 저장 위치 | `.env` 파일 (`.gitignore` 포함, 버전 관리 제외) |

> **운영 환경 권장사항**: 키를 `.env` 파일 대신 환경 비밀 관리 시스템(Vault, AWS KMS 등)에서 주입할 것을 권장.

### 6.3.4 PII 마스킹 (Public API 응답)

관리자가 아닌 사용자에게 제공하는 API 응답에서는 개인정보를 마스킹하여 표시:

| 유형 | 원본 | 마스킹 결과 |
|------|------|------------|
| 이름 (`name`) | 홍길동 | 홍*동 |
| 이메일 (`email`) | hong@example.com | h***@example.com |
| 전화번호 (`phone`) | 010-1234-5678 | 010-****-5678 |
| 소속 (`org`) | 스마트코어 | 스마*** |

- UTF-8 인코딩 인식 (한글/영어 모두 올바르게 처리)
- 관리자(JWT admin) 요청 시에만 복호화된 전체 데이터 제공

### 6.3.5 Fail-Open 설계

암호화/복호화 실패 시 서비스 가용성을 유지하기 위한 안전 장치:

- **암호화 실패**: 평문 반환 (데이터 손실 방지) + 에러 로그
- **복호화 실패**: 암호화된 원본 반환 + 에러 로그
- **키 미설정**: 암호화 비활성화 -- 평문 통과 (개발 환경 호환)
- **`ENC:` 접두사 감지**: 암호화된 데이터와 기존 평문 데이터 혼재 시에도 올바르게 처리

---

## 6.4 적용 서비스

### 6.4.1 PKD Management Service

**대상 테이블**: `api_client_requests`

| 컬럼 | 데이터 유형 | 컬럼 크기 |
|------|-----------|----------|
| `requester_name` | 요청자 성명 | VARCHAR(1024) |
| `requester_org` | 요청자 소속 | VARCHAR(1024) |
| `requester_contact_phone` | 요청자 전화번호 | VARCHAR(1024) |
| `requester_contact_email` | 요청자 이메일 | VARCHAR(1024) |

**소스 파일**:
- `services/pkd-management/src/auth/personal_info_crypto.h` -- 암호화 유틸리티 헤더
- `services/pkd-management/src/auth/personal_info_crypto.cpp` -- AES-256-GCM 구현
- `services/pkd-management/src/repositories/api_client_request_repository.cpp` -- INSERT 시 암호화, SELECT 시 복호화
- `services/pkd-management/src/handlers/api_client_request_handler.cpp` -- Public API 응답 시 PII 마스킹
- `services/pkd-management/src/infrastructure/service_container.cpp` -- Phase 0 초기화

### 6.4.2 PA Service

**대상 테이블**: `pa_verification`

| 컬럼 | 데이터 유형 | 컬럼 크기 |
|------|-----------|----------|
| `document_number` | **여권번호** (고유식별정보) | VARCHAR(1024) |
| `client_ip` | 클라이언트 IP | VARCHAR(1024) |
| `user_agent` | User-Agent 문자열 | TEXT / VARCHAR2(4000) |

**소스 파일**:
- `services/pa-service/src/auth/personal_info_crypto.h` -- 암호화 유틸리티 헤더 (PKD Management와 동일)
- `services/pa-service/src/auth/personal_info_crypto.cpp` -- AES-256-GCM 구현
- `services/pa-service/src/repositories/pa_verification_repository.cpp` -- INSERT 시 암호화, SELECT 시 복호화
- `services/pa-service/src/infrastructure/service_container.cpp` -- Step 0 초기화

### 6.4.3 전송 구간 암호화

| 구간 | 방식 | 설정 파일 |
|------|------|----------|
| 외부 → API Gateway | **TLS 1.2/1.3** (HTTPS :443) | `nginx/api-gateway-ssl.conf` |
| API Gateway → Backend | HTTP (내부 Docker 네트워크) | - |
| 인증서 | Private CA (RSA 4096) + 서버 인증서 (RSA 2048) | `scripts/ssl/init-cert.sh` |

---

## 6.5 DB 스키마 변경 사항

### 6.5.1 PostgreSQL

```sql
-- api_client_requests (PKD Management)
-- 기존: VARCHAR(255) -> 변경: VARCHAR(1024)
requester_name VARCHAR(1024) NOT NULL,
requester_org VARCHAR(1024) NOT NULL,
requester_contact_phone VARCHAR(1024),
requester_contact_email VARCHAR(1024) NOT NULL,

-- pa_verification (PA Service)
-- 기존: VARCHAR(50) -> 변경: VARCHAR(1024)
document_number VARCHAR(1024),
-- 기존: VARCHAR(45) -> 변경: VARCHAR(1024)
client_ip VARCHAR(1024),
-- user_agent: TEXT (변경 없음)
```

### 6.5.2 Oracle

```sql
-- api_client_requests (PKD Management)
-- 기존: VARCHAR2(255) -> 변경: VARCHAR2(1024)
requester_name VARCHAR2(1024) NOT NULL,
requester_org VARCHAR2(1024) NOT NULL,
requester_contact_phone VARCHAR2(1024),
requester_contact_email VARCHAR2(1024) NOT NULL,

-- pa_verification (PA Service)
-- 기존: VARCHAR2(50) -> 변경: VARCHAR2(1024)
document_number VARCHAR2(1024),
-- 기존: VARCHAR2(45) -> 변경: VARCHAR2(1024)
client_ip VARCHAR2(1024),
-- user_agent: VARCHAR2(4000) (변경 없음)
```

### 6.5.3 기존 데이터 마이그레이션

기존 평문 데이터는 마이그레이션 없이 그대로 사용 가능:
- `decrypt()` 함수는 `ENC:` 접두사가 없는 값을 평문으로 인식하여 그대로 반환
- 신규 INSERT부터 암호화 적용, 기존 데이터는 조회 시 평문 그대로 반환

별도 마이그레이션 스크립트가 필요한 경우 (기존 데이터 일괄 암호화):
```sql
-- 주의: 애플리케이션 레벨에서 처리해야 함 (DB 함수로는 AES-256-GCM 구현 불가)
-- 별도 마이그레이션 도구 개발 필요
```

---

## 6.6 설정 및 운영

### 6.6.1 암호화 활성화

```bash
# 1. 키 생성
openssl rand -hex 32

# 2. .env 파일에 추가
echo "PII_ENCRYPTION_KEY=<생성된_64자_hex>" >> .env

# 3. 서비스 재시작
docker compose -f docker/docker-compose.yaml restart pkd-management pa-service
```

### 6.6.2 암호화 비활성화 (개발/테스트용)

```bash
# .env에서 키를 제거하거나 빈 값 설정
PII_ENCRYPTION_KEY=

# 서비스 재시작
docker compose restart pkd-management pa-service
```

### 6.6.3 키 로테이션

현재 버전에서는 키 로테이션 자동화가 구현되어 있지 않음. 키 변경 시:

1. 기존 데이터를 구 키로 복호화
2. 신규 키로 재암호화
3. `.env` 파일의 `PII_ENCRYPTION_KEY` 교체
4. 서비스 재시작

> **향후 개선**: 키 버전 관리 + 자동 마이그레이션 도구 개발 권장

### 6.6.4 로그 확인

```bash
# 암호화 활성화 확인
docker logs icao-local-pkd-pkd-management 2>&1 | grep "PII Crypto"
# 출력: [PII Crypto] AES-256-GCM encryption ENABLED for personal information fields

docker logs icao-local-pkd-pa-service 2>&1 | grep "PII Crypto"
# 출력: [PII Crypto] AES-256-GCM encryption ENABLED for personal information fields

# 키 미설정 시
# 출력: [PII Crypto] PII_ENCRYPTION_KEY not set - personal info encryption DISABLED
```

---

## 6.7 법적 준수 체크리스트

| 항목 | 법적 근거 | 적용 상태 |
|------|----------|:---:|
| 고유식별정보(여권번호) 저장 시 암호화 | 법 제24조, 시행령 제21조, 고시 제7조 | **충족** |
| 고유식별정보 전송 시 암호화 (TLS) | 법 제24조, 시행령 제21조, 고시 제7조 | **충족** |
| 비밀번호 일방향 해시 저장 | 고시 제7조 제1항 | **충족** (bcrypt) |
| KISA 승인 암호 알고리즘 사용 | 고시 제7조, KISA 안내서 | **충족** (AES-256) |
| 접근 통제 및 권한 제한 | 법 제29조, 고시 제5조 | **충족** (JWT + RBAC) |
| 접속기록 보관 | 법 제29조, 고시 제8조 | **충족** (auth_audit_log, operation_audit_log) |
| 개인정보 최소 수집 | 법 제16조 | **충족** (PA 검증 필수 정보만 수집) |
| 일반 개인정보 암호화 (권고) | 법 제29조 | **충족** (요청자 정보 전체 암호화) |

---

## 참고 법령

| 법령 | 번호 |
|------|------|
| 개인정보 보호법 | 법률 제19234호 (2023.3.14. 일부개정) |
| 개인정보 보호법 시행령 | 대통령령 제34211호 |
| 개인정보의 안전성 확보조치 기준 | 개인정보보호위원회 고시 제2021-2호 |
| 개인정보의 기술적 관리적 보호조치 기준 | 방송통신위원회 고시 제2020-9호 |
| KISA 암호 알고리즘 및 키 길이 이용 안내서 | 한국인터넷진흥원 (2024) |
| NIST SP 800-38D | GCM Mode Specification |

---

## 부록 A: 운영 데이터

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

**Copyright 2026 SMARTCORE Inc. All rights reserved.**
