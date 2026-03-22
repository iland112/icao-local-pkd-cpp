# FASTpass® SPKD — API Client Guide

**Version**: v2.39.0 | **Last Updated**: 2026-03-22

---

## Part 1: API Client Administration

---

### 개요

ICAO Local PKD 시스템은 외부 Client Agent(출입국관리시스템, 외교부 연동 Agent 등)가 REST API를 안전하게 사용할 수 있도록 **API Key 기반 인증**을 지원합니다.

#### JWT vs API Key 비교

| 항목 | JWT (웹 UI) | API Key (외부 Agent) |
|------|------------|---------------------|
| **인증 방식** | ID/PW 로그인 -> 토큰 발급 | `X-API-Key` 헤더 |
| **만료** | 1시간 (갱신 가능) | 관리자 설정 (무기한 가능) |
| **대상** | 웹 브라우저 사용자 | 서버 간 M2M 통신 |
| **Rate Limit** | 사용자별 공통 | 클라이언트별 개별 설정 |
| **권한 제어** | admin/user 역할 | 12개 세부 Permission |
| **IP 제한** | 없음 | 클라이언트별 IP 화이트리스트 |
| **관리** | 사용자 관리 페이지 | API Client 관리 페이지 |

#### Base URL

```
# HTTPS (운영 환경, Private CA 인증서 필요)
https://pkd.smartcoreinc.com/api/auth/api-clients

# HTTP (내부 네트워크)
http://pkd.smartcoreinc.com/api/auth/api-clients

# API Gateway (직접)
http://localhost:8080/api/auth/api-clients
```

#### 사전 조건

- **관리자 계정**: 모든 API Client 관리 API는 admin 역할의 JWT 토큰이 필요합니다
- JWT 토큰 발급: `POST /api/auth/login` -> `token` 필드 사용

```bash
# JWT 토큰 발급
TOKEN=$(curl -s -X POST https://pkd.smartcoreinc.com/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username": "admin", "password": "admin123"}' | jq -r '.token')
```

---

### API Client 관리 API

#### API Endpoints Summary

| # | Method | Path | Description |
|---|--------|------|-------------|
| 1 | `POST` | `/api/auth/api-clients` | 새 클라이언트 생성 + API Key 발급 |
| 2 | `GET` | `/api/auth/api-clients` | 클라이언트 목록 조회 |
| 3 | `GET` | `/api/auth/api-clients/{id}` | 클라이언트 상세 조회 |
| 4 | `PUT` | `/api/auth/api-clients/{id}` | 클라이언트 수정 |
| 5 | `DELETE` | `/api/auth/api-clients/{id}` | 클라이언트 비활성화 |
| 6 | `POST` | `/api/auth/api-clients/{id}/regenerate` | API Key 재발급 |
| 7 | `GET` | `/api/auth/api-clients/{id}/usage` | 사용 통계 조회 |

---

#### 1. 클라이언트 생성 (`POST /api/auth/api-clients`)

새 외부 클라이언트를 등록하고 API Key를 발급합니다.

> **주의**: API Key는 이 응답에서 **단 한 번만** 표시됩니다. 반드시 안전하게 저장하세요.

**Request**:
```bash
curl -X POST https://pkd.smartcoreinc.com/api/auth/api-clients \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "client_name": "Immigration Agent",
    "description": "출입국관리시스템 연동 Agent",
    "permissions": ["pa:verify", "cert:read", "ai:read"],
    "allowed_ips": ["192.168.1.0/24"],
    "rate_limit_per_minute": 120,
    "rate_limit_per_hour": 2000,
    "rate_limit_per_day": 20000
  }'
```

**Request 필드**:

| 필드 | 타입 | 필수 | 기본값 | 설명 |
|------|------|------|--------|------|
| `client_name` | string | **필수** | - | 클라이언트 식별 이름 |
| `description` | string | 선택 | - | 클라이언트 설명 |
| `permissions` | string[] | 선택 | `[]` | 부여할 Permission 목록 |
| `allowed_endpoints` | string[] | 선택 | `[]` | 허용 엔드포인트 패턴 (비어있으면 전체 허용) |
| `allowed_ips` | string[] | 선택 | `[]` | 허용 IP/CIDR (비어있으면 전체 허용) |
| `rate_limit_per_minute` | integer | 선택 | `60` | 분당 요청 제한 |
| `rate_limit_per_hour` | integer | 선택 | `1000` | 시간당 요청 제한 |
| `rate_limit_per_day` | integer | 선택 | `10000` | 일일 요청 제한 |
| `expires_at` | string | 선택 | `null` | 만료 시간 (ISO 8601, null이면 무기한) |

**Response** (200):
```json
{
  "success": true,
  "warning": "API Key is only shown in this response. Store it securely.",
  "client": {
    "id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
    "client_name": "Immigration Agent",
    "api_key_prefix": "NggoCnqh",
    "api_key": "icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6",
    "description": "출입국관리시스템 연동 Agent",
    "permissions": ["pa:verify", "cert:read", "ai:read"],
    "allowed_endpoints": [],
    "allowed_ips": ["192.168.1.0/24"],
    "rate_limit_per_minute": 120,
    "rate_limit_per_hour": 2000,
    "rate_limit_per_day": 20000,
    "is_active": true,
    "expires_at": "",
    "last_used_at": "",
    "total_requests": 0,
    "created_by": "admin-uuid",
    "created_at": "2026-02-24T12:00:00.000000",
    "updated_at": "2026-02-24T12:00:00.000000"
  }
}
```

---

#### 2. 클라이언트 목록 조회 (`GET /api/auth/api-clients`)

등록된 모든 클라이언트를 페이지네이션으로 조회합니다.

```bash
# 활성 클라이언트만 조회
curl -X GET "https://pkd.smartcoreinc.com/api/auth/api-clients?active_only=true&limit=20&offset=0" \
  -H "Authorization: Bearer $TOKEN"
```

**Query Parameters**:

| 파라미터 | 타입 | 기본값 | 설명 |
|----------|------|--------|------|
| `active_only` | string | - | `"true"` 시 활성 클라이언트만 |
| `limit` | integer | `100` | 페이지 크기 |
| `offset` | integer | `0` | 건너뛸 항목 수 |

