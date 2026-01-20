# ICAO Auto Sync Integration Strategy Analysis

**Date**: 2026-01-19
**Author**: Development Team
**Version**: 1.0
**Status**: Analysis Complete

---

## Executive Summary

**Recommendation**: **Integrate into existing PKD Management Service** (:8081)

**Rationale**: The ICAO version checking feature is tightly coupled with the file upload workflow and shares the same database schema. Creating a new microservice would introduce unnecessary complexity, network overhead, and maintenance burden.

---

## 1. Current Service Architecture Analysis

### 1.1 Existing Services Overview

| Service | Port | Purpose | LoC | API Endpoints |
|---------|------|---------|-----|---------------|
| **pkd-management** | 8081 | File upload, parsing, certificate validation | 6,087 | 20+ |
| **pa-service** | 8082 | Passive Authentication verification | 3,554 | 10+ |
| **sync-service** | 8083 | DB-LDAP synchronization monitoring | 1,906 | 15 |
| **monitoring-service** | 8084 | System metrics & service health | 904 | 5+ |

### 1.2 API Gateway Routing (Nginx)

```nginx
/api/upload/*        → pkd-management:8081
/api/certificates/*  → pkd-management:8081
/api/health/*        → pkd-management:8081
/api/pa/*            → pa-service:8082
/api/sync/*          → sync-service:8083
/api/monitoring/*    → monitoring-service:8084
```

### 1.3 Service Responsibilities

**PKD Management** (:8081):
- ✅ LDIF/Master List file upload
- ✅ File parsing and validation
- ✅ Certificate storage (DB + LDAP)
- ✅ Upload history tracking
- ✅ Certificate search and export
- ✅ Trust chain validation

**PA Service** (:8082):
- Passive Authentication verification
- SOD/DG parsing
- Verification history

**Sync Service** (:8083):
- DB-LDAP synchronization monitoring
- Discrepancy detection
- Auto reconciliation
- Daily sync scheduler

**Monitoring Service** (:8084):
- System resource monitoring (CPU, memory, disk)
- Service health checks
- Log analysis

---

## 2. Integration Options Comparison

### Option 1: New Microservice (icao-sync-service :8085) ❌

**Pros**:
- ✅ Separation of concerns
- ✅ Independent scaling
- ✅ Isolated failure domain

**Cons**:
- ❌ **Tight coupling with upload workflow**: ICAO version detection triggers manual upload → creates circular dependency
- ❌ **Database schema overlap**: `icao_pkd_versions` table needs foreign key to `uploaded_file` (pkd-management)
- ❌ **Code duplication**: HTTP client, HTML parsing, database connection logic
- ❌ **Increased complexity**: Additional service to deploy, monitor, and maintain
- ❌ **Network overhead**: API calls between services for workflow coordination
- ❌ **Deployment burden**: 5th microservice (already have 4)
- ❌ **API Gateway update**: New routing rules (`/api/icao/*`)

**Architecture Impact**:
```
User checks for updates → icao-sync-service:8085 → DB
User downloads LDIF → Frontend → pkd-management:8081 → DB
                                                       ↓
                                    Update icao_pkd_versions (cross-service)
```

### Option 2: Integrate into PKD Management Service (:8081) ✅ **RECOMMENDED**

**Pros**:
- ✅ **Natural fit**: ICAO version check is part of the upload workflow lifecycle
- ✅ **Shared database schema**: Direct access to `uploaded_file` and `icao_pkd_versions` tables
- ✅ **Code reuse**: Existing DB connection pool, logging, error handling
- ✅ **No new deployment**: Leverage existing service infrastructure
- ✅ **Simplified API**: All upload-related APIs in one place (`/api/upload/*`, `/api/icao/*`)
- ✅ **Minimal routing change**: API Gateway already routes upload traffic to pkd-management
- ✅ **Atomic transactions**: Version detection + upload can be in same transaction
- ✅ **Easier testing**: Single service to test upload workflow end-to-end

**Cons**:
- ⚠️ Increases pkd-management complexity (6,087 → ~6,500 LoC)
- ⚠️ Requires rebuilding pkd-management service

**Architecture Impact**:
```
User checks for updates → pkd-management:8081 → DB → Email notification
                              ↓
User downloads LDIF → Frontend → pkd-management:8081 → DB (atomic update)
```

### Option 3: Integrate into Sync Service (:8083) ❌

**Pros**:
- ✅ Sync service already handles scheduled tasks (daily sync)
- ✅ Could reuse scheduler infrastructure

**Cons**:
- ❌ **Misaligned responsibility**: Sync service focuses on DB-LDAP synchronization, not external portal monitoring
- ❌ **Database coupling**: Still needs access to `uploaded_file` table (foreign service)
- ❌ **Confusing API**: `/api/sync/icao/*` doesn't make semantic sense
- ❌ **Workflow coordination**: Still needs to notify pkd-management for upload linkage

---

## 3. Decision Matrix

