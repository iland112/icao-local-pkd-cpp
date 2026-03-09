# 외부 Client Agent REST API 접근 구현 계획서

**Version**: 1.0
**Date**: 2026-02-24
**Status**: Draft
**Target Version**: v2.21.0

---

## 1. 개요

### 1.1 목적
외부 Client Agent(출입국관리시스템, 외교부 연동 Agent 등)가 ICAO Local PKD 시스템의 REST API를 안전하게 사용할 수 있도록 **API Key 기반 인증**, **접근 권한 관리**, **Rate Limiting** 기능을 구현한다.

### 1.2 배경
현재 시스템은 웹 UI 사용자를 위한 JWT(HS256) 인증만 지원하며, 서버 간 통신(M2M)에 적합한 인증 메커니즘이 없다. 외부 agent는 사용자명/비밀번호 로그인 없이 API Key 헤더 하나로 인증할 수 있어야 한다.

### 1.3 방안 선택

| 기준 | A. API Key | B. OAuth2 CC | C. mTLS |
|------|-----------|-------------|---------|
| 구현 난이도 | **낮음** (1~2일) | 높음 (5~7일) | 중간 (3~4일) |
| 클라이언트 복잡도 | **낮음** (헤더 1개) | 중간 (토큰 발급 필요) | 높음 (인증서 필요) |
| 보안 수준 | 중 (HTTPS 필수) | 중상 | 최고 |
| Rate Limit 제어 | **세밀** (per-client) | 세밀 | IP 기반만 |
| 운영 편의성 | **높음** (즉시 발급/폐기) | 중간 | 낮음 |
| 현재 아키텍처 호환 | **최고** | 중간 | 중간 |

**선택: 방안 A — API Key 기반 인증**

---

## 2. 현재 시스템 분석

### 2.1 인증 체계
- JWT (HS256) + RBAC (`is_admin` boolean + `permissions` JSON array)
- 토큰 만료: 3600초 (1시간), `POST /api/auth/refresh`로 갱신
- 비밀번호: PBKDF2-HMAC-SHA256 (310,000 iterations)

### 2.2 AuthMiddleware
- `doFilter()`: 49개 public endpoint regex 패턴 → JWT Bearer 토큰 검증
- `X-API-Key` 헤더 검증 미구현 (추가 필요)

### 2.3 nginx Rate Limiting
| Zone | 제한 | 대상 |
|------|------|------|
| `api_limit` | 100 req/s per IP | 전체 API |
| `upload_limit` | 5 req/min per user | 업로드 |
| `export_limit` | 1 req/min per user | 인증서 내보내기 |
| `pa_verify_limit` | 2 req/min per user | PA 검증 |
| `login_limit` | 5 req/min per IP | 로그인 |

### 2.4 제한 사항 (보완 필요)
1. API Key 미지원 — 서버 간 통신에 부적합
2. 일반 사용자와 외부 agent의 Rate Limit 구분 없음
3. Agent별 세밀한 접근 제어 불가 (7개 permission만 존재)
4. 서버 측 토큰 폐기 불가 (JWT stateless 한계)

---

## 3. 설계

### 3.1 API Key 형식

```
형식: icao_{prefix}_{random}
예시: icao_ab12cd34_K7mP9xYzR2qW5vBnL8sT3dFgH6jA4eC
      ^^^^^^^^     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
      접두사(8자)    랜덤 토큰 (32자, base62)
```

- **접두사**: 목록 조회 시 식별용 (`api_key_prefix` 컬럼 저장)
- **해시 저장**: DB에는 `SHA-256(api_key)` 해시만 저장, 원본은 발급 시 1회만 표시
- **총 길이**: `icao_` (5) + prefix (8) + `_` (1) + random (32) = **46자**

### 3.2 DB 스키마

#### 3.2.1 PostgreSQL