**Response** (200):
```json
{
  "success": true,
  "total": 3,
  "clients": [
    {
      "id": "a1b2c3d4-...",
      "client_name": "Immigration Agent",
      "api_key_prefix": "NggoCnqh",
      "is_active": true,
      "total_requests": 1542,
      "last_used_at": "2026-02-24T15:30:00.000000",
      ...
    }
  ]
}
```

> **참고**: 목록 조회에서는 `api_key` 필드가 포함되지 않습니다. API Key는 생성/재발급 시에만 표시됩니다.

---

#### 3. 클라이언트 상세 조회 (`GET /api/auth/api-clients/{id}`)

```bash
curl -X GET https://pkd.smartcoreinc.com/api/auth/api-clients/{id} \
  -H "Authorization: Bearer $TOKEN"
```

**Response** (200):
```json
{
  "success": true,
  "client": {
    "id": "a1b2c3d4-...",
    "client_name": "Immigration Agent",
    "api_key_prefix": "NggoCnqh",
    "permissions": ["pa:verify", "cert:read", "ai:read"],
    "rate_limit_per_minute": 120,
    "total_requests": 1542,
    ...
  }
}
```

---

#### 4. 클라이언트 수정 (`PUT /api/auth/api-clients/{id}`)

제공된 필드만 업데이트됩니다. 생략된 필드는 변경되지 않습니다.

```bash
# Permission 변경 + Rate Limit 상향
curl -X PUT https://pkd.smartcoreinc.com/api/auth/api-clients/{id} \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "permissions": ["pa:verify", "cert:read", "cert:export", "ai:read"],
    "rate_limit_per_minute": 200
  }'
```

```bash
# 클라이언트 비활성화
curl -X PUT https://pkd.smartcoreinc.com/api/auth/api-clients/{id} \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"is_active": false}'
```

---

#### 5. 클라이언트 비활성화 (`DELETE /api/auth/api-clients/{id}`)

클라이언트를 비활성화합니다 (Soft Delete). 레코드는 유지되지만 API Key로 인증할 수 없습니다.

```bash
curl -X DELETE https://pkd.smartcoreinc.com/api/auth/api-clients/{id} \
  -H "Authorization: Bearer $TOKEN"
```

**Response** (200):
```json
{
  "success": true,
  "message": "Client deactivated"
}
```

---

#### 6. API Key 재발급 (`POST /api/auth/api-clients/{id}/regenerate`)

기존 API Key를 무효화하고 새 키를 발급합니다.

> **주의**: 기존 키는 즉시 사용 불가합니다. 새 키는 이 응답에서 **단 한 번만** 표시됩니다.

```bash
curl -X POST https://pkd.smartcoreinc.com/api/auth/api-clients/{id}/regenerate \
  -H "Authorization: Bearer $TOKEN"
```

**Response** (200):
```json
{
  "success": true,
  "warning": "New API Key is only shown in this response. Store it securely.",
  "client": {
    "id": "a1b2c3d4-...",
    "client_name": "Immigration Agent",
    "api_key_prefix": "XyZ9AbCd",
    "api_key": "icao_XyZ9AbCd_new_random_key_value_here_32chars",
    ...
  }
}
```

---

#### 7. 사용 통계 조회 (`GET /api/auth/api-clients/{id}/usage`)

지정된 기간 동안의 클라이언트 API 사용 통계를 조회합니다.

```bash
# 최근 30일 통계
curl -X GET "https://pkd.smartcoreinc.com/api/auth/api-clients/{id}/usage?days=30" \
  -H "Authorization: Bearer $TOKEN"
```

**Query Parameters**:

| 파라미터 | 타입 | 기본값 | 설명 |
|----------|------|--------|------|
| `days` | integer | `7` | 조회 기간 (일) |

**Response** (200):
```json
{
  "success": true,
  "client_id": "a1b2c3d4-...",
  "days": 30,
  "usage": {
    "total_requests": 15420,
    "top_endpoints": [
      {"endpoint": "/api/pa/verify", "count": 8500},
      {"endpoint": "/api/certificates/search", "count": 3200},
      {"endpoint": "/api/certificates/pa-lookup", "count": 2100}
    ]
  }
}
```

---

### PA Service 연동 (nginx auth_request)

PA Service(`/api/pa/*`) 요청의 API Key 추적은 **nginx auth_request** 메커니즘으로 구현됩니다.

#### 동작 방식

```
Client ──X-API-Key──→ nginx (/api/pa/verify)
                        ├── auth_request → PKD Management /api/auth/internal/check
                        │     ↓ API Key 검증 + Rate Limit + Usage Log
                        │     ↓ 200 OK (허용) / 401 (거부) / 403 (Rate Limit)
                        └── proxy_pass → PA Service (8082)
```

1. 클라이언트가 `X-API-Key` 헤더와 함께 PA 요청 전송
2. nginx가 내부 subrequest로 PKD Management에 인증 확인 (`/api/auth/internal/check`)
3. PKD Management가 API Key 검증 -> Rate Limit 체크 -> 사용 로그 기록
4. 인증 성공(200) 시 nginx가 PA Service로 요청 전달, 실패 시 JSON 에러 반환

#### 관리자 참고사항

- **Permission**: PA 통신용 클라이언트는 `pa:verify` 또는 `pa:read` 포함 필수
- **사용량 모니터링**: `/api/auth/api-clients/{id}/usage` API에서 `/api/pa/verify` 등 PA 엔드포인트 사용량 확인
- **Rate Limit**: 전체 엔드포인트(PKD Management + PA)에 대해 공유 Rate Limiter 적용
- **API Key 미제공 시**: 기존과 동일하게 public 접근 허용 (하위 호환)

---

### Permission 모델

12개의 세부 Permission으로 클라이언트별 접근 범위를 제어합니다.

