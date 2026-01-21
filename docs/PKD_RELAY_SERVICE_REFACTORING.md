# PKD Relay Service Refactoring Plan

**Date**: 2026-01-20
**Version**: v2.0.0
**Status**: 🚧 In Progress

---

## Executive Summary

This document outlines the comprehensive refactoring plan to reorganize the microservices architecture by introducing **PKD Relay Service** (formerly Sync Service) and restructuring **PKD Management Service** for better separation of concerns.

**Objectives**:
1. Rename Sync Service → **PKD Relay Service** (External data relay)
2. Move Upload & Parsing functionality from PKD Management → PKD Relay Service
3. Move ICAO Auto Sync functionality from PKD Management → PKD Relay Service
4. Streamline PKD Management Service to focus on internal data processing

**Expected Benefits**:
- ✅ Clear separation: External relay vs Internal processing
- ✅ Improved scalability: Independent service scaling
- ✅ Better fault isolation: Upload failures don't affect validation
- ✅ Consistent API design: `/api/relay/*` prefix for all external data sources

---

## Current Architecture (v1.7.0)

```
┌─────────────────────────────────────────────────────────────┐
│              PKD Management Service (:8081)                  │
├─────────────────────────────────────────────────────────────┤
│ • LDIF/ML Upload & Parsing                                  │
│ • ICAO Auto Sync (Version Detection & Notification)         │
│ • Certificate Validation (Trust Chain)                      │
│ • LDAP Storage                                              │
│ • Certificate Search & Export                               │
│ • API: /api/upload/*, /api/icao/*, /api/certificates/*     │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                Sync Service (:8083)                          │
├─────────────────────────────────────────────────────────────┤
│ • DB-LDAP Synchronization Monitoring                        │
│ • Auto Reconciliation                                       │
│ • API: /api/sync/*                                          │
└─────────────────────────────────────────────────────────────┘
```

**Problems**:
- ❌ PKD Management has too many responsibilities (7 features)
- ❌ ICAO Auto Sync is logically a "sync" operation but in wrong service
- ❌ Upload & Parsing should be separated from internal processing
- ❌ Inconsistent API naming (upload, icao, sync all different services)

---

## Target Architecture (v2.0.0)

```
┌─────────────────────────────────────────────────────────────┐
│              PKD Relay Service (:8083)                       │
│              (External Data → Internal System)               │
├─────────────────────────────────────────────────────────────┤
│ 1. ICAO Portal Relay                                        │
│    ├── HTML Fetching & Parsing                             │
│    ├── Version Detection & Notification                    │
│    └── API: /api/relay/icao/*                              │
│                                                              │
│ 2. File Upload Relay                                        │
│    ├── LDIF/Master List Upload & Parsing                   │
│    ├── Temp JSON Storage                                    │
│    └── API: /api/relay/upload/*                            │
│                                                              │
│ 3. DB-LDAP Sync Monitor                                    │
│    ├── Sync Status & Statistics                            │
│    ├── Auto Reconciliation                                  │
│    └── API: /api/relay/sync/*                              │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│           PKD Management Service (:8081)                     │
│           (Internal PKD Data Processing)                     │
├─────────────────────────────────────────────────────────────┤
│ 1. Certificate Validation                                   │
│    ├── Trust Chain Verification                            │
│    └── API: /api/validation/*                              │
│                                                              │
│ 2. Certificate Storage                                      │
│    ├── DB Storage (PostgreSQL)                             │
│    ├── LDAP Storage (OpenLDAP)                             │
│    └── API: /api/storage/*                                 │
│                                                              │
│ 3. Certificate Search & Export                              │
│    ├── LDAP Search                                          │
│    ├── Certificate Export (DER/PEM/ZIP)                    │
│    └── API: /api/certificates/*                            │
└─────────────────────────────────────────────────────────────┘
```

---

## Migration Phases

### Phase 1: Directory Restructuring ✅ (Current)

**Rename Service Directory**:
```bash
mv services/sync-service services/pkd-relay-service
```