```sql
-- API Client 테이블
CREATE TABLE api_clients (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    client_name VARCHAR(255) NOT NULL,
    api_key_hash VARCHAR(64) NOT NULL,            -- SHA-256 hex (64자 고정)
    api_key_prefix VARCHAR(16) NOT NULL,           -- "icao_ab12cd34" (목록 식별용)
    description TEXT,

    -- 접근 권한
    permissions JSONB DEFAULT '[]'::jsonb,          -- ["cert:read", "pa:verify", ...]
    allowed_endpoints JSONB DEFAULT '[]'::jsonb,    -- ["/api/pa/.*", "/api/certificates/.*"]
    allowed_ips JSONB DEFAULT '[]'::jsonb,           -- ["192.168.1.100", "10.0.0.0/24"]

    -- Rate Limit (per-client)
    rate_limit_per_minute INTEGER DEFAULT 60,
    rate_limit_per_hour INTEGER DEFAULT 1000,
    rate_limit_per_day INTEGER DEFAULT 10000,

    -- 상태 관리
    is_active BOOLEAN DEFAULT true,
    expires_at TIMESTAMP,                           -- NULL = 무기한
    last_used_at TIMESTAMP,
    total_requests BIGINT DEFAULT 0,

    -- 감사
    created_by UUID REFERENCES users(id),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE UNIQUE INDEX idx_api_clients_key_hash ON api_clients(api_key_hash);
CREATE INDEX idx_api_clients_prefix ON api_clients(api_key_prefix);
CREATE INDEX idx_api_clients_active ON api_clients(is_active);

-- API 사용 로그 테이블
CREATE TABLE api_client_usage_log (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    client_id UUID REFERENCES api_clients(id) ON DELETE SET NULL,
    client_name VARCHAR(255),
    endpoint VARCHAR(512),
    method VARCHAR(10),
    status_code INTEGER,
    response_time_ms INTEGER,
    ip_address VARCHAR(45),
    user_agent TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_usage_log_client_time ON api_client_usage_log(client_id, created_at);
CREATE INDEX idx_usage_log_created ON api_client_usage_log(created_at);
```

#### 3.2.2 Oracle

```sql
CREATE TABLE api_clients (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    client_name VARCHAR2(255) NOT NULL,
    api_key_hash VARCHAR2(64) NOT NULL,
    api_key_prefix VARCHAR2(16) NOT NULL,
    description VARCHAR2(4000),

    permissions CLOB DEFAULT '[]',
    allowed_endpoints CLOB DEFAULT '[]',
    allowed_ips CLOB DEFAULT '[]',

    rate_limit_per_minute NUMBER(10) DEFAULT 60,
    rate_limit_per_hour NUMBER(10) DEFAULT 1000,
    rate_limit_per_day NUMBER(10) DEFAULT 10000,

    is_active NUMBER(1) DEFAULT 1,
    expires_at TIMESTAMP,
    last_used_at TIMESTAMP,
    total_requests NUMBER(19) DEFAULT 0,

    created_by VARCHAR2(36),
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    updated_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE UNIQUE INDEX idx_api_clients_key_hash ON api_clients(api_key_hash);

CREATE TABLE api_client_usage_log (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    client_id VARCHAR2(36),
    client_name VARCHAR2(255),
    endpoint VARCHAR2(512),
    method VARCHAR2(10),
    status_code NUMBER(5),
    response_time_ms NUMBER(10),
    ip_address VARCHAR2(45),
    user_agent VARCHAR2(4000),
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_usage_log_client_time ON api_client_usage_log(client_id, created_at);
```

### 3.3 인증 흐름

```
외부 Agent                          nginx (API Gateway)              PKD Management
    |                                     |                              |
    |-- GET /api/certificates/search ---->|                              |
    |   Header: X-API-Key: icao_ab12...   |                              |
    |                                     |-- proxy_pass --------------->|
    |                                     |                              |
    |                                     |                   [AuthMiddleware::doFilter]
    |                                     |                   ① Public endpoint? → pass
    |                                     |                   ② Bearer JWT? → 기존 JWT 검증
    |                                     |                   ③ X-API-Key? → API Key 검증
    |                                     |                      a. SHA-256(key) → DB lookup
    |                                     |                      b. is_active + expires_at 확인
    |                                     |                      c. IP 화이트리스트 확인
    |                                     |                      d. 엔드포인트 + 권한 확인
    |                                     |                      e. Rate Limit 확인
    |                                     |                      f. last_used_at + total_requests 갱신
    |                                     |                   ④ 인증 없음 → 401
    |                                     |                              |
    |<------------ 200 OK + Rate Headers -|<-----------------------------|
    |   X-RateLimit-Limit: 60             |                              |
    |   X-RateLimit-Remaining: 59         |                              |
    |   X-RateLimit-Reset: 1708790460     |                              |
```

### 3.4 Rate Limit 설계

#### 계층 구조
| 계층 | 위치 | 대상 | 목적 |
|------|------|------|------|
| L1 | nginx | Per-IP | DDoS/brute force 방어 (기존) |
| L2 | Application | Per-API-Client | 클라이언트별 사용량 제어 (신규) |

#### Application Rate Limiter (인메모리 슬라이딩 윈도우)

