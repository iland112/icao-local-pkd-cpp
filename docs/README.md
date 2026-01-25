# ICAO Local PKD - Documentation

**Version**: v2.1.0
**Last Updated**: 2026-01-26

---

## 📖 Essential Documents (Start Here)

### Development & Operations
- **[DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md)** ⭐ - **START HERE**
  - Credentials, helper scripts, daily workflow
  - Build process, testing, troubleshooting
  - All essential information in one place

- **[DEPLOYMENT_PROCESS.md](DEPLOYMENT_PROCESS.md)** - CI/CD pipeline
  - GitHub Actions workflow
  - Artifact management
  - Image name mapping

- **[LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md)** - ARM64 deployment
  - Automated deployment script
  - Luckfox-specific configuration
  - Troubleshooting

- **[DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md)** - Build troubleshooting
  - Cache issues and solutions
  - Version mismatch prevention
  - Build verification

- **[FRONTEND_BUILD_GUIDE.md](FRONTEND_BUILD_GUIDE.md)** - Frontend workflow
  - Build automation
  - Common pitfalls
  - Verification steps

### API & Integration
- **[PA_API_GUIDE.md](PA_API_GUIDE.md)** - Passive Authentication API
  - External client guide
  - Request/response formats
  - Error handling

- **[LDAP_QUERY_GUIDE.md](LDAP_QUERY_GUIDE.md)** - LDAP operations
  - Correct connection parameters
  - Search patterns
  - Troubleshooting

### Architecture
- **[SOFTWARE_ARCHITECTURE.md](SOFTWARE_ARCHITECTURE.md)** - System design
  - Service layer overview
  - Database schema
  - LDAP DIT structure

- **[PKD_RELAY_SERVICE_REFACTORING_STATUS.md](PKD_RELAY_SERVICE_REFACTORING_STATUS.md)** - Service separation (v2.0.0)
  - Clean architecture
  - Service responsibilities
  - API endpoints

---

## 📋 Feature Documentation

### Security (v1.8.0 - v2.0.0)
- **[PHASE1_SECURITY_IMPLEMENTATION.md](PHASE1_SECURITY_IMPLEMENTATION.md)**
  - Credential externalization
  - SQL injection fixes (21 queries)
  - File upload security

- **[PHASE2_SECURITY_IMPLEMENTATION.md](PHASE2_SECURITY_IMPLEMENTATION.md)**
  - Complete SQL hardening (7 queries)
  - 100% parameterized queries

- **[SECURITY_HARDENING_STATUS.md](SECURITY_HARDENING_STATUS.md)**
  - Overall security status
  - Threat model
  - Mitigation strategies

### Features
- **[AUTO_RECONCILE_DESIGN.md](AUTO_RECONCILE_DESIGN.md)** - Auto reconciliation (v1.6.0)
- **[CERTIFICATE_SEARCH_QUICKSTART.md](CERTIFICATE_SEARCH_QUICKSTART.md)** - Certificate search (v1.6.0)

### Reference
- **[PKD_RELAY_SERVICE_REFACTORING.md](PKD_RELAY_SERVICE_REFACTORING.md)** - Detailed refactoring guide
- **[ICAO_PKD_COST_ANALYSIS.md](ICAO_PKD_COST_ANALYSIS.md)** - Cost analysis

---

## 🗄️ Archived Documents

**Location**: [archive/](archive/)

Historical implementation documents (50 files):
- Sprint planning and completion summaries (Sprint 1-3)
  - Sprint 1: LDAP DN standardization (3 files)
  - Sprint 2: Link certificate validation core (2 files)
  - Sprint 3: Trust chain integration (6 files)
- Phase completion documents (Collection 002, PKD Relay phases 6-7)
- ICAO Auto Sync implementation (Phase 7-8)
- Phase 3-4 Security planning (Authentication, LDAP injection, Rate limiting)
- CSCA issues and validation enhancements
- Master List upload verification
- Data processing rules
- Old architecture documents

**Note**: Archive documents are for historical reference only. Refer to current documentation for active features.

---

## 📊 Document Organization

