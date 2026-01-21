# PKD Relay Service Phase 6: Frontend API Refactoring

**Version**: 2.0.0
**Date**: 2026-01-20
**Status**: Planning

---

## Overview

Phase 6 focuses on Frontend API layer refactoring to align with the new backend service architecture while maintaining all existing functionality, especially SSE (Server-Sent Events) handling.

### Key Principle

**Backend stays unchanged (v1.7.0 endpoints remain functional)**
- PKD Management: Upload, ICAO, Certificate APIs
- PA Service: Passive Authentication APIs
- Sync Service (now PKD Relay): Sync monitoring APIs

**Frontend creates abstraction layer (v2.0.0 API structure)**
- New API modules: `relayApi.ts`, `pkdApi.ts`
- Backward compatible with existing services
- Future-proof for service migration

---

## Current API Structure (v1.7.0)

### services/api.ts Analysis

**Single monolithic API file** with 4 service groups:

1. **Health & Upload APIs** → PKD Management (port 8081)
   - `healthApi.*` → `/api/health/*`
   - `uploadApi.*` → `/api/upload/*`
   - `ldapApi.*` → `/api/ldap/*` (certificate search)
   - **SSE**: `createProgressEventSource()` → `/api/progress/stream/{uploadId}`

2. **PA APIs** → PA Service (port 8082)
   - `paApi.*` → `/api/pa/*`

3. **Sync APIs** → Sync Service (port 8083)
   - `syncServiceApi.*` → `/api/sync/*`

4. **Monitoring APIs** → Monitoring Service (port 8084)
   - `monitoringServiceApi.*` → `/api/monitoring/*`

### Critical SSE Implementation

**FileUpload.tsx** uses sophisticated SSE handling:

```typescript
// Line 447: Create SSE connection
const eventSource = createProgressEventSource(id);

// Line 453-457: Connection established
eventSource.addEventListener('connected', () => {
  setSseConnected(true);
});

// Line 460-468: Progress events
eventSource.addEventListener('progress', (event) => {
  const progress: UploadProgress = JSON.parse(event.data);
  handleProgressUpdate(progress);
});

// Line 480-495: Error handling with reconnect
eventSource.onerror = () => {
  if (reconnectAttempts < maxReconnectAttempts) {
    setTimeout(() => connectToProgressStream(id), 1000);
  }
};

// Line 498: Polling backup
startPolling(id);
```

**Stage Mapping** (Line 517-534):
- `PARSING`: 10-50% → 0-100% (stage percentage)
- `VALIDATION`: 55-70% → 0-50%
- `DB_SAVING`: 72-85% → 50-100%
- `LDAP_SAVING`: 87-100% → 0-100%

**Status Detection** (Line 505-510):
- `*_STARTED`, `*_IN_PROGRESS` → 'IN_PROGRESS'
- `*_COMPLETED`, `COMPLETED` → 'COMPLETED'
- `FAILED` → 'FAILED'

---

## Target API Structure (v2.0.0)

### New File Structure

```
frontend/src/services/
├── api.ts                      # Main axios instance + base config
├── relayApi.ts                 # PKD Relay Service APIs (NEW)
│   ├── uploadApi              # Upload & parsing
│   ├── icaoApi                # ICAO version monitoring (future)
│   ├── syncApi                # DB-LDAP sync
│   └── createProgressEventSource  # SSE factory
├── pkdApi.ts                   # PKD Management APIs (NEW)
│   ├── healthApi              # System health
│   ├── certificateApi         # Certificate search & export
│   └── ldapApi                # Direct LDAP queries
├── paApi.ts                    # PA Service APIs (UNCHANGED)
└── monitoringApi.ts            # Monitoring Service APIs (UNCHANGED)
```

### API Routing

**Current (v1.7.0)**:
```
/api/upload/*     → PKD Management (8081)
/api/icao/*       → PKD Management (8081)
/api/sync/*       → Sync Service (8083)
```

