# 개인정보 암호화 적용 가이드

**버전**: v1.0.0
**최초 작성일**: 2026-03-11
**관련 프로젝트 버전**: v2.31.0
**상태**: 적용 완료 (PKD Management + PA Service)

---

## 1. 법적 근거

### 1.1 개인정보 보호법 (법률 제19234호)

#### 제24조 (고유식별정보의 처리 제한)

> 개인정보처리자가 고유식별정보를 처리하는 경우에는 그 고유식별정보가 분실·도난·유출·위조·변조 또는 훼손되지 아니하도록 대통령령으로 정하는 바에 따라 **암호화 등 안전성 확보에 필요한 조치**를 하여야 한다. (제3항)

**고유식별정보의 범위** (시행령 제19조):

| 정보 유형 | 고유식별정보 여부 | 근거 |
|-----------|:---:|------|
| 주민등록번호 | **O** | 시행령 제19조 제1호 |
| **여권번호** | **O** | **시행령 제19조 제2호** |
| 운전면허번호 | **O** | 시행령 제19조 제3호 |
| 외국인등록번호 | **O** | 시행령 제19조 제4호 |

> **핵심**: 본 프로젝트에서 PA 검증 시 처리되는 **여권번호(document_number)**는 고유식별정보에 해당하며, **저장 및 전송 시 암호화가 법적 의무**임.

#### 제29조 (안전조치의무)

> 개인정보처리자는 개인정보가 분실·도난·유출·위조·변조 또는 훼손되지 아니하도록 내부 관리계획 수립, 접근 통제 및 접근권한 제한 조치, **개인정보의 암호화**, 접속기록의 보관 및 위조·변조 방지 조치, 보안프로그램의 설치 및 갱신 등 대통령령으로 정하는 바에 따라 안전성 확보에 필요한 조치를 하여야 한다.

### 1.2 개인정보 보호법 시행령

#### 시행령 제21조 (고유식별정보의 안전성 확보 조치)

고유식별정보(여권번호 포함)를 처리하는 경우:
- **정보통신망을 통하여 송·수신할 때**: 암호화 필수
- **저장할 때**: 암호화 필수 (encryption at rest)

#### 시행령 제30조 (개인정보의 안전성 확보 조치)

개인정보처리자가 제29조에 따라 취해야 할 안전성 확보 조치의 세부 기준:
1. 내부 관리계획의 수립·시행
2. **접근 통제 및 접근권한 제한 조치**
3. **개인정보의 암호화**
4. 접속기록의 보관 및 점검
5. 보안프로그램의 설치·갱신

### 1.3 개인정보의 안전성 확보조치 기준 (고시 제2021-2호)

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

## 2. 프로젝트 내 개인정보 현황

### 2.1 개인정보 분류

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

### 2.2 암호화 미적용 (의무 아님)

| 데이터 | 위치 | 사유 |
|--------|------|------|
| 인증서 데이터 (CSCA/DSC/CRL) | `certificate`, LDAP | 공개키 기반 **공개 데이터** — 개인정보 비해당 |
| 인증서 Subject DN | `certificate` | 기관/국가 DN — 일반적으로 개인 식별 불가 |
| 감사 로그 IP/User-Agent | `auth_audit_log`, `operation_audit_log` | **접속기록** — 고시 제8조에 따라 보관 의무 (암호화 의무 아님) |
| 국가 코드 | `pa_verification` | 단독으로 개인 식별 불가 |

---

## 3. 기술 구현

### 3.1 암호화 방식

**AES-256-GCM** (Authenticated Encryption with Associated Data)

| 항목 | 값 |
|------|-----|
| 알고리즘 | AES-256 (KISA 승인, 고시 제7조 충족) |
| 모드 | GCM (Galois/Counter Mode) |
| 키 길이 | 256비트 (32바이트) |
| IV (초기화 벡터) | 96비트 (12바이트) — NIST SP 800-38D 권고 |
| 인증 태그 | 128비트 (16바이트) |
| IV 생성 | `RAND_bytes()` — 매 암호화마다 랜덤 생성 |

**AES-256-GCM을 선택한 이유**:
- **기밀성 + 무결성 동시 보장**: GCM 모드는 암호화와 동시에 인증 태그를 생성하여 데이터 변조 감지 가능
- **KISA 승인 알고리즘**: 「암호 알고리즘 및 키 길이 이용 안내서」에서 AES-256 권장
- **NIST 표준**: SP 800-38D에서 GCM 모드 표준화
- **성능**: CTR 기반으로 병렬 처리 가능, CBC 대비 우수한 성능