**Update Directory Structure**:
```
services/
├── pkd-relay-service/        # ✨ RENAMED
│   ├── src/
│   │   ├── main.cpp           # Service entry point
│   │   ├── relay/             # ✨ NEW namespace
│   │   │   ├── icao/          # ICAO Auto Sync (from pkd-management)
│   │   │   ├── upload/        # File Upload & Parsing (from pkd-management)
│   │   │   └── sync/          # DB-LDAP Sync (existing)
│   │   ├── infrastructure/    # HTTP, Email, LDAP clients
│   │   ├── repositories/      # Database access
│   │   └── utils/             # HTML Parser, etc.
│   ├── CMakeLists.txt
│   ├── vcpkg.json
│   └── Dockerfile
└── pkd-management/
    ├── src/
    │   ├── main.cpp           # Service entry point
    │   ├── validation/        # ✨ NEW: Trust Chain only
    │   ├── storage/           # ✨ NEW: DB + LDAP storage
    │   ├── services/          # Certificate Service (existing)
    │   └── repositories/      # Certificate, CRL repos
    ├── CMakeLists.txt
    └── Dockerfile
```

### Phase 2: Code Migration (PKD Management → PKD Relay)

**2.1. ICAO Auto Sync Module**

**Move to PKD Relay**:
```bash
# Domain
cp services/pkd-management/src/domain/models/icao_version.h \
   services/pkd-relay-service/src/domain/models/

# Services
cp services/pkd-management/src/services/icao_sync_service.* \
   services/pkd-relay-service/src/relay/icao/

# Handlers
cp services/pkd-management/src/handlers/icao_handler.* \
   services/pkd-relay-service/src/relay/icao/

# Repositories
cp services/pkd-management/src/repositories/icao_version_repository.* \
   services/pkd-relay-service/src/repositories/

# Infrastructure
cp services/pkd-management/src/infrastructure/http/* \
   services/pkd-relay-service/src/infrastructure/http/

cp services/pkd-management/src/infrastructure/notification/* \
   services/pkd-relay-service/src/infrastructure/notification/

# Utils
cp services/pkd-management/src/utils/html_parser.* \
   services/pkd-relay-service/src/utils/
```

**Delete from PKD Management**:
```bash
# After successful migration and testing
rm -rf services/pkd-management/src/handlers/icao_handler.*
rm -rf services/pkd-management/src/services/icao_sync_service.*
rm -rf services/pkd-management/src/repositories/icao_version_repository.*
rm -rf services/pkd-management/src/infrastructure/http/*
rm -rf services/pkd-management/src/infrastructure/notification/*
rm -rf services/pkd-management/src/utils/html_parser.*
```

**2.2. Upload & Parsing Module**

**Move to PKD Relay**:
```bash
# Processing
cp services/pkd-management/src/processing/* \
   services/pkd-relay-service/src/relay/upload/
```

**Keep in PKD Management**:
- Validation logic (Trust Chain verification)
- Storage logic (DB + LDAP)
- Certificate Search & Export

### Phase 3: API Endpoint Refactoring

**3.1. PKD Relay Service** (`services/pkd-relay-service/src/main.cpp`)

```cpp
// ============================================
// ICAO Relay Endpoints
// ============================================
app().registerHandler("/api/icao/latest",
    [icaoHandler](const HttpRequestPtr& req, auto&& callback) {
        icaoHandler->handleGetLatest(req, std::move(callback));
    }, {Get});

app().registerHandler("/api/icao/history",
    [icaoHandler](const HttpRequestPtr& req, auto&& callback) {
        icaoHandler->handleGetHistory(req, std::move(callback));
    }, {Get});

app().registerHandler("/api/icao/check-updates",
    [icaoHandler](const HttpRequestPtr& req, auto&& callback) {
        icaoHandler->handleCheckUpdates(req, std::move(callback));
    }, {Post});

app().registerHandler("/api/icao/status",
    [icaoHandler](const HttpRequestPtr& req, auto&& callback) {
        icaoHandler->handleGetStatus(req, std::move(callback));
    }, {Get});

// ============================================
// Upload Relay Endpoints
// ============================================
app().registerHandler("/api/upload/ldif",
    [uploadHandler](const HttpRequestPtr& req, auto&& callback) {
        uploadHandler->handleLdifUpload(req, std::move(callback));
    }, {Post});

app().registerHandler("/api/upload/masterlist",
    [uploadHandler](const HttpRequestPtr& req, auto&& callback) {
        uploadHandler->handleMasterListUpload(req, std::move(callback));
    }, {Post});

app().registerHandler("/api/upload/history",
    [uploadHandler](const HttpRequestPtr& req, auto&& callback) {
        uploadHandler->handleGetHistory(req, std::move(callback));
    }, {Get});

// ============================================
// Sync Monitor Endpoints (existing)
// ============================================
app().registerHandler("/api/sync/status", ...);
app().registerHandler("/api/sync/stats", ...);
app().registerHandler("/api/sync/trigger", ...);
```

