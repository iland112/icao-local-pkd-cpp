# PKD Relay Service - API Endpoint Mapping

**Date**: 2026-01-20
**Version**: v2.0.0
**Status**: In Progress

---

## Overview

This document defines the API endpoint mapping during the PKD Relay Service refactoring.

**Goal**: Separate external data relay operations from internal data processing operations.

---

## Current State (v1.7.0)

### PKD Management Service (:8081)
- Upload/Parsing
- ICAO Auto Sync
- Certificate Validation
- Certificate Search
- Health Check

### Sync Service (:8083)
- DB-LDAP Sync Monitoring
- Auto Reconciliation

---

## Target State (v2.0.0)

### PKD Relay Service (:8083) - **Data Relay Layer**
External data collection and relay operations

### PKD Management Service (:8081) - **Data Processing Layer**
Internal data processing, validation, storage, and search

---

## API Endpoint Migration Plan

### 1. ICAO Auto Sync (PKD Management → PKD Relay)

| Current Endpoint | New Endpoint | Service | Status |
|-----------------|--------------|---------|--------|
| `GET /api/icao/latest` | `GET /api/relay/icao/latest` | PKD Relay | ⏳ Pending |
| `GET /api/icao/history` | `GET /api/relay/icao/history` | PKD Relay | ⏳ Pending |
| `POST /api/icao/check-updates` | `POST /api/relay/icao/check-updates` | PKD Relay | ⏳ Pending |
| `GET /api/icao/status` | `GET /api/relay/icao/status` | PKD Relay | ⏳ Pending |

**Rationale**: ICAO portal monitoring is an external data relay operation.

### 2. Upload & Parsing (PKD Management → PKD Relay)

| Current Endpoint | New Endpoint | Service | Status |
|-----------------|--------------|---------|--------|
| `POST /api/upload/ldif` | `POST /api/relay/upload/ldif` | PKD Relay | ⏳ Pending |
| `POST /api/upload/masterlist` | `POST /api/relay/upload/masterlist` | PKD Relay | ⏳ Pending |
| `GET /api/upload/history` | `GET /api/relay/upload/history` | PKD Relay | ⏳ Pending |
| `GET /api/upload/statistics` | `GET /api/relay/upload/statistics` | PKD Relay | ⏳ Pending |
| `GET /api/progress/stream/{id}` | `GET /api/relay/upload/progress/{id}` | PKD Relay | ⏳ Pending |
| `DELETE /api/upload/{id}` | `DELETE /api/relay/upload/{id}` | PKD Relay | ⏳ Pending |

**Rationale**: File upload and parsing are external data relay operations.

### 3. DB-LDAP Sync (Stays in PKD Relay)

| Current Endpoint | New Endpoint | Service | Status |
|-----------------|--------------|---------|--------|
| `GET /api/sync/health` | `GET /api/relay/sync/health` | PKD Relay | ⏳ Pending |
| `GET /api/sync/status` | `GET /api/relay/sync/status` | PKD Relay | ⏳ Pending |
| `GET /api/sync/stats` | `GET /api/relay/sync/stats` | PKD Relay | ⏳ Pending |
| `POST /api/sync/trigger` | `POST /api/relay/sync/trigger` | PKD Relay | ⏳ Pending |
| `GET /api/sync/config` | `GET /api/relay/sync/config` | PKD Relay | ⏳ Pending |
| `POST /api/sync/config` | `POST /api/relay/sync/config` | PKD Relay | ⏳ Pending |
| `GET /api/sync/reconcile/history` | `GET /api/relay/sync/reconcile/history` | PKD Relay | ⏳ Pending |
| `GET /api/sync/reconcile/{id}` | `GET /api/relay/sync/reconcile/{id}` | PKD Relay | ⏳ Pending |

**Rationale**: DB-LDAP sync monitoring is a relay coordination operation.

### 4. PKD Management (Stays, focus on internal processing)

| Endpoint | Service | Status |
|----------|---------|--------|
| `GET /api/health` | PKD Management | ✅ No change |
| `GET /api/health/database` | PKD Management | ✅ No change |
| `GET /api/health/ldap` | PKD Management | ✅ No change |
| `GET /api/certificates/search` | PKD Management | ✅ No change |
| `GET /api/certificates/countries` | PKD Management | ✅ No change |
| `GET /api/certificates/detail` | PKD Management | ✅ No change |
| `GET /api/certificates/export/file` | PKD Management | ✅ No change |
| `GET /api/certificates/export/country` | PKD Management | ✅ No change |

**Rationale**: Certificate search and export are internal data processing operations.

---

## URL Prefix Convention

### PKD Relay Service
- **Base**: `/api/relay/*`
- **ICAO Relay**: `/api/relay/icao/*`
- **Upload Relay**: `/api/relay/upload/*`
- **Sync Monitor**: `/api/relay/sync/*`

