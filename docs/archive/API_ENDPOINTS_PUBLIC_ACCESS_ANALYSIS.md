# API 엔드포인트 Public Access 분석

**작성일**: 2026-02-02
**버전**: v2.3.2
**목적**: 전체 시스템의 API 엔드포인트 중 Public Access가 필요한 항목 식별

---

## 1. 분석 방법론

### 1.1 접근 기준

**Public Access 필요**:
- 로그인 전 접근 가능해야 하는 페이지
- Read-only 조회 기능 (민감 정보 미포함)
- 시스템 모니터링 정보
- Health check

**인증 필수**:
- 데이터 변경 (POST, PUT, DELETE)
- 민감 정보 조회
- 관리자 전용 기능
- 사용자별 데이터

---

## 2. 프론트엔드 페이지별 API 사용 현황

### 2.1 Public Access 페이지 (로그인 불필요)

| 페이지 | 경로 | 사용 API | Public 여부 |
|--------|------|----------|------------|
| **Dashboard** | `/` | `/api/upload/countries` | ✅ Public |
| **Certificate Search** | `/pkd/certificates` | `/api/certificates/countries`<br>`/api/certificates/search` | ✅ Public |
| **PA Verify** | `/pa/verify` | `/api/pa/parse-sod`<br>`/api/pa/parse-dg1`<br>`/api/pa/parse-dg2`<br>`/api/pa/verify` | ✅ Public |
| **Sync Dashboard** | `/sync` | `/api/sync/status`<br>`/api/sync/stats` | ✅ Public |
| **ICAO Status** | `/icao/status` | `/api/icao/status`<br>`/api/icao/latest`<br>`/api/icao/history` | ✅ Public |
| **PA History** | `/pa/history` | `/api/pa/history`<br>`/api/pa/statistics` | ✅ Public (Read-only) |
| **Monitoring** | `/monitoring` | `/api/health`<br>`/api/health/database`<br>`/api/health/ldap` | ✅ Public |

### 2.2 인증 필수 페이지

| 페이지 | 경로 | 사용 API | 이유 |
|--------|------|----------|------|
| **File Upload** | `/pkd/upload` | `/api/upload/ldif`<br>`/api/upload/masterlist` | 🔒 데이터 변경 |
| **Upload History** | `/pkd/uploads` | `/api/upload/history`<br>`/api/upload/{id}` | 🔒 업로드 내역 |
| **Upload Dashboard** | `/pkd/upload-dashboard` | `/api/upload/statistics` | 🔒 상세 통계 |
| **Certificate Export** | - | `/api/certificates/export/country`<br>`/api/certificates/export/file` | 🔒 데이터 다운로드 |
| **User Management** | `/admin/users` | `/api/auth/users` | 🔒 관리자 전용 |
| **Audit Logs** | `/admin/audit-log`<br>`/admin/operation-audit` | `/api/audit/operations` | 🔒 감사 로그 |
| **Profile** | `/profile` | `/api/auth/profile` | 🔒 사용자별 데이터 |

---

## 3. 서비스별 Public 엔드포인트 권장사항

### 3.1 PKD Management Service (port 8081)

#### 현재 설정 (auth_middleware.cpp)

```cpp
std::set<std::string> AuthMiddleware::publicEndpoints_ = {
    "^/api/health.*",              // ✅ Health check
    "^/api/auth/login$",           // ✅ Login
    "^/api/auth/register$",        // ✅ Registration
    "^/api/audit/.*",              // ⚠️ TEMPORARY
    "^/api/upload/countries$",     // ✅ Dashboard
    "^/api/certificates/countries$", // ✅ Cert search
    "^/api/certificates/search.*", // ✅ Cert search
    "^/static/.*",                 // ✅ Static files
    "^/api-docs.*",                // ✅ API docs
    "^/swagger-ui/.*"              // ✅ Swagger UI
};
```

#### 추가 필요 엔드포인트

```cpp
// ICAO Monitoring
"^/api/icao/status$",          // ICAO version status
"^/api/icao/latest$",          // Latest version info
"^/api/icao/history.*",        // Version history

// Sync Monitoring
"^/api/sync/status$",          // Sync status
"^/api/sync/stats$",           // Sync statistics

// PA Service History (Read-only)
"^/api/pa/history.*",          // PA verification history
"^/api/pa/statistics$",        // PA statistics
```

#### 인증 유지 필요 (변경 작업)

