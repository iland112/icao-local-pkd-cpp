# CSR 기반 TLS 인증서 발급 및 ICAO PKD 연결 — 구현 보고서

> **작성일**: 2026-03-22
> **상태**: 전체 구현 완료 (Phase 1~3)
> **목표**: CSR 관리 → Private CA 서명 → ICAO PKD LDAP TLS 상호 인증 End-to-End 연동

---

## 1. ICAO PKD LDAP 통신 전체 절차

### 1.1 전체 흐름도

```
┌─────────────────────────────────────────────────────────────────────────┐
│                                                                         │
│   [Step 1]  CSR 생성                                                    │
│   ┌────────────────────────────────────────────┐                       │
│   │  CSR 관리 페이지 (/admin/csr)               │                       │
│   │                                            │                       │
│   │  관리자 입력:                                │                       │
│   │    Country Code: KR                        │                       │
│   │    Organization: Ministry of Justice        │                       │
│   │    Common Name:  KRDownloader03            │                       │
│   │                                            │                       │
│   │  자동 생성:                                  │                       │
│   │    RSA-2048 키 페어 (공개키 + 개인키)         │                       │
│   │    PKCS#10 CSR (SHA256withRSA 서명)          │                       │
│   │                                            │                       │
│   │  DB 저장:                                   │                       │
│   │    csr_pem ← AES-256-GCM 암호화            │                       │
│   │    private_key_encrypted ← AES-256-GCM     │                       │
│   │    status: CREATED                         │                       │
│   └────────────────────────────────────────────┘                       │
│                         │                                               │
│                         ▼                                               │
│   [Step 2]  인증서 발급 (2가지 경로)                                     │
│   ┌─────────────────────┬──────────────────────┐                       │
│   │  경로 A: 로컬 CA     │  경로 B: ICAO 발급    │                       │
│   │  (개발/테스트)        │  (프로덕션)           │                       │
│   │                     │                      │                       │
│   │  "CA 인증서 발급"    │  "PEM 내보내기"       │                       │
│   │  버튼 클릭           │  → CSR 파일 다운로드   │                       │
│   │       │             │       │              │                       │
│   │       ▼             │       ▼              │                       │
│   │  Private CA가       │  ICAO에 이메일 제출   │                       │
│   │  CSR 자동 서명       │       │              │                       │
│   │  (SHA256, 365일)    │       ▼              │                       │
│   │       │             │  ICAO가 D-Trust CA로 │                       │
│   │       │             │  인증서 발급          │                       │
│   │       │             │       │              │                       │
│   │       │             │       ▼              │                       │
│   │       │             │  "ICAO 발급 인증서    │                       │
│   │       │             │   등록" 버튼 클릭     │                       │
│   │       │             │  (공개키 매칭 검증)    │                       │
│   │       ▼             │       ▼              │                       │
│   │  status: ISSUED     │  status: ISSUED      │                       │
│   └─────────────────────┴──────────────────────┘                       │
│                         │                                               │
│                         ▼                                               │
│   [Step 3]  TLS 인증서 파일 배포                                         │
│   ┌────────────────────────────────────────────┐                       │
│   │  /app/icao-tls/ (Docker 볼륨)               │                       │
│   │                                            │                       │
│   │  ┌──────────────┐  CSR 서명 시 자동 생성    │                       │
│   │  │ client.pem   │  ← 클라이언트 인증서      │                       │
│   │  │              │     (CA 서명된 공개키)     │                       │
│   │  ├──────────────┤                          │                       │
│   │  │ client-key   │  ← 클라이언트 개인키      │                       │
│   │  │   .pem       │     (DB에서 복호화)       │                       │
│   │  ├──────────────┤                          │                       │
│   │  │ ca.pem       │  ← CA 인증서             │                       │
│   │  │              │     (서버 검증용)         │                       │
│   │  └──────────────┘                          │                       │
│   │                                            │                       │
│   │  ┌──────────────┐  init-icao-sim-cert.sh   │                       │
│   │  │ ldap-server  │  ← LDAP 서버 인증서      │                       │
│   │  │   .crt       │     (Private CA 서명)     │                       │
│   │  ├──────────────┤                          │                       │
│   │  │ ldap-server  │  ← LDAP 서버 개인키      │                       │
│   │  │   .key       │                          │                       │
│   │  └──────────────┘                          │                       │
│   └────────────────────────────────────────────┘                       │
│                         │                                               │
│                         ▼                                               │
│   [Step 4]  TLS 상호 인증 연결 (LDAPS)                                   │
│   ┌────────────────────────────────────────────────────────────┐       │
│   │                                                            │       │
│   │  PKD Relay                    ICAO PKD LDAP               │       │
│   │  ┌──────────┐                ┌──────────────┐             │       │
│   │  │          │───── TLS ─────→│              │             │       │
│   │  │ LDAP V3  │  ClientHello   │  LDAPS :636  │             │       │
│   │  │ Client   │←── ServerHello │  OpenLDAP    │             │       │
│   │  │          │   + 서버 인증서  │  TLS 서버     │             │       │
│   │  │          │                │              │             │       │
│   │  │  ① 서버 인증서 검증         │              │             │       │
│   │  │    ca.pem으로 서버 cert 확인│              │             │       │
│   │  │                           │              │             │       │
│   │  │  ② 클라이언트 인증서 전송    │              │             │       │
│   │  │    client.pem ──────────→ │              │             │       │
│   │  │                           │              │             │       │
│   │  │                           │ ③ 클라이언트   │             │       │
│   │  │                           │   인증서 검증  │             │       │
│   │  │                           │   ca.pem으로  │             │       │
│   │  │                           │   client 확인 │             │       │
│   │  │                           │              │             │       │
│   │  │  ④ TLS 핸드셰이크 완료      │              │             │       │
│   │  │    (암호화 채널 수립)        │              │             │       │
│   │  │                           │              │             │       │
│   │  │  ⑤ LDAP Bind (2가지 모드)  │              │             │       │
│   │  │                           │              │             │       │
│   │  │  [모드 A] Simple Bind      │              │             │       │
│   │  │    over TLS               │              │             │       │
│   │  │    DN + password 전송      │              │             │       │
│   │  │    (개발/스테이징)          │              │             │       │
│   │  │                           │              │             │       │
│   │  │  [모드 B] SASL EXTERNAL    │              │             │       │
│   │  │    인증서 기반 신원 확인     │              │             │       │
│   │  │    비밀번호 불필요          │              │             │       │
│   │  │    (프로덕션 ICAO PKD)     │              │             │       │
│   │  │                           │              │             │       │
│   │  │  ⑥ LDAP Search 시작       │              │             │       │
│   │  │    인증서 다운로드          │              │             │       │
│   │  └──────────┘                └──────────────┘             │       │
│   │                                                            │       │
│   └────────────────────────────────────────────────────────────┘       │
│                         │                                               │
│                         ▼                                               │
│   [Step 5]  인증서 동기화                                                │
│   ┌────────────────────────────────────────────────────────────┐       │
│   │                                                            │       │
│   │  ICAO PKD LDAP                     Local PKD              │       │
│   │  dc=download,dc=pkd,              ┌──────────────┐       │       │
│   │  dc=icao,dc=int                   │              │       │       │
│   │                                   │   Oracle DB  │       │       │
│   │  ┌─ dc=data ──────────┐          │   + 메타추출  │       │       │
│   │  │  c=KR              │  ──────→  │   + Trust    │       │       │
│   │  │   o=csca (CSCA)    │  LDAP V3  │     Chain    │       │       │
│   │  │   o=dsc  (DSC)     │  검색     │     검증     │       │       │
│   │  │   o=crl  (CRL)     │          │   + CRL 확인  │       │       │
│   │  │  c=JP              │          │              │       │       │
│   │  │   o=csca           │          ├──────────────┤       │       │
│   │  │   o=dsc            │          │              │       │       │
│   │  │   ...              │          │  Local LDAP  │       │       │
│   │  └────────────────────┘          │  OpenLDAP    │       │       │
│   │  ┌─ dc=nc-data ───────┐          │  (MMR)       │       │       │
│   │  │  c=XX              │  ──────→  │              │       │       │
│   │  │   o=dsc (DSC_NC)   │          │              │       │       │
│   │  └────────────────────┘          └──────────────┘       │       │
│   │                                                            │       │
│   │  처리 파이프라인:                                            │       │
│   │  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐          │       │
│   │  │ CSCA   │→ │  DSC   │→ │  CRL   │→ │ DSC_NC │          │       │
│   │  │ 889건  │  │30,046건│  │  90건  │  │ 502건  │          │       │
│   │  └────────┘  └────────┘  └────────┘  └────────┘          │       │
│   │       │           │           │           │                │       │
│   │       ▼           ▼           ▼           ▼                │       │
│   │  fingerprint 중복 체크 (SHA-256)                            │       │
│   │       │                                                    │       │
│   │       ├── 기존: SKIP                                       │       │
│   │       │                                                    │       │
│   │       └── 신규:                                            │       │
│   │            ├── X.509 메타데이터 22개 필드 추출               │       │
│   │            ├── DB 저장 (certificate 테이블)                 │       │
│   │            ├── LDAP 저장 (로컬 OpenLDAP)                   │       │
│   │            ├── Trust Chain 검증 (DSC→Link→Root CSCA)       │       │
│   │            ├── CRL 폐기 확인                               │       │
│   │            └── validation_result 테이블 저장               │       │
│   │                                                            │       │
│   └────────────────────────────────────────────────────────────┘       │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 인증서 신뢰 체인 (Trust Chain)

```
Private CA (ca.key / ca.crt)
    │
    │  ICAO Local PKD Private CA
    │  C=KR/O=SmartCore Inc./OU=PKD Operations
    │  RSA 4096-bit, 10년 유효
    │
    ├── [서버 인증서] ldap-server.crt
    │       Subject: C=CH/O=ICAO/OU=PKD/CN=icao-pkd-ldap
    │       SAN: DNS=icao-pkd-ldap, localhost
    │       RSA 2048-bit, 365일 유효
    │       용도: ICAO PKD LDAP 서버의 LDAPS 서비스
    │
    └── [클라이언트 인증서] client.pem
            Subject: C=KR/O=Ministry of Justice/CN=KRDownloader03
            RSA 2048-bit, 365일 유효
            용도: PKD Relay가 SASL EXTERNAL 인증에 사용

