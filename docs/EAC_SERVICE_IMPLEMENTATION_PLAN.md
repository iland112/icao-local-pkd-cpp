# BSI TR-03110 EAC Service 구현 계획서

**버전**: v0.2.0
**작성일**: 2026-03-13
**상태**: 계획 수립 (실험 버전)
**브랜치**: `feature/eac-service`

---

## 1. 개요

### 1.1 목적

기존 ICAO Local PKD 시스템에 BSI TR-03110 (Advanced Security Mechanisms for Machine Readable Travel Documents) 호환 기능을 추가한다. **별도의 마이크로서비스(EAC Service)**로 구현하여 기존 서비스에 영향 없이 실험 및 검증한다.

### 1.2 BSI TR-03110 개요

BSI TR-03110은 독일 연방정보보안청(BSI)이 정의한 전자여권 확장 보안 메커니즘이다.

| 프로토콜 | 설명 | 본 시스템 관련성 |
|----------|------|-----------------|
| **PACE** | Password Authenticated Connection Establishment (BAC 대체) | 스코프 외 (리더기 필요) |
| **CA** | Chip Authentication (칩과 강화된 키 합의) | 스코프 외 (칩 통신 필요) |
| **TA** | Terminal Authentication (단말기 권한 증명) | **핵심 — CVC 인증서 관리** |
| **EAC PKI** | CVCA → DV → IS 인증서 체계 | **핵심 — 인증서 저장/검증/배포** |

### 1.3 ICAO 9303 vs BSI TR-03110 PKI 비교

```
ICAO 9303 PKI (현재 지원)           BSI TR-03110 EAC PKI (추가)
┌────────────────────┐              ┌────────────────────────┐
│      CSCA          │              │        CVCA            │
│  (Country Signing  │              │  (Country Verifying    │
│   CA, X.509)       │              │   CA, CVC)             │
└────────┬───────────┘              └────────┬───────────────┘
         │ 발급                              │ 발급
┌────────┴───────────┐              ┌────────┴───────────────┐
│      DSC           │              │        DV              │
│  (Document Signer, │              │  (Document Verifier,   │
│   X.509)           │              │   CVC)                 │
└────────────────────┘              └────────┬───────────────┘
         │ SOD 서명                          │ 발급
         ▼                          ┌────────┴───────────────┐
  [여권 칩 데이터]                   │        IS              │
  PA로 무결성 검증                   │  (Inspection System,   │
                                    │   CVC)                 │
                                    └────────────────────────┘
                                             │ TA 수행
                                             ▼
                                     [여권 칩 DG3/DG4]
                                     지문/홍채 접근 인가
```

| 구분 | ICAO 9303 | BSI TR-03110 |
|------|-----------|--------------|
| 인증서 형식 | X.509 (ASN.1/DER) | CVC (TLV, ISO 7816) |
| 신뢰 체인 | CSCA → DSC (2단계) | CVCA → DV → IS (3단계) |
| 인증서 수명 | 수년 | 수일~수주 (짧은 수명) |
| 주요 목적 | 칩 데이터 무결성 (PA) | 단말기 권한 증명 (TA) |
| OpenSSL 지원 | 완전 지원 | **미지원** (직접 TLV 파싱) |
| 권한 제어 | 없음 | CHAT 비트마스크 (DG3/DG4 접근 제어) |

### 1.4 스코프 정의

**포함 (실험 버전):**
- CVC 인증서 파싱 (TLV 바이트 파싱, C++ 구현)
- CVC 업로드/저장/검색/내보내기
- EAC 신뢰체인 검증 (CVCA → DV → IS)
- CVC 서명 검증 (OpenSSL EVP API)
- CHAT 권한 분석 및 시각화
- DB 저장 (PostgreSQL + Oracle, IQueryExecutor 패턴)
- REST API 제공 (Drogon :8086)
- 프론트엔드 관리 페이지

**제외 (하드웨어/프로토콜 의존):**
- PACE 프로토콜 실행 (스마트카드 리더 필요)
- Chip Authentication 실행 (칩 통신 필요)
- Terminal Authentication 프로토콜 실행 (리더-칩 세션 필요)
- DV/IS 인증서 발급 (CA 기능, HSM 필요)
- 온라인 CVC 갱신 (DVCA/TCC 통신)
- LDAP 저장 (검증 후 Phase 2에서 추가)

---

## 2. 시스템 아키텍처

### 2.1 서비스 배치

```
API Gateway (nginx :80/:443)
  ├── /api/upload, /api/certificates  → PKD Management (:8081)   [기존]
  ├── /api/pa                         → PA Service (:8082)        [기존]
  ├── /api/sync                       → PKD Relay (:8083)         [기존]
  ├── /api/monitoring                 → Monitoring (:8084)        [기존]
  ├── /api/ai                         → AI Analysis (:8085)       [기존]
  └── /api/eac                        → EAC Service (:8086)       [신규]
```