| Permission | 설명 | 관련 엔드포인트 |
|------------|------|----------------|
| `cert:read` | 인증서 검색/조회 | `/api/certificates/search`, `/api/certificates/detail`, `/api/certificates/validation` |
| `cert:export` | 인증서 내보내기 | `/api/certificates/export/*` |
| `pa:verify` | PA 검증 수행 | `/api/pa/verify`, `/api/pa/parse-*` |
| `pa:read` | PA 이력 조회 | `/api/pa/history`, `/api/pa/{id}`, `/api/pa/statistics` |
| `pa:stats` | PA 통계 조회 | `/api/pa/statistics` |
| `upload:read` | 업로드 이력 조회 | `/api/upload/history`, `/api/upload/detail/*`, `/api/upload/statistics` |
| `upload:write` | 파일 업로드 | `/api/upload/ldif`, `/api/upload/masterlist`, `/api/upload/certificate` |
| `report:read` | 보고서 조회 | `/api/certificates/dsc-nc/report`, `/api/certificates/crl/report` |
| `ai:read` | AI 분석 결과 조회 | `/api/ai/certificate/*`, `/api/ai/anomalies`, `/api/ai/statistics` |
| `sync:read` | 동기화 상태 조회 | `/api/sync/status`, `/api/sync/stats` |
| `icao:read` | ICAO 버전 조회 | `/api/icao/status`, `/api/icao/latest`, `/api/icao/history` |
| `api-client:manage` | API 클라이언트 관리 | 관리자 전용 |

#### Permission 설정 예시

```json
// PA 검증 전용 Agent
{"permissions": ["pa:verify"]}

// 인증서 조회 + AI 분석 Agent
{"permissions": ["cert:read", "cert:export", "ai:read"]}

// 전체 읽기 Agent
{"permissions": ["cert:read", "cert:export", "pa:verify", "pa:read", "pa:stats", "upload:read", "report:read", "ai:read", "sync:read", "icao:read"]}
```

> **참고**: `permissions`가 빈 배열(`[]`)이면 모든 Public 엔드포인트에 접근할 수 있지만, 특정 Permission이 필요한 Protected 엔드포인트는 접근이 거부됩니다.

---

### Rate Limiting

#### 3-Tier Rate Limiting

| 계층 | 윈도우 | 기본값 | 설명 |
|------|--------|--------|------|
| **분당** | 60초 슬라이딩 | 60 req/min | 순간 트래픽 제어 |
| **시간당** | 3600초 슬라이딩 | 1,000 req/hr | 중간 범위 제어 |
| **일일** | 86400초 슬라이딩 | 10,000 req/day | 일간 사용량 제어 |

#### Rate Limit 초과 시 응답

Rate Limit 초과 시 `429 Too Many Requests` 응답이 반환됩니다:

```
HTTP/1.1 429 Too Many Requests
X-RateLimit-Limit: 60
X-RateLimit-Remaining: 0
X-RateLimit-Reset: 1708776000
Retry-After: 45

{
  "success": false,
  "error": "Rate limit exceeded",
  "limit": 60,
  "remaining": 0,
  "reset_at": 1708776000,
  "window": "per_minute"
}
```

**응답 헤더**:

| 헤더 | 설명 |
|------|------|
| `X-RateLimit-Limit` | 현재 윈도우의 최대 요청 수 |
| `X-RateLimit-Remaining` | 남은 요청 수 |
| `X-RateLimit-Reset` | 윈도우 리셋 Unix timestamp |
| `Retry-After` | 재시도까지 남은 초 |

#### nginx Rate Limiting (L1)

API Key Rate Limiting(L2) 외에도 nginx에서 IP 기반 L1 Rate Limiting이 적용됩니다:

| Zone | 제한 | 대상 |
|------|------|------|
| `api_limit` | 100 req/s per IP | 전체 API |
| `upload_limit` | 5 req/min per user | 업로드 |
| `pa_verify_limit` | 2 req/min per user | PA 검증 |

---

### IP 화이트리스트

`allowed_ips`에 IP 주소 또는 CIDR 범위를 지정하여 접근을 제한할 수 있습니다.

```json
// 특정 IP만 허용
{"allowed_ips": ["192.168.1.100"]}

// 서브넷 허용
{"allowed_ips": ["192.168.1.0/24", "10.0.0.0/8"]}

// 전체 허용 (기본값)
{"allowed_ips": []}
```

허용되지 않은 IP에서 요청 시:
```json
{
  "success": false,
  "error": "IP not allowed"
}
```

---

### 보안 권장사항

1. **HTTPS 필수**: API Key가 네트워크에 평문으로 노출되지 않도록 반드시 HTTPS를 사용하세요.
2. **API Key 안전 저장**: API Key는 생성 시 한 번만 표시됩니다. 환경 변수 또는 시크릿 매니저에 저장하세요.
3. **최소 권한 원칙**: 클라이언트에 필요한 Permission만 부여하세요.
4. **정기 키 교체**: 보안을 위해 주기적으로 API Key를 재발급하세요 (`/regenerate`).
5. **IP 제한 활용**: 가능하면 `allowed_ips`를 설정하여 접근 IP를 제한하세요.
6. **미사용 클라이언트 비활성화**: 더 이상 사용하지 않는 클라이언트는 즉시 비활성화하세요.
7. **만료 설정**: 임시 연동의 경우 `expires_at`을 설정하여 자동 만료되도록 하세요.
8. **사용량 모니터링**: `/usage` API를 통해 비정상적인 사용 패턴을 주기적으로 확인하세요.

---

### 프론트엔드 관리 페이지

웹 UI에서도 API Client를 관리할 수 있습니다:

- **URL**: `https://pkd.smartcoreinc.com/admin/api-clients`
- **접근**: 사이드바 -> "Admin & Security" -> "API 클라이언트"
- **기능**: 클라이언트 생성, 목록 조회, 상세 정보, 키 재발급, 비활성화

---

### 에러 코드

| HTTP Status | 에러 | 설명 |
|-------------|------|------|
| `400` | Bad Request | 필수 필드 누락 또는 잘못된 JSON |
| `401` | Unauthorized | API Key 없음 또는 잘못된 키 |
| `403` | Forbidden | 비활성 클라이언트, 만료, IP 차단, 권한 부족 |
| `404` | Not Found | 클라이언트 ID 없음 |
| `429` | Too Many Requests | Rate Limit 초과 |
| `500` | Internal Server Error | 서버 오류 |

---

## Part 2: Client Integration Guide

---

### 개요

이 문서는 ICAO Local PKD 시스템에 **API Key로 연동하는 외부 클라이언트 애플리케이션** 개발자를 위한 가이드입니다.

API Key를 발급받으면 별도의 로그인 없이 `X-API-Key` HTTP 헤더 하나로 인증서 검색, PA 검증, AI 분석 등의 API를 사용할 수 있습니다.

#### API Key 인증 흐름