```cpp
class ApiKeyRateLimiter {
    struct RateWindow {
        std::atomic<int64_t> minuteCount;
        std::atomic<int64_t> hourCount;
        std::atomic<int64_t> dayCount;
        std::chrono::steady_clock::time_point minuteStart;
        std::chrono::steady_clock::time_point hourStart;
        std::chrono::steady_clock::time_point dayStart;
    };

    std::unordered_map<std::string, RateWindow> windows_;  // client_id → window
    std::shared_mutex mutex_;

    bool isAllowed(const std::string& clientId,
                   int limitPerMin, int limitPerHour, int limitPerDay);
    RateLimitInfo getRateLimitInfo(const std::string& clientId, int limitPerMin);
};
```

#### Rate Limit 초과 응답

```json
HTTP 429 Too Many Requests
Retry-After: 45
X-RateLimit-Limit: 60
X-RateLimit-Remaining: 0
X-RateLimit-Reset: 1708790460

{
    "success": false,
    "error": "Rate limit exceeded",
    "message": "분당 요청 제한(60회)을 초과했습니다",
    "limit": 60,
    "window": "per_minute",
    "retry_after_seconds": 45
}
```

### 3.5 권한 모델

#### Permission 목록

| Permission | 설명 | 대상 엔드포인트 |
|-----------|------|----------------|
| `cert:read` | 인증서 검색/조회 | `/api/certificates/search`, `/api/certificates/validation` |
| `cert:export` | 인증서 내보내기 | `/api/certificates/export/*` |
| `pa:verify` | PA 검증 | `/api/pa/verify`, `/api/pa/parse-*` |
| `pa:read` | PA 이력 조회 | `/api/pa/history`, `/api/pa/{id}` |
| `upload:read` | 업로드 이력 조회 | `/api/upload/history`, `/api/upload/detail/*` |
| `upload:write` | 파일 업로드 | `/api/upload/ldif`, `/api/upload/masterlist`, `/api/upload/certificate` |
| `report:read` | 보고서 조회 | `/api/certificates/dsc-nc/report`, `/api/certificates/crl/report` |
| `ai:read` | AI 분석 결과 조회 | `/api/ai/*` |
| `sync:read` | Sync 상태 조회 | `/api/sync/status`, `/api/sync/stats` |
| `icao:read` | ICAO 버전 조회 | `/api/icao/*` |

#### 접근 검증 로직

```
1. permissions 확인: 요청 엔드포인트에 필요한 permission 보유?
2. allowed_endpoints 확인: 빈 배열 = 전체 허용, 값 있으면 regex 매칭
3. allowed_ips 확인: 빈 배열 = 전체 허용, 값 있으면 IP/CIDR 매칭
```

### 3.6 API 엔드포인트

#### 관리 API (Admin Only)

```
POST   /api/auth/api-clients                    — API Key 발급
GET    /api/auth/api-clients                    — 클라이언트 목록
GET    /api/auth/api-clients/{id}               — 클라이언트 상세
PUT    /api/auth/api-clients/{id}               — 클라이언트 수정
DELETE /api/auth/api-clients/{id}               — 클라이언트 비활성화
POST   /api/auth/api-clients/{id}/regenerate    — API Key 재발급
GET    /api/auth/api-clients/{id}/usage         — 사용량 통계
```

#### 요청/응답 예시

**API Key 발급** — `POST /api/auth/api-clients`
```json
// Request
{
    "client_name": "출입국관리시스템",
    "description": "출입국 심사 시 여권 PA 검증용",
    "permissions": ["pa:verify", "pa:read", "cert:read"],
    "allowed_ips": ["192.168.1.100", "192.168.1.101"],
    "rate_limit_per_minute": 120,
    "rate_limit_per_hour": 5000,
    "rate_limit_per_day": 50000,
    "expires_at": null
}

// Response (200 OK)
{
    "success": true,
    "client": {
        "id": "a1b2c3d4-e5f6-...",
        "client_name": "출입국관리시스템",
        "api_key": "icao_ab12cd34_K7mP9xYzR2qW5vBnL8sT3dFgH6jA4eC",
        "api_key_prefix": "icao_ab12cd34",
        "permissions": ["pa:verify", "pa:read", "cert:read"],
        "rate_limit_per_minute": 120,
        "expires_at": null,
        "created_at": "2026-02-24T10:00:00Z"
    },
    "warning": "API Key는 이 응답에서만 확인 가능합니다. 안전하게 보관하세요."
}
```

---

## 4. 구현 계획

### Phase 1: Backend (PKD Management Service)

#### 4.1 DB 스키마 추가
- **파일**: `docker/init-scripts/07-api-clients-schema.sql` (PostgreSQL)
- **파일**: `docker/db-oracle/init/07-api-clients-schema.sql` (Oracle)
- `api_clients` 테이블 + `api_client_usage_log` 테이블
- 인덱스: `api_key_hash` (UNIQUE), `is_active`, `created_at`