### 2.2 기술 스택

| 항목 | 선택 | 근거 |
|------|------|------|
| 언어 | **C++20** | 프로젝트 메인 언어, 기존 4개 C++ 서비스와 통일 |
| 프레임워크 | **Drogon** | 기존 서비스와 동일한 HTTP 프레임워크 |
| DB 추상화 | **IQueryExecutor** | 기존 shared/lib/database 재사용 (PostgreSQL + Oracle) |
| DB 풀 | **icao::database** | 기존 공유 라이브러리 (RAII 패턴) |
| CVC 파싱 | **shared/lib/cvc-parser** | 신규 공유 라이브러리 (TLV 직접 파싱) |
| 서명 검증 | **OpenSSL EVP API** | CVC 서명 검증 (RSA/ECDSA), 기존 의존성 재사용 |
| 감사 로그 | **icao::audit** | 기존 공유 라이브러리 (operation_audit_log) |
| 설정 관리 | **icao::config** | 기존 공유 라이브러리 (환경변수 기반) |
| 로깅 | **spdlog** | 기존 공유 라이브러리 (icao::logging) |
| DI 패턴 | **ServiceContainer** | 기존 pImpl + non-owning pointer 패턴 |

### 2.3 디렉토리 구조

```
shared/lib/cvc-parser/                     ← 신규 공유 라이브러리
├── CMakeLists.txt
├── include/icao/cvc/
│   ├── tlv.h                              # 범용 TLV (Tag-Length-Value) 파서
│   ├── cvc_parser.h                       # BSI TR-03110 CVC 인증서 파서
│   ├── cvc_certificate.h                  # CVC 도메인 모델 (CvcCertificate 구조체)
│   ├── chat_decoder.h                     # CHAT 비트마스크 디코더
│   ├── cvc_signature.h                    # CVC 서명 검증 (OpenSSL EVP)
│   └── eac_oids.h                         # BSI TR-03110 OID 정의
└── src/
    ├── tlv.cpp
    ├── cvc_parser.cpp
    ├── chat_decoder.cpp
    └── cvc_signature.cpp

services/eac-service/                      ← 신규 마이크로서비스
├── CMakeLists.txt
├── Dockerfile
├── src/
│   ├── main.cpp                           # Drogon 앱 (:8086), 라우트 등록
│   │
│   ├── infrastructure/
│   │   ├── app_config.h                   # AppConfig 구조체 (환경변수)
│   │   ├── service_container.h            # ServiceContainer (pImpl 패턴)
│   │   └── service_container.cpp
│   │
│   ├── domain/
│   │   └── cvc_models.h                   # CvcCertificateRecord, EacTrustChainRecord
│   │
│   ├── repositories/
│   │   ├── cvc_certificate_repository.h   # CVC CRUD (IQueryExecutor)
│   │   ├── cvc_certificate_repository.cpp
│   │   ├── eac_trust_chain_repository.h   # 신뢰체인 기록
│   │   └── eac_trust_chain_repository.cpp
│   │
│   ├── services/
│   │   ├── cvc_service.h                  # CVC 업로드/검색/CRUD 비즈니스 로직
│   │   ├── cvc_service.cpp
│   │   ├── eac_chain_validator.h          # CVCA→DV→IS 체인 검증
│   │   └── eac_chain_validator.cpp
│   │
│   └── handlers/
│       ├── eac_upload_handler.h           # POST /api/eac/upload, /preview
│       ├── eac_upload_handler.cpp
│       ├── eac_certificate_handler.h      # GET /api/eac/certificates, /{id}
│       ├── eac_certificate_handler.cpp
│       ├── eac_validation_handler.h       # GET /api/eac/certificates/{id}/chain
│       ├── eac_validation_handler.cpp
│       ├── eac_statistics_handler.h       # GET /api/eac/statistics, /countries
│       └── eac_statistics_handler.cpp
│
└── tests/                                 # GTest 단위 테스트
    ├── CMakeLists.txt
    ├── test_tlv_parser.cpp
    ├── test_cvc_parser.cpp
    ├── test_chat_decoder.cpp
    ├── test_cvc_signature.cpp
    └── fixtures/                          # 테스트용 CVC 바이너리
        └── README.md
```

### 2.4 공유 라이브러리 의존 관계

```
services/eac-service
├── shared/lib/cvc-parser      ← 신규 (CVC TLV 파싱 + 서명 검증)
├── shared/lib/database        ← 기존 (IQueryExecutor, DB 풀)
├── shared/lib/audit           ← 기존 (감사 로그)
├── shared/lib/config          ← 기존 (설정 관리)
├── shared/lib/logging         ← 기존 (spdlog)
└── shared/lib/exception       ← 기존 (커스텀 예외)
```