**3.2. PKD Management Service** (`services/pkd-management/src/main.cpp`)

```cpp
// ============================================
// Validation Endpoints (NEW)
// ============================================
app().registerHandler("/api/validation/start/{uploadId}",
    [validationHandler](const HttpRequestPtr& req, auto&& callback, std::string uploadId) {
        validationHandler->handleStartValidation(req, std::move(callback), uploadId);
    }, {Post});

app().registerHandler("/api/validation/status/{uploadId}",
    [validationHandler](const HttpRequestPtr& req, auto&& callback, std::string uploadId) {
        validationHandler->handleGetStatus(req, std::move(callback), uploadId);
    }, {Get});

// ============================================
// Storage Endpoints (NEW)
// ============================================
app().registerHandler("/api/storage/upload/{uploadId}",
    [storageHandler](const HttpRequestPtr& req, auto&& callback, std::string uploadId) {
        storageHandler->handleUploadToLdap(req, std::move(callback), uploadId);
    }, {Post});

app().registerHandler("/api/storage/status/{uploadId}",
    [storageHandler](const HttpRequestPtr& req, auto&& callback, std::string uploadId) {
        storageHandler->handleGetStatus(req, std::move(callback), uploadId);
    }, {Get});

// ============================================
// Certificate Endpoints (existing, keep as is)
// ============================================
app().registerHandler("/api/certificates/search", ...);
app().registerHandler("/api/certificates/countries", ...);
app().registerHandler("/api/certificates/export/file", ...);
app().registerHandler("/api/certificates/export/country", ...);
```

### Phase 4: Nginx API Gateway Update

**File**: `nginx/api-gateway.conf`

```nginx
# ============================================
# PKD Relay Service
# ============================================

# ICAO Auto Sync Relay
location /api/relay/icao/ {
    proxy_pass http://pkd-relay:8083/api/icao/;
    include /etc/nginx/proxy_params;
}

# File Upload Relay
location /api/relay/upload/ {
    proxy_pass http://pkd-relay:8083/api/upload/;
    include /etc/nginx/proxy_params;
    client_max_body_size 100M;
    proxy_request_buffering off;
}

# DB-LDAP Sync Monitor
location /api/relay/sync/ {
    proxy_pass http://pkd-relay:8083/api/sync/;
    include /etc/nginx/proxy_params;
}

# ============================================
# PKD Management Service
# ============================================

# Validation
location /api/validation/ {
    proxy_pass http://pkd-management:8081;
    include /etc/nginx/proxy_params;
}

# Storage
location /api/storage/ {
    proxy_pass http://pkd-management:8081;
    include /etc/nginx/proxy_params;
}

# Certificates (existing)
location /api/certificates/ {
    proxy_pass http://pkd-management:8081;
    include /etc/nginx/proxy_params;
}

# Health (existing)
location /api/health {
    proxy_pass http://pkd-management:8081;
    include /etc/nginx/proxy_params;
}
```

### Phase 5: Docker Compose Update

**File**: `docker/docker-compose.yaml`

