# ICAO Local PKD - Development Guide

**Current Version**: v2.1.0
**Last Updated**: 2026-01-26
**Status**: Production Ready

---

## Quick Start

### Essential Information

**Services**: PKD Management (:8081), PA Service (:8082), PKD Relay (:8083)
**API Gateway**: http://localhost:8080/api
**Frontend**: http://localhost:3000

**Technology Stack**: C++20, Drogon, PostgreSQL 15, OpenLDAP, React 19

### Daily Commands

```bash
# Start system
./docker-start.sh

# Rebuild service
./scripts/rebuild-pkd-relay.sh [--no-cache]

# Helper functions
source scripts/ldap-helpers.sh && ldap_count_all
source scripts/db-helpers.sh && db_count_crls

# Health check
./docker-health.sh
```

**Complete Guide**: See [docs/DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md)

---

## Architecture

### Service Layer

```
Frontend (React) → API Gateway (Nginx) → 3 Backend Services → DB/LDAP
```

**PKD Management**: Upload, Certificate Search, ICAO Sync
**PA Service**: Passive Authentication verification
**PKD Relay**: DB-LDAP Sync, Auto Reconciliation

### LDAP Structure

```
dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
├── dc=data
│   └── c={COUNTRY}
│       ├── o=csca (CSCA certificates)
│       ├── o=dsc  (DSC certificates)
│       ├── o=crl  (CRLs)
│       └── o=ml   (Master Lists)
└── dc=nc-data
    └── c={COUNTRY}
        └── o=dsc  (Non-conformant DSC)
```

---

## Current Features (v2.1.0)

### Core Functionality
- ✅ LDIF/Master List upload (AUTO/MANUAL modes)
- ✅ Certificate validation (Trust Chain, CRL, Link Certificates)
- ✅ LDAP integration (MMR cluster, Software LB)
- ✅ Passive Authentication (ICAO 9303)
- ✅ DB-LDAP sync monitoring
- ✅ Auto reconciliation (CSCA/DSC/CRL)
- ✅ Certificate search & export
- ✅ ICAO PKD version monitoring
- ✅ Trust chain visualization (frontend)
- ✅ Link certificate validation (Sprint 3)

### Security (v1.8.0 - v2.0.0)
- ✅ 100% Parameterized SQL queries (28 queries total)
- ✅ Credential externalization (.env)
- ✅ File upload validation (MIME, path sanitization)
- ✅ JWT authentication + RBAC
- ✅ Audit logging (IP tracking)

### Recent Changes (v2.1.0)
- ✅ Link certificate validation (Sprint 3)
- ✅ Trust chain building and visualization
- ✅ CSCA in-memory cache (80% performance improvement)
- ✅ Validation result APIs with trust chain path
- ✅ Frontend trust chain visualization component

---

## API Endpoints

### PKD Management (via :8080/api)

- `POST /upload/ldif` - Upload LDIF file
- `POST /upload/masterlist` - Upload Master List
- `GET /upload/history` - Upload history
- `GET /upload/{uploadId}/validations` - Validation results with trust chain
- `GET /certificates/search` - Search certificates
- `GET /certificates/validation?fingerprint={sha256}` - Certificate validation result
- `GET /certificates/export/country` - Export by country

### ICAO Auto Sync
- `POST /icao/check-updates` - Manual version check
- `GET /icao/status` - Version comparison
- `GET /icao/latest` - Latest versions
- `GET /icao/history` - Detection history

### PA Service (via :8080/api/pa)
- `POST /verify` - PA verification
- `POST /parse-sod` - Parse SOD
- `POST /parse-dg1` - Parse DG1 (MRZ)
- `POST /parse-dg2` - Parse DG2 (Face)

### PKD Relay (via :8080/api/sync)
- `GET /status` - Full sync status
- `GET /stats` - Statistics
- `POST /reconcile` - Trigger reconciliation
- `GET /reconcile/history` - Reconciliation history