```
외부 Agent                           ICAO Local PKD
   |                                      |
   |  HTTP Request                        |
   |  X-API-Key: icao_XXXXXXXX_...  ---->  |
   |                                      | → SHA-256 해시 비교
   |                                      | → Permission 확인
   |                                      | → IP 화이트리스트 확인
   |                                      | → Rate Limit 확인
   |  <----  200 OK (JSON Response)       |
   |                                      |
```

#### Base URL

**HTTPS (운영 환경, 권장)**:
```
# Private CA 인증서(ca.crt) 설치 필요
https://pkd.smartcoreinc.com/api
```

**HTTP (내부 네트워크)**:
```
http://pkd.smartcoreinc.com/api
```

**WiFi 네트워크 (SC-WiFi)**:
```
http://192.168.1.70:8080/api
```

**유선 LAN (내부 네트워크)**:
```
http://192.168.100.10:8080/api
```

> **Note**: HTTPS 사용 시 Private CA 인증서(`ca.crt`)를 클라이언트에 설치해야 합니다. 관리자에게 인증서 파일을 요청하세요.

---

### 인증 방법

모든 API 요청에 `X-API-Key` 헤더를 포함하세요:

```
X-API-Key: icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6
```

#### API Key 형식

```
icao_{prefix}_{random}
 |      |        |
 |      |        └── 32자 랜덤 문자열 (Base62)
 |      └── 8자 키 접두사 (관리 UI에서 식별용)
 └── 고정 접두사
```

- 총 길이: 46자
- 대소문자 구분됨 (Case-Sensitive)
- API Key는 발급 시 **한 번만 표시**되며, 시스템에는 SHA-256 해시만 저장됩니다

#### API Key 발급

API Key는 시스템 관리자가 발급합니다. 관리자에게 다음 정보를 전달하세요:
- **클라이언트 이름**: 시스템/서비스 이름 (예: "출입국관리 Agent")
- **필요한 기능**: PA 검증, 인증서 검색 등
- **접속 IP**: 클라이언트 서버의 IP 주소 또는 대역
- **예상 사용량**: 분당/시간당/일일 예상 요청 수

---

### 사용 가능 엔드포인트

API Key로 접근할 수 있는 주요 엔드포인트입니다. 관리자가 설정한 Permission에 따라 접근 가능 범위가 다를 수 있습니다.

#### PA 검증 (Permission: `pa:verify`)

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/api/pa/verify` | PA 검증 (8단계 전체 프로세스) |
| `POST` | `/api/pa/parse-sod` | SOD 메타데이터 파싱 |
| `POST` | `/api/pa/parse-dg1` | DG1 -> MRZ 파싱 |
| `POST` | `/api/pa/parse-dg2` | DG2 -> 얼굴 이미지 추출 |
| `POST` | `/api/pa/parse-mrz-text` | MRZ 텍스트 파싱 |

> **Usage Tracking (v2.22.0+)**: PA Service 요청도 `X-API-Key` 헤더로 추적됩니다. 요청 시마다 사용량이 자동 기록되며, 관리자가 사용 통계 API로 PA 엔드포인트별 호출 횟수를 확인할 수 있습니다.

#### 인증서 검색 (Permission: `cert:read`)

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/certificates/search` | 인증서 검색 (국가/유형/상태 필터) |
| `GET` | `/api/certificates/detail` | 인증서 상세 조회 |
| `GET` | `/api/certificates/validation` | 검증 결과 조회 (fingerprint) |
| `POST` | `/api/certificates/pa-lookup` | 간편 PA 조회 (Subject DN/Fingerprint) |
| `GET` | `/api/certificates/countries` | 국가 목록 조회 |

#### AI 분석 (Permission: `ai:read`)

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/ai/certificate/{fingerprint}` | 인증서 AI 분석 결과 |
| `GET` | `/api/ai/certificate/{fingerprint}/forensic` | 포렌식 상세 (10개 카테고리) |
| `GET` | `/api/ai/anomalies` | 이상 인증서 목록 |
| `GET` | `/api/ai/statistics` | AI 분석 전체 통계 |

#### 보고서 (Permission: `report:read`)

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/certificates/dsc-nc/report` | 표준 부적합 DSC 보고서 |
| `GET` | `/api/certificates/crl/report` | CRL 보고서 |
| `GET` | `/api/certificates/crl/{id}` | CRL 상세 |
| `GET` | `/api/certificates/doc9303-checklist` | Doc 9303 적합성 체크리스트 |

---

### 연동 예제

#### curl

```bash
# API Key 변수 설정
API_KEY="icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6"
BASE_URL="https://pkd.smartcoreinc.com"

# 1. 인증서 검색 (한국, DSC)
curl -s "$BASE_URL/api/certificates/search?country=KR&certType=DSC&limit=10" \
  -H "X-API-Key: $API_KEY" | jq .

# 2. PA 간편 조회 (Subject DN)
curl -s -X POST "$BASE_URL/api/certificates/pa-lookup" \
  -H "X-API-Key: $API_KEY" \
  -H "Content-Type: application/json" \
  -d '{"subjectDn": "/C=KR/O=Government of Korea/CN=Document Signer 1234"}' | jq .

# 3. PA 전체 검증 (SOD + DG)
curl -s -X POST "$BASE_URL/api/pa/verify" \
  -H "X-API-Key: $API_KEY" \
  -H "Content-Type: application/json" \
  -d '{
    "sod": "'$(base64 -w0 sod.bin)'",
    "dataGroups": {
      "1": "'$(base64 -w0 dg1.bin)'",
      "2": "'$(base64 -w0 dg2.bin)'"
    }
  }' | jq .

# 4. AI 분석 결과 조회
curl -s "$BASE_URL/api/ai/certificate/920693cd1283824ffdf48a3579fc35528122f3de46bab2ecdaef402db6d92e4e" \
  -H "X-API-Key: $API_KEY" | jq .

# 5. 헬스 체크 (인증 불필요)
curl -s "$BASE_URL/api/health" | jq .
```

---

#### Python (requests)

