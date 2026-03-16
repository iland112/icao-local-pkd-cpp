# ICAO Local PKD - Documentation

**Version**: v2.35.0
**Last Updated**: 2026-03-16

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
- **[API_DEVELOPER_MANUAL.md](API_DEVELOPER_MANUAL.md)** - REST API 개발자 매뉴얼 (v2.35.0)
  - 전체 API 레퍼런스 (인증서, PA, AI, 동기화, ICAO, CSR 관리, 코드 마스터)
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
- **[SOFTWARE_ARCHITECTURE.md](SOFTWARE_ARCHITECTURE.md)** - System Design (v2.35.0)
  - 5-service microarchitecture overview
  - Database schema (20 tables), LDAP DIT structure, design patterns

- **[ARCHITECTURE_DESIGN_PRINCIPLES.md](ARCHITECTURE_DESIGN_PRINCIPLES.md)** - Design Principles
  - DDD, Microservices, Query Executor, Provider/Adapter patterns

- **[FRONTEND_DESIGN_SYSTEM.md](FRONTEND_DESIGN_SYSTEM.md)** - Frontend UI/UX Design System
  - Color theme, components, tokens, layout patterns

- **[PAGE_FUNCTIONALITY_GUIDE.md](PAGE_FUNCTIONALITY_GUIDE.md)** - Frontend 페이지 기능 상세 가이드
  - 27개 페이지 전체 기능 명세

### Security & Trust Chain
- **[PII_ENCRYPTION_COMPLIANCE.md](PII_ENCRYPTION_COMPLIANCE.md)** - PII Encryption (개인정보보호법 제29조)
- **[EPASSPORT_TRUST_CHAIN_GUIDE.md](EPASSPORT_TRUST_CHAIN_GUIDE.md)** - Trust Chain Technical Guide
- **[DSC_NC_HANDLING.md](DSC_NC_HANDLING.md)** - DSC Non-Conformant Handling
- **[DOC9303_COMPLIANCE_CHECKS.md](DOC9303_COMPLIANCE_CHECKS.md)** - Doc 9303 Compliance Checks
- **[BSI_TR03110_ALGORITHM_SUPPORT.md](BSI_TR03110_ALGORITHM_SUPPORT.md)** - BSI TR-03110 Algorithm Support

### AI & Analysis
- **[AI_ANALYSIS_DASHBOARD_GUIDE.md](AI_ANALYSIS_DASHBOARD_GUIDE.md)** - AI Dashboard Guide
- **[CERTIFICATE_SOURCE_MANAGEMENT.md](CERTIFICATE_SOURCE_MANAGEMENT.md)** - Certificate Source Management
- **[ICAO_PKD_COMPLIANCE_ANALYSIS.md](ICAO_PKD_COMPLIANCE_ANALYSIS.md)** - ICAO PKD Compliance Analysis
- **[COMPETITIVE_ANALYSIS.md](COMPETITIVE_ANALYSIS.md)** - Competitive Analysis

### Reference
- **[MASTER_LIST_PROCESSING_GUIDE.md](MASTER_LIST_PROCESSING_GUIDE.md)** - Master List Format & Processing
- **[ICAO_PKD_COST_ANALYSIS.md](ICAO_PKD_COST_ANALYSIS.md)** - ICAO PKD Cost Analysis
- **[EAC_SERVICE_IMPLEMENTATION_PLAN.md](EAC_SERVICE_IMPLEMENTATION_PLAN.md)** - EAC Service (Experimental)
- **[DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md)** - Build Cache Troubleshooting
- **[VERSION_HISTORY.md](VERSION_HISTORY.md)** - 전체 버전 이력

### Legal
- **[EULA.md](EULA.md)** - End User License Agreement
- **[LICENSE_COMPLIANCE.md](LICENSE_COMPLIANCE.md)** - License Compliance

---

## API Specifications

**Location**: [openapi/](openapi/)

| Spec | Service | Version |
|------|---------|---------|
| [pkd-management.yaml](openapi/pkd-management.yaml) | PKD Management | v2.35.0 |
| [pa-service.yaml](openapi/pa-service.yaml) | PA Service | v2.1.4 |
| [pkd-relay-service.yaml](openapi/pkd-relay-service.yaml) | PKD Relay | v2.30.0 |
| [monitoring-service.yaml](openapi/monitoring-service.yaml) | Monitoring | v1.2.0 |

**Swagger UI**: http://localhost:8090 (or http://localhost/api-docs/)

---

## Archived Documents

**Location**: [archive/](archive/)

155+ historical implementation documents including:
- Sprint planning and completion summaries
- Phase completion reports (Security, Oracle migration, Repository pattern)
- Feature design and implementation plans (completed)
- Fix reports, security audit reports, load test results, troubleshooting records

These documents are for historical reference only. All current information is captured in the active reference docs above and in [CLAUDE.md](../CLAUDE.md).

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
- **SSL setup**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#https--ssl)

### Related
- **[../CLAUDE.md](../CLAUDE.md)** - Project quick reference (v2.35.0)
  - Architecture overview, API endpoints, version history
  - Helper scripts, common issues, key decisions