---

## Development Workflow

### 1. Code Changes

```bash
# Edit source
vim services/pkd-relay-service/src/relay/sync/reconciliation_engine.cpp

# Update version (for cache busting)
vim services/pkd-relay-service/src/main.cpp
# Change: spdlog::info("... v2.0.X ...")
```

### 2. Build & Deploy

```bash
# Quick rebuild (uses cache)
./scripts/rebuild-pkd-relay.sh

# Force rebuild (no cache)
./scripts/rebuild-pkd-relay.sh --no-cache
```

### 3. Testing

```bash
# Load helpers
source scripts/ldap-helpers.sh
source scripts/db-helpers.sh

# Prepare test data
db_reset_crl_flags

# Test reconciliation
curl -X POST http://localhost:8080/api/sync/reconcile \
  -H "Content-Type: application/json" \
  -d '{"dryRun": false}' | jq .

# Verify results
ldap_count_all
db_latest_reconciliation_logs
```

---

## Credentials (DO NOT COMMIT)

**PostgreSQL**:
- Host: postgres:5432
- Database: localpkd
- User: pkd
- Password: (from .env)

**LDAP**:
- Host: openldap1:389, openldap2:389
- Admin DN: cn=admin,dc=ldap,dc=smartcoreinc,dc=com
- Password: ldap_test_password_123
- Base DN: dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com

---

## Helper Scripts

### rebuild-pkd-relay.sh
Rebuild and deploy PKD Relay service with optional --no-cache

### ldap-helpers.sh
```bash
source scripts/ldap-helpers.sh

ldap_info                  # Show connection info
ldap_count_all             # Count all certificates
ldap_count_certs CRL       # Count CRLs
ldap_search_country KR     # Search by country
ldap_delete_all_crls       # Delete all CRLs (testing)
```

### db-helpers.sh
```bash
source scripts/db-helpers.sh

db_info                          # Show connection info
db_count_certs                   # Count certificates
db_count_crls                    # Count CRLs
db_reset_crl_flags               # Reset CRL flags
db_reconciliation_summary 10     # Last 10 reconciliations
db_latest_reconciliation_logs    # Latest logs
db_sync_status 10                # Sync history
```

---

## Common Issues & Solutions

### Build version mismatch
**Problem**: Binary version doesn't match source
**Solution**: `./scripts/rebuild-pkd-relay.sh --no-cache`

### LDAP authentication failed
**Problem**: `ldap_bind: Invalid credentials (49)`
**Solution**: Use `ldap_test_password_123` (NOT "admin")

### Reconciliation logs missing
**Problem**: reconciliation_log table has no entries
**Solution**: Check table has `cert_fingerprint VARCHAR(64)` (NOT `cert_id INTEGER`)

### CRLs not syncing
**Problem**: DB shows stored_in_ldap=TRUE but LDAP has 0 CRLs
**Solution**: `db_reset_crl_flags` then trigger reconciliation

---

## Documentation

- **[DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md)** - Complete development guide (credentials, commands, troubleshooting)
- **[LUCKFOX_DEPLOYMENT.md](docs/LUCKFOX_DEPLOYMENT.md)** - ARM64 deployment guide
- **[DOCKER_BUILD_CACHE.md](docs/DOCKER_BUILD_CACHE.md)** - Build cache troubleshooting
- **[PA_API_GUIDE.md](docs/PA_API_GUIDE.md)** - PA Service API guide

---

## Version History

### v2.1.0 (2026-01-26) - Sprint 3 Complete

**Sprint 3: Link Certificate Validation Integration**

- ✅ **Trust Chain Building** (Phase 1)
  - Recursive trust chain construction with link certificate support
  - Multi-level chain validation (DSC → Link Cert → Link Cert → Root CSCA)
  - Real-world examples: Latvia (3-level), Philippines (3-level), Luxembourg (org change)