```python
"""
ICAO Local PKD — 외부 Agent 연동 예제 (Python)
pip install requests
"""
import requests
import base64
import json

# 설정
BASE_URL = "https://pkd.smartcoreinc.com"
API_KEY = "icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6"

# HTTPS Private CA 인증서 (ca.crt 경로 지정)
CA_CERT = "/path/to/ca.crt"  # 또는 verify=False (개발 환경)

headers = {
    "X-API-Key": API_KEY,
    "Content-Type": "application/json"
}


def search_certificates(country: str, cert_type: str = "DSC", limit: int = 10):
    """인증서 검색"""
    resp = requests.get(
        f"{BASE_URL}/api/certificates/search",
        params={"country": country, "certType": cert_type, "limit": limit},
        headers=headers,
        verify=CA_CERT
    )
    resp.raise_for_status()
    data = resp.json()
    print(f"[인증서 검색] {country} {cert_type}: {data.get('total', 0)}건")
    return data


def pa_lookup(subject_dn: str):
    """PA 간편 조회 (Subject DN)"""
    resp = requests.post(
        f"{BASE_URL}/api/certificates/pa-lookup",
        json={"subjectDn": subject_dn},
        headers=headers,
        verify=CA_CERT
    )
    resp.raise_for_status()
    data = resp.json()
    validation = data.get("validation")
    if validation:
        print(f"[PA 조회] Status: {validation['validationStatus']}, "
              f"Trust Chain: {validation['trustChainValid']}")
    else:
        print("[PA 조회] 검증 결과 없음")
    return data


def pa_verify(sod_path: str, dg1_path: str, dg2_path: str = None):
    """PA 전체 검증 (SOD + Data Groups)"""
    with open(sod_path, "rb") as f:
        sod_b64 = base64.b64encode(f.read()).decode()
    with open(dg1_path, "rb") as f:
        dg1_b64 = base64.b64encode(f.read()).decode()

    payload = {
        "sod": sod_b64,
        "dataGroups": {"1": dg1_b64}
    }
    if dg2_path:
        with open(dg2_path, "rb") as f:
            payload["dataGroups"]["2"] = base64.b64encode(f.read()).decode()

    resp = requests.post(
        f"{BASE_URL}/api/pa/verify",
        json=payload,
        headers=headers,
        verify=CA_CERT
    )
    resp.raise_for_status()
    data = resp.json()
    result = data.get("result", {})
    print(f"[PA 검증] Overall: {result.get('overallResult')}, "
          f"Country: {result.get('issuingCountry')}")
    return data


def get_ai_analysis(fingerprint: str):
    """AI 인증서 분석 결과 조회"""
    resp = requests.get(
        f"{BASE_URL}/api/ai/certificate/{fingerprint}",
        headers=headers,
        verify=CA_CERT
    )
    resp.raise_for_status()
    data = resp.json()
    result = data.get("result", {})
    print(f"[AI 분석] Risk: {result.get('risk_level')}, "
          f"Score: {result.get('risk_score')}")
    return data


# 사용 예시
if __name__ == "__main__":
    # 1. 한국 DSC 인증서 검색
    certs = search_certificates("KR", "DSC", limit=5)

    # 2. PA 간편 조회
    pa_lookup("/C=KR/O=Government of Korea/CN=Document Signer 1234")

    # 3. AI 분석 결과
    get_ai_analysis("920693cd1283824ffdf48a3579fc35528122f3de46bab2ecdaef402db6d92e4e")
```

---

#### Java (HttpURLConnection)

```java
/**
 * ICAO Local PKD — 외부 Agent 연동 예제 (Java)
 * JDK 11+
 */
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Base64;

public class IcaoPkdClient {

    private static final String BASE_URL = "https://pkd.smartcoreinc.com";
    private static final String API_KEY = "icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6";

    private final HttpClient client;

    public IcaoPkdClient() {
        // Private CA 사용 시 SSLContext 설정 필요
        this.client = HttpClient.newBuilder()
                .version(HttpClient.Version.HTTP_1_1)
                .build();
    }

    /**
     * 인증서 검색
     */
    public String searchCertificates(String country, String certType, int limit)
            throws Exception {
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(String.format(
                    "%s/api/certificates/search?country=%s&certType=%s&limit=%d",
                    BASE_URL, country, certType, limit)))
                .header("X-API-Key", API_KEY)
                .GET()
                .build();

        HttpResponse<String> response = client.send(request,
                HttpResponse.BodyHandlers.ofString());

        System.out.printf("[인증서 검색] Status: %d%n", response.statusCode());
        return response.body();
    }

    /**
     * PA 간편 조회
     */
    public String paLookup(String subjectDn) throws Exception {
        String body = String.format("{\"subjectDn\": \"%s\"}", subjectDn);

        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(BASE_URL + "/api/certificates/pa-lookup"))
                .header("X-API-Key", API_KEY)
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(body))
                .build();

        HttpResponse<String> response = client.send(request,
                HttpResponse.BodyHandlers.ofString());

        System.out.printf("[PA 조회] Status: %d%n", response.statusCode());
        return response.body();
    }

    /**
     * PA 전체 검증
     */
    public String paVerify(Path sodPath, Path dg1Path) throws Exception {
        String sodB64 = Base64.getEncoder().encodeToString(Files.readAllBytes(sodPath));
        String dg1B64 = Base64.getEncoder().encodeToString(Files.readAllBytes(dg1Path));

        String body = String.format(
            "{\"sod\": \"%s\", \"dataGroups\": {\"1\": \"%s\"}}",
            sodB64, dg1B64);

        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(BASE_URL + "/api/pa/verify"))
                .header("X-API-Key", API_KEY)
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(body))
                .build();

        HttpResponse<String> response = client.send(request,
                HttpResponse.BodyHandlers.ofString());

        System.out.printf("[PA 검증] Status: %d%n", response.statusCode());
        return response.body();
    }

    public static void main(String[] args) throws Exception {
        IcaoPkdClient client = new IcaoPkdClient();

        // 인증서 검색
        String result = client.searchCertificates("KR", "DSC", 5);
        System.out.println(result);

        // PA 간편 조회
        String lookup = client.paLookup(
            "/C=KR/O=Government of Korea/CN=Document Signer 1234");
        System.out.println(lookup);
    }
}
```

---

#### C# (HttpClient)