**Future (v2.0.0 with API Gateway v2)**:
```
/api/relay/upload/*  → PKD Relay (8083)
/api/relay/icao/*    → PKD Relay (8083)
/api/relay/sync/*    → PKD Relay (8083)

# Backward compatibility (Gateway rewrite)
/api/upload/*     → /api/relay/upload/*
/api/icao/*       → /api/relay/icao/*
/api/sync/*       → /api/relay/sync/*
```

**Phase 6 Strategy**:
- Frontend uses `/api/relay/*` (new paths)
- API Gateway v1 still active (no immediate switch needed)
- When ready, switch to Gateway v2 (transparent to frontend)

---

## Implementation Plan

### Step 1: Create relayApi.ts

**Responsibilities**:
- Upload & parsing endpoints
- ICAO version monitoring (placeholder for future)
- DB-LDAP sync monitoring
- **SSE connection factory** (critical!)

**File**: `frontend/src/services/relayApi.ts`

```typescript
import axios from 'axios';

const relayApi = axios.create({
  baseURL: '/api/relay',  // Future-proof path
  timeout: 60000,
});

// Upload APIs (moved from uploadApi)
export const uploadApi = {
  uploadLdif: (file: File, processingMode: string = 'AUTO') => {
    const formData = new FormData();
    formData.append('file', file);
    formData.append('processingMode', processingMode);
    return relayApi.post('/upload/ldif', formData, {
      headers: { 'Content-Type': undefined },
      timeout: 300000,
    });
  },

  // ... other upload methods

  triggerParse: (uploadId: string) =>
    relayApi.post(`/upload/${uploadId}/parse`),

  triggerValidate: (uploadId: string) =>
    relayApi.post(`/upload/${uploadId}/validate`),

  triggerLdapUpload: (uploadId: string) =>
    relayApi.post(`/upload/${uploadId}/ldap`),
};

// CRITICAL: SSE connection factory
export const createProgressEventSource = (uploadId: string): EventSource => {
  // Use /api/relay/progress for future, but fallback to /api/progress for now
  return new EventSource(`/api/relay/progress/stream/${uploadId}`);
};

// Sync APIs (moved from syncServiceApi)
export const syncApi = {
  getStatus: () => relayApi.get('/sync/status'),
  getHistory: (limit: number = 20) =>
    relayApi.get('/sync/history', { params: { limit } }),
  triggerCheck: () => relayApi.post('/sync/check'),
  triggerReconcile: () => relayApi.post('/sync/reconcile'),
  // ... other sync methods
};

export default relayApi;
```

### Step 2: Create pkdApi.ts

**Responsibilities**:
- System health checks
- Certificate search & export
- Direct LDAP queries
- Upload statistics & history (read-only views)

**File**: `frontend/src/services/pkdApi.ts`

```typescript
import axios from 'axios';

const pkdApi = axios.create({
  baseURL: '/api',  // PKD Management base
  timeout: 30000,
});

// Health APIs
export const healthApi = {
  check: () => pkdApi.get('/health'),
  checkDatabase: () => pkdApi.get('/health/database'),
  checkLdap: () => pkdApi.get('/health/ldap'),
};

// Certificate APIs
export const certificateApi = {
  search: (params: { country?: string; certType?: string; limit?: number }) =>
    pkdApi.get('/certificates/search', { params }),

  getCountries: () => pkdApi.get('/certificates/countries'),

  getDetail: (dn: string) =>
    pkdApi.get('/certificates/detail', { params: { dn } }),

  exportFile: (dn: string, format: 'DER' | 'PEM') =>
    pkdApi.get('/certificates/export/file', {
      params: { dn, format },
      responseType: 'blob',
    }),

  exportCountry: (country: string, format: 'DER' | 'PEM') =>
    pkdApi.get('/certificates/export/country', {
      params: { country, format },
      responseType: 'blob',
    }),
};

// Upload history & statistics (read-only)
export const uploadHistoryApi = {
  getHistory: (params: PageRequest) =>
    pkdApi.get('/upload/history', { params }),

  getDetail: (uploadId: string) =>
    pkdApi.get(`/upload/detail/${uploadId}`),

  getStatistics: () => pkdApi.get('/upload/statistics'),

  getCountryStatistics: (limit: number = 20) =>
    pkdApi.get('/upload/countries', { params: { limit } }),
};

export default pkdApi;
```

