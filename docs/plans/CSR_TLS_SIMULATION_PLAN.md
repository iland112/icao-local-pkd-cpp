# CSR 기반 TLS 시뮬레이션 구현 계획서

> **작성일**: 2026-03-22
> **상태**: 계획 수립
> **목표**: CSR 관리 → Private CA 서명 → ICAO PKD LDAP TLS 상호 인증 시뮬레이션 End-to-End 연동

---

## 1. 배경 및 목적

### 현재 상태

- CSR 생성/내보내기/인증서 등록 기능 구현 완료 (v2.35.0)
- ICAO PKD 시뮬레이션 LDAP 서버 구축 완료 (v2.39.0)
- ICAO LDAP 자동 동기화 + TLS 클라이언트 코드 구현 완료
- **문제**: CSR 발급 → TLS 연결 간 연결 고리 없음 (Simple Bind만 사용)

### 목표

CSR 관리 페이지에서 시뮬레이션 인증서를 발급하고, ICAO PKD LDAP에 TLS 상호 인증으로 연결하는 **End-to-End 시뮬레이션 환경** 구축

### 기대 효과

1. 프로덕션 전환 전 TLS 상호 인증 전체 흐름 검증 가능
2. CSR 관리 → ICAO PKD 연결까지 UI에서 원클릭 설정
3. 시뮬레이션/프로덕션 모드 .env 전환만으로 운영

---

## 2. 아키텍처

### 전체 흐름

```
CsrManagement 페이지                    ICAO PKD 동기화 페이지
┌────────────────┐                    ┌──────────────────┐
│ [1] CSR 생성    │                    │ 연결 테스트       │
│ RSA-2048       │                    │ → TLS 상호 인증   │
│                │                    │   SASL EXTERNAL   │
│ [2] 시뮬레이션  │                    │                  │
│   인증서 발급   │──→ client.pem ──→ │ 수동 동기화       │
│ Private CA 서명 │   client-key.pem  │ → LDAPS:636      │
│                │   ca.pem          │                  │
│ [3] ICAO PKD   │                    │                  │
│   연결 적용    │──→ Relay TLS 전환  │                  │
└────────────────┘                    └──────────────────┘
        │                                     │
        ▼                                     ▼
   PKD Management                     icao-pkd-ldap
   (CSR + CA 서명)                   (LDAPS:636, TLS 서버)
```

### 인증서 체인

```
Private CA (ca.key / ca.crt)
├── 시뮬레이션 LDAP 서버 인증서 (icao-sim-server.crt)
│   → icao-pkd-ldap 컨테이너가 LDAPS 서비스에 사용
└── 클라이언트 인증서 (client.crt)
    → CSR에서 Private CA로 서명하여 발급
    → PKD Relay가 SASL EXTERNAL 인증에 사용
```

---

## 3. 구현 상세

### Phase 1: Private CA 자동 서명 API + Frontend UI

#### 백엔드

| 항목 | 설명 |
|------|------|
| **신규 API** | `POST /api/csr/{id}/sign` |
| **구현 파일** | `csr_handler.cpp`, `csr_service.cpp` |
| **동작** | 1. CSR DER 로드 (DB에서 복호화) |
|         | 2. Private CA key/cert 로드 (`/app/ssl/ca.key`, `/app/ssl/ca.crt`) |
|         | 3. OpenSSL `X509_REQ` → `X509` 서명 (SHA256, 365일) |
|         | 4. 클라이언트 인증서 PEM 생성 |
|         | 5. csr_request 테이블 업데이트 (ISSUED, issued_certificate_*) |
|         | 6. 인증서 파일 저장 (`/app/icao-tls/client.pem`) |
|         | 7. 개인키 파일 저장 (`/app/icao-tls/client-key.pem`, DB에서 복호화) |
|         | 8. CA 인증서 복사 (`/app/icao-tls/ca.pem`) |
| **신규 API** | `POST /api/csr/{id}/apply-to-relay` |
| **동작** | PKD Relay의 ICAO LDAP config를 TLS 모드로 전환하는 설정 API 호출 |