- ✅ **Master List Link Certificate Validation** (Phase 2, Task 3.3)
  - Updated Master List processing to detect and validate link certificates
  - 536 certificates: 476 self-signed CSCAs (88.8%) + 60 link certificates (11.2%)
  - All stored as certificate_type='CSCA' with proper validation

- ✅ **CSCA Cache Performance Optimization** (Phase 2, Task 3.4)
  - In-memory cache for 536 certificates across 215 unique Subject DNs
  - 80% performance improvement (50ms → 10ms per DSC validation)
  - 5x faster bulk processing (25min → 5min for 30,000 DSCs)
  - 99.99% reduction in PostgreSQL load (30,000 queries → ~1 query)

- ✅ **Validation Result APIs** (Phase 3, Task 3.5)
  - `GET /api/upload/{uploadId}/validations` - Paginated validation results
  - `GET /api/certificates/validation?fingerprint={sha256}` - Single cert validation
  - Trust chain path included in response (e.g., "DSC → Link → Root")

- ✅ **Frontend Trust Chain Visualization** (Phase 3, Task 3.6)
  - Reusable TrustChainVisualization component (compact + full modes)
  - ValidationDemo page with 7 sample scenarios
  - Integration with Certificate Search and Upload Detail pages
  - Dark mode support and responsive design

**Sprint 3 Documentation**:

- `docs/archive/SPRINT3_PHASE1_COMPLETION.md` - Trust chain building
- `docs/archive/SPRINT3_TASK33_COMPLETION.md` - Master List link cert validation
- `docs/archive/SPRINT3_TASK34_COMPLETION.md` - CSCA cache optimization
- `docs/archive/SPRINT3_TASK35_COMPLETION.md` - Validation result APIs
- `docs/archive/SPRINT3_TASK36_COMPLETION.md` - Frontend visualization

### v2.0.6 (2026-01-25)

- **DSC_NC excluded from reconciliation** - ICAO deprecated nc-data in 2021
- ICAO standards compliance: nc-data is legacy only (pre-2021 uploads)
- PA Service verification: Does not use DSC_NC (DSC extracted from SOD)
- Reconciliation scope: CSCA, DSC, CRL only

### v2.0.5 (2026-01-25)
- CRL reconciliation support (findMissingCrlsInLdap, processCrls, addCrl)
- reconciliation_log UUID fix (cert_id INTEGER → cert_fingerprint VARCHAR)
- Development helper scripts (rebuild-pkd-relay.sh, ldap-helpers.sh, db-helpers.sh)

### v2.0.4 (2026-01-25)
- Auto parent DN creation in LDAP

### v2.0.3 (2026-01-24)
- Fingerprint-based DN format

### v2.0.0 (2026-01-21)
- Service separation (PKD Relay Service)
- Frontend sidebar reorganization

### v1.8.0 - v1.9.0 (Security Hardening)
- 100% Parameterized queries
- Credential externalization
- File upload security

---

## Key Architectural Decisions

### Database Schema
- UUIDs for primary keys (certificate.id, crl.id, uploaded_file.id)
- Fingerprint-based LDAP DNs (SHA-256 hex)
- Separate tables: certificate, crl, master_list
- Audit tables: reconciliation_summary, reconciliation_log, sync_status

### LDAP Strategy
- Read: Software Load Balancing (openldap1:389, openldap2:389)
- Write: Direct to primary (openldap1:389)
- DN format: `cn={FINGERPRINT},o={TYPE},c={COUNTRY},dc=data,...`
- Object classes: pkdDownload (certs), cRLDistributionPoint (CRLs)

### Reconciliation Logic
1. Find missing entities (stored_in_ldap=FALSE)
2. Verify against LDAP (actual existence check)
3. Add to LDAP with parent DN auto-creation
4. Mark as stored (stored_in_ldap=TRUE)
5. Log operations (reconciliation_log with fingerprint)

---

## Contact

For detailed information, see [docs/DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md)