```yaml
services:
  # ============================================
  # PKD Relay Service (RENAMED from sync-service)
  # ============================================
  pkd-relay:
    build:
      context: ../services/pkd-relay-service
      dockerfile: Dockerfile
    container_name: pkd-relay
    ports:
      - "8083:8083"
    environment:
      - SERVICE_NAME=pkd-relay-service
      - SERVICE_PORT=8083
      - DATABASE_URL=postgresql://pkd:pkd123@postgres:5432/pkd
      - LDAP_URL=ldap://haproxy:389
    depends_on:
      - postgres
      - haproxy
    networks:
      - pkd-network

  # ============================================
  # PKD Management Service (existing)
  # ============================================
  pkd-management:
    build:
      context: ../services/pkd-management
      dockerfile: Dockerfile
    container_name: pkd-management
    ports:
      - "8081:8081"
    environment:
      - SERVICE_NAME=pkd-management-service
      - SERVICE_PORT=8081
      - DATABASE_URL=postgresql://pkd:pkd123@postgres:5432/pkd
      - LDAP_URL=ldap://haproxy:389
    depends_on:
      - postgres
      - haproxy
    networks:
      - pkd-network
```

### Phase 6: Frontend API Refactoring

**6.1. Create Relay API Client**

**File**: `frontend/src/api/relayApi.ts` (NEW)

```typescript
import axios from 'axios';

const BASE_URL = '/api/relay';

export const relayApi = {
  // ICAO Auto Sync
  icao: {
    getLatest: () =>
      axios.get(`${BASE_URL}/icao/latest`),

    getHistory: (limit: number = 10) =>
      axios.get(`${BASE_URL}/icao/history`, { params: { limit } }),

    checkUpdates: () =>
      axios.post(`${BASE_URL}/icao/check-updates`),

    getStatus: () =>
      axios.get(`${BASE_URL}/icao/status`),
  },

  // File Upload
  upload: {
    ldif: (file: File, mode: 'auto' | 'manual') => {
      const formData = new FormData();
      formData.append('file', file);
      return axios.post(`${BASE_URL}/upload/ldif?mode=${mode}`, formData, {
        headers: { 'Content-Type': 'multipart/form-data' },
      });
    },

    masterlist: (file: File) => {
      const formData = new FormData();
      formData.append('file', file);
      return axios.post(`${BASE_URL}/upload/masterlist`, formData, {
        headers: { 'Content-Type': 'multipart/form-data' },
      });
    },

    getHistory: () =>
      axios.get(`${BASE_URL}/upload/history`),

    getStatistics: () =>
      axios.get(`${BASE_URL}/upload/statistics`),
  },

  // DB-LDAP Sync
  sync: {
    getStatus: () =>
      axios.get(`${BASE_URL}/sync/status`),

    getStats: () =>
      axios.get(`${BASE_URL}/sync/stats`),

    trigger: () =>
      axios.post(`${BASE_URL}/sync/trigger`),

    getReconcileHistory: () =>
      axios.get(`${BASE_URL}/sync/reconcile/history`),
  },
};
```

**6.2. Create PKD Management API Client**

**File**: `frontend/src/api/pkdApi.ts` (NEW)

```typescript
import axios from 'axios';

const BASE_URL = '/api';

export const pkdApi = {
  // Validation
  validation: {
    start: (uploadId: string) =>
      axios.post(`${BASE_URL}/validation/start/${uploadId}`),

    getStatus: (uploadId: string) =>
      axios.get(`${BASE_URL}/validation/status/${uploadId}`),
  },

  // Storage
  storage: {
    uploadToLdap: (uploadId: string) =>
      axios.post(`${BASE_URL}/storage/upload/${uploadId}`),

    getStatus: (uploadId: string) =>
      axios.get(`${BASE_URL}/storage/status/${uploadId}`),
  },

  // Certificate Search & Export (existing)
  certificates: {
    search: (params: SearchParams) =>
      axios.get(`${BASE_URL}/certificates/search`, { params }),

    getCountries: () =>
      axios.get(`${BASE_URL}/certificates/countries`),

    getDetail: (dn: string) =>
      axios.get(`${BASE_URL}/certificates/detail`, { params: { dn } }),

    exportFile: (dn: string, format: 'der' | 'pem') =>
      axios.get(`${BASE_URL}/certificates/export/file`, {
        params: { dn, format },
        responseType: 'blob',
      }),

    exportCountry: (country: string, format: 'der' | 'pem') =>
      axios.get(`${BASE_URL}/certificates/export/country`, {
        params: { country, format },
        responseType: 'blob',
      }),
  },
};
```