#### 프론트엔드

| 항목 | 설명 |
|------|------|
| **CsrManagement.tsx** | CSR 상세 다이얼로그에 "인증서 발급" 버튼 추가 (CREATED 상태에서만) |
| **CsrManagement.tsx** | ISSUED 상태에서 "ICAO PKD 연결 적용" 버튼 추가 |
| **csrApi** | `signWithCA(id)`, `applyToRelay(id)` API 함수 추가 |

### Phase 2: 시뮬레이션 LDAP TLS 서버 설정

| 항목 | 설명 |
|------|------|
| **스크립트** | `scripts/ssl/init-icao-sim-cert.sh` — icao-pkd-ldap용 서버 인증서 발급 |
| **Docker** | `icao-pkd-ldap` 컨테이너: `LDAP_TLS=true`, 포트 636 추가 |
| **볼륨** | `.docker-data/ssl/` → `/container/service/slapd/assets/certs/` 마운트 |
| **docker-compose** | TLS 환경변수 + 636 포트 매핑 추가 |

### Phase 3: PKD Relay 동적 TLS 전환

| 항목 | 설명 |
|------|------|
| **API 확장** | `PUT /api/sync/icao-ldap/config` — useTls, certFile, keyFile, caCertFile 필드 추가 |
| **Frontend** | IcaoLdapSync 설정 패널에 TLS 모드 표시 + 인증서 정보 |
| **IcaoLdapSyncConfig** | useTls, tlsCertFile, tlsKeyFile, tlsCaCertFile 필드 추가 |

---

## 4. 파일 변경 목록

### 신규 파일

| 파일 | 설명 |
|------|------|
| `scripts/ssl/init-icao-sim-cert.sh` | 시뮬레이션 LDAP 서버 인증서 발급 |

### 수정 파일

| 파일 | 변경 내용 |
|------|---------|
| `services/pkd-management/src/handlers/csr_handler.cpp` | sign, apply-to-relay 엔드포인트 |
| `services/pkd-management/src/services/csr_service.h/cpp` | signWithCA(), applyToRelay() 메서드 |
| `services/pkd-management/src/main.cpp` | 라우트 등록 |
| `docker/docker-compose.yaml` | icao-pkd-ldap TLS 설정, PKD Management CA 볼륨 |
| `frontend/src/pages/CsrManagement.tsx` | CA 인증서 발급 + ICAO 연결 적용 버튼 |
| `frontend/src/pages/IcaoLdapSync.tsx` | TLS 상태 표시 확장 |
| `nginx/api-gateway.conf` | `/api/csr/*/sign` location |

---

## 5. 사용자 시나리오

### 시뮬레이션 모드 (로컬 개발)

```
1. CSR 관리 페이지 → "CSR 생성" → RSA-2048 키 생성
2. "인증서 발급" 클릭 → Private CA로 즉시 서명 → ISSUED
3. "ICAO PKD 연결 적용" 클릭 → Relay TLS 모드 자동 전환
4. ICAO PKD 동기화 페이지 → "연결 테스트" → TLS 상호 인증 확인
5. "수동 동기화" → LDAPS 암호화 채널로 인증서 다운로드
```

### 프로덕션 모드

```
1. CSR 관리 페이지 → "CSR 생성" → RSA-2048 키 생성
2. "PEM 내보내기" → ICAO에 이메일 제출
3. ICAO 발급 인증서 수신 → "인증서 등록" → 공개키 매칭 검증 → ISSUED
4. "ICAO PKD 연결 적용" → .env 변경 (ICAO_LDAP_HOST=pkddownloadsg.icao.int)
5. LDAPS:636 + SASL EXTERNAL 연결
```

---

## 6. 일정

| Phase | 내용 | 예상 |
|-------|------|------|
| Phase 1 | Private CA 자동 서명 + UI | 핵심 구현 |
| Phase 2 | LDAP TLS 서버 설정 | 인프라 설정 |
| Phase 3 | Relay 동적 전환 + E2E 테스트 | 연동 검증 |