### 3.2 저장 형식

```
ENC:<hex(IV[12bytes] + ciphertext[N bytes] + tag[16bytes])>
```

- **접두사** `ENC:` — 암호화된 값과 평문을 구분 (하위 호환성)
- **IV**: 12바이트 랜덤 값 (hex 24자)
- **Ciphertext**: 평문과 동일 길이
- **Tag**: GCM 인증 태그 16바이트 (hex 32자)
- **총 오버헤드**: `4 + (12 + N + 16) * 2` = `60 + 2N` 문자

**예시**: "홍길동" (9바이트 UTF-8) → `ENC:` + 74자 hex = 총 78자

### 3.3 키 관리

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
| 키 미설정 시 | 암호화 비활성화 — 평문 저장 (개발/테스트용) |
| 키 로딩 | `std::call_once` — 프로세스 시작 시 1회 로드 |
| 키 저장 위치 | `.env` 파일 (`.gitignore` 포함, 버전 관리 제외) |

> **운영 환경 권장사항**: 키를 `.env` 파일 대신 환경 비밀 관리 시스템(Vault, AWS KMS 등)에서 주입할 것을 권장.

### 3.4 PII 마스킹 (Public API 응답)

관리자가 아닌 사용자에게 제공하는 API 응답에서는 개인정보를 마스킹하여 표시:

| 유형 | 원본 | 마스킹 결과 |
|------|------|------------|
| 이름 (`name`) | 홍길동 | 홍*동 |
| 이메일 (`email`) | hong@example.com | h***@example.com |
| 전화번호 (`phone`) | 010-1234-5678 | 010-****-5678 |
| 소속 (`org`) | 스마트코어 | 스마*** |

- UTF-8 인코딩 인식 (한글/영어 모두 올바르게 처리)
- 관리자(JWT admin) 요청 시에만 복호화된 전체 데이터 제공

### 3.5 Fail-Open 설계

암호화/복호화 실패 시 서비스 가용성을 유지하기 위한 안전 장치:

- **암호화 실패**: 평문 반환 (데이터 손실 방지) + 에러 로그
- **복호화 실패**: 암호화된 원본 반환 + 에러 로그
- **키 미설정**: 암호화 비활성화 — 평문 통과 (개발 환경 호환)
- **`ENC:` 접두사 감지**: 암호화된 데이터와 기존 평문 데이터 혼재 시에도 올바르게 처리

---

## 4. 적용 서비스

### 4.1 PKD Management Service

**대상 테이블**: `api_client_requests`

| 컬럼 | 데이터 유형 | 컬럼 크기 |
|------|-----------|----------|
| `requester_name` | 요청자 성명 | VARCHAR(1024) |
| `requester_org` | 요청자 소속 | VARCHAR(1024) |
| `requester_contact_phone` | 요청자 전화번호 | VARCHAR(1024) |
| `requester_contact_email` | 요청자 이메일 | VARCHAR(1024) |

**소스 파일**:
- `services/pkd-management/src/auth/personal_info_crypto.h` — 암호화 유틸리티 헤더
- `services/pkd-management/src/auth/personal_info_crypto.cpp` — AES-256-GCM 구현
- `services/pkd-management/src/repositories/api_client_request_repository.cpp` — INSERT 시 암호화, SELECT 시 복호화
- `services/pkd-management/src/handlers/api_client_request_handler.cpp` — Public API 응답 시 PII 마스킹
- `services/pkd-management/src/infrastructure/service_container.cpp` — Phase 0 초기화

### 4.2 PA Service

**대상 테이블**: `pa_verification`

| 컬럼 | 데이터 유형 | 컬럼 크기 |
|------|-----------|----------|
| `document_number` | **여권번호** (고유식별정보) | VARCHAR(1024) |
| `client_ip` | 클라이언트 IP | VARCHAR(1024) |
| `user_agent` | User-Agent 문자열 | TEXT / VARCHAR2(4000) |

**소스 파일**:
- `services/pa-service/src/auth/personal_info_crypto.h` — 암호화 유틸리티 헤더 (PKD Management와 동일)
- `services/pa-service/src/auth/personal_info_crypto.cpp` — AES-256-GCM 구현
- `services/pa-service/src/repositories/pa_verification_repository.cpp` — INSERT 시 암호화, SELECT 시 복호화
- `services/pa-service/src/infrastructure/service_container.cpp` — Step 0 초기화

### 4.3 전송 구간 암호화