### Step 3: Update Imports in Components

**Critical Components with SSE**:

1. **FileUpload.tsx** (Line 18)
   ```typescript
   // OLD
   import { uploadApi, createProgressEventSource } from '@/services/api';

   // NEW
   import { uploadApi, createProgressEventSource } from '@/services/relayApi';
   ```

2. **UploadHistory.tsx**
   ```typescript
   // OLD
   import { uploadApi } from '@/services/api';

   // NEW
   import { uploadApi } from '@/services/relayApi';        // Write operations
   import { uploadHistoryApi } from '@/services/pkdApi';   // Read operations
   ```

3. **SyncDashboard.tsx**
   ```typescript
   // OLD
   import { syncServiceApi } from '@/services/api';

   // NEW
   import { syncApi } from '@/services/relayApi';
   ```

4. **CertificateSearch.tsx**
   ```typescript
   // OLD
   import { ldapApi } from '@/services/api';

   // NEW
   import { certificateApi } from '@/services/pkdApi';
   ```

### Step 4: Backward Compatibility Handling

**Approach**: Gradual migration with alias exports

**services/api.ts** (legacy file, kept for backward compatibility):
```typescript
// Phase 6 Transition: Re-export from new API modules
export { uploadApi, createProgressEventSource, syncApi } from './relayApi';
export { healthApi, certificateApi } from './pkdApi';
export { paApi } from './paApi';
export { monitoringServiceApi } from './monitoringApi';

// Deprecated warning in console (dev mode only)
if (import.meta.env.DEV) {
  console.warn('[DEPRECATED] Importing from @/services/api is deprecated. Use specific API modules instead.');
}
```

This allows existing imports to continue working while new code uses the new structure.

### Step 5: SSE Path Migration Strategy

**Challenge**: SSE path needs to change from `/api/progress` to `/api/relay/progress`

**Solution**: Feature flag with environment variable

```typescript
// relayApi.ts
const USE_RELAY_SSE = import.meta.env.VITE_USE_RELAY_SSE === 'true';

export const createProgressEventSource = (uploadId: string): EventSource => {
  const basePath = USE_RELAY_SSE ? '/api/relay/progress' : '/api/progress';
  return new EventSource(`${basePath}/stream/${uploadId}`);
};
```

**Deployment Strategy**:
1. Deploy frontend with `VITE_USE_RELAY_SSE=false` (default)
2. Test all functionality with old paths
3. Switch API Gateway to v2 (enables `/api/relay/*` routes)
4. Set `VITE_USE_RELAY_SSE=true`
5. Verify SSE still works with new paths

---

## Testing Strategy

### Unit Tests

**Test SSE Path Resolution**:
```typescript
describe('relayApi.createProgressEventSource', () => {
  it('should use /api/progress when flag is false', () => {
    const eventSource = createProgressEventSource('test-id');
    expect(eventSource.url).toContain('/api/progress/stream/test-id');
  });

  it('should use /api/relay/progress when flag is true', () => {
    // Set env var
    const eventSource = createProgressEventSource('test-id');
    expect(eventSource.url).toContain('/api/relay/progress/stream/test-id');
  });
});
```

### Integration Tests

**FileUpload SSE Flow**:
1. Upload LDIF file (AUTO mode)
2. Verify SSE connection established
3. Receive `PARSING_STARTED` event → UI shows "파싱 중"
4. Receive `PARSING_COMPLETED` event → UI shows "파싱 완료"
5. Receive `VALIDATION_STARTED` event → UI shows "검증 중"
6. Receive `LDAP_SAVING_COMPLETED` event → UI shows "완료"
7. Verify progress percentages calculated correctly