---

## 3. CVC 인증서 형식

### 3.1 CVC TLV 구조 (BSI TR-03110 Part 3, Appendix C)

```
CV Certificate [0x7F21]
├── Certificate Body [0x7F4E]
│   ├── Certificate Profile Identifier [0x5F29]  (1 byte, 값: 0x00)
│   ├── Certification Authority Reference [0x42]  (CAR, 가변 길이)
│   │   └── 형식: "{국가코드}{홀더 니모닉}{시퀀스}" (예: "DECVCA00001")
│   ├── Public Key [0x7F49]
│   │   ├── Object Identifier [0x06]              (알고리즘 OID)
│   │   └── Key Parameters                         (알고리즘별 상이)
│   │       ├── RSA: Modulus [0x81] + Exponent [0x82]
│   │       └── ECDSA: Prime [0x81] + A [0x82] + B [0x83] +
│   │                  Generator [0x84] + Order [0x85] + Cofactor [0x87]
│   ├── Certificate Holder Reference [0x5F20]     (CHR, 가변 길이)
│   │   └── 형식: "{국가코드}{홀더 니모닉}{시퀀스}" (예: "DEDV000001")
│   ├── Certificate Holder Auth Template [0x7F4C] (CHAT)
│   │   ├── Object Identifier [0x06]              (역할 OID)
│   │   └── Discretionary Data [0x53]             (권한 비트마스크)
│   ├── Certificate Effective Date [0x5F25]       (6 bytes, YYMMDD BCD)
│   └── Certificate Expiration Date [0x5F24]      (6 bytes, YYMMDD BCD)
└── Signature [0x5F37]                             (가변 길이)
```

### 3.2 C++ 도메인 모델

```cpp
// shared/lib/cvc-parser/include/icao/cvc/cvc_certificate.h

namespace icao::cvc {

enum class CvcType { CVCA, DV_DOMESTIC, DV_FOREIGN, IS };
enum class ChatRole { IS, AT, ST, UNKNOWN };

struct ChatInfo {
    ChatRole role;
    std::string roleOid;
    std::vector<uint8_t> authorizationBits;
    std::vector<std::string> permissions;  // 디코딩된 권한 목록
};

struct CvcPublicKey {
    std::string algorithmOid;
    std::string algorithmName;              // "id-TA-ECDSA-SHA-256" 등
    std::vector<uint8_t> rawData;           // 공개키 파라미터 원본
    // RSA
    std::vector<uint8_t> modulus;
    std::vector<uint8_t> exponent;
    // ECDSA
    std::vector<uint8_t> prime;
    std::vector<uint8_t> coeffA;
    std::vector<uint8_t> coeffB;
    std::vector<uint8_t> generator;
    std::vector<uint8_t> order;
    std::vector<uint8_t> cofactor;
};

struct CvcCertificate {
    // TLV 식별자
    std::string car;                        // Certification Authority Reference
    std::string chr;                        // Certificate Holder Reference
    uint8_t profileIdentifier = 0x00;

    // 유형 (CAR/CHR 패턴에서 추론)
    CvcType type;
    std::string countryCode;                // CAR/CHR 앞 2자리

    // CHAT
    ChatInfo chat;

    // 공개키
    CvcPublicKey publicKey;

    // 유효기간
    std::string effectiveDate;              // "YYYYMMDD"
    std::string expirationDate;             // "YYYYMMDD"

    // 서명
    std::vector<uint8_t> signature;

    // 원본 데이터
    std::vector<uint8_t> bodyRaw;           // Certificate Body 원본 (서명 검증용)
    std::vector<uint8_t> rawBinary;         // 전체 CVC 바이너리
    std::string fingerprintSha256;          // SHA-256 해시
};

} // namespace icao::cvc
```

### 3.3 CHAT 역할 및 권한

**역할 OID:**
| OID | 역할 |
|-----|------|
| `0.4.0.127.0.7.3.1.2.1` | id-IS (Inspection System) |
| `0.4.0.127.0.7.3.1.2.2` | id-AT (Authentication Terminal) |
| `0.4.0.127.0.7.3.1.2.3` | id-ST (Signature Terminal) |

**IS 권한 비트마스크 (id-IS용):**
| 비트 | 권한 | 설명 |
|------|------|------|
| 0 | Read DG3 | 지문 데이터 읽기 |
| 1 | Read DG4 | 홍채 데이터 읽기 |

