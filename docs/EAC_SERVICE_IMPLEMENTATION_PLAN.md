# BSI TR-03110 EAC Service 구현 계획서

**버전**: v0.1.0 (초안)
**작성일**: 2026-03-12
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
| OpenSSL 지원 | 완전 지원 | **미지원** (직접 파싱) |
| 권한 제어 | 없음 | CHAT 비트마스크 (DG3/DG4 접근 제어) |

### 1.4 스코프 정의

**포함 (실험 버전):**
- CVC 인증서 파싱 (TLV 바이트 파싱)
- CVC 업로드/저장/검색/내보내기
- EAC 신뢰체인 검증 (CVCA → DV → IS)
- CHAT 권한 분석 및 시각화
- DB 저장 (PostgreSQL + Oracle)
- REST API 제공 (:8086)
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
| 언어 | Python 3.12 | 빠른 프로토타이핑, CVC 파싱 유연성 |
| 프레임워크 | FastAPI | AI Analysis 서비스와 동일, 검증된 패턴 |
| DB ORM | SQLAlchemy 2.0 | 기존 AI 서비스 패턴 재사용 |
| DB 드라이버 | asyncpg + oracledb | Multi-DBMS 지원 (기존 패턴) |
| CVC 파싱 | 직접 구현 (TLV) | 표준 라이브러리 부재 |
| 암호화 | cryptography (pyca) | CVC 서명 검증 (RSA/ECDSA) |

### 2.3 디렉토리 구조