```cpp
// Upload Operations (POST/DELETE)
/api/upload/ldif              // 🔒 LDIF upload
/api/upload/masterlist        // 🔒 Master List upload
/api/upload/history           // 🔒 Upload history (detailed)
/api/upload/{id}              // 🔒 Upload detail
/api/upload/statistics        // 🔒 Upload statistics (detailed)
/api/upload/{id}/delete       // 🔒 Delete upload

// Certificate Export (Data download)
/api/certificates/export/country  // 🔒 Export by country
/api/certificates/export/file     // 🔒 Export to file

// Sync Operations (POST)
/api/sync/check               // 🔒 Trigger sync check
/api/sync/reconcile           // 🔒 Trigger reconciliation

// ICAO Operations (POST)
/api/icao/check-updates       // 🔒 Trigger version check

// Audit Logs
/api/audit/operations         // 🔒 Remove from public (currently TEMPORARY)
/api/audit/operations/stats   // 🔒 Remove from public
```

---

### 3.2 PA Service (port 8082)

#### Public Access 권장

```cpp
// PA Verification (Core functionality - should be public for demo)
"^/api/pa/verify$",            // PA verification
"^/api/pa/parse-sod$",         // Parse SOD
"^/api/pa/parse-dg1$",         // Parse DG1 (MRZ)
"^/api/pa/parse-dg2$",         // Parse DG2 (Face)
"^/api/pa/parse-mrz-text$",    // Parse MRZ text

// PA History (Read-only)
"^/api/pa/history.*",          // PA history
"^/api/pa/statistics$",        // PA statistics
"^/api/pa/[a-f0-9-]+$",        // PA detail by ID (GET only)
"^/api/pa/[a-f0-9-]+/datagroups$", // DataGroups detail
```

**참고**: PA Service는 demo/verification 용도로 public access가 합리적
단, production에서는 rate limiting 필수

---

### 3.3 PKD Relay Service (port 8083)

#### Public Access 권장

```cpp
// Sync Monitoring
"^/api/sync/status$",          // Sync status (read-only)
"^/api/sync/stats$",           // Sync statistics (read-only)
"^/api/reconcile/history.*",   // Reconciliation history (read-only)
```

#### 인증 필수

```cpp
// Sync Operations
/api/sync/check               // 🔒 Trigger sync
/api/reconcile                // 🔒 Trigger reconciliation
```

---

## 4. 권장 최종 Public Endpoints 설정

### 4.1 PKD Management (auth_middleware.cpp)

```cpp
std::set<std::string> AuthMiddleware::publicEndpoints_ = {
    // System
    "^/api/health.*",              // Health check endpoints
    "^/api/auth/login$",           // Login endpoint
    "^/api/auth/register$",        // Registration endpoint

    // Dashboard & Statistics (Read-only, public info)
    "^/api/upload/countries$",     // Dashboard country statistics

    // Certificate Search (Read-only, public directory)
    "^/api/certificates/countries$", // Country list for search
    "^/api/certificates/search.*",   // Certificate search

    // ICAO Monitoring (Read-only, public info)
    "^/api/icao/status$",          // ICAO version status
    "^/api/icao/latest$",          // Latest version info
    "^/api/icao/history.*",        // Version check history

    // Sync Monitoring (Read-only, public info)
    "^/api/sync/status$",          // Sync status
    "^/api/sync/stats$",           // Sync statistics

    // PA Service (Forwarded, demo functionality)
    "^/api/pa/verify$",            // PA verification
    "^/api/pa/parse-.*",           // PA parsing utilities
    "^/api/pa/history.*",          // PA history (read-only)
    "^/api/pa/statistics$",        // PA statistics
    "^/api/pa/[a-f0-9-]+$",        // PA detail by ID
    "^/api/pa/[a-f0-9-]+/datagroups$", // DataGroups

    // Static files & Documentation
    "^/static/.*",                 // Static files
    "^/api-docs.*",                // API documentation
    "^/swagger-ui/.*"              // Swagger UI
};
```

### 4.2 제거할 항목

```cpp
// ❌ 제거: 임시로 추가했던 audit 전체 공개
"^/api/audit/.*",              // Should require authentication
```

**대신**: 관리자만 접근 가능하도록 명시적 인증 필터 추가

---

## 5. 보안 고려사항

### 5.1 Public 엔드포인트 위험도