**6.3. Update Existing API Files**

Replace imports in all frontend pages:

```typescript
// Before
import { uploadApi } from '@/api/uploadApi';
import { icaoApi } from '@/api/icaoApi';

// After
import { relayApi } from '@/api/relayApi';
import { pkdApi } from '@/api/pkdApi';

// Usage
const data = await relayApi.upload.getHistory();
const certs = await pkdApi.certificates.search(params);
```

### Phase 7: vcpkg Dependencies Update

**7.1. PKD Relay Service** (`services/pkd-relay-service/vcpkg.json`)

```json
{
  "name": "pkd-relay-service",
  "version": "2.0.0",
  "dependencies": [
    "drogon",
    "libpq",           // PostgreSQL (uploaded_file, icao_pkd_versions)
    "spdlog",
    "jsoncpp",
    "curl",            // HttpClient (ICAO Portal)
    "openssl",         // EmailSender + LDIF parsing
    "nlohmann-json",   // JSON temp file storage
    "libldap"          // LDAP client (for sync monitoring)
  ]
}
```

**7.2. PKD Management Service** (`services/pkd-management/vcpkg.json`)

```json
{
  "name": "pkd-management-service",
  "version": "2.0.0",
  "dependencies": [
    "drogon",
    "libpq",           // PostgreSQL (certificate, crl, master_list)
    "openldap",        // LDAP client (storage)
    "spdlog",
    "jsoncpp",
    "openssl"          // X509 validation
  ]
}
```

---

## Service Communication

### AUTO Mode Workflow

```cpp
// ============================================
// PKD Relay Service
// ============================================
void onAutoUploadComplete(const std::string& uploadId) {
    // Relay completed parsing → Trigger validation

    httpClient->post(
        "http://pkd-management:8081/api/validation/start/" + uploadId,
        [uploadId](const HttpResponsePtr& resp) {
            if (resp->statusCode() == 200) {
                spdlog::info("[Relay] Validation triggered for {}", uploadId);
            } else {
                spdlog::error("[Relay] Validation failed for {}", uploadId);
            }
        }
    );
}

// ============================================
// PKD Management Service
// ============================================
void onValidationComplete(const std::string& uploadId) {
    // Validation done → Auto-trigger LDAP storage

    storageService->uploadToLdap(uploadId, [uploadId](bool success) {
        if (success) {
            spdlog::info("[PKD Mgmt] Storage completed for {}", uploadId);
            updateUploadStatus(uploadId, "COMPLETED");
        } else {
            spdlog::error("[PKD Mgmt] Storage failed for {}", uploadId);
            updateUploadStatus(uploadId, "FAILED");
        }
    });
}
```

### MANUAL Mode Workflow

```
1. [User] Upload file → POST /api/relay/upload/ldif?mode=manual
2. [Relay] Parse → Save JSON → Status: PARSING → PENDING
3. [User] Click "Validate" → POST /api/validation/start/{uploadId}
4. [PKD Mgmt] Load JSON → Trust Chain → Status: VALIDATED
5. [User] Click "Upload to LDAP" → POST /api/storage/upload/{uploadId}
6. [PKD Mgmt] DB → LDAP → Status: COMPLETED
```

---

## Testing Strategy

### Unit Tests

**PKD Relay Service**:
- ICAO HTML Parser (table format + link fallback)
- LDIF Parser (30,000+ entries)
- Upload file validation (size, format)
- Sync status calculation

**PKD Management Service**:
- Trust Chain validation logic
- LDAP connection handling
- Certificate search filters
- Export ZIP generation

### Integration Tests

**Service-to-Service Communication**:
- Relay → PKD Management HTTP calls
- AUTO mode end-to-end workflow
- Error propagation and rollback