```csharp
/**
 * ICAO Local PKD — 외부 Agent 연동 예제 (C#)
 * .NET 6+
 */
using System.Net.Http;
using System.Text;
using System.Text.Json;

public class IcaoPkdClient
{
    private const string BaseUrl = "https://pkd.smartcoreinc.com";
    private const string ApiKey = "icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6";

    private readonly HttpClient _client;

    public IcaoPkdClient()
    {
        // Private CA 사용 시 HttpClientHandler에서 ServerCertificateCustomValidationCallback 설정
        _client = new HttpClient();
        _client.DefaultRequestHeaders.Add("X-API-Key", ApiKey);
    }

    /// <summary>인증서 검색</summary>
    public async Task<string> SearchCertificatesAsync(
        string country, string certType = "DSC", int limit = 10)
    {
        var url = $"{BaseUrl}/api/certificates/search" +
                  $"?country={country}&certType={certType}&limit={limit}";

        var response = await _client.GetAsync(url);
        response.EnsureSuccessStatusCode();

        var result = await response.Content.ReadAsStringAsync();
        Console.WriteLine($"[인증서 검색] {country} {certType}: Status {response.StatusCode}");
        return result;
    }

    /// <summary>PA 간편 조회</summary>
    public async Task<string> PaLookupAsync(string subjectDn)
    {
        var payload = JsonSerializer.Serialize(new { subjectDn });
        var content = new StringContent(payload, Encoding.UTF8, "application/json");

        var response = await _client.PostAsync(
            $"{BaseUrl}/api/certificates/pa-lookup", content);
        response.EnsureSuccessStatusCode();

        var result = await response.Content.ReadAsStringAsync();
        Console.WriteLine($"[PA 조회] Status {response.StatusCode}");
        return result;
    }

    /// <summary>PA 전체 검증</summary>
    public async Task<string> PaVerifyAsync(string sodPath, string dg1Path)
    {
        var sodB64 = Convert.ToBase64String(await File.ReadAllBytesAsync(sodPath));
        var dg1B64 = Convert.ToBase64String(await File.ReadAllBytesAsync(dg1Path));

        var payload = JsonSerializer.Serialize(new
        {
            sod = sodB64,
            dataGroups = new Dictionary<string, string> { { "1", dg1B64 } }
        });

        var content = new StringContent(payload, Encoding.UTF8, "application/json");
        var response = await _client.PostAsync($"{BaseUrl}/api/pa/verify", content);
        response.EnsureSuccessStatusCode();

        var result = await response.Content.ReadAsStringAsync();
        Console.WriteLine($"[PA 검증] Status {response.StatusCode}");
        return result;
    }

    /// <summary>AI 분석 결과 조회</summary>
    public async Task<string> GetAiAnalysisAsync(string fingerprint)
    {
        var response = await _client.GetAsync(
            $"{BaseUrl}/api/ai/certificate/{fingerprint}");
        response.EnsureSuccessStatusCode();

        var result = await response.Content.ReadAsStringAsync();
        Console.WriteLine($"[AI 분석] Status {response.StatusCode}");
        return result;
    }
}

// 사용 예시
var client = new IcaoPkdClient();
var certs = await client.SearchCertificatesAsync("KR", "DSC", 5);
var lookup = await client.PaLookupAsync(
    "/C=KR/O=Government of Korea/CN=Document Signer 1234");
```

---

### Rate Limiting 응답 처리

#### Python에서의 처리

```python
import time

def call_api_with_retry(url, headers, max_retries=3):
    """Rate Limit 대응 재시도 로직"""
    for attempt in range(max_retries):
        resp = requests.get(url, headers=headers, verify=CA_CERT)

        if resp.status_code == 429:
            retry_after = int(resp.headers.get("Retry-After", 60))
            print(f"Rate limit 초과. {retry_after}초 후 재시도... (시도 {attempt + 1}/{max_retries})")
            time.sleep(retry_after)
            continue

        resp.raise_for_status()
        return resp.json()

    raise Exception("최대 재시도 횟수 초과")
```

#### Java에서의 처리

```java
public String callWithRetry(HttpRequest request, int maxRetries) throws Exception {
    for (int i = 0; i < maxRetries; i++) {
        HttpResponse<String> response = client.send(request,
                HttpResponse.BodyHandlers.ofString());

        if (response.statusCode() == 429) {
            String retryAfter = response.headers()
                    .firstValue("Retry-After").orElse("60");
            System.out.printf("Rate limit 초과. %s초 후 재시도...%n", retryAfter);
            Thread.sleep(Long.parseLong(retryAfter) * 1000);
            continue;
        }

        return response.body();
    }
    throw new RuntimeException("최대 재시도 횟수 초과");
}
```

---

### 에러 코드 및 대응

| HTTP Status | 원인 | 대응 |
|-------------|------|------|
| `401 Unauthorized` | API Key가 없거나 잘못됨 (**주의**: PA 엔드포인트에서는 미등록 키도 허용) | `X-API-Key` 헤더 확인, 키 값 검증 |
| `403 Forbidden` | 비활성 클라이언트, 만료, IP 차단, 권한 부족 | 관리자에게 클라이언트 상태 확인 요청 |
| `429 Too Many Requests` | Rate Limit 초과 | `Retry-After` 헤더 값만큼 대기 후 재시도 |
| `500 Internal Server Error` | 서버 오류 | 관리자에게 보고 |

#### 에러 응답 형식

```json
{
  "success": false,
  "error": "에러 유형",
  "message": "상세 설명"
}
```

---

### FAQ / 트러블슈팅

#### Q. API Key를 분실했습니다.

API Key는 발급 시 한 번만 표시되며, 시스템에 저장되지 않습니다. 관리자에게 **키 재발급**을 요청하세요. 기존 키는 즉시 무효화됩니다.

#### Q. 403 Forbidden 오류가 발생합니다.

가능한 원인:
1. **클라이언트 비활성화**: 관리자가 클라이언트를 비활성화했을 수 있습니다
2. **키 만료**: `expires_at`이 설정된 경우 만료되었을 수 있습니다
3. **IP 제한**: `allowed_ips`에 현재 IP가 포함되지 않을 수 있습니다
4. **Permission 부족**: 요청한 엔드포인트에 대한 Permission이 없습니다

관리자에게 클라이언트 상태와 설정을 확인 요청하세요.

#### Q. 429 Too Many Requests 오류가 자주 발생합니다.

현재 Rate Limit 설정을 확인하세요 (관리자에게 문의). 필요한 경우 관리자가 Rate Limit을 상향할 수 있습니다.

요청 빈도를 줄이는 방법:
- 인증서 검색 시 `limit` 파라미터를 늘려 페이지 수를 줄이기
- PA 간편 조회(`/api/certificates/pa-lookup`)를 사용하여 전체 검증 호출 줄이기
- 응답 결과를 로컬에 캐싱하기

#### Q. HTTPS 연결 시 인증서 오류가 발생합니다.