#### 4.2 Repository 계층
- **파일**: `services/pkd-management/src/repositories/api_client_repository.h/.cpp`
- CRUD: `create`, `findByKeyHash`, `findById`, `findAll`, `update`, `deactivate`
- 사용량: `updateLastUsed`, `incrementTotalRequests`
- Multi-DBMS: PostgreSQL + Oracle `IQueryExecutor` 기반

#### 4.3 API Key 생성 유틸리티
- **파일**: `services/pkd-management/src/auth/api_key_generator.h/.cpp`
- 암호학적 난수 생성 (`/dev/urandom` 또는 OpenSSL `RAND_bytes`)
- `generateApiKey()` → `{key, hash, prefix}` 반환
- `hashApiKey(key)` → SHA-256 hex

#### 4.4 Rate Limiter
- **파일**: `services/pkd-management/src/middleware/api_rate_limiter.h/.cpp`
- 인메모리 슬라이딩 윈도우 카운터
- Thread-safe (`std::shared_mutex`)
- `isAllowed(clientId, limits)` → `{allowed, remaining, resetAt}`
- 주기적 만료 윈도우 정리 (5분마다)

#### 4.5 AuthMiddleware 확장
- **파일**: `services/pkd-management/src/middleware/auth_middleware.cpp` (수정)
- `doFilter()` 수정: JWT → API Key → Public → 401 순서
- `validateApiKey()` 메서드 추가
- Request attributes: `client_id`, `client_name`, `auth_type` ("jwt" | "api_key")
- Rate Limit 헤더 자동 추가 (`X-RateLimit-*`)

#### 4.6 API Client Handler
- **파일**: `services/pkd-management/src/handlers/api_client_handler.h/.cpp`
- 7개 엔드포인트 구현
- Admin JWT 인증 필수 (`requireAdmin()`)
- 사용량 통계 집계 (시간별/일별/월별)

#### 4.7 ServiceContainer 확장
- `ApiClientRepository` + `ApiKeyRateLimiter` 등록
- `ApiClientHandler` Route 등록

### Phase 2: Frontend

#### 4.8 API Client 관리 페이지
- **파일**: `frontend/src/pages/ApiClientManagement.tsx`
- **Route**: `/admin/api-clients`
- 클라이언트 목록 (그리드 카드 레이아웃, UserManagement 패턴 참조)
- 통계 카드: 총 클라이언트, 활성, 만료 예정, 오늘 총 요청
- 클라이언트 카드: 이름, prefix, 권한 배지, 상태, 최근 사용, 일일 사용량 바
- 생성/수정/삭제 다이얼로그
- API Key 발급 결과 복사 다이얼로그 (1회성 표시, 클립보드 복사)

#### 4.9 사용량 대시보드 (클라이언트 상세)
- 시간별 요청 추이 차트 (Recharts AreaChart)
- 엔드포인트별 사용량 분포 (BarChart)
- 최근 요청 로그 테이블

#### 4.10 Sidebar 메뉴 추가
- "관리" 섹션에 "API 클라이언트" 메뉴 (Key icon)
- Admin 전용

### Phase 3: 검증 및 배포

#### 4.11 빌드 검증
- `cd frontend && npm run build` — TypeScript + Vite 빌드 성공
- Docker 빌드 (pkd-management) 성공
- ARM64 빌드 + luckfox 배포

#### 4.12 기능 검증
- API Key 발급 → curl 테스트 → Rate Limit 동작 확인
- 만료된 키 거부, 비활성 키 거부, IP 화이트리스트 동작
- 기존 JWT 인증 정상 동작 (regression 없음)

---

## 5. 파일 변경 목록

### 신규 파일
| 파일 | 목적 |
|------|------|
| `docker/init-scripts/07-api-clients-schema.sql` | PostgreSQL 스키마 |
| `docker/db-oracle/init/07-api-clients-schema.sql` | Oracle 스키마 |
| `services/pkd-management/src/repositories/api_client_repository.h` | Repository 헤더 |
| `services/pkd-management/src/repositories/api_client_repository.cpp` | Repository 구현 |
| `services/pkd-management/src/auth/api_key_generator.h` | API Key 생성 유틸리티 헤더 |
| `services/pkd-management/src/auth/api_key_generator.cpp` | API Key 생성 유틸리티 구현 |
| `services/pkd-management/src/middleware/api_rate_limiter.h` | Rate Limiter 헤더 |
| `services/pkd-management/src/middleware/api_rate_limiter.cpp` | Rate Limiter 구현 |
| `services/pkd-management/src/handlers/api_client_handler.h` | Handler 헤더 |
| `services/pkd-management/src/handlers/api_client_handler.cpp` | Handler 구현 |
| `frontend/src/pages/ApiClientManagement.tsx` | 관리 페이지 |
| `frontend/src/api/apiClientApi.ts` | Frontend API 모듈 |