```
docs/
├── README.md                                   # This file
│
├── Essential (8 files) ⭐
│   ├── DEVELOPMENT_GUIDE.md                    # START HERE
│   ├── DEPLOYMENT_PROCESS.md
│   ├── LUCKFOX_DEPLOYMENT.md
│   ├── DOCKER_BUILD_CACHE.md
│   ├── FRONTEND_BUILD_GUIDE.md
│   ├── PA_API_GUIDE.md
│   ├── LDAP_QUERY_GUIDE.md
│   └── SOFTWARE_ARCHITECTURE.md
│
├── Security (3 files)
│   ├── PHASE1_SECURITY_IMPLEMENTATION.md
│   ├── PHASE2_SECURITY_IMPLEMENTATION.md
│   └── SECURITY_HARDENING_STATUS.md
│
├── Features (2 files)
│   ├── AUTO_RECONCILE_DESIGN.md
│   └── CERTIFICATE_SEARCH_QUICKSTART.md
│
├── Reference (3 files)
│   ├── PKD_RELAY_SERVICE_REFACTORING.md
│   ├── PKD_RELAY_SERVICE_REFACTORING_STATUS.md
│   └── ICAO_PKD_COST_ANALYSIS.md
│
├── Sprint Summaries (1 file)
│   └── SPRINT3_COMPLETION_SUMMARY.md
│
├── archive/ (50 files)
│   ├── SPRINT1_*.md (3 files)
│   ├── SPRINT2_*.md (2 files)
│   ├── SPRINT3_*.md (6 files)
│   ├── COLLECTION_002_*.md (4 files)
│   ├── PKD_RELAY_SERVICE_*.md (4 files)
│   ├── ICAO_AUTO_SYNC_*.md (2 files)
│   ├── PHASE3_AUTHENTICATION_PLAN.md
│   ├── PHASE4*.md (6 files)
│   ├── CSCA_*.md (2 files)
│   ├── VALIDATION_DETAIL_ENHANCEMENT_PLAN.md
│   ├── INTEGRATED_IMPLEMENTATION_ROADMAP.md
│   └── ... (14 more files)
│
├── Sprint Summaries (1 file)
│   └── SPRINT3_COMPLETION_SUMMARY.md           # v2.1.0 release
│
└── openapi/ (API Specifications)
    ├── pkd-management.yaml
    ├── pa-service.yaml
    └── pkd-relay.yaml
```

---

## 🔍 Quick Reference

### Common Tasks
- **Start development**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#daily-commands)
- **Build service**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#development-workflow)
- **Deploy to Luckfox**: [LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md#automated-deployment-recommended)
- **Troubleshoot build**: [DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md#troubleshooting)
- **Use PA API**: [PA_API_GUIDE.md](PA_API_GUIDE.md#quick-start)

### Credentials & Configuration
- **Database**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#credentials-do-not-commit)
- **LDAP**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#credentials-do-not-commit)
- **Helper scripts**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#helper-scripts)

### Architecture & Design
- **Service separation**: [PKD_RELAY_SERVICE_REFACTORING_STATUS.md](PKD_RELAY_SERVICE_REFACTORING_STATUS.md)
- **Security design**: [SECURITY_HARDENING_STATUS.md](SECURITY_HARDENING_STATUS.md)
- **System overview**: [SOFTWARE_ARCHITECTURE.md](SOFTWARE_ARCHITECTURE.md)

---

## 📝 Document Lifecycle

### Active Documents (17 files)
Current features, guides, API documentation, troubleshooting

### Archived Documents (44 files)
- **When to archive**: Feature completed, superseded, or historical only
- **Location**: `docs/archive/`
- **Purpose**: Historical reference, implementation notes

### Maintenance Schedule
- **Monthly**: Review active docs for updates
- **Quarterly**: Archive completed implementation docs
- **Yearly**: Archive old sprint/phase documents

---

## 📌 Related Documentation

- **[../CLAUDE.md](../CLAUDE.md)** - Quick reference guide (v2.1.0)
  - Quick start, architecture, API endpoints
  - Helper scripts, common issues
  - Version history (concise)

- **[SPRINT3_COMPLETION_SUMMARY.md](SPRINT3_COMPLETION_SUMMARY.md)** - Sprint 3 release notes
  - Trust chain validation implementation
  - CSCA cache optimization (80% faster)
  - Frontend visualization component
  - Performance metrics and testing

- **[../scripts/](../scripts/)** - Helper scripts
  - `rebuild-pkd-relay.sh` - Build automation
  - `ldap-helpers.sh` - LDAP operations
  - `db-helpers.sh` - Database operations

---

## Version History

### v2.1.0 (2026-01-26)

- **Sprint 3 Complete**: Link certificate validation integration
- **Added**: SPRINT3_COMPLETION_SUMMARY.md (comprehensive release notes)
- **Updated**: CLAUDE.md with v2.1.0 features
- **Archive**: 50 historical documents (Sprint 3 tasks)

### v2.0.5 (2026-01-25)

- **Major cleanup**: 61 → 17 active docs (72% reduction)
- **Added**: DEVELOPMENT_GUIDE.md (consolidated development info)
- **Archived**: 44 historical documents
- **Reorganized**: Clear structure with archive/

### v2.0.0 (2026-01-21)

- Service separation documentation
- PKD Relay Service refactoring guides

### v1.8.0 - v1.9.0

- Security hardening documentation
- SQL injection prevention guides

---

**For questions or issues, start with [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md).**