| Criteria | New Service | PKD Management | Sync Service | Weight |
|----------|-------------|----------------|--------------|--------|
| **Workflow Cohesion** | ❌ Low | ✅ High | ⚠️ Medium | 25% |
| **Code Reuse** | ❌ Low | ✅ High | ⚠️ Medium | 20% |
| **Deployment Complexity** | ❌ High | ✅ Low | ⚠️ Medium | 20% |
| **Database Coupling** | ❌ High | ✅ Low | ❌ High | 15% |
| **API Semantics** | ⚠️ Medium | ✅ High | ❌ Low | 10% |
| **Maintainability** | ❌ Low | ✅ High | ⚠️ Medium | 10% |
| **Weighted Score** | **28%** | **88%** | **48%** | **100%** |

**Winner**: **PKD Management Service** (88% score)

---

## 4. Recommended Implementation Strategy

### 4.1 Module Structure

Add new module to `services/pkd-management/`:

```
services/pkd-management/
├── src/
│   ├── main.cpp                      # Existing
│   ├── icao/                         # NEW MODULE
│   │   ├── version_checker.h
│   │   ├── version_checker.cpp
│   │   ├── html_parser.h
│   │   ├── html_parser.cpp
│   │   ├── notification_sender.h
│   │   └── notification_sender.cpp
│   ├── ldif/                         # Existing
│   ├── reconciliation/               # Existing
│   └── common/                       # Existing
```

### 4.2 API Endpoints (New)

Add to pkd-management:

```cpp
GET  /api/icao/check-updates     // Manual trigger + cron job
GET  /api/icao/latest             // Get latest detected versions
GET  /api/icao/history?limit=10   // Version detection history
```

### 4.3 Database Schema

```sql
-- Already planned in Tier 1 plan
CREATE TABLE icao_pkd_versions (
    id SERIAL PRIMARY KEY,
    collection_type VARCHAR(50) NOT NULL,
    file_name VARCHAR(255) NOT NULL UNIQUE,
    file_version INTEGER NOT NULL,
    detected_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    downloaded_at TIMESTAMP,
    imported_at TIMESTAMP,
    status VARCHAR(50) NOT NULL DEFAULT 'DETECTED',
    notification_sent BOOLEAN DEFAULT FALSE,
    notification_sent_at TIMESTAMP,
    import_upload_id INTEGER REFERENCES uploaded_file(id),  -- ✅ Same service
    certificate_count INTEGER,
    error_message TEXT
);

-- Link back to uploads (pkd-management owns both tables)
ALTER TABLE uploaded_file
ADD COLUMN icao_version_id INTEGER REFERENCES icao_pkd_versions(id),
ADD COLUMN is_icao_official BOOLEAN DEFAULT FALSE;
```

### 4.4 Workflow Integration

**Existing Upload Flow** (unchanged):
```
POST /api/upload/ldif → Parse → Validate → DB + LDAP → History
```

**New Version Check Flow** (integrated):
```
Cron job → GET /api/icao/check-updates → Detect new version → DB + Email
                                                                  ↓
Admin downloads LDIF → POST /api/upload/ldif → Auto-link icao_version_id
```

**Frontend Dashboard**:
- Add "ICAO Status" card to existing Dashboard page
- Show latest detected versions
- "Check for Updates" button
- Upload History page shows ICAO version badge

### 4.5 Code Organization

**Separation Strategy**:
- Create `src/icao/` subdirectory for ICAO-specific logic
- Keep main.cpp clean by using forward declarations
- Register API handlers in main.cpp
- Delegate implementation to IcaoVersionChecker class

**Example**:
```cpp
// main.cpp (lines 6000-6100)
#include "icao/version_checker.h"

std::unique_ptr<icao::VersionChecker> icaoVersionChecker;

// Initialize in main()
icaoVersionChecker = std::make_unique<icao::VersionChecker>(conninfo);

// Register endpoints
app.registerHandler("/api/icao/check-updates", [](req, callback) {
    auto result = icaoVersionChecker->checkForUpdates();
    callback(HttpResponse::newHttpJsonResponse(result));
}, {Get});
```

---

## 5. Alternative Rejected: Monitoring Service

**Why not monitoring-service?**
- ❌ Monitoring service is for **passive observation** (metrics, health checks)
- ❌ ICAO sync is an **active workflow** (triggers uploads, modifies data)
- ❌ Semantic mismatch: Monitoring shouldn't trigger business logic

---

## 6. Risk Assessment

### Integration Risks

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| pkd-management becomes too large | Medium | Low | Modularize with `src/icao/` subdirectory |
| Build time increases | Low | Low | ICAO module only adds ~500 LoC + libcurl |
| Deployment complexity | Low | Low | Same Docker build process |
| Testing burden | Medium | Medium | Add unit tests for IcaoVersionChecker |

### Operational Risks

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| ICAO portal HTML changes | High | Medium | Graceful failure, log error, continue service |
| Network connectivity issues | Medium | Low | Retry logic, exponential backoff |
| Email notification failures | Low | Medium | Log to database, manual check via API |

