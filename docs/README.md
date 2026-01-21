# ICAO Local PKD Documentation

**Last Updated**: 2026-01-21
**Version**: 1.7.1

---

## 📚 Core Documentation

### Deployment & Operations
- **[DEPLOYMENT_PROCESS.md](DEPLOYMENT_PROCESS.md)** - Complete deployment pipeline guide
- **[DEPLOYMENT_READY.md](DEPLOYMENT_READY.md)** - Production readiness checklist
- **[DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md)** - ⚠️ Critical: Read before every deployment
- **[LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md)** - ARM64 deployment to Luckfox device
- **[FRONTEND_BUILD_GUIDE.md](FRONTEND_BUILD_GUIDE.md)** - Frontend build and deployment workflow

### API & Integration Guides
- **[PA_API_GUIDE.md](PA_API_GUIDE.md)** - Passive Authentication API for external clients
- **[API_GATEWAY_ARCHITECTURE.md](API_GATEWAY_ARCHITECTURE.md)** - Nginx API Gateway architecture
- **[LDAP_QUERY_GUIDE.md](LDAP_QUERY_GUIDE.md)** - LDAP query patterns and troubleshooting

---

## 🎯 Feature Documentation

### Certificate Search (v1.6.0+)
- **[CERTIFICATE_SEARCH_QUICKSTART.md](CERTIFICATE_SEARCH_QUICKSTART.md)** - Quick start guide for certificate search feature

### Auto Reconcile (v1.6.0+)
- **[AUTO_RECONCILE_DESIGN.md](AUTO_RECONCILE_DESIGN.md)** - DB-LDAP auto reconciliation design

### ICAO Auto Sync (v1.7.0+)
- **[ICAO_AUTO_SYNC_PHASE78_COMPLETE.md](ICAO_AUTO_SYNC_PHASE78_COMPLETE.md)** - Complete Phase 7-8 implementation
- **[ICAO_AUTO_SYNC_CRON_SETUP.md](ICAO_AUTO_SYNC_CRON_SETUP.md)** - Automated version checking setup (future)

---

## 📊 Planning & Analysis

- **[ICAO_PKD_COST_ANALYSIS.md](ICAO_PKD_COST_ANALYSIS.md)** - Cost analysis and optimization strategies

---

## 📁 Archive & Presentations

### Archive (`archive/`)
Contains historical planning documents and early implementation designs:
- Initial implementation plans
- Early refactoring proposals
- Build guides (superseded by DEPLOYMENT_PROCESS.md)
- Technical whitepaper drafts

### Presentations (`presentations/`)
Contains presentation materials and project proposals:
- ICAO_PKD_Presentation.md
- ICAO_PKD_Presentation_Mermaid.md
- ICAO_PKD_Proposal.md

---

## 🗂️ Directory Structure

```
docs/
├── README.md                                 (this file)
│
├── Core Documentation (8 files)
│   ├── Deployment & Operations (5)
│   │   ├── DEPLOYMENT_PROCESS.md
│   │   ├── DEPLOYMENT_READY.md
│   │   ├── DOCKER_BUILD_CACHE.md            ⚠️ Critical
│   │   ├── LUCKFOX_DEPLOYMENT.md
│   │   └── FRONTEND_BUILD_GUIDE.md
│   └── API & Integration (3)
│       ├── PA_API_GUIDE.md
│       ├── API_GATEWAY_ARCHITECTURE.md
│       └── LDAP_QUERY_GUIDE.md
│
├── Feature Documentation (5 files)
│   ├── CERTIFICATE_SEARCH_QUICKSTART.md
│   ├── AUTO_RECONCILE_DESIGN.md
│   ├── ICAO_AUTO_SYNC_PHASE78_COMPLETE.md
│   └── ICAO_AUTO_SYNC_CRON_SETUP.md
│
├── Planning & Analysis (1 file)
│   └── ICAO_PKD_COST_ANALYSIS.md
│
├── archive/ (7 files)
│   ├── ICAO_LOCAL_PKD_CPP_IMPLEMENTATION_PLAN.md
│   ├── ICAO_PKD_AUTO_SYNC_TIER1_PLAN.md
│   ├── PKD_MANAGEMENT_REFACTORING_PLAN.md
│   ├── SERVICE_SEPARATION_PLAN.md
│   ├── BUILD.md
│   ├── PA_V2_API_CLIENT_REQUEST.md
│   └── TECHNICAL_WHITEPAPER.md
│
├── presentations/ (3 files)
│   ├── ICAO_PKD_Presentation.md
│   ├── ICAO_PKD_Presentation_Mermaid.md
│   └── ICAO_PKD_Proposal.md
│
└── openapi/ (API Specifications)
    ├── pkd-management.yaml
    ├── pa-service.yaml
    └── sync-service.yaml
```

---

## 📝 Document Organization Notes

### Removed Documents (2026-01-21)
The following documents were removed as they are superseded by current documentation:

**ICAO Auto Sync** (9 files):
- Intermediate implementation stages consolidated into PHASE78_COMPLETE.md
- Bug fix records (UUID, Portal Changes) - issues resolved
- Testing documents - merged into PHASE78_COMPLETE.md

**Certificate Search** (3 files):
- Implementation and status documents consolidated into QUICKSTART.md
- Design document - feature completed

**Other** (2 files):
- AUTO_RECONCILE_IMPLEMENTATION.md - integrated into CLAUDE.md
- CERTIFICATE_VALIDATION_COMPARISON.md - early comparison document

**Total Removed**: 14 files (~200KB)

---

## 🔍 Quick Reference

### For Developers
- Start with: [DEPLOYMENT_PROCESS.md](DEPLOYMENT_PROCESS.md)
- Before deployment: [DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md) ⚠️
- Frontend changes: [FRONTEND_BUILD_GUIDE.md](FRONTEND_BUILD_GUIDE.md)

### For External Clients
- PA Service integration: [PA_API_GUIDE.md](PA_API_GUIDE.md)
- Certificate search: [CERTIFICATE_SEARCH_QUICKSTART.md](CERTIFICATE_SEARCH_QUICKSTART.md)

### For Operations
- Production deployment: [DEPLOYMENT_READY.md](DEPLOYMENT_READY.md)
- ARM64 deployment: [LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md)
- LDAP troubleshooting: [LDAP_QUERY_GUIDE.md](LDAP_QUERY_GUIDE.md)

---

## 📌 Main Project Documentation

For high-level project overview and change log, see the main project file:
- **[../CLAUDE.md](../CLAUDE.md)** - Project overview, architecture, and complete change log

---

**Document Maintenance**: This README is updated when major documentation changes occur.
**Last Cleanup**: 2026-01-21 (Removed 14 obsolete documents, organized into archive/presentations)
