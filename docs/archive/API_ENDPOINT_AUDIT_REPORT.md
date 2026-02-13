# API Endpoint Audit Report

**생성일시**: 2026-02-06
**목적**: Frontend API 호출과 Backend 엔드포인트 규격 비교
**발견된 문제**: `/api/upload/detail/:id` 엔드포인트 401 Unauthorized 에러

---

## 1. Frontend API 호출 목록

### 1.1 pkdApi.ts (PKD Management Service)

| Method | Frontend Path | Backend Path | Public Access | Status |
|--------|--------------|--------------|---------------|--------|
| GET | /api/health | /api/health | ✅ Yes | ✅ OK |
| GET | /api/health/database | /api/health/database | ✅ Yes | ✅ OK |
| GET | /api/health/ldap | /api/health/ldap | ✅ Yes | ✅ OK |
| GET | /api/certificates/search | /api/certificates/search | ✅ Yes | ✅ OK |
| GET | /api/certificates/countries | /api/certificates/countries | ✅ Yes | ✅ OK |
| GET | /api/certificates/detail | /api/certificates/detail | ✅ Yes | ✅ OK |
| GET | /api/certificates/export/file | /api/certificates/export/file | ✅ Yes | ✅ OK |
| GET | /api/certificates/export/country | /api/certificates/export/country | ✅ Yes | ✅ OK |
| GET | /api/upload/history | /api/upload/history | ✅ Yes | ✅ OK |
| **GET** | **/api/upload/detail/:id** | **/api/upload/detail/{uploadId}** | **❌ NO** | **🔴 401 ERROR** |
| GET | /api/upload/:id/issues | /api/upload/{uploadId}/issues | ✅ Yes | ✅ OK |
| GET | /api/upload/statistics | /api/upload/statistics | ✅ Yes | ✅ OK |
| GET | /api/upload/countries | /api/upload/countries | ✅ Yes | ✅ OK |
| GET | /api/upload/countries/detailed | /api/upload/countries/detailed | ✅ Yes | ✅ OK |
| GET | /api/upload/changes | /api/upload/changes | ✅ Yes | ✅ OK |
| GET | /api/upload/:id/ldif-structure | /api/upload/{uploadId}/ldif-structure | ✅ Yes | ✅ OK |
| GET | /api/ldap/statistics | ? | ? | ❓ Unknown |
| GET | /api/ldap/certificates | ? | ? | ❓ Unknown |
| GET | /api/ldap/certificates/:fingerprint | ? | ? | ❓ Unknown |
| GET | /api/ldap/crls | ? | ? | ❓ Unknown |
| GET | /api/ldap/revocation/check | ? | ? | ❓ Unknown |

### 1.2 relayApi.ts (PKD Relay Service)

| Method | Frontend Path | Backend Path | Public Access | Status |
|--------|--------------|--------------|---------------|--------|
| POST | /api/upload/ldif | /api/upload/ldif | ✅ Yes (TEMP) | ✅ OK |
| POST | /api/upload/masterlist | /api/upload/masterlist | ✅ Yes (TEMP) | ✅ OK |
| GET | /api/upload/history | /api/upload/history | ✅ Yes | ✅ OK |
| **GET** | **/api/upload/detail/:id** | **/api/upload/detail/{uploadId}** | **❌ NO** | **🔴 401 ERROR** |
| POST | /api/upload/:id/parse | /api/upload/{uploadId}/parse | ✅ Yes (TEMP) | ✅ OK |
| POST | /api/upload/:id/validate | /api/upload/{uploadId}/validate | ✅ Yes (TEMP) | ✅ OK |
| POST | /api/upload/:id/ldap | ? | ? | ❓ Unknown |
| DELETE | /api/upload/:id | /api/upload/{uploadId} | ✅ Yes (TEMP) | ✅ OK |
| EventSource | /api/progress/stream/:id | /api/progress/stream/{uploadId} | ✅ Yes | ✅ OK |
| GET | /api/progress/status/:id | /api/progress/status/{uploadId} | ✅ Yes | ✅ OK |
| GET | /api/sync/status | ? (relay service) | ? | ❓ Unknown |
| GET | /api/sync/history | ? (relay service) | ? | ❓ Unknown |
| POST | /api/sync/check | ? (relay service) | ? | ❓ Unknown |
| GET | /api/sync/discrepancies | ? (relay service) | ? | ❓ Unknown |
| POST | /api/sync/reconcile | ? (relay service) | ? | ❓ Unknown |
| GET | /api/sync/health | ? (relay service) | ? | ❓ Unknown |
| GET | /api/sync/config | ? (relay service) | ? | ❓ Unknown |
| PUT | /api/sync/config | ? (relay service) | ? | ❓ Unknown |
| POST | /api/sync/revalidate | ? (relay service) | ? | ❓ Unknown |
| GET | /api/sync/revalidate/history | ? (relay service) | ? | ❓ Unknown |
| GET | /api/sync/reconcile/history | ? (relay service) | ? | ❓ Unknown |
| GET | /api/sync/reconcile/:id | ? (relay service) | ? | ❓ Unknown |
| GET | /api/sync/reconcile/stats | ? (relay service) | ? | ❓ Unknown |