| 엔드포인트 | 위험도 | 완화 조치 |
|-----------|--------|----------|
| Certificate Search | 🟡 중간 | Rate limiting, 결과 제한 |
| PA Verify | 🟡 중간 | Rate limiting, 파일 크기 제한 |
| Upload History | 🔴 높음 | 🔒 인증 필수 유지 |
| Audit Logs | 🔴 높음 | 🔒 인증 필수로 변경 |
| File Upload | 🔴 높음 | 🔒 인증 필수 유지 |
| Certificate Export | 🟡 중간 | 🔒 인증 필수 권장 |

### 5.2 Rate Limiting 권장

**nginx 설정 추가 필요**:

```nginx
# Rate limiting zones
limit_req_zone $binary_remote_addr zone=pa_verify:10m rate=10r/m;
limit_req_zone $binary_remote_addr zone=cert_search:10m rate=30r/m;
limit_req_zone $binary_remote_addr zone=general_api:10m rate=60r/m;

# Apply to locations
location /api/pa/verify {
    limit_req zone=pa_verify burst=5 nodelay;
    proxy_pass http://pa-service:8082;
}

location /api/certificates/search {
    limit_req zone=cert_search burst=10 nodelay;
    proxy_pass http://pkd-management:8081;
}
```

---

## 6. 구현 계획

### Phase 1: 즉시 적용 (긴급) ⏱️ 30분

**목표**: 현재 발생한 401 에러 해결

**작업**:
1. Certificate Search 엔드포인트 추가 ✅ (완료)
   ```cpp
   "^/api/certificates/countries$",
   "^/api/certificates/search.*",
   ```

2. 추가 public 페이지 확인 및 엔드포인트 추가
   ```cpp
   "^/api/icao/status$",
   "^/api/icao/latest$",
   "^/api/icao/history.*",
   "^/api/sync/status$",
   "^/api/sync/stats$",
   ```

3. 빌드 및 배포
   ```bash
   cd docker
   docker-compose build --no-cache pkd-management
   docker-compose up -d --force-recreate pkd-management
   ```

---

### Phase 2: PA Service Public Access (1시간)

**목표**: PA 검증 기능을 public으로 개방

**작업**:
1. PA Service에도 AuthMiddleware 구현 (또는 nginx에서 처리)
2. PA 관련 엔드포인트를 public으로 설정
3. Rate limiting 추가

---

### Phase 3: Audit 엔드포인트 보안 강화 (1시간)

**목표**: 임시로 public 처리한 audit 엔드포인트를 인증 필수로 변경

**작업**:
1. `"^/api/audit/.*"` 제거
2. Admin 페이지에서만 접근 가능하도록 명시적 체크
3. 테스트 및 검증

---

### Phase 4: Rate Limiting 구현 (2시간)

**목표**: Public 엔드포인트에 대한 남용 방지

**작업**:
1. nginx rate limiting 설정
2. 각 엔드포인트별 적절한 limit 설정
3. 모니터링 및 조정

---

## 7. 테스트 체크리스트

### 7.1 Public Access 검증

- [ ] 홈페이지 (Dashboard) 로그인 없이 로드
- [ ] Certificate Search 로그인 없이 검색 가능
- [ ] PA Verify 로그인 없이 검증 가능
- [ ] Sync Dashboard 로그인 없이 조회 가능
- [ ] ICAO Status 로그인 없이 조회 가능
- [ ] Health Check 로그인 없이 조회 가능

### 7.2 인증 필수 검증

- [ ] File Upload 로그인 없이 401 반환
- [ ] Upload History 로그인 없이 401 반환
- [ ] Certificate Export 로그인 없이 401 반환
- [ ] User Management 로그인 없이 401 반환
- [ ] Audit Logs 로그인 없이 401 반환 (Phase 3 이후)

### 7.3 로그인 후 정상 작동

- [ ] 모든 페이지 로그인 후 정상 접근
- [ ] 파일 업로드 기능 정상 작동
- [ ] 관리자 기능 정상 작동

---

## 8. 결론

### 8.1 현재 상태

- ✅ Dashboard, Certificate Search public 처리 완료
- ⚠️ ICAO, Sync, PA 엔드포인트 추가 필요
- ⚠️ Audit 엔드포인트 보안 강화 필요

### 8.2 권장 조치

**즉시 (Phase 1)**:
- ICAO, Sync 엔드포인트를 public endpoints에 추가
- 빌드 및 배포하여 모든 public 페이지 정상화

**단기 (Phase 2-3)**:
- PA Service public access 설정
- Audit 엔드포인트 인증 강화

**중기 (Phase 4)**:
- Rate limiting 구현
- 모니터링 및 최적화

---

**작성자**: Claude Sonnet 4.5
**검토**: Phase 1 완료 후 사용자 피드백 반영 필요