ICAO Local PKD 시스템은 **Private CA** 기반 TLS를 사용합니다. 공인 CA가 아니므로 클라이언트에 CA 인증서를 설치해야 합니다.

```bash
# Python: verify 파라미터에 ca.crt 경로 지정
requests.get(url, headers=headers, verify="/path/to/ca.crt")

# curl: --cacert 옵션 사용
curl --cacert /path/to/ca.crt -H "X-API-Key: $API_KEY" $URL

# Java: KeyStore에 CA 인증서 등록
keytool -import -alias pkd-ca -file ca.crt -keystore cacerts
```

관리자에게 `ca.crt` 파일을 요청하세요.

#### Q. 헬스 체크는 어떻게 하나요?

인증 없이 호출 가능한 헬스 체크 엔드포인트:

```bash
# 서비스 상태
curl https://pkd.smartcoreinc.com/api/health

# 데이터베이스 연결 상태
curl https://pkd.smartcoreinc.com/api/health/database

# LDAP 연결 상태
curl https://pkd.smartcoreinc.com/api/health/ldap
```

#### Q. 미등록 API Key로 PA 요청을 보내면 어떻게 되나요?

**PA 엔드포인트(`/api/pa/*`)에서는 미등록/유효하지 않은 API Key를 전송해도 요청이 정상 처리됩니다** (v2.22.1+). PA Service는 Public API이므로 하위 호환성을 위해 미등록 키를 차단하지 않습니다. 등록된 유효한 API Key인 경우에만 사용량 추적이 적용됩니다.

> PKD Management 엔드포인트(`/api/certificates/*`, `/api/upload/*` 등)에서는 미등록 API Key가 `401 Unauthorized`를 반환합니다.

#### Q. 어떤 엔드포인트가 인증 없이 접근 가능한가요?

대부분의 조회 엔드포인트는 Public입니다 (인증 없이 사용 가능). API Key는 사용량 추적과 Rate Limit 개별 관리를 위해 권장됩니다.

인증이 **필수**인 엔드포인트:
- 파일 업로드 (`/api/upload/ldif`, `/api/upload/masterlist`, `/api/upload/certificate`)
- 사용자 관리 (`/api/auth/users`)
- 감사 로그 (`/api/auth/audit-log`, `/api/audit/operations`)
- 코드 관리 수정 (`POST/PUT/DELETE /api/code-master`)

---

### OpenAPI 스펙

자세한 API 명세는 OpenAPI (Swagger) 문서를 참고하세요:

- **Swagger UI**: `https://pkd.smartcoreinc.com/api-docs`
- **OpenAPI YAML**: `docs/openapi/pkd-management.yaml`
- **PA Service YAML**: `docs/openapi/pa-service.yaml`

---

## Part 3: Certificate Search Quick Start

---

### Quick Access

#### Frontend UI
**URL**: http://localhost:13080/pkd/certificates

**Navigation**: Sidebar > Certificate Management > "인증서 조회"

#### API Endpoint
**Base URL**: http://localhost:18080/api/certificates

---

### Authentication

#### JWT (Browser / Internal)
Certificate search endpoints are **public** (no JWT required).

#### API Key (External / M2M)
External clients can authenticate using the `X-API-Key` header (added in v2.21.0). The API key requires the **`cert:read`** permission for search endpoints and **`cert:export`** for export endpoints.

```bash
curl -H "X-API-Key: icao_xxxx_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" \
  "http://localhost:18080/api/certificates/search?limit=10"
```

---

### Quick Examples

#### 1. Search All Certificates
```bash
curl "http://localhost:18080/api/certificates/search?limit=10"
```
**Returns**: First 10 certificates from 31,212 total

#### 2. Search by Country
```bash
curl "http://localhost:18080/api/certificates/search?country=KR&limit=10"
```
**Returns**: First 10 certificates from Korea (227 total)

#### 3. Search by Country + Type
```bash
curl "http://localhost:18080/api/certificates/search?country=KR&certType=DSC&limit=10"
```
**Returns**: DSC certificates from Korea only

#### 4. Pagination
```bash
curl "http://localhost:18080/api/certificates/search?country=KR&limit=10&offset=20"
```
**Returns**: Certificates 21-30 from Korea

#### 5. Filter by Source Type (v2.29.0+)
```bash
curl "http://localhost:18080/api/certificates/search?source=PA_EXTRACTED&limit=10"
```
**Returns**: Certificates auto-registered from PA verification

#### 6. Get Certificate Detail
```bash
curl "http://localhost:18080/api/certificates/detail?dn={FULL_DN}"
```
**Returns**: Full certificate metadata

#### 7. Export Single Certificate (DER)
```bash
curl "http://localhost:18080/api/certificates/export/file?dn={DN}&format=der" -o cert.crt
```
**Downloads**: Binary DER certificate file

#### 8. Export Single Certificate (PEM)
```bash
curl "http://localhost:18080/api/certificates/export/file?dn={DN}&format=pem" -o cert.pem
```
**Downloads**: Base64 PEM certificate file

#### 9. Export Country Certificates (ZIP)
```bash
curl "http://localhost:18080/api/certificates/export/country?country=KR&format=pem" -o KR_certs.zip
```
**Downloads**: ZIP archive with all Korea certificates

#### 10. Export All Certificates (DIT-structured ZIP)
```bash
curl "http://localhost:18080/api/certificates/export/all?format=pem" -o all_certs.zip
```
**Downloads**: Full LDAP DIT-structured ZIP archive with all certificates, CRLs, and Master Lists

#### 11. Using X-API-Key (v2.21.0+)
```bash
curl -H "X-API-Key: icao_xxxx_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" \
  "http://localhost:18080/api/certificates/search?country=KR&certType=DSC&limit=10"
```
**Returns**: Same as example 3, authenticated via API key (requires `cert:read` permission)

---

### Search Parameters

| Parameter | Type | Required | Values | Description |
|-----------|------|----------|--------|-------------|
| `country` | string | No | ISO 3166-1 alpha-2 | Filter by country code (e.g., KR, US) |
| `certType` | string | No | CSCA, DSC, DSC_NC, CRL, ML | Filter by certificate type |
| `validity` | string | No | VALID, EXPIRED, NOT_YET_VALID, all | Filter by validity status (default: all) |
| `source` | string | No | LDIF_PARSED, ML_PARSED, FILE_UPLOAD, PA_EXTRACTED, DL_PARSED | Filter by certificate source type (v2.29.0+) |
| `searchTerm` | string | No | any text | Search in Common Name (CN) |
| `limit` | integer | No | 1-200 | Results per page (default: 50) |
| `offset` | integer | No | 0+ | Pagination offset (default: 0) |