---

## 2. 발견된 문제

### 🔴 CRITICAL: `/api/upload/detail/:id` 엔드포인트 401 Unauthorized

**현상**:
- Frontend에서 업로드 상세정보 다이얼로그를 열 때 401 에러 발생
- 에러 메시지: `"Missing Authorization header"`

**원인**:
```cpp
// auth_middleware.cpp Line 26-27
"^/api/upload/[a-f0-9\\-]+$",  // Upload detail by ID
"^/api/upload/[a-f0-9\\-]+/.*", // Upload sub-resources
```

Line 26 패턴 `^/api/upload/[a-f0-9\\-]+$`는:
- `/api/upload/7d33ba60-1e4c-4793-a158-4d1807b039aa` → ✅ 허용
- `/api/upload/detail/7d33ba60-1e4c-4793-a158-4d1807b039aa` → ❌ 거부

**이유**: "detail" 부분이 UUID 패턴 `[a-f0-9\\-]+`과 일치하지 않음

**해결 방법**:
```cpp
// auth_middleware.cpp에 추가 필요
"^/api/upload/detail/[a-f0-9\\-]+$", // Upload detail by ID (correct pattern)
```

---

## 3. Public Endpoints 현황 (auth_middleware.cpp)

```cpp
std::set<std::string> AuthMiddleware::publicEndpoints_ = {
    // System & Authentication
    "^/api/health.*",
    "^/api/auth/login$",
    "^/api/auth/register$",

    // Dashboard & Statistics
    "^/api/upload/countries$",
    "^/api/upload/countries/detailed.*",
    "^/api/upload/history.*",      // ✅ Upload history
    "^/api/upload/statistics$",
    "^/api/upload/changes.*",
    "^/api/upload/[a-f0-9\\-]+$",  // ❌ /api/upload/:id (deprecated?)
    "^/api/upload/[a-f0-9\\-]+/.*", // ✅ /api/upload/:id/issues, /api/upload/:id/ldif-structure

    // 🔴 MISSING: /api/upload/detail/:id 패턴이 없음!

    // File Upload (TEMPORARY for testing)
    "^/api/upload/ldif$",
    "^/api/upload/masterlist$",
    "^/api/progress.*",

    // Certificate Search
    "^/api/certificates/countries$",
    "^/api/certificates/search.*",
    "^/api/certificates/validation.*",
    "^/api/certificates/export/.*",

    // ICAO PKD Monitoring
    "^/api/icao/status$",
    "^/api/icao/latest$",
    "^/api/icao/history.*",

    // Sync Dashboard
    "^/api/sync/status$",
    "^/api/sync/stats$",
    "^/api/reconcile/history.*",

    // Audit Logs
    "^/api/audit/operations$",
    "^/api/audit/operations/stats$",

    // PA Service
    "^/api/pa/verify$",
    "^/api/pa/parse-sod$",
    "^/api/pa/parse-dg1$",
    "^/api/pa/parse-dg2$",
    // ... (생략)
};
```

---

## 4. 권장 조치사항

### 4.1 즉시 수정 필요 (CRITICAL)

```cpp
// services/pkd-management/src/middleware/auth_middleware.cpp Line 26-28
// ❌ BEFORE:
"^/api/upload/[a-f0-9\\-]+$",  // Upload detail by ID
"^/api/upload/[a-f0-9\\-]+/.*", // Upload sub-resources

// ✅ AFTER:
"^/api/upload/[a-f0-9\\-]+$",  // Upload by ID (deprecated pattern)
"^/api/upload/detail/[a-f0-9\\-]+$", // Upload detail by ID (NEW - fixes 401 error)
"^/api/upload/[a-f0-9\\-]+/.*", // Upload sub-resources (issues, ldif-structure, etc.)
```

### 4.2 검증 필요 엔드포인트

1. **LDAP 관련 엔드포인트** (pkdApi.ts에서 호출하지만 backend 미확인):
   - /api/ldap/statistics
   - /api/ldap/certificates
   - /api/ldap/certificates/:fingerprint
   - /api/ldap/crls
   - /api/ldap/revocation/check

2. **Sync 관련 엔드포인트** (relayApi.ts에서 호출하지만 pkd-management에는 없을 수 있음):
   - PKD Relay Service (port 8083)로 라우팅되는지 확인 필요

### 4.3 배포 후 테스트 체크리스트

- [ ] `/api/upload/detail/:id` 엔드포인트 접근 가능 확인
- [ ] Frontend 업로드 상세 다이얼로그 정상 작동 확인
- [ ] LDAP 저장 통계 정상 표시 확인
- [ ] 모든 public endpoints 401 에러 없음 확인

---

## 5. 결론

**문제 요약**:
- Frontend는 `/api/upload/detail/:id` 형식으로 호출
- Backend는 `/api/upload/detail/{uploadId}` 형식으로 등록
- Public endpoints에는 이 패턴을 허용하는 정규표현식이 없음

**즉시 조치**:
- `auth_middleware.cpp`에 `"^/api/upload/detail/[a-f0-9\\-]+$"` 패턴 추가
- 서비스 재빌드 및 재배포 필요
