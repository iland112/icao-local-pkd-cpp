# ICAO Local PKD - Documentation

**Version**: v2.9.0
**Last Updated**: 2026-02-13

---

## Essential Documents (Start Here)

### Development & Operations
- **[DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md)** - **START HERE**
  - Credentials (PostgreSQL, Oracle, LDAP)
  - Helper scripts, daily workflow
  - Multi-DBMS support (PostgreSQL + Oracle)
  - Build process, testing, troubleshooting

- **[DEPLOYMENT_PROCESS.md](DEPLOYMENT_PROCESS.md)** - CI/CD Pipeline
  - GitHub Actions ARM64 build workflow
  - Artifact management & deployment script
  - Build times and cache strategy

- **[LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md)** - ARM64 Deployment
  - Luckfox-specific network and service configuration
  - `network_mode: host` architecture
  - Troubleshooting

- **[BUILD_SOP.md](BUILD_SOP.md)** - Build Verification Procedures
  - Docker cache management
  - Build verification checklist
  - Common cache-related issues

### API & Integration
- **[PA_API_GUIDE.md](PA_API_GUIDE.md)** - Passive Authentication API (v2.1.1)
  - 12 endpoints documentation
  - 8-step verification process
  - DSC auto-registration, DG2 face image extraction

- **[LDAP_QUERY_GUIDE.md](LDAP_QUERY_GUIDE.md)** - LDAP Operations
  - Correct connection parameters
  - Search patterns and filters
  - Authentication troubleshooting

- **[CERTIFICATE_SEARCH_QUICKSTART.md](CERTIFICATE_SEARCH_QUICKSTART.md)** - Certificate Search
  - Search filters (country, type, status, source)
  - Export functionality

### Architecture & Design
- **[SOFTWARE_ARCHITECTURE.md](SOFTWARE_ARCHITECTURE.md)** - System Design
  - 4-service microarchitecture overview
  - Database schema, LDAP DIT structure
  - Design patterns

- **[ARCHITECTURE_DESIGN_PRINCIPLES.md](ARCHITECTURE_DESIGN_PRINCIPLES.md)** - Design Principles
  - DDD, Microservices, Strategy Pattern, SRP
  - Foundational design decisions

### Reference
- **[MASTER_LIST_PROCESSING_GUIDE.md](MASTER_LIST_PROCESSING_GUIDE.md)** - Master List Format & Processing
- **[DVL_ANALYSIS.md](DVL_ANALYSIS.md)** - Deviation List Format Analysis
- **[ICAO_PKD_COST_ANALYSIS.md](ICAO_PKD_COST_ANALYSIS.md)** - ICAO PKD Cost Analysis
- **[DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md)** - Build Cache Troubleshooting
- **[FRONTEND_BUILD_GUIDE.md](FRONTEND_BUILD_GUIDE.md)** - Frontend Build Workflow

---

## API Specifications

**Location**: [openapi/](openapi/)

| Spec | Service | Version |
|------|---------|---------|
| [pkd-management.yaml](openapi/pkd-management.yaml) | PKD Management | v2.9.1 |
| [pa-service.yaml](openapi/pa-service.yaml) | PA Service | v2.1.1 |
| [pkd-relay.yaml](openapi/pkd-relay.yaml) | PKD Relay | v2.0.0 |

**Swagger UI**: http://localhost:8090 (or http://localhost:8080/api-docs/)

---

## Archived Documents

**Location**: [archive/](archive/)

100+ historical implementation documents including:
- Sprint planning and completion summaries (Sprint 1-3)
- Phase completion reports (Security, Oracle migration, Repository pattern)
- Feature design and implementation notes
- Fix reports and troubleshooting records
- Refactoring plans and progress tracking

These documents are for historical reference only. All current information is captured in the active reference docs above and in [CLAUDE.md](../CLAUDE.md).

---

## Document Organization

```
docs/
+-- README.md                              # This file (index)
+-- DEVELOPMENT_GUIDE.md                   # Primary dev reference
+-- DEPLOYMENT_PROCESS.md                  # CI/CD pipeline
+-- LUCKFOX_DEPLOYMENT.md                  # ARM64 deployment
+-- BUILD_SOP.md                           # Build verification
+-- PA_API_GUIDE.md                        # PA Service API guide
+-- LDAP_QUERY_GUIDE.md                    # LDAP operations guide
+-- CERTIFICATE_SEARCH_QUICKSTART.md       # Certificate search guide
+-- SOFTWARE_ARCHITECTURE.md               # System architecture
+-- ARCHITECTURE_DESIGN_PRINCIPLES.md      # Design principles
+-- MASTER_LIST_PROCESSING_GUIDE.md        # ML format reference
+-- DVL_ANALYSIS.md                        # Deviation List reference
+-- ICAO_PKD_COST_ANALYSIS.md             # Cost analysis
+-- DOCKER_BUILD_CACHE.md                  # Build cache troubleshooting
+-- FRONTEND_BUILD_GUIDE.md                # Frontend build guide
+-- openapi/                               # OpenAPI specifications
|   +-- pkd-management.yaml
|   +-- pa-service.yaml
|   +-- pkd-relay.yaml
+-- archive/                               # 100+ historical documents
```

---

## Quick Reference

### Common Tasks
- **Start development**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#daily-commands)
- **Build a service**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#2-build-and-deploy)
- **Deploy to Luckfox**: [LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md)
- **Troubleshoot build**: [DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md)
- **Use PA API**: [PA_API_GUIDE.md](PA_API_GUIDE.md)
- **Switch DB (PostgreSQL/Oracle)**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#switching-databases)

### Related
- **[../CLAUDE.md](../CLAUDE.md)** - Project quick reference (v2.9.0)
  - Architecture overview, API endpoints, version history
  - Helper scripts, common issues, key decisions