**Manual Mode 3-Stage Flow**:
1. Upload LDIF (MANUAL mode) → Stage 1 button enabled
2. Click "Stage 1: 파싱" → SSE receives `PARSING_*` events
3. Verify Stage 2 button enabled after parse complete
4. Click "Stage 2: 검증" → SSE receives `VALIDATION_*` + `DB_SAVING_*` events
5. Verify Stage 3 button enabled
6. Click "Stage 3: LDAP 업로드" → SSE receives `LDAP_SAVING_*` events

### E2E Tests

**Cross-Service API Flow**:
1. Upload file via `/api/relay/upload` (PKD Relay)
2. Check statistics via `/api/upload/statistics` (PKD Management - read-only)
3. Search certificates via `/api/certificates/search` (PKD Management)
4. Trigger sync via `/api/relay/sync/check` (PKD Relay)

---

## Migration Checklist

### Phase 6a: Preparation (Current)
- [x] Analyze current API usage
- [x] Document SSE handling requirements
- [x] Design new API structure
- [ ] Create `relayApi.ts` with SSE factory
- [ ] Create `pkdApi.ts` with certificate APIs
- [ ] Add backward compatibility exports in `api.ts`

### Phase 6b: Component Updates
- [ ] Update FileUpload.tsx imports
- [ ] Update UploadHistory.tsx imports
- [ ] Update SyncDashboard.tsx imports
- [ ] Update CertificateSearch.tsx imports
- [ ] Update Dashboard.tsx imports
- [ ] Update all other components

### Phase 6c: Testing
- [ ] Unit test new API modules
- [ ] Integration test SSE with AUTO mode
- [ ] Integration test SSE with MANUAL mode (3 stages)
- [ ] E2E test cross-service flows
- [ ] Test with API Gateway v1 (current)
- [ ] Test with API Gateway v2 (new relay paths)

### Phase 6d: Deployment
- [ ] Deploy frontend with `VITE_USE_RELAY_SSE=false`
- [ ] Verify production works with old paths
- [ ] Switch to API Gateway v2
- [ ] Enable `VITE_USE_RELAY_SSE=true`
- [ ] Monitor SSE connections
- [ ] Remove backward compatibility layer (future cleanup)

---

## Risk Assessment

### High Risk
- **SSE Connection Breakage**: If path changes incorrectly, users won't see upload progress
  - Mitigation: Feature flag + polling backup (already exists)
  - Fallback: EventSource.onerror triggers polling

### Medium Risk
- **Import Path Confusion**: Developers might not know which API to use
  - Mitigation: Clear JSDoc comments + TypeScript exports
  - Deprecation warnings in dev mode

### Low Risk
- **Performance**: Minimal impact (same endpoints, different modules)
- **Bundle Size**: Slight increase (~2KB for new modules)

---

## Success Criteria

### Functionality
- ✅ All existing features work identically
- ✅ SSE progress updates work in AUTO mode
- ✅ SSE progress updates work in MANUAL mode (3 stages)
- ✅ Certificate search/export works
- ✅ Sync monitoring works

### Code Quality
- ✅ Logical separation of relay vs management APIs
- ✅ Clear module responsibilities
- ✅ TypeScript type safety maintained
- ✅ No breaking changes for existing code

### Future-Proof
- ✅ Easy to migrate Upload/ICAO modules to relay service
- ✅ Easy to switch API Gateway versions
- ✅ Ready for service consolidation (if needed)

---

## Future Enhancements (Post-Phase 6)

### Phase 7: Backend Service Migration (Optional)
- Move Upload module from PKD Management to PKD Relay
- Move ICAO module from PKD Management to PKD Relay
- Frontend already ready (no changes needed!)

### Phase 8: Shared Library Approach (Optional)
- Extract common code (LdifProcessor, ProcessingStrategy) to shared lib
- Both services use shared library
- Reduces code duplication

---

**Status**: Ready to implement Phase 6a (Create API modules)
**Next Step**: Create `relayApi.ts` with SSE support
**Estimated Effort**: 2-3 hours
**Risk Level**: LOW (backward compatible)