---

## 7. Implementation Checklist

### Phase 1: Database (Day 1)
- [ ] Create `icao_pkd_versions` table
- [ ] Alter `uploaded_file` table (add icao_version_id)
- [ ] Write migration scripts (up/down)

### Phase 2: Backend (Days 2-4)
- [ ] Create `src/icao/` module directory
- [ ] Implement `IcaoVersionChecker` class
- [ ] Implement HTML parser (regex-based)
- [ ] Add HTTP client (libcurl or Drogon HttpClient)
- [ ] Implement notification sender (email SMTP)
- [ ] Register API endpoints in main.cpp
- [ ] Add unit tests

### Phase 3: Frontend (Day 5)
- [ ] Add ICAO Status card to Dashboard
- [ ] Add "Check for Updates" button
- [ ] Add version history display
- [ ] Link Upload History to ICAO versions

### Phase 4: Cron Job (Day 6)
- [ ] Create `scripts/icao-version-check.sh`
- [ ] Configure crontab (daily 8am)
- [ ] Test manual execution

### Phase 5: Testing & Deployment (Days 7-8)
- [ ] Unit tests (HTML parsing, version comparison)
- [ ] Integration tests (API endpoints)
- [ ] Docker build test
- [ ] Deploy to staging
- [ ] Deploy to production

---

## 8. Performance Impact Analysis

### Build Time Impact
- **Current**: pkd-management build time ~10-15 minutes (with cache)
- **After**: +30 seconds (libcurl already in vcpkg, minimal new code)
- **Verdict**: Negligible

### Runtime Impact
- **Memory**: +5MB (libcurl, HTML parser)
- **CPU**: Negligible (version check runs once per day)
- **Network**: 1 HTTPS request per day to ICAO portal
- **Verdict**: Negligible

### Database Impact
- **New table**: `icao_pkd_versions` (~100 rows/year)
- **Queries**: +3 queries per version check (SELECT, INSERT, UPDATE)
- **Storage**: <1KB per version record
- **Verdict**: Negligible

---

## 9. Long-term Maintenance Considerations

### Code Maintainability
- ✅ All upload-related code in one service (easier to understand)
- ✅ Fewer deployment artifacts (4 services instead of 5)
- ✅ Fewer Docker images to manage
- ✅ Fewer GitHub Actions workflows

### Operational Overhead
- ✅ Fewer health checks to monitor
- ✅ Fewer log files to aggregate
- ✅ Fewer service dependencies to coordinate

### Scalability
- ⚠️ ICAO version check is not CPU/memory intensive (once per day)
- ✅ If needed, can extract to separate service later (refactor-friendly)

---

## 10. Final Recommendation

**Decision**: **Integrate ICAO Auto Sync into PKD Management Service**

**Justification**:
1. **Workflow Cohesion**: ICAO version detection → manual download → upload is a single logical workflow
2. **Database Coupling**: Both `icao_pkd_versions` and `uploaded_file` tables are tightly related
3. **Code Reuse**: Leverage existing DB connection, logging, error handling infrastructure
4. **Deployment Simplicity**: No new service to deploy, monitor, or maintain
5. **API Semantics**: `/api/icao/*` naturally belongs with `/api/upload/*` in the same service
6. **Low Risk**: Modular design (`src/icao/`) keeps code organized and testable

**Next Steps**:
1. Review and approve this analysis document
2. Proceed with Phase 1 (Database Migration)
3. Update CLAUDE.md with new module structure
4. Begin implementation following Tier 1 plan

---

## Appendix A: Service Responsibility Matrix (After Integration)

| Responsibility | pkd-management | pa-service | sync-service | monitoring-service |
|----------------|----------------|------------|--------------|-------------------|
| File Upload | ✅ | | | |
| Certificate Validation | ✅ | | | |
| Certificate Search | ✅ | | | |
| **ICAO Version Check** | ✅ NEW | | | |
| PA Verification | | ✅ | | |
| DB-LDAP Sync | | | ✅ | |
| System Monitoring | | | | ✅ |

---

## Appendix B: API Endpoints After Integration

### pkd-management (:8081)
```
# Existing
POST   /api/upload/ldif
POST   /api/upload/masterlist
GET    /api/upload/history
GET    /api/upload/statistics
GET    /api/certificates/search
GET    /api/health

# NEW (ICAO Auto Sync)
GET    /api/icao/check-updates
GET    /api/icao/latest
GET    /api/icao/history?limit=10
```

### pa-service (:8082)
```
POST   /api/pa/verify
GET    /api/pa/statistics
GET    /api/pa/history
```

### sync-service (:8083)
```
GET    /api/sync/status
GET    /api/sync/reconcile/history
POST   /api/sync/trigger-daily
```

### monitoring-service (:8084)
```
GET    /api/monitoring/metrics
GET    /api/monitoring/services
GET    /api/monitoring/logs
```

---

**Document Status**: Ready for Implementation
**Approved By**: _______________
**Date**: _______________
