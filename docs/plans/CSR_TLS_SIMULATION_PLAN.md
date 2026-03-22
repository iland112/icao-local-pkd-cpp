# CSR 기반 TLS 인증서 발급 및 ICAO PKD 연결 — 구현 보고서

> **작성일**: 2026-03-22
> **상태**: 전체 구현 완료 (Phase 1~3)
> **목표**: CSR 관리 → Private CA 서명 → ICAO PKD LDAP TLS 상호 인증 End-to-End 연동

---

## 1. 구현 완료 항목

### Phase 1: Private CA 인증서 발급 + UI (완료)

#### Backend — `POST /api/csr/{id}/sign`

| 항목 | 구현 내용 |
|------|---------|
| **API** | `POST /api/csr/{id}/sign` — Private CA로 CSR 서명 |
| **서명 로직** | OpenSSL `X509_REQ` → `X509` 변환, SHA256 서명, 365일 유효 |
| **CA 로드** | `/app/ssl/ca.key` + `/app/ssl/ca.crt` (환경변수 CA_KEY_PATH, CA_CERT_PATH로 변경 가능) |
| **인증서 저장** | 기존 `registerCertificate()` 재사용 → DB ISSUED 자동 전환 |
| **TLS 파일 출력** | `/app/icao-tls/client.pem`, `client-key.pem`, `ca.pem` 저장 |
| **개인키 복호화** | DB에서 AES-256-GCM 복호화 → PEM 파일 저장 |
| **감사 로그** | `CSR_GENERATE` 타입으로 기록 (metadata: action=signWithCA) |

#### Frontend — CsrManagement 페이지

| 버튼 | 상태 | 동작 |
|------|------|------|
| **CA 인증서 발급** (blue) | CREATED | Private CA로 즉시 서명 → ISSUED 전환 |
| **ICAO 발급 인증서 등록** (amber) | CREATED | PEM 수동 입력 → 공개키 매칭 → ISSUED (프로덕션용) |
| **ICAO PKD 연결 적용** (green) | ISSUED | ICAO PKD 동기화 페이지로 네비게이션 |

#### Docker 설정

```yaml
# PKD Management 볼륨 추가
volumes:
  - ../.docker-data/ssl:/app/ssl:ro              # Private CA (read-only)
  - ../.docker-data/icao-pkd-tls:/app/icao-tls   # TLS 출력 (client cert/key)
```

### 파일 변경 목록

| 파일 | 변경 |
|------|------|
| `services/pkd-management/src/services/csr_service.h` | `signWithCA()` 메서드 선언 |
| `services/pkd-management/src/services/csr_service.cpp` | `signWithCA()` 구현 (120줄) |
| `services/pkd-management/src/handlers/csr_handler.h` | `handleSignWithCA()` 선언 |
| `services/pkd-management/src/handlers/csr_handler.cpp` | `/api/csr/{id}/sign` 엔드포인트 + 핸들러 |
| `frontend/src/services/csrApi.ts` | `signWithCA(id)` API 함수 |
| `frontend/src/pages/CsrManagement.tsx` | CA 발급 + ICAO 연결 적용 버튼 |
| `docker/docker-compose.yaml` | PKD Management CA/TLS 볼륨 마운트 |

---

## 2. 구현 완료 항목 (Phase 2~3)

### Phase 2: ICAO PKD LDAP TLS 서버 설정 (완료)

| 항목 | 내용 | 상태 |
|------|------|------|
| 서버 인증서 발급 | `scripts/ssl/init-icao-sim-cert.sh` — Private CA로 LDAP 서버 cert 발급 | ✅ 완료 |
| Docker TLS 활성화 | `LDAP_TLS=true`, `LDAP_TLS_VERIFY_CLIENT=try` | ✅ 완료 |
| LDAPS 포트 636 | docker-compose `13636:636` 매핑 | ✅ 완료 |
| TLS 볼륨 마운트 | `.docker-data/icao-pkd-tls/` → `/container/service/slapd/assets/certs/` | ✅ 완료 |

### Phase 3: PKD Relay TLS 연결 (완료)

| 항목 | 내용 | 상태 |
|------|------|------|
| .env TLS 설정 | `ICAO_LDAP_USE_TLS=true`, `ICAO_LDAP_PORT=636` | ✅ 완료 |
| 연결 테스트 | `POST /api/sync/icao-ldap/test` → TLS Mutual Auth (SASL EXTERNAL), 58ms | ✅ 성공 |
| E2E 검증 | CSR 생성 → CA 서명 → client.pem → LDAPS:636 SASL EXTERNAL | ✅ 성공 |

---

## 3. 사용자 시나리오

### 현재 (Phase 1 완료)

```
1. CSR 관리 (/admin/csr) → "CSR 생성" → RSA-2048 키 생성
2. 상세 다이얼로그 → "CA 인증서 발급" → Private CA로 즉시 서명
   → status: CREATED → ISSUED
   → /app/icao-tls/ 에 client.pem, client-key.pem, ca.pem 저장
3. "ICAO PKD 연결 적용" → ICAO PKD 동기화 페이지로 이동
```

### 프로덕션 전환 시

```
1. CSR 관리 → "CSR 생성" → "PEM 내보내기" → ICAO에 제출
2. ICAO 발급 인증서 수신 → "ICAO 발급 인증서 등록" → 공개키 매칭 → ISSUED
3. .env 변경: ICAO_LDAP_HOST=pkddownloadsg.icao.int, ICAO_LDAP_USE_TLS=true
```

---

## 4. 검증 결과

| 항목 | 결과 |
|------|------|
| PKD Management Docker 빌드 | [100%] Built target pkd-management |
| Frontend TypeScript 검증 | 에러 없음 |
| Frontend Docker 빌드 | Built |
| ICAO LDAP LDAPS 내부 테스트 | ldapsearch -H ldaps://localhost 성공 |
| PKD Relay TLS 연결 테스트 | TLS Mutual Auth (SASL EXTERNAL), 58ms |
| TLS 파일 생성 | client.pem + client-key.pem + ca.pem + ldap-server.crt/key |
| 전체 서비스 healthy | ✅ |

### E2E 시나리오 검증

```
1. CSR 생성 (POST /api/csr/generate) → RSA-2048 ✅
2. CA 인증서 발급 (POST /api/csr/{id}/sign) → Private CA 서명 ✅
3. TLS 파일 자동 저장 → client.pem, client-key.pem, ca.pem ✅
4. LDAP TLS 서버 → icao-pkd-ldap:636 LDAPS ✅
5. PKD Relay TLS 연결 → SASL EXTERNAL 인증 58ms ✅
```
