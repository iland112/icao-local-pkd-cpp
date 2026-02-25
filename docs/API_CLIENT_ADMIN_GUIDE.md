# API Client 관리자 가이드

**Version**: 1.0.0
**Last Updated**: 2026-02-24
**대상**: ICAO Local PKD 시스템 관리자
**API Version**: v2.21.0

---

## 개요

ICAO Local PKD 시스템은 외부 Client Agent(출입국관리시스템, 외교부 연동 Agent 등)가 REST API를 안전하게 사용할 수 있도록 **API Key 기반 인증**을 지원합니다.

### JWT vs API Key 비교

| 항목 | JWT (웹 UI) | API Key (외부 Agent) |
|------|------------|---------------------|
| **인증 방식** | ID/PW 로그인 → 토큰 발급 | `X-API-Key` 헤더 |
| **만료** | 1시간 (갱신 가능) | 관리자 설정 (무기한 가능) |
| **대상** | 웹 브라우저 사용자 | 서버 간 M2M 통신 |
| **Rate Limit** | 사용자별 공통 | 클라이언트별 개별 설정 |
| **권한 제어** | admin/user 역할 | 10개 세부 Permission |
| **IP 제한** | 없음 | 클라이언트별 IP 화이트리스트 |
| **관리** | 사용자 관리 페이지 | API Client 관리 페이지 |

### Base URL

```
# HTTPS (운영 환경, Private CA 인증서 필요)
https://pkd.smartcoreinc.com/api/auth/api-clients

# HTTP (내부 네트워크)
http://pkd.smartcoreinc.com/api/auth/api-clients

# API Gateway (직접)
http://localhost:8080/api/auth/api-clients
```

### 사전 조건

- **관리자 계정**: 모든 API Client 관리 API는 admin 역할의 JWT 토큰이 필요합니다
- JWT 토큰 발급: `POST /api/auth/login` → `token` 필드 사용

```bash
# JWT 토큰 발급
TOKEN=$(curl -s -X POST https://pkd.smartcoreinc.com/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username": "admin", "password": "admin123"}' | jq -r '.token')
```

---

## API Client 관리 API

### API Endpoints Summary

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

### 1. 클라이언트 생성 (`POST /api/auth/api-clients`)

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

### 2. 클라이언트 목록 조회 (`GET /api/auth/api-clients`)

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

### 3. 클라이언트 상세 조회 (`GET /api/auth/api-clients/{id}`)

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

### 4. 클라이언트 수정 (`PUT /api/auth/api-clients/{id}`)

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

### 5. 클라이언트 비활성화 (`DELETE /api/auth/api-clients/{id}`)

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

### 6. API Key 재발급 (`POST /api/auth/api-clients/{id}/regenerate`)

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

### 7. 사용 통계 조회 (`GET /api/auth/api-clients/{id}/usage`)

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

## PA Service 연동 (nginx auth_request)

PA Service(`/api/pa/*`) 요청의 API Key 추적은 **nginx auth_request** 메커니즘으로 구현됩니다.

### 동작 방식

```
Client ──X-API-Key──→ nginx (/api/pa/verify)
                        ├── auth_request → PKD Management /api/auth/internal/check
                        │     ↓ API Key 검증 + Rate Limit + Usage Log
                        │     ↓ 200 OK (허용) / 401 (거부) / 403 (Rate Limit)
                        └── proxy_pass → PA Service (8082)
```

1. 클라이언트가 `X-API-Key` 헤더와 함께 PA 요청 전송
2. nginx가 내부 subrequest로 PKD Management에 인증 확인 (`/api/auth/internal/check`)
3. PKD Management가 API Key 검증 → Rate Limit 체크 → 사용 로그 기록
4. 인증 성공(200) 시 nginx가 PA Service로 요청 전달, 실패 시 JSON 에러 반환

### 관리자 참고사항

- **Permission**: PA 통신용 클라이언트는 `pa:verify` 또는 `pa:read` 포함 필수
- **사용량 모니터링**: `/api/auth/api-clients/{id}/usage` API에서 `/api/pa/verify` 등 PA 엔드포인트 사용량 확인
- **Rate Limit**: 전체 엔드포인트(PKD Management + PA)에 대해 공유 Rate Limiter 적용
- **API Key 미제공 시**: 기존과 동일하게 public 접근 허용 (하위 호환)

---

## Permission 모델

10개의 세부 Permission으로 클라이언트별 접근 범위를 제어합니다.