#### Source Type Values

| Value | Description |
|-------|-------------|
| `LDIF_PARSED` | Extracted from LDIF file upload |
| `ML_PARSED` | Extracted from Master List file upload |
| `FILE_UPLOAD` | Individual certificate file upload (PEM, DER, P7B, DL, CRL) |
| `PA_EXTRACTED` | Auto-registered from PA (Passive Authentication) verification |
| `DL_PARSED` | Extracted from Deviation List file |

**Note**: When a `source` filter is applied, the search uses DB-based querying instead of LDAP search.

---

### Required Permissions (API Key)

| Endpoint | Permission |
|----------|------------|
| `GET /api/certificates/search` | `cert:read` |
| `GET /api/certificates/countries` | `cert:read` |
| `GET /api/certificates/validation` | `cert:read` |
| `GET /api/certificates/doc9303-checklist` | `cert:read` |
| `GET /api/certificates/export/*` | `cert:export` |

The 12 available API key permissions are: `cert:read`, `cert:export`, `pa:verify`, `pa:read`, `pa:stats`, `upload:read`, `upload:write`, `report:read`, `ai:read`, `sync:read`, `icao:read`, `api-client:manage`.

---

### Response Format

#### Search Response
```json
{
  "success": true,
  "total": 227,
  "limit": 10,
  "offset": 0,
  "certificates": [
    {
      "dn": "cn=...,o=dsc,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com",
      "cn": "DS0120080307 1",
      "sn": "0D",
      "country": "KR",
      "certType": "DSC",
      "subjectDn": "CN=DS0120080307 1,O=certificates,C=KR",
      "issuerDn": "CN=CSCA,OU=MOFA,O=Government,C=KR",
      "fingerprint": "a1b2c3d4...",
      "validFrom": "2008-03-07T00:00:00Z",
      "validTo": "2018-03-07T23:59:59Z",
      "validity": "EXPIRED",
      "isSelfSigned": false
    }
  ]
}
```

#### Detail Response
```json
{
  "success": true,
  "dn": "...",
  "cn": "...",
  "sn": "...",
  "country": "KR",
  "certType": "DSC",
  "subjectDn": "...",
  "issuerDn": "...",
  "fingerprint": "...",
  "validFrom": "2008-03-07T00:00:00Z",
  "validTo": "2018-03-07T23:59:59Z",
  "validity": "EXPIRED",
  "isSelfSigned": false
}
```

---

### Important Notes

#### CertType Filter Limitation
**Issue**: Filtering by `certType` alone (without `country`) does not effectively filter by type.

**Reason**: LDAP hierarchy requires country for type-based filtering (`o=csca,c=XX,...`).

**Solution**: Always specify country when filtering by type:
```bash
# This works correctly
curl "http://localhost:18080/api/certificates/search?country=KR&certType=CSCA&limit=10"
```

#### Source Filter Uses DB Search
When the `source` parameter is specified, the search bypasses LDAP and queries the database directly. This means DN format in results may differ (parsed from DB fields rather than LDAP DN).

#### Export Format
- **DER**: Binary format (`.crt` or `.der` extension)
- **PEM**: Base64 encoded (`.pem` extension)
- **ZIP**: Multiple certificates in archive (`.zip` extension)

#### Performance
- **Search**: < 500ms for filtered queries
- **Export (single)**: < 100ms
- **Export (country ZIP)**: Varies by country size (1-5s)

---

### Frontend Testing Checklist

Visit: http://localhost:13080/pkd/certificates

- [ ] **Page loads** without errors
- [ ] **Search all** - Click "검색" without filters
- [ ] **Country filter** - Select a country (e.g., Korea), click "검색"
- [ ] **Type filter** - Select country + type (e.g., Korea + DSC)
- [ ] **Source filter** - Select a source type (e.g., PA_EXTRACTED)
- [ ] **Pagination** - Navigate to next/previous page
- [ ] **Page size** - Change results per page (10, 25, 50, 100, 200)
- [ ] **Certificate detail** - Click file icon on any row
- [ ] **Export single** - Click download icon on any row
- [ ] **Export country (DER)** - Enter country code, click DER export
- [ ] **Export country (PEM)** - Enter country code, click PEM export
- [ ] **Export all (PEM)** - Click full export button
- [ ] **Export all (DER)** - Click full export button

---

### Troubleshooting

#### Backend Not Responding
```bash
# Check service status
docker ps | grep pkd-management

# Check logs
docker logs icao-local-pkd-management --tail 50

# Restart service
docker compose -f docker/docker-compose.yaml restart pkd-management
```

#### LDAP Connection Issues
```bash
# Check LDAP status
docker logs icao-local-pkd-haproxy --tail 20

# Test LDAP directly
docker exec icao-local-pkd-management ldapsearch -H ldap://haproxy:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" -w admin \
  -b "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(objectClass=*)" dn | head -20
```

#### Frontend Not Loading
```bash
# Check frontend status
docker logs icao-local-pkd-frontend --tail 20

# Restart frontend
docker compose -f docker/docker-compose.yaml restart frontend

# Force browser refresh
# Ctrl+Shift+R (Windows/Linux) or Cmd+Shift+R (Mac)
```

---

### Quick Stats

- **Total Certificates**: 31,212
- **Countries**: 150+
- **CSCA**: ~845
- **DSC**: ~29,838
- **DSC_NC**: ~502
- **CRL**: 69
- **Response Time**: < 500ms
- **Architecture**: Clean Architecture (4 layers)
- **Build**: v2.37.0

---

### Additional Documentation

- **Implementation Guide**: [CERTIFICATE_SEARCH_IMPLEMENTATION.md](./CERTIFICATE_SEARCH_IMPLEMENTATION.md)
- **Complete Status Report**: [CERTIFICATE_SEARCH_STATUS.md](./CERTIFICATE_SEARCH_STATUS.md)
- **Design Document**: [PKD_CERTIFICATE_SEARCH_DESIGN.md](./PKD_CERTIFICATE_SEARCH_DESIGN.md)

---

**Copyright 2026 SMARTCORE Inc. All rights reserved.**