### 수정 파일
| 파일 | 변경 내용 |
|------|----------|
| `services/pkd-management/src/middleware/auth_middleware.h` | `validateApiKey()` 메서드 추가 |
| `services/pkd-management/src/middleware/auth_middleware.cpp` | `doFilter()` X-API-Key 분기 추가 |
| `services/pkd-management/src/infrastructure/service_container.h` | ApiClientRepository 등록 |
| `services/pkd-management/src/infrastructure/service_container.cpp` | 초기화 로직 |
| `services/pkd-management/CMakeLists.txt` | 신규 소스 파일 추가 |
| `nginx/api-gateway.conf` | `/api/auth/api-clients` location 추가 |
| `nginx/api-gateway-ssl.conf` | 동일 |
| `frontend/src/App.tsx` | Route 추가 |
| `frontend/src/components/Sidebar.tsx` | 메뉴 추가 |
| `CLAUDE.md` | API 엔드포인트 + 버전 히스토리 업데이트 |

---

## 6. 외부 Agent 사용 예시

### 6.1 PA 검증

```bash
curl -X POST https://pkd.smartcoreinc.com/api/pa/verify \
  -H "X-API-Key: icao_ab12cd34_K7mP9xYzR2qW5vBnL8sT3dFgH6jA4eC" \
  -H "Content-Type: multipart/form-data" \
  -F "sod=@/path/to/SOD.bin" \
  -F "dg1=@/path/to/DG1.bin"
```

### 6.2 인증서 검색

```bash
curl -X GET "https://pkd.smartcoreinc.com/api/certificates/search?country=KR&type=DSC" \
  -H "X-API-Key: icao_ab12cd34_K7mP9xYzR2qW5vBnL8sT3dFgH6jA4eC"
```

### 6.3 PA Lookup (경량 검증)

```bash
curl -X POST https://pkd.smartcoreinc.com/api/certificates/pa-lookup \
  -H "X-API-Key: icao_ab12cd34_K7mP9xYzR2qW5vBnL8sT3dFgH6jA4eC" \
  -H "Content-Type: application/json" \
  -d '{"subjectDn": "/C=KR/O=MOGAHA/CN=Document Signer 1234"}'
```

### 6.4 Python Agent 예시

```python
import requests

API_BASE = "https://pkd.smartcoreinc.com"
API_KEY = "icao_ab12cd34_K7mP9xYzR2qW5vBnL8sT3dFgH6jA4eC"
HEADERS = {"X-API-Key": API_KEY}

# PA Lookup
resp = requests.post(f"{API_BASE}/api/certificates/pa-lookup",
    headers=HEADERS,
    json={"subjectDn": "/C=KR/O=MOGAHA/CN=Document Signer 1234"})

result = resp.json()
print(f"Status: {result['validation']['validationStatus']}")
print(f"Rate Limit Remaining: {resp.headers.get('X-RateLimit-Remaining')}")
```

---

## 7. 보안 고려사항

| 항목 | 대책 |
|------|------|
| API Key 전송 | HTTPS 필수 (TLS 1.2+, 이미 구현) |
| Key 저장 | DB에 SHA-256 해시만 저장, 원본 1회 표시 후 폐기 |
| Key 노출 대응 | 즉시 `is_active = false` → 재발급 |
| IP 제한 | `allowed_ips` 화이트리스트 (CIDR 지원) |
| Key 순환 | `expires_at` 설정 권장 (90일), 만료 전 알림 |
| Brute Force | nginx IP rate limit (100 req/s) + 잘못된 키 감사 로그 |
| 감사 추적 | 모든 사용 `api_client_usage_log` 기록 |
| 최소 권한 | 클라이언트별 필요한 permission만 부여 |

---

## 8. 향후 확장

### Phase 2 (필요 시)
- Redis 기반 분산 Rate Limiting (다중 인스턴스 환경)
- 사용량 대시보드 알림 (Rate Limit 임계치 도달 시)
- 월간 Quota 관리

### Phase 3 (확장 시)
- OAuth2 Client Credentials 지원 (3rd party SaaS 연동)
- mTLS 클라이언트 인증서 계층 추가 (정부기관 간 연동)
- API 버전 관리 (`/api/v2/...`)