동일 CA → 서버/클라이언트 상호 검증 가능
```

### 1.3 TLS 상호 인증 상세 시퀀스

```
  PKD Relay (Client)                              ICAO PKD LDAP (Server)
       │                                                │
   ①  │──── TCP Connect ──────────────────────────────→│  ldaps://host:636
       │                                                │
   ②  │──── ClientHello ─────────────────────────────→│  TLS 1.2/1.3
       │     (지원 cipher suites, 프로토콜 버전)          │
       │                                                │
   ③  │←─── ServerHello + ServerCertificate ──────────│
       │     (ldap-server.crt 전송)                      │
       │                                                │
   ④  │     서버 인증서 검증:                             │
       │     ca.pem으로 ldap-server.crt 서명 확인         │
       │     CN/SAN 호스트명 일치 확인                     │
       │     유효기간 확인                                │
       │                                                │
   ⑤  │←─── CertificateRequest ──────────────────────│
       │     (서버가 클라이언트 인증서 요청)                │
       │                                                │
   ⑥  │──── ClientCertificate ───────────────────────→│
       │     (client.pem 전송)                           │
       │                                                │
   ⑦  │──── CertificateVerify ──────────────────────→│
       │     (client-key.pem으로 서명 증명)               │
       │                                                │
   ⑧  │                                                │  클라이언트 인증서 검증:
       │                                                │  ca.pem으로 client.pem 서명 확인
       │                                                │  Subject DN에서 신원 추출
       │                                                │
   ⑨  │──── Finished ←─→ Finished ──────────────────│  TLS 핸드셰이크 완료
       │     (암호화 채널 수립)                            │
       │                                                │
   ⑩  │──── LDAP Bind ─────────────────────────────→│
       │                                                │
       │   [모드 A] Simple Bind over TLS (개발/스테이징)   │
       │     DN="cn=admin,dc=icao,dc=int"                │
       │     password="***"                              │
       │     ※ TLS 암호화 채널에서 전송 (평문 아님)         │
       │                                                │
       │   [모드 B] SASL EXTERNAL (프로덕션 ICAO PKD)     │
       │     DN="" mechanism="EXTERNAL"                  │
       │     비밀번호 없음 — 인증서로 인증 완료             │
       │                                                │
   ⑪  │←─── Bind Success ───────────────────────────│
       │                                                │
   ⑫  │──── LDAP Search ────────────────────────────→│
       │     base: dc=data,dc=download,...               │
       │     filter: (objectClass=pkdDownload)           │
       │     scope: subtree                              │
       │                                                │
   ⑬  │←─── Search Results (인증서 바이너리) ──────────│
       │     userCertificate;binary (DER)                │
       │     certificateRevocationList;binary (DER)      │
       │                                                │
   ⑭  │──── Unbind ─────────────────────────────────→│
       │                                                │