**API Gateway Tests**:
- `/api/relay/*` routing
- `/api/validation/*` routing
- CORS headers
- Rate limiting

### Smoke Tests

```bash
# PKD Relay Service
curl http://localhost:8080/api/relay/icao/latest
curl http://localhost:8080/api/relay/upload/history
curl http://localhost:8080/api/relay/sync/status

# PKD Management Service
curl http://localhost:8080/api/certificates/search?country=KR
curl http://localhost:8080/api/health
```

---

## Rollback Plan

### If Critical Issues Arise

**Option 1: Quick Rollback** (Git revert)
```bash
git revert HEAD~5..HEAD  # Revert last 5 commits
docker compose down
docker compose up -d --build
```

**Option 2: Blue-Green Deployment**
- Keep v1.7.0 containers running
- Deploy v2.0.0 to new containers
- Switch Nginx upstream if successful
- Fallback to v1.7.0 if issues

### Rollback Triggers

- API response time > 5s
- Error rate > 5%
- Critical feature broken (upload, validation, search)
- Database corruption

---

## Documentation Updates

### Files to Update

- [x] `docs/PKD_RELAY_SERVICE_REFACTORING.md` (this document)
- [ ] `CLAUDE.md` - Architecture diagram, API endpoints, service responsibilities
- [ ] `README.md` - Quick start guide, service overview
- [ ] `docs/openapi/pkd-relay-service.yaml` - OpenAPI 3.0 spec
- [ ] `docs/openapi/pkd-management-service.yaml` - Updated spec
- [ ] `docs/DEPLOYMENT_PROCESS.md` - Build and deployment steps
- [ ] `docs/LUCKFOX_DEPLOYMENT.md` - ARM64 deployment guide

### New Documentation

- [ ] `docs/SERVICE_COMMUNICATION.md` - Inter-service HTTP calls, error handling
- [ ] `docs/MANUAL_UPLOAD_WORKFLOW.md` - 3-stage MANUAL mode guide
- [ ] `docs/AUTO_UPLOAD_WORKFLOW.md` - AUTO mode end-to-end flow

---

## Timeline Estimate

| Phase | Duration | Dependencies |
|-------|----------|--------------|
| Phase 1: Directory Restructuring | 1 day | None |
| Phase 2: Code Migration | 2 days | Phase 1 |
| Phase 3: API Refactoring | 2 days | Phase 2 |
| Phase 4: Nginx Update | 0.5 day | Phase 3 |
| Phase 5: Docker Compose | 0.5 day | Phase 3 |
| Phase 6: Frontend Refactoring | 2 days | Phase 3 |
| Phase 7: vcpkg Dependencies | 0.5 day | Phase 2 |
| Testing & Bug Fixes | 2 days | All phases |
| Documentation | 1 day | All phases |
| **Total** | **~11 days** | - |

---

## Success Criteria

- [ ] All API endpoints return HTTP 200 for valid requests
- [ ] MANUAL mode 3-stage upload workflow functional
- [ ] AUTO mode end-to-end workflow functional
- [ ] ICAO Auto Sync daily cron job working
- [ ] Certificate Search returns results
- [ ] Zero regression in existing features
- [ ] Docker build succeeds for both services
- [ ] Frontend loads without errors
- [ ] API Gateway routes correctly
- [ ] Documentation updated and reviewed

---

## Risk Mitigation

### High Risk: Service Communication Failures

**Mitigation**:
- Implement retry logic (3 attempts, exponential backoff)
- Add circuit breaker pattern
- Comprehensive logging at service boundaries

### Medium Risk: Database Schema Changes

**Mitigation**:
- No schema changes required (tables shared between services)
- Both services use same PostgreSQL database
- Test DB connections thoroughly

### Low Risk: Build Failures

**Mitigation**:
- vcpkg dependencies well-tested in v1.7.0
- CI/CD pipeline validates builds before merge
- Incremental builds reduce compile time

---

**Document Owner**: kbjung
**Last Updated**: 2026-01-20
**Status**: Planning Complete, Ready for Implementation