```
services/eac-service/
├── Dockerfile
├── requirements.txt
├── pytest.ini
├── app/
│   ├── __init__.py
│   ├── main.py                     # FastAPI 앱 (:8086)
│   ├── config.py                   # DB_TYPE 기반 설정 (Settings)
│   ├── database.py                 # PostgreSQL + Oracle 이중 엔진
│   │
│   ├── models/                     # SQLAlchemy 모델
│   │   ├── __init__.py
│   │   └── cvc_certificate.py      # cvc_certificate 테이블 모델
│   │
│   ├── schemas/                    # Pydantic 응답 스키마
│   │   ├── __init__.py
│   │   └── cvc.py                  # CvcCertificateResponse, CvcUploadResponse 등
│   │
│   ├── parsers/                    # CVC 파싱 엔진
│   │   ├── __init__.py
│   │   ├── tlv.py                  # 범용 TLV (Tag-Length-Value) 파서
│   │   ├── cvc_parser.py           # BSI TR-03110 CVC 인증서 파서
│   │   └── chat_decoder.py         # CHAT 비트마스크 디코더
│   │
│   ├── services/                   # 비즈니스 로직
│   │   ├── __init__.py
│   │   ├── cvc_service.py          # CVC 업로드/검색/CRUD
│   │   ├── eac_chain_validator.py  # CVCA→DV→IS 신뢰체인 검증
│   │   └── cvc_signature.py        # CVC 서명 검증 (RSA/ECDSA)
│   │
│   └── routers/                    # API 엔드포인트
│       ├── __init__.py
│       ├── health.py               # GET /health
│       ├── upload.py               # POST /api/eac/upload, /preview
│       ├── certificates.py         # GET /api/eac/certificates, /{id}
│       ├── validation.py           # GET /api/eac/trust-chain/{fp}
│       └── statistics.py           # GET /api/eac/statistics
│
├── tests/                          # pytest 테스트
│   ├── conftest.py
│   ├── test_tlv_parser.py
│   ├── test_cvc_parser.py
│   ├── test_chat_decoder.py
│   ├── test_chain_validator.py
│   └── fixtures/                   # 테스트용 CVC 바이너리
│       └── README.md
│
└── docs/
    └── CVC_FORMAT_REFERENCE.md     # CVC TLV 구조 참조 문서
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

### 3.2 CHAT 역할 및 권한

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

### 3.3 EAC 알고리즘 OID

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
    - eac  # 선택적 활성화
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

### 7.3 DB 초기화

- PostgreSQL: `docker/init-scripts/20-eac-schema.sql`
- Oracle: `docker/db-oracle/init/20-eac-schema.sql`

---

## 8. 구현 단계

### Phase 1: 서비스 스캐폴딩 + CVC 파서 (1-2주)

**목표**: FastAPI 서비스 기동 + CVC TLV 파서 구현 + 단위 테스트

| 작업 | 파일 | 설명 |
|------|------|------|
| 1.1 | `Dockerfile`, `requirements.txt`, `main.py` | AI Analysis 패턴 복제 |
| 1.2 | `config.py`, `database.py` | Multi-DBMS 설정 |
| 1.3 | `parsers/tlv.py` | 범용 TLV 파서 (tag, length, value 추출) |
| 1.4 | `parsers/cvc_parser.py` | CVC 구조 파싱 (Body, Public Key, Signature) |
| 1.5 | `parsers/chat_decoder.py` | CHAT 비트마스크 → 권한 목록 |
| 1.6 | `tests/test_*.py` | 파서 단위 테스트 |
| 1.7 | `routers/health.py` | 헬스 체크 엔드포인트 |

**완료 기준**: `POST /api/eac/upload/preview`로 CVC 바이너리 업로드 시 파싱 결과 반환

### Phase 2: DB + CRUD API (1-2주)

**목표**: CVC 저장/검색/통계 API 완성

| 작업 | 파일 | 설명 |
|------|------|------|
| 2.1 | DB 스키마 (PostgreSQL + Oracle) | `cvc_certificate`, `eac_trust_chain` 테이블 |
| 2.2 | `models/cvc_certificate.py` | SQLAlchemy 모델 |
| 2.3 | `schemas/cvc.py` | Pydantic 응답 스키마 |
| 2.4 | `services/cvc_service.py` | CRUD + 중복 체크 + 통계 |
| 2.5 | `routers/upload.py` | 업로드 + 미리보기 엔드포인트 |
| 2.6 | `routers/certificates.py` | 검색 + 상세 엔드포인트 |
| 2.7 | `routers/statistics.py` | 통계 엔드포인트 |

**완료 기준**: CVC 업로드 → DB 저장 → 검색 → 상세 조회 전 흐름 동작

### Phase 3: 신뢰체인 검증 (1-2주)

**목표**: CVCA → DV → IS 체인 구축 + CVC 서명 검증

| 작업 | 파일 | 설명 |
|------|------|------|
| 3.1 | `services/cvc_signature.py` | CVC 서명 검증 (RSA/ECDSA) |
| 3.2 | `services/eac_chain_validator.py` | CAR 기반 발급자 조회 → 체인 구축 |
| 3.3 | `routers/validation.py` | 신뢰체인 조회 엔드포인트 |
| 3.4 | 단위/통합 테스트 | 체인 검증 테스트 |

**완료 기준**: IS 인증서 → DV → CVCA 체인 검증 결과 반환

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

**목표**: Docker Compose 통합, nginx 라우팅, CI/CD

| 작업 | 파일 | 설명 |
|------|------|------|
| 5.1 | `docker-compose.yaml` | EAC 서비스 추가 (profile: eac) |
| 5.2 | nginx 설정 | `/api/eac` location 블록 |
| 5.3 | 헬스 체크 스크립트 | EAC 서비스 포함 |
| 5.4 | 문서 업데이트 | CLAUDE.md, OpenAPI 스펙 |

---

## 9. 리스크 및 대응

| 리스크 | 수준 | 영향 | 대응 |
|--------|------|------|------|
| CVC 테스트 데이터 부재 | **높음** | 파서 검증 불가 | 자체 CVCA 생성 도구 구현 또는 BSI 테스트 인증서 활용 |
| TLV 파싱 복잡성 | **높음** | 구현 지연 | BSI TR-03110 Part 3 Appendix C 철저 참조, 단위 테스트 우선 |
| CVC 서명 검증 | **중간** | EAC OID → 표준 알고리즘 매핑 오류 | cryptography 라이브러리 활용, 단계적 검증 |
| Oracle BLOB 처리 | **중간** | 데이터 잘림 | 기존 `RAWTOHEX(DBMS_LOB.SUBSTR(...))` 패턴 적용 |
| 프론트엔드 공수 | **낮음** | 기존 패턴 재사용으로 경감 | CertificateSearch/UploadDashboard 패턴 복제 |

---

## 10. 향후 확장 계획

실험 버전 검증 후 고려할 확장:

1. **LDAP 저장**: `o=cvca`, `o=dv`, `o=is` OU 추가 + PKD Relay 동기화
2. **AI 분석 통합**: CVC 이상 탐지 (짧은 유효기간 패턴, CHAT 권한 이상)
3. **기존 PA 서비스 연동**: PA 검증 시 EAC PKI 상태 참조
4. **C++ 공유 라이브러리**: 검증된 파서를 `shared/lib/cvc-parser/`로 마이그레이션
5. **PACE/CA/TA 프로토콜**: 스마트카드 리더 통합 (별도 프로젝트)

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

### B. 관련 기존 코드 참조

| 파일 | 참조 이유 |
|------|-----------|
| `services/ai-analysis/app/main.py` | FastAPI 서비스 구조 패턴 |
| `services/ai-analysis/app/config.py` | Multi-DBMS 설정 패턴 |
| `services/ai-analysis/app/database.py` | SQLAlchemy 이중 엔진 패턴 |
| `services/ai-analysis/Dockerfile` | Python 서비스 Docker 빌드 패턴 |
| `shared/lib/icao-validation/include/icao/validation/providers.h` | Provider 인터페이스 패턴 |
| `docker/init-scripts/01-core-schema.sql` | PostgreSQL 스키마 패턴 |
| `docker/db-oracle/init/03-core-schema.sql` | Oracle 스키마 패턴 |