**AT 권한 비트마스크 (id-AT용, eID):**
| 비트 | 권한 | 설명 |
|------|------|------|
| 0 | Age Verification | 연령 확인 |
| 1 | Community ID Verification | 커뮤니티 ID 확인 |
| 2 | Restricted Identification | 제한 식별 |
| 3 | Privileged Terminal | 특권 단말기 |
| 4 | CAN allowed | CAN 사용 가능 |
| 5 | PIN Management | PIN 관리 |
| 6 | Install Certificate | 인증서 설치 |
| 7 | Install Qualified Certificate | 자격 인증서 설치 |

### 3.4 EAC 알고리즘 OID

| OID | 알고리즘 | 용도 |
|-----|----------|------|
| `0.4.0.127.0.7.2.2.2.1` | id-TA-RSA-v1-5-SHA-1 | TA (RSA PKCS#1 v1.5) |
| `0.4.0.127.0.7.2.2.2.2` | id-TA-RSA-v1-5-SHA-256 | TA (RSA PKCS#1 v1.5) |
| `0.4.0.127.0.7.2.2.2.3` | id-TA-RSA-PSS-SHA-1 | TA (RSA-PSS) |
| `0.4.0.127.0.7.2.2.2.4` | id-TA-RSA-PSS-SHA-256 | TA (RSA-PSS) |
| `0.4.0.127.0.7.2.2.2.5` | id-TA-ECDSA-SHA-1 | TA (ECDSA) |
| `0.4.0.127.0.7.2.2.2.6` | id-TA-ECDSA-SHA-224 | TA (ECDSA) |
| `0.4.0.127.0.7.2.2.2.7` | id-TA-ECDSA-SHA-256 | TA (ECDSA) |
| `0.4.0.127.0.7.2.2.2.8` | id-TA-ECDSA-SHA-384 | TA (ECDSA) |
| `0.4.0.127.0.7.2.2.2.9` | id-TA-ECDSA-SHA-512 | TA (ECDSA) |

---

## 4. DB 스키마

### 4.1 PostgreSQL

```sql
-- CVC 인증서 저장 테이블
CREATE TABLE IF NOT EXISTS cvc_certificate (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    upload_id UUID,                              -- uploaded_file FK (선택)

    -- CVC 유형
    cvc_type VARCHAR(20) NOT NULL,               -- CVCA, DV_DOMESTIC, DV_FOREIGN, IS
    country_code VARCHAR(3) NOT NULL,

    -- CVC 식별자 (X.509의 Issuer DN / Subject DN 대응)
    car VARCHAR(100) NOT NULL,                   -- Certificate Authority Reference
    chr VARCHAR(100) NOT NULL,                   -- Certificate Holder Reference

    -- CHAT (Certificate Holder Authorization Template)
    chat_oid VARCHAR(100),                       -- 역할 OID (id-IS, id-AT, id-ST)
    chat_role VARCHAR(30),                       -- 디코딩된 역할명
    chat_authorization BYTEA,                    -- 권한 비트마스크 (원본)
    chat_permissions TEXT,                       -- 디코딩된 권한 목록 (JSON)

    -- 공개키
    public_key_oid VARCHAR(100),                 -- 알고리즘 OID
    public_key_algorithm VARCHAR(50),            -- 알고리즘명 (예: id-TA-ECDSA-SHA-256)
    public_key_data BYTEA,                       -- 공개키 파라미터 (원본)

    -- 유효기간
    effective_date DATE NOT NULL,
    expiration_date DATE NOT NULL,

    -- 원본 데이터
    cvc_binary BYTEA NOT NULL,                   -- CVC 인증서 바이너리
    fingerprint_sha256 VARCHAR(64) NOT NULL,     -- SHA-256 해시

    -- 서명 검증
    signature_valid BOOLEAN,
    signature_data BYTEA,                        -- 서명 바이너리

    -- 신뢰체인 검증
    issuer_cvc_id UUID REFERENCES cvc_certificate(id),
    validation_status VARCHAR(20) DEFAULT 'PENDING',
    validation_message TEXT,

    -- 메타
    source_type VARCHAR(30) DEFAULT 'FILE_UPLOAD',
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),

    CONSTRAINT uq_cvc_fingerprint UNIQUE (fingerprint_sha256),
    CONSTRAINT chk_cvc_type CHECK (cvc_type IN ('CVCA', 'DV_DOMESTIC', 'DV_FOREIGN', 'IS')),
    CONSTRAINT chk_cvc_validation CHECK (validation_status IN ('VALID', 'INVALID', 'PENDING', 'EXPIRED'))
);

CREATE INDEX idx_cvc_country ON cvc_certificate(country_code);
CREATE INDEX idx_cvc_type ON cvc_certificate(cvc_type);
CREATE INDEX idx_cvc_car ON cvc_certificate(car);
CREATE INDEX idx_cvc_chr ON cvc_certificate(chr);
CREATE INDEX idx_cvc_type_country ON cvc_certificate(cvc_type, country_code);
CREATE INDEX idx_cvc_effective ON cvc_certificate(effective_date);
CREATE INDEX idx_cvc_expiration ON cvc_certificate(expiration_date);

-- EAC 신뢰체인 기록
CREATE TABLE IF NOT EXISTS eac_trust_chain (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    is_certificate_id UUID NOT NULL REFERENCES cvc_certificate(id),
    dv_certificate_id UUID REFERENCES cvc_certificate(id),
    cvca_certificate_id UUID REFERENCES cvc_certificate(id),
    chain_valid BOOLEAN DEFAULT FALSE,
    chain_path TEXT,                              -- "IS→DV→CVCA" 경로 문자열
    chain_depth INTEGER DEFAULT 0,
    validation_message TEXT,
    validated_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_eac_chain_is ON eac_trust_chain(is_certificate_id);
```

### 4.2 Oracle

```sql
CREATE TABLE cvc_certificate (
    id RAW(16) DEFAULT SYS_GUID() PRIMARY KEY,
    upload_id RAW(16),

    cvc_type VARCHAR2(20) NOT NULL,
    country_code VARCHAR2(3) NOT NULL,

    car VARCHAR2(100) NOT NULL,
    chr VARCHAR2(100) NOT NULL,

    chat_oid VARCHAR2(100),
    chat_role VARCHAR2(30),
    chat_authorization BLOB,
    chat_permissions VARCHAR2(4000),

    public_key_oid VARCHAR2(100),
    public_key_algorithm VARCHAR2(50),
    public_key_data BLOB,

    effective_date DATE NOT NULL,
    expiration_date DATE NOT NULL,

    cvc_binary BLOB NOT NULL,
    fingerprint_sha256 VARCHAR2(64) NOT NULL,

    signature_valid NUMBER(1),
    signature_data BLOB,

    issuer_cvc_id RAW(16) REFERENCES cvc_certificate(id),
    validation_status VARCHAR2(20) DEFAULT 'PENDING',
    validation_message VARCHAR2(4000),

    source_type VARCHAR2(30) DEFAULT 'FILE_UPLOAD',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT SYSTIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT SYSTIMESTAMP,

    CONSTRAINT uq_cvc_fingerprint UNIQUE (fingerprint_sha256),
    CONSTRAINT chk_cvc_type CHECK (cvc_type IN ('CVCA', 'DV_DOMESTIC', 'DV_FOREIGN', 'IS')),
    CONSTRAINT chk_cvc_validation CHECK (validation_status IN ('VALID', 'INVALID', 'PENDING', 'EXPIRED'))
);

CREATE INDEX idx_cvc_country ON cvc_certificate(country_code);
CREATE INDEX idx_cvc_type ON cvc_certificate(cvc_type);
CREATE INDEX idx_cvc_car ON cvc_certificate(car);
CREATE INDEX idx_cvc_chr ON cvc_certificate(chr);
CREATE INDEX idx_cvc_type_country ON cvc_certificate(cvc_type, country_code);

CREATE TABLE eac_trust_chain (
    id RAW(16) DEFAULT SYS_GUID() PRIMARY KEY,
    is_certificate_id RAW(16) NOT NULL REFERENCES cvc_certificate(id),
    dv_certificate_id RAW(16) REFERENCES cvc_certificate(id),
    cvca_certificate_id RAW(16) REFERENCES cvc_certificate(id),
    chain_valid NUMBER(1) DEFAULT 0,
    chain_path VARCHAR2(200),
    chain_depth NUMBER(5) DEFAULT 0,
    validation_message VARCHAR2(4000),
    validated_at TIMESTAMP WITH TIME ZONE DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_eac_chain_is ON eac_trust_chain(is_certificate_id);
```

---

## 5. API 엔드포인트

### 5.1 엔드포인트 목록

| Method | Path | 설명 | 인증 |
|--------|------|------|------|
| `GET` | `/api/eac/health` | 헬스 체크 | 없음 |
| `POST` | `/api/eac/upload` | CVC 인증서 업로드 | JWT |
| `POST` | `/api/eac/upload/preview` | CVC 미리보기 (파싱만) | 없음 |
| `GET` | `/api/eac/certificates` | CVC 인증서 검색 | 없음 |
| `GET` | `/api/eac/certificates/{id}` | CVC 인증서 상세 | 없음 |
| `GET` | `/api/eac/certificates/{id}/chain` | EAC 신뢰체인 조회 | 없음 |
| `GET` | `/api/eac/statistics` | EAC PKI 통계 | 없음 |
| `GET` | `/api/eac/countries` | CVC 보유 국가 목록 | 없음 |
| `GET` | `/api/eac/export/{format}` | CVC 인증서 내보내기 | 없음 |

### 5.2 주요 응답 스키마

```json
// POST /api/eac/upload/preview 응답
{
  "success": true,
  "certificate": {
    "cvcType": "DV_DOMESTIC",
    "countryCode": "DE",
    "car": "DECVCA00001",
    "chr": "DEDV000001",
    "chat": {
      "role": "id-IS",
      "roleOid": "0.4.0.127.0.7.3.1.2.1",
      "permissions": ["Read DG3", "Read DG4"]
    },
    "publicKey": {
      "algorithm": "id-TA-ECDSA-SHA-256",
      "oid": "0.4.0.127.0.7.2.2.2.7"
    },
    "effectiveDate": "2026-03-01",
    "expirationDate": "2026-04-01",
    "fingerprintSha256": "a1b2c3..."
  }
}

// GET /api/eac/statistics 응답
{
  "total": 150,
  "byType": {
    "CVCA": 5,
    "DV_DOMESTIC": 20,
    "DV_FOREIGN": 15,
    "IS": 110
  },
  "byCountry": [
    {"countryCode": "DE", "count": 45},
    {"countryCode": "FR", "count": 30}
  ],
  "validCount": 120,
  "expiredCount": 30,
  "chatPermissionDistribution": {
    "Read DG3": 110,
    "Read DG4": 85
  }
}
```

---

## 6. 프론트엔드 변경

### 6.1 신규 페이지

| 페이지 | 라우트 | 설명 | 참조 패턴 |
|--------|--------|------|-----------|
| CvcUpload | `/eac/upload` | CVC 업로드 + 미리보기 | CertificateUpload.tsx |
| CvcSearch | `/eac/certificates` | CVC 검색 + 상세 | CertificateSearch.tsx |
| EacDashboard | `/eac/dashboard` | EAC PKI 통계 | UploadDashboard.tsx |
| EacTrustChain | `/eac/trust-chain` | CVCA→DV→IS 시각화 | TrustChainValidationReport.tsx |

### 6.2 신규 컴포넌트

| 컴포넌트 | 설명 |
|----------|------|
| `CvcDetailDialog` | CVC 인증서 상세 모달 (General / CHAT 권한 / Trust Chain 탭) |
| `ChatPermissionsCard` | CHAT 비트마스크 시각화 (체크박스 그리드) |
| `EacTrustChainVisualization` | CVCA→DV→IS 트리 시각화 |
| `CvcSearchFilters` | CVC 검색 필터 (국가, 유형, 상태) |

### 6.3 사이드바 변경

```
EAC 관리 (Shield 아이콘)          ← 신규 섹션
├── CVC 업로드                    ← /eac/upload
├── CVC 인증서 조회               ← /eac/certificates
├── EAC 통계                     ← /eac/dashboard
└── EAC Trust Chain              ← /eac/trust-chain
```

### 6.4 API 모듈

`frontend/src/services/eacApi.ts` — EAC Service API 클라이언트

---

## 7. 인프라 변경

### 7.1 Docker Compose

```yaml
# docker/docker-compose.yaml에 추가
eac-service:
  build:
    context: ..
    dockerfile: services/eac-service/Dockerfile
    args:
      BASE_IMAGE: ${VCPKG_BASE_IMAGE:-icao-vcpkg-base:latest}
  container_name: eac-service
  environment:
    - DB_TYPE=${DB_TYPE:-postgres}
    - DB_HOST=${DB_HOST:-postgres}
    - DB_PORT=${DB_PORT:-5432}
    - DB_NAME=${DB_NAME:-localpkd}
    - DB_USER=${DB_USER:-pkd}
    - DB_PASSWORD=${DB_PASSWORD}
    - ORACLE_HOST=${ORACLE_HOST:-oracle}
    - ORACLE_PORT=${ORACLE_PORT:-1521}
    - ORACLE_SERVICE_NAME=${ORACLE_SERVICE_NAME:-XEPDB1}
    - ORACLE_USER=${ORACLE_USER:-pkd_user}
    - ORACLE_PASSWORD=${ORACLE_PASSWORD}
    - THREAD_NUM=${EAC_THREAD_NUM:-8}
  ports:
    - "8086:8086"
  healthcheck:
    test: ["CMD", "curl", "-f", "http://localhost:8086/api/eac/health"]
    interval: 30s
    timeout: 10s
    retries: 3
  restart: unless-stopped
  depends_on:
    postgres:
      condition: service_healthy
  profiles:
    - eac  # 선택적 활성화 (docker compose --profile eac up)
```

### 7.2 nginx API Gateway

```nginx
# /api/eac location 블록 추가
location /api/eac {
    set $eac_upstream http://eac-service:8086;
    proxy_pass $eac_upstream;
    include proxy_params;
}
```

### 7.3 Dockerfile (Multi-Stage Build)

```dockerfile
# services/eac-service/Dockerfile
ARG BASE_IMAGE=icao-vcpkg-base:latest
FROM ${BASE_IMAGE} AS builder

WORKDIR /app
COPY shared/ shared/
COPY services/eac-service/ services/eac-service/

WORKDIR /app/services/eac-service
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . -j$(nproc)

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 libpq5 curl && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/services/eac-service/build/eac-service /usr/local/bin/
EXPOSE 8086
HEALTHCHECK --interval=30s --timeout=10s --retries=3 \
    CMD curl -f http://localhost:8086/api/eac/health || exit 1
CMD ["eac-service"]
```

### 7.4 DB 초기화

- PostgreSQL: `docker/init-scripts/20-eac-schema.sql`
- Oracle: `docker/db-oracle/init/20-eac-schema.sql`

---

## 8. 구현 단계

### Phase 1: 공유 라이브러리 + CVC 파서 (1-2주)

**목표**: `shared/lib/cvc-parser` 공유 라이브러리 구현 + GTest 단위 테스트

| 작업 | 파일 | 설명 |
|------|------|------|
| 1.1 | `shared/lib/cvc-parser/CMakeLists.txt` | 공유 라이브러리 빌드 설정 |
| 1.2 | `cvc_certificate.h` | CVC 도메인 모델 (구조체) |
| 1.3 | `eac_oids.h` | BSI TR-03110 OID 상수 + 알고리즘 매핑 |
| 1.4 | `tlv.h/.cpp` | 범용 TLV 파서 (tag, length, value 추출, 경계 검증) |
| 1.5 | `cvc_parser.h/.cpp` | CVC 구조 파싱 (Body, Public Key, Signature, 날짜) |
| 1.6 | `chat_decoder.h/.cpp` | CHAT 비트마스크 → 역할/권한 목록 디코딩 |
| 1.7 | `cvc_signature.h/.cpp` | CVC 서명 검증 (OpenSSL EVP, RSA/ECDSA) |
| 1.8 | `tests/test_*.cpp` | GTest 단위 테스트 (TLV, CVC 파서, CHAT, 서명) |

**완료 기준**: 테스트 CVC 바이너리를 파싱하여 모든 필드 추출 + 서명 검증 통과

### Phase 2: 서비스 스캐폴딩 + DB + CRUD API (1-2주)

**목표**: Drogon 서비스 기동 + CVC 저장/검색/통계 API 완성

| 작업 | 파일 | 설명 |
|------|------|------|
| 2.1 | `Dockerfile`, `CMakeLists.txt`, `main.cpp` | 서비스 기본 구조 (PKD Relay 패턴) |
| 2.2 | `app_config.h`, `service_container.h/.cpp` | DI 컨테이너 (기존 패턴) |
| 2.3 | DB 스키마 (PostgreSQL + Oracle) | `cvc_certificate`, `eac_trust_chain` |
| 2.4 | `domain/cvc_models.h` | DB 레코드 모델 |
| 2.5 | `repositories/cvc_certificate_repository.h/.cpp` | CRUD (IQueryExecutor) |
| 2.6 | `services/cvc_service.h/.cpp` | 업로드/검색/중복체크/통계 |
| 2.7 | `handlers/eac_upload_handler.h/.cpp` | POST /upload, /preview |
| 2.8 | `handlers/eac_certificate_handler.h/.cpp` | GET /certificates, /{id} |
| 2.9 | `handlers/eac_statistics_handler.h/.cpp` | GET /statistics, /countries |

**완료 기준**: CVC 업로드 → DB 저장 → 검색 → 상세 조회 → 통계 전체 동작

### Phase 3: 신뢰체인 검증 (1-2주)

**목표**: CVCA → DV → IS 체인 구축 + 서명 검증

| 작업 | 파일 | 설명 |
|------|------|------|
| 3.1 | `services/eac_chain_validator.h/.cpp` | CAR 기반 발급자 조회 → 체인 구축 |
| 3.2 | `repositories/eac_trust_chain_repository.h/.cpp` | 체인 기록 저장/조회 |
| 3.3 | `handlers/eac_validation_handler.h/.cpp` | GET /certificates/{id}/chain |
| 3.4 | GTest 통합 테스트 | 체인 검증 시나리오 테스트 |

**완료 기준**: IS 인증서 → DV → CVCA 체인 검증 결과 반환 + DB 기록

### Phase 4: 프론트엔드 (2-3주)

**목표**: CVC 관리 페이지 4개 + 사이드바 통합

| 작업 | 파일 | 설명 |
|------|------|------|
| 4.1 | `eacApi.ts` | API 클라이언트 모듈 |
| 4.2 | `CvcUpload.tsx` | 업로드 + 미리보기 페이지 |
| 4.3 | `CvcSearch.tsx` + `CvcDetailDialog.tsx` | 검색 + 상세 |
| 4.4 | `EacDashboard.tsx` | 통계 대시보드 |
| 4.5 | `EacTrustChain.tsx` | 신뢰체인 시각화 |
| 4.6 | `Sidebar.tsx`, `App.tsx` | 라우트 + 메뉴 통합 |

**완료 기준**: 프론트엔드에서 CVC 업로드/검색/통계/체인 시각화 전체 동작

### Phase 5: 인프라 통합 (1주)

**목표**: Docker Compose 통합, nginx 라우팅, 문서

| 작업 | 파일 | 설명 |
|------|------|------|
| 5.1 | `docker-compose.yaml` | EAC 서비스 추가 (profile: eac) |
| 5.2 | nginx 설정 | `/api/eac` location 블록 |
| 5.3 | 헬스 체크/빌드 스크립트 | EAC 서비스 포함 |
| 5.4 | 문서 업데이트 | CLAUDE.md, OpenAPI 스펙 |

---

## 9. 리스크 및 대응

| 리스크 | 수준 | 영향 | 대응 |
|--------|------|------|------|
| CVC 테스트 데이터 부재 | **높음** | 파서 검증 불가 | BSI 공개 테스트 인증서 활용, 자체 테스트 벡터 생성 |
| TLV 파싱 경계 검증 | **높음** | 버퍼 오버리드 | 모든 오프셋 접근 전 경계 검사, GTest fuzz 테스트 |
| CVC 서명 검증 복잡성 | **중간** | EAC OID → OpenSSL EVP 매핑 오류 | 기존 algorithm_compliance.cpp 패턴 참조 |
| Oracle BLOB 처리 | **중간** | 데이터 잘림 | 기존 `RAWTOHEX(DBMS_LOB.SUBSTR(...))` 패턴 적용 |
| vcpkg 의존성 | **낮음** | 빌드 환경 차이 | 기존 vcpkg-base 이미지 재사용, 추가 의존성 없음 |
| 프론트엔드 공수 | **낮음** | 기존 패턴 재사용으로 경감 | CertificateSearch/UploadDashboard 패턴 복제 |

---

## 10. 향후 확장 계획

실험 버전 검증 후 고려할 확장:

1. **LDAP 저장**: `o=cvca`, `o=dv`, `o=is` OU 추가 + PKD Relay 동기화
2. **AI 분석 통합**: CVC 이상 탐지 (짧은 유효기간 패턴, CHAT 권한 이상)
3. **기존 PA 서비스 연동**: PA 검증 시 EAC PKI 상태 참조
4. **PACE/CA/TA 프로토콜**: 스마트카드 리더 통합 (별도 프로젝트)

---

## 부록

### A. 참조 규격

| 규격 | 버전 | 설명 |
|------|------|------|
| BSI TR-03110 Part 1 | 2.21 | EAC 프로토콜 개요 |
| BSI TR-03110 Part 2 | 2.21 | 프로토콜 명세 (PACE, CA, TA) |
| BSI TR-03110 Part 3 | 2.21 | 데이터 구조 (CVC 형식, CHAT, OID) |
| BSI TR-03110 Part 4 | 2.21 | 프로파일 및 알고리즘 |
| ISO 7816-4 | - | TLV 데이터 구조 기반 |
| ICAO 9303 Part 11 | - | EAC 보안 메커니즘 (ICAO 관점) |

### B. 기존 코드 참조 (C++ 패턴)

| 파일 | 참조 이유 |
|------|-----------|
| `services/pkd-relay/src/main.cpp` | 경량 Drogon 서비스 구조 패턴 |
| `services/pkd-relay/src/infrastructure/service_container.h` | ServiceContainer pImpl 패턴 |
| `services/pkd-management/src/handlers/certificate_handler.h` | Handler 패턴 (라우트 등록) |
| `services/pkd-management/src/repositories/certificate_repository.h` | Repository 패턴 (IQueryExecutor) |
| `shared/lib/database/include/icao/database/i_query_executor.h` | DB 추상화 인터페이스 |
| `shared/lib/icao-validation/src/algorithm_compliance.cpp` | 알고리즘 OID 매핑 패턴 |
| `shared/lib/icao9303/src/sod_parser.cpp` | 바이너리 파싱 패턴 (ASN.1 TLV) |
| `shared/lib/certificate-parser/` | 인증서 메타데이터 추출 패턴 |
| `docker/init-scripts/01-core-schema.sql` | PostgreSQL 스키마 패턴 |
| `docker/db-oracle/init/03-core-schema.sql` | Oracle 스키마 패턴 |