| 구간 | 방식 | 설정 파일 |
|------|------|----------|
| 외부 → API Gateway | **TLS 1.2/1.3** (HTTPS :443) | `nginx/api-gateway-ssl.conf` |
| API Gateway → Backend | HTTP (내부 Docker 네트워크) | - |
| 인증서 | Private CA (RSA 4096) + 서버 인증서 (RSA 2048) | `scripts/ssl/init-cert.sh` |

---

## 5. DB 스키마 변경 사항

### 5.1 PostgreSQL

```sql
-- api_client_requests (PKD Management)
-- 기존: VARCHAR(255) → 변경: VARCHAR(1024)
requester_name VARCHAR(1024) NOT NULL,
requester_org VARCHAR(1024) NOT NULL,
requester_contact_phone VARCHAR(1024),
requester_contact_email VARCHAR(1024) NOT NULL,

-- pa_verification (PA Service)
-- 기존: VARCHAR(50) → 변경: VARCHAR(1024)
document_number VARCHAR(1024),
-- 기존: VARCHAR(45) → 변경: VARCHAR(1024)
client_ip VARCHAR(1024),
-- user_agent: TEXT (변경 없음)
```

### 5.2 Oracle

```sql
-- api_client_requests (PKD Management)
-- 기존: VARCHAR2(255) → 변경: VARCHAR2(1024)
requester_name VARCHAR2(1024) NOT NULL,
requester_org VARCHAR2(1024) NOT NULL,
requester_contact_phone VARCHAR2(1024),
requester_contact_email VARCHAR2(1024) NOT NULL,

-- pa_verification (PA Service)
-- 기존: VARCHAR2(50) → 변경: VARCHAR2(1024)
document_number VARCHAR2(1024),
-- 기존: VARCHAR2(45) → 변경: VARCHAR2(1024)
client_ip VARCHAR2(1024),
-- user_agent: VARCHAR2(4000) (변경 없음)
```

### 5.3 기존 데이터 마이그레이션

기존 평문 데이터는 마이그레이션 없이 그대로 사용 가능:
- `decrypt()` 함수는 `ENC:` 접두사가 없는 값을 평문으로 인식하여 그대로 반환
- 신규 INSERT부터 암호화 적용, 기존 데이터는 조회 시 평문 그대로 반환

별도 마이그레이션 스크립트가 필요한 경우 (기존 데이터 일괄 암호화):
```sql
-- 주의: 애플리케이션 레벨에서 처리해야 함 (DB 함수로는 AES-256-GCM 구현 불가)
-- 별도 마이그레이션 도구 개발 필요
```

---

## 6. 설정 및 운영

### 6.1 암호화 활성화

```bash
# 1. 키 생성
openssl rand -hex 32

# 2. .env 파일에 추가
echo "PII_ENCRYPTION_KEY=<생성된_64자_hex>" >> .env

# 3. 서비스 재시작
docker compose -f docker/docker-compose.yaml restart pkd-management pa-service
```

### 6.2 암호화 비활성화 (개발/테스트용)

```bash
# .env에서 키를 제거하거나 빈 값 설정
PII_ENCRYPTION_KEY=

# 서비스 재시작
docker compose restart pkd-management pa-service
```

### 6.3 키 로테이션

현재 버전에서는 키 로테이션 자동화가 구현되어 있지 않음. 키 변경 시:

1. 기존 데이터를 구 키로 복호화
2. 신규 키로 재암호화
3. `.env` 파일의 `PII_ENCRYPTION_KEY` 교체
4. 서비스 재시작

> **향후 개선**: 키 버전 관리 + 자동 마이그레이션 도구 개발 권장

### 6.4 로그 확인

```bash
# 암호화 활성화 확인
docker logs icao-local-pkd-pkd-management 2>&1 | grep "PII Crypto"
# 출력: [PII Crypto] AES-256-GCM encryption ENABLED for personal information fields

docker logs icao-local-pkd-pa-service 2>&1 | grep "PII Crypto"
# 출력: [PII Crypto] AES-256-GCM encryption ENABLED for personal information fields

# 키 미설정 시
# 출력: [PII Crypto] PII_ENCRYPTION_KEY not set — personal info encryption DISABLED
```

---

## 7. 법적 준수 체크리스트

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
| 개인정보의 기술적·관리적 보호조치 기준 | 방송통신위원회 고시 제2020-9호 |
| KISA 암호 알고리즘 및 키 길이 이용 안내서 | 한국인터넷진흥원 (2024) |
| NIST SP 800-38D | GCM Mode Specification |