| Permission | 설명 | 관련 엔드포인트 |
|------------|------|----------------|
| `cert:read` | 인증서 검색/조회 | `/api/certificates/search`, `/api/certificates/detail`, `/api/certificates/validation` |
| `cert:export` | 인증서 내보내기 | `/api/certificates/export/*` |
| `pa:verify` | PA 검증 수행 | `/api/pa/verify`, `/api/pa/parse-*` |
| `pa:read` | PA 이력 조회 | `/api/pa/history`, `/api/pa/{id}`, `/api/pa/statistics` |
| `upload:read` | 업로드 이력 조회 | `/api/upload/history`, `/api/upload/detail/*`, `/api/upload/statistics` |
| `upload:write` | 파일 업로드 | `/api/upload/ldif`, `/api/upload/masterlist`, `/api/upload/certificate` |
| `report:read` | 보고서 조회 | `/api/certificates/dsc-nc/report`, `/api/certificates/crl/report` |
| `ai:read` | AI 분석 결과 조회 | `/api/ai/certificate/*`, `/api/ai/anomalies`, `/api/ai/statistics` |
| `sync:read` | 동기화 상태 조회 | `/api/sync/status`, `/api/sync/stats` |
| `icao:read` | ICAO 버전 조회 | `/api/icao/status`, `/api/icao/latest`, `/api/icao/history` |

### Permission 설정 예시

```json
// PA 검증 전용 Agent
{"permissions": ["pa:verify"]}

// 인증서 조회 + AI 분석 Agent
{"permissions": ["cert:read", "cert:export", "ai:read"]}

// 전체 읽기 Agent
{"permissions": ["cert:read", "cert:export", "pa:verify", "pa:read", "upload:read", "report:read", "ai:read", "sync:read", "icao:read"]}
```

> **참고**: `permissions`가 빈 배열(`[]`)이면 모든 Public 엔드포인트에 접근할 수 있지만, 특정 Permission이 필요한 Protected 엔드포인트는 접근이 거부됩니다.

---

## Rate Limiting

### 3-Tier Rate Limiting

| 계층 | 윈도우 | 기본값 | 설명 |
|------|--------|--------|------|
| **분당** | 60초 슬라이딩 | 60 req/min | 순간 트래픽 제어 |
| **시간당** | 3600초 슬라이딩 | 1,000 req/hr | 중간 범위 제어 |
| **일일** | 86400초 슬라이딩 | 10,000 req/day | 일간 사용량 제어 |

### Rate Limit 초과 시 응답

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

### nginx Rate Limiting (L1)

API Key Rate Limiting(L2) 외에도 nginx에서 IP 기반 L1 Rate Limiting이 적용됩니다:

| Zone | 제한 | 대상 |
|------|------|------|
| `api_limit` | 100 req/s per IP | 전체 API |
| `upload_limit` | 5 req/min per user | 업로드 |
| `pa_verify_limit` | 2 req/min per user | PA 검증 |

---

## IP 화이트리스트

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

## 보안 권장사항

1. **HTTPS 필수**: API Key가 네트워크에 평문으로 노출되지 않도록 반드시 HTTPS를 사용하세요.

2. **API Key 안전 저장**: API Key는 생성 시 한 번만 표시됩니다.
   - 환경 변수 또는 시크릿 매니저에 저장
   - 소스 코드나 로그에 포함하지 마세요

3. **최소 권한 원칙**: 클라이언트에 필요한 Permission만 부여하세요.

4. **정기 키 교체**: 보안을 위해 주기적으로 API Key를 재발급하세요 (`/regenerate`).

5. **IP 제한 활용**: 가능하면 `allowed_ips`를 설정하여 접근 IP를 제한하세요.

6. **미사용 클라이언트 비활성화**: 더 이상 사용하지 않는 클라이언트는 즉시 비활성화하세요.

7. **만료 설정**: 임시 연동의 경우 `expires_at`을 설정하여 자동 만료되도록 하세요.

8. **사용량 모니터링**: `/usage` API를 통해 비정상적인 사용 패턴을 주기적으로 확인하세요.

---

## 프론트엔드 관리 페이지

웹 UI에서도 API Client를 관리할 수 있습니다:

- **URL**: `https://pkd.smartcoreinc.com/admin/api-clients`
- **접근**: 사이드바 → "Admin & Security" → "API 클라이언트"
- **기능**: 클라이언트 생성, 목록 조회, 상세 정보, 키 재발급, 비활성화

---

## 에러 코드

| HTTP Status | 에러 | 설명 |
|-------------|------|------|
| `400` | Bad Request | 필수 필드 누락 또는 잘못된 JSON |
| `401` | Unauthorized | API Key 없음 또는 잘못된 키 |
| `403` | Forbidden | 비활성 클라이언트, 만료, IP 차단, 권한 부족 |
| `404` | Not Found | 클라이언트 ID 없음 |
| `429` | Too Many Requests | Rate Limit 초과 |
| `500` | Internal Server Error | 서버 오류 |

---

## Changelog

| 버전 | 날짜 | 변경 내용 |
|------|------|----------|
| 1.0.0 | 2026-02-24 | 초기 작성 (v2.21.0 API Client 기능) |
