# ICAO Local PKD - Documentation

**Version**: v2.30.0
**Last Updated**: 2026-03-09

---

## Essential Documents (Start Here)

### Development & Operations
- **[DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md)** - **START HERE**
  - Credentials (PostgreSQL, Oracle, LDAP)
  - Helper scripts, daily workflow
  - Multi-DBMS support (PostgreSQL + Oracle)
  - Build process, testing (438 test cases), troubleshooting

- **[DEPLOYMENT_PROCESS.md](DEPLOYMENT_PROCESS.md)** - CI/CD Pipeline
  - GitHub Actions ARM64 build workflow
  - Artifact management & deployment script

- **[PODMAN_DEPLOYMENT.md](PODMAN_DEPLOYMENT.md)** - Production Podman Deployment (RHEL 9)
  - SELinux Rootless Podman, aardvark-dns, troubleshooting

- **[LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md)** - ARM64 Deployment
  - Luckfox-specific network and service configuration

- **[SERVER_SETUP_10.0.0.220.md](SERVER_SETUP_10.0.0.220.md)** - Production Server Setup

- **[BUILD_SOP.md](BUILD_SOP.md)** - Build Verification Procedures

### API & Integration
- **[API_DEVELOPER_MANUAL.md](API_DEVELOPER_MANUAL.md)** - REST API 개발자 매뉴얼 (v2.25.5)
  - 전체 API 레퍼런스 (인증서, PA, AI, 동기화, ICAO, 코드 마스터)
  - 외부 클라이언트 연동 예제 (Python, Java, C#, curl)

- **[PA_API_GUIDE.md](PA_API_GUIDE.md)** - Passive Authentication API
  - 12 endpoints, 8-step verification process
  - DSC auto-registration, DG2 face image, DSC conformance check

- **[API_CLIENT_ADMIN_GUIDE.md](API_CLIENT_ADMIN_GUIDE.md)** - API Client 관리자 가이드
  - API Key 인증, 권한 관리, Rate Limiting

- **[API_CLIENT_USER_GUIDE.md](API_CLIENT_USER_GUIDE.md)** - API Client 사용자 가이드
  - 외부 연동 가이드, Python/Java/C#/curl 예제

- **[LDAP_QUERY_GUIDE.md](LDAP_QUERY_GUIDE.md)** - LDAP Operations

- **[CERTIFICATE_SEARCH_QUICKSTART.md](CERTIFICATE_SEARCH_QUICKSTART.md)** - Certificate Search

### Architecture & Design
- **[SOFTWARE_ARCHITECTURE.md](SOFTWARE_ARCHITECTURE.md)** - System Design
  - 5-service microarchitecture overview
  - Database schema, LDAP DIT structure, design patterns

- **[ARCHITECTURE_DESIGN_PRINCIPLES.md](ARCHITECTURE_DESIGN_PRINCIPLES.md)** - Design Principles
  - DDD, Microservices, Query Executor, Provider/Adapter patterns

- **[FRONTEND_DESIGN_SYSTEM.md](FRONTEND_DESIGN_SYSTEM.md)** - Frontend UI/UX Design System
  - Color theme, components, tokens, layout patterns

- **[PAGE_FUNCTIONALITY_GUIDE.md](PAGE_FUNCTIONALITY_GUIDE.md)** - Frontend 페이지 기능 상세 가이드
  - 24개 페이지 전체 기능 명세

### Security & Trust Chain
- **[SECURITY_AUDIT_REPORT.md](SECURITY_AUDIT_REPORT.md)** - Security Audit Report
- **[SECURITY_FIX_ACTION_PLAN.md](SECURITY_FIX_ACTION_PLAN.md)** - Security Fix Implementation
- **[EPASSPORT_TRUST_CHAIN_GUIDE.md](EPASSPORT_TRUST_CHAIN_GUIDE.md)** - Trust Chain Technical Guide
- **[DSC_TRUST_CHAIN_FAILURE_REPORT.md](DSC_TRUST_CHAIN_FAILURE_REPORT.md)** - Trust Chain Failure Analysis
- **[DSC_NC_HANDLING.md](DSC_NC_HANDLING.md)** - DSC Non-Conformant Handling
- **[DOC9303_COMPLIANCE_CHECKS.md](DOC9303_COMPLIANCE_CHECKS.md)** - Doc 9303 Compliance Checks

### AI & Analysis
- **[AI_ANALYSIS_DASHBOARD_GUIDE.md](AI_ANALYSIS_DASHBOARD_GUIDE.md)** - AI Dashboard Guide
- **[CERTIFICATE_SOURCE_MANAGEMENT.md](CERTIFICATE_SOURCE_MANAGEMENT.md)** - Certificate Source Management

### Testing & Performance
- **[LOAD_TEST_PLAN.md](LOAD_TEST_PLAN.md)** - 부하 테스트 계획서
- **[LOAD_TEST_RESULTS.md](LOAD_TEST_RESULTS.md)** - 부하 테스트 결과
- **[VERSION_HISTORY.md](VERSION_HISTORY.md)** - 전체 버전 이력

### Reference
- **[MASTER_LIST_PROCESSING_GUIDE.md](MASTER_LIST_PROCESSING_GUIDE.md)** - Master List Format & Processing
- **[DVL_ANALYSIS.md](DVL_ANALYSIS.md)** - Deviation List Format Analysis
- **[ICAO_PKD_COST_ANALYSIS.md](ICAO_PKD_COST_ANALYSIS.md)** - ICAO PKD Cost Analysis
- **[ICAO_PKD_DATA_ANALYSIS.md](ICAO_PKD_DATA_ANALYSIS.md)** - ICAO PKD Data Analysis
- **[DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md)** - Build Cache Troubleshooting
- **[FRONTEND_BUILD_GUIDE.md](FRONTEND_BUILD_GUIDE.md)** - Frontend Build Workflow

### Legal
- **[EULA.md](EULA.md)** - End User License Agreement
- **[LICENSE_COMPLIANCE.md](LICENSE_COMPLIANCE.md)** - License Compliance

---

## API Specifications

**Location**: [openapi/](openapi/)

| Spec | Service | Version |
|------|---------|---------|
| [pkd-management.yaml](openapi/pkd-management.yaml) | PKD Management | v2.30.0 |
| [pa-service.yaml](openapi/pa-service.yaml) | PA Service | v2.1.4 |
| [pkd-relay-service.yaml](openapi/pkd-relay-service.yaml) | PKD Relay | v2.30.0 |
| [monitoring-service.yaml](openapi/monitoring-service.yaml) | Monitoring | v1.2.0 |

**Swagger UI**: http://localhost:8090 (or http://localhost/api-docs/)

---

## Archived Documents

**Location**: [archive/](archive/)

145+ historical implementation documents including:
- Sprint planning and completion summaries
- Phase completion reports (Security, Oracle migration, Repository pattern)
- Feature design and implementation plans (completed)
- Fix reports and troubleshooting records

These documents are for historical reference only. All current information is captured in the active reference docs above and in [CLAUDE.md](../CLAUDE.md).

---

## Document Organization

```
docs/
+-- README.md                              # This file (index)
+-- DEVELOPMENT_GUIDE.md                   # Primary dev reference
+-- API_DEVELOPER_MANUAL.md                # REST API developer manual
+-- PA_API_GUIDE.md                        # PA Service API guide
+-- API_CLIENT_ADMIN_GUIDE.md              # API Client admin guide
+-- API_CLIENT_USER_GUIDE.md               # API Client user guide
+-- SOFTWARE_ARCHITECTURE.md               # System architecture
+-- ARCHITECTURE_DESIGN_PRINCIPLES.md      # Design principles
+-- FRONTEND_DESIGN_SYSTEM.md              # Frontend design system
+-- PAGE_FUNCTIONALITY_GUIDE.md            # Frontend page functionality
+-- SECURITY_AUDIT_REPORT.md               # Security audit
+-- SECURITY_FIX_ACTION_PLAN.md            # Security fix log
+-- EPASSPORT_TRUST_CHAIN_GUIDE.md         # Trust chain guide
+-- DSC_TRUST_CHAIN_FAILURE_REPORT.md      # Trust chain failure analysis
+-- DSC_NC_HANDLING.md                     # DSC non-conformant handling
+-- DOC9303_COMPLIANCE_CHECKS.md           # Doc 9303 compliance checks
+-- CERTIFICATE_SOURCE_MANAGEMENT.md       # Certificate source tracking
+-- CERTIFICATE_SEARCH_QUICKSTART.md       # Certificate search guide
+-- AI_ANALYSIS_DASHBOARD_GUIDE.md         # AI dashboard guide
+-- MASTER_LIST_PROCESSING_GUIDE.md        # ML format reference
+-- DVL_ANALYSIS.md                        # Deviation List reference
+-- ICAO_PKD_COST_ANALYSIS.md              # Cost analysis
+-- ICAO_PKD_DATA_ANALYSIS.md              # Data analysis
+-- VERSION_HISTORY.md                     # Version history
+-- DEPLOYMENT_PROCESS.md                  # CI/CD pipeline
+-- PODMAN_DEPLOYMENT.md                   # Podman deployment (RHEL 9)
+-- LUCKFOX_DEPLOYMENT.md                  # ARM64 deployment
+-- SERVER_SETUP_10.0.0.220.md             # Production server setup
+-- BUILD_SOP.md                           # Build verification
+-- DOCKER_BUILD_CACHE.md                  # Build cache troubleshooting
+-- FRONTEND_BUILD_GUIDE.md                # Frontend build guide
+-- LOAD_TEST_PLAN.md                      # Load test plan
+-- LOAD_TEST_RESULTS.md                   # Load test results
+-- LDAP_QUERY_GUIDE.md                    # LDAP operations guide
+-- EULA.md                                # End User License Agreement
+-- LICENSE_COMPLIANCE.md                  # License compliance
+-- openapi/                               # OpenAPI specifications
|   +-- pkd-management.yaml
|   +-- pa-service.yaml
|   +-- pkd-relay-service.yaml
|   +-- monitoring-service.yaml
+-- presentations/                         # Snapshot presentations (v2.0.0)
+-- archive/                               # 145+ historical documents
```

---

## Quick Reference

### Common Tasks
- **Start development**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#daily-commands)
- **Build a service**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#2-build-and-deploy)
- **Deploy to Production (Podman)**: [PODMAN_DEPLOYMENT.md](PODMAN_DEPLOYMENT.md)
- **Deploy to Luckfox**: [LUCKFOX_DEPLOYMENT.md](LUCKFOX_DEPLOYMENT.md)
- **Troubleshoot build**: [DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md)
- **Use PA API**: [PA_API_GUIDE.md](PA_API_GUIDE.md)
- **Switch DB (PostgreSQL/Oracle)**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#switching-databases)
- **Run unit tests**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#unit-tests)
- **SSL setup**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#https--ssl)

### Related
- **[../CLAUDE.md](../CLAUDE.md)** - Project quick reference (v2.30.0)
  - Architecture overview, API endpoints, version history
  - Helper scripts, common issues, key decisions