### PKD Management Service
- **Base**: `/api/*` (unchanged)
- **Health**: `/api/health/*`
- **Certificates**: `/api/certificates/*`

---

## Migration Strategy

### Phase 1: Dual Endpoint Support (Backward Compatible)
- Keep old endpoints working in PKD Management
- Add new endpoints in PKD Relay
- Update Frontend to use new endpoints
- Test both paths

### Phase 2: Deprecation Notice
- Add deprecation warnings to old endpoints
- Update all documentation
- Notify users

### Phase 3: Removal (v2.1.0+)
- Remove old endpoints from PKD Management
- Keep only new endpoints in PKD Relay

---

## Frontend API Refactoring

### New API Files

1. **`frontend/src/api/relayApi.ts`** (NEW)
   - `icaoApi`: ICAO relay operations
   - `uploadApi`: Upload relay operations
   - `syncApi`: Sync monitor operations

2. **`frontend/src/api/pkdApi.ts`** (NEW)
   - `healthApi`: Health check operations
   - `certificateApi`: Certificate search/export operations

3. **`frontend/src/api/index.ts`** (UPDATED)
   - Re-export all APIs

### API Client Structure

```typescript
// relayApi.ts
export const relayApi = {
  icao: {
    getLatest: () => axios.get('/api/relay/icao/latest'),
    getHistory: (limit?: number) => axios.get('/api/relay/icao/history', { params: { limit } }),
    checkUpdates: () => axios.post('/api/relay/icao/check-updates'),
    getStatus: () => axios.get('/api/relay/icao/status'),
  },
  upload: {
    uploadLdif: (file: File, mode: string) => axios.post('/api/relay/upload/ldif', formData),
    uploadMasterList: (file: File, mode: string) => axios.post('/api/relay/upload/masterlist', formData),
    getHistory: (params?: any) => axios.get('/api/relay/upload/history', { params }),
    getStatistics: () => axios.get('/api/relay/upload/statistics'),
    deleteUpload: (id: string) => axios.delete(`/api/relay/upload/${id}`),
  },
  sync: {
    getHealth: () => axios.get('/api/relay/sync/health'),
    getStatus: () => axios.get('/api/relay/sync/status'),
    getStats: () => axios.get('/api/relay/sync/stats'),
    triggerSync: () => axios.post('/api/relay/sync/trigger'),
    getConfig: () => axios.get('/api/relay/sync/config'),
    updateConfig: (config: any) => axios.post('/api/relay/sync/config', config),
    getReconcileHistory: (params?: any) => axios.get('/api/relay/sync/reconcile/history', { params }),
    getReconcileDetail: (id: string) => axios.get(`/api/relay/sync/reconcile/${id}`),
  },
};

// pkdApi.ts
export const pkdApi = {
  health: {
    getHealth: () => axios.get('/api/health'),
    getDatabase: () => axios.get('/api/health/database'),
    getLdap: () => axios.get('/api/health/ldap'),
  },
  certificates: {
    search: (params: any) => axios.get('/api/certificates/search', { params }),
    getCountries: () => axios.get('/api/certificates/countries'),
    getDetail: (dn: string) => axios.get('/api/certificates/detail', { params: { dn } }),
    exportFile: (dn: string, format: string) => axios.get('/api/certificates/export/file', { params: { dn, format }, responseType: 'blob' }),
    exportCountry: (country: string, format: string) => axios.get('/api/certificates/export/country', { params: { country, format }, responseType: 'blob' }),
  },
};
```

---

## Testing Strategy

### Unit Tests
- Test each API endpoint independently
- Mock external dependencies

### Integration Tests
- Test API Gateway routing
- Test end-to-end flows

### Smoke Tests
- Verify all endpoints return 200 OK
- Verify backward compatibility

---

## Rollback Plan

If issues occur:
1. Switch Nginx routing back to old endpoints
2. Update Frontend to use old endpoints
3. Investigate and fix issues
4. Retry migration

---

## Timeline

| Phase | Duration | Status |
|-------|----------|--------|
| Phase 3a: Define API endpoints | 1 hour | ⏳ In Progress |
| Phase 3b: Update PKD Management | 2 hours | ⏳ Pending |
| Phase 4: Update Nginx Gateway | 1 hour | ⏳ Pending |
| Phase 5: Update Docker Compose | 30 min | ⏳ Pending |
| Phase 6: Frontend refactoring | 3 hours | ⏳ Pending |
| Testing | 2 hours | ⏳ Pending |
| **Total** | **~10 hours** | |

---

**Document Created**: 2026-01-20
**Last Updated**: 2026-01-20 19:15
**Status**: Draft - Phase 3a in progress