```

### 1.4 환경별 설정 비교

```
┌──────────────────────────────────────────────────────────────────┐
│              모드 1: 평문 (개발 빠른 테스트)                       │
│                                                                  │
│  .env:                                                           │
│    ICAO_LDAP_USE_TLS=false                                      │
│    ICAO_LDAP_HOST=icao-pkd-ldap                                 │
│    ICAO_LDAP_PORT=389                  ← LDAP (평문)             │
│    ICAO_LDAP_BIND_DN=cn=admin,dc=icao,dc=int                   │
│                                                                  │
│  인증: Simple Bind (평문 채널)                                   │
│  용도: 빠른 기능 테스트, TLS 인증서 불필요                        │
│                                                                  │
├──────────────────────────────────────────────────────────────────┤
│              모드 2: TLS + Simple Bind (개발/스테이징)             │
│                                                                  │
│  .env:                                                           │
│    ICAO_LDAP_USE_TLS=true                                       │
│    ICAO_LDAP_HOST=icao-pkd-ldap                                 │
│    ICAO_LDAP_PORT=636                  ← LDAPS (암호화)          │
│    ICAO_LDAP_BIND_DN=cn=admin,dc=icao,dc=int                   │
│    ICAO_LDAP_TLS_CERT_FILE=/app/icao-tls/client.pem             │
│    ICAO_LDAP_TLS_KEY_FILE=/app/icao-tls/client-key.pem          │
│    ICAO_LDAP_TLS_CA_CERT_FILE=/app/icao-tls/ca.pem              │
│                                                                  │
│  인증: Simple Bind over TLS (암호화 채널 + DN/password)           │
│  CA: Private CA (자체 서명)                                      │
│  인증서: CSR 관리 → "CA 인증서 발급" 버튼                        │
│                                                                  │
├──────────────────────────────────────────────────────────────────┤
│              모드 3: TLS + SASL EXTERNAL (프로덕션)               │
│                                                                  │
│  .env:                                                           │
│    ICAO_LDAP_USE_TLS=true                                       │
│    ICAO_LDAP_HOST=pkddownloadsg.icao.int                        │
│    ICAO_LDAP_PORT=636                  ← LDAPS (암호화)          │
│    ICAO_LDAP_BIND_DN=                  ← 비워두면 SASL EXTERNAL  │
│    ICAO_LDAP_TLS_CERT_FILE=/app/icao-tls/client.pem             │
│    ICAO_LDAP_TLS_KEY_FILE=/app/icao-tls/client-key.pem          │
│    ICAO_LDAP_TLS_CA_CERT_FILE=/app/icao-tls/d-trust-ca.pem     │
│                                                                  │
│  인증: SASL EXTERNAL (인증서 기반, 비밀번호 불필요)               │
│  CA: D-Trust Extended Validation TLS CA (ICAO 공인 CA)           │
│  인증서: CSR PEM → ICAO 제출 → ICAO 발급 → "인증서 등록"        │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘

모드 전환: .env 값 변경만으로 전환 (코드 변경 없음)
핵심 분기: ICAO_LDAP_BIND_DN 유무 → Simple Bind / SASL EXTERNAL 자동 선택
```

---

## 2. 구현 상세

### Phase 1: Private CA 인증서 발급 + UI (완료)

#### Backend — `POST /api/csr/{id}/sign`

| 항목 | 구현 내용 |
|------|---------|
| **API** | `POST /api/csr/{id}/sign` — Private CA로 CSR 서명 |
| **서명 로직** | OpenSSL `X509_REQ` → `X509` 변환, SHA256 서명, 365일 유효 |
| **CA 로드** | `/app/ssl/ca.key` + `/app/ssl/ca.crt` (환경변수로 변경 가능) |
| **인증서 저장** | 기존 `registerCertificate()` 재사용 → DB ISSUED 자동 전환 |
| **TLS 파일 출력** | `/app/icao-tls/client.pem`, `client-key.pem`, `ca.pem` 저장 |
| **개인키 복호화** | DB에서 AES-256-GCM 복호화 → PEM 파일 저장 |
| **감사 로그** | `CSR_GENERATE` 타입으로 기록 |

#### Frontend — CsrManagement 페이지

| 버튼 | 상태 | 동작 |
|------|------|------|
| **CA 인증서 발급** (blue) | CREATED | Private CA로 즉시 서명 → ISSUED |
| **ICAO 발급 인증서 등록** (amber) | CREATED | PEM 수동 입력 → 공개키 매칭 → ISSUED |
| **ICAO PKD 연결 적용** (green) | ISSUED | ICAO PKD 동기화 페이지로 이동 |

### Phase 2: ICAO PKD LDAP TLS 서버 설정 (완료)

| 항목 | 내용 |
|------|------|
| 서버 인증서 발급 | `scripts/ssl/init-icao-sim-cert.sh` — Private CA로 서버 cert 발급 |
| Docker TLS 활성화 | `LDAP_TLS=true`, `LDAP_TLS_VERIFY_CLIENT=try` |
| LDAPS 포트 | `13636:636` |
| TLS 볼륨 | `.docker-data/icao-pkd-tls/` → slapd certs |

### Phase 3: PKD Relay TLS 연결 (완료)

| 항목 | 내용 |
|------|------|
| .env TLS 설정 | `ICAO_LDAP_USE_TLS=true`, `ICAO_LDAP_PORT=636` |
| 인증 모드 | TLS / Simple Bind (BIND_DN 설정 시) 또는 SASL EXTERNAL (BIND_DN 미설정 시) |
| 연결 테스트 결과 | TLS / Simple Bind, **11ms**, 31,277건 조회 성공 |
| E2E 검증 | CSR → CA 서명 → client.pem → LDAPS:636 → 동기화 ✅ |

---

## 3. 파일 구조

```
.docker-data/icao-pkd-tls/
├── ca.pem              ← Private CA 인증서 (서버/클라이언트 공용)
├── client.pem          ← 클라이언트 인증서 (CSR → CA 서명)
├── client-key.pem      ← 클라이언트 개인키 (DB 복호화)
├── ldap-server.crt     ← LDAP 서버 인증서 (Private CA 서명)
└── ldap-server.key     ← LDAP 서버 개인키
```

## 4. 검증 결과

| 항목 | 결과 |
|------|------|
| PKD Management Docker 빌드 | ✅ Built |
| Frontend TypeScript 검증 | ✅ 에러 없음 |
| ICAO LDAP LDAPS 내부 테스트 | ✅ ldapsearch -H ldaps://localhost |
| PKD Relay TLS 연결 | ✅ TLS / Simple Bind, 11ms, 31,277건 |
| TLS 파일 생성 | ✅ 5개 파일 (client + server + CA) |
| 전체 서비스 | ✅ healthy |

### E2E 시나리오 검증

```
1. CSR 생성 (POST /api/csr/generate)           → RSA-2048      ✅
2. CA 인증서 발급 (POST /api/csr/{id}/sign)      → Private CA 서명 ✅
3. TLS 파일 자동 저장                            → 3개 파일       ✅
4. LDAP TLS 서버 (icao-pkd-ldap:636)            → LDAPS 활성화   ✅
5. PKD Relay TLS 연결 (TLS / Simple Bind)        → 11ms           ✅
6. 인증서 동기화 (CSCA/DSC/CRL/DSC_NC)          → 31,277건       ✅
```
