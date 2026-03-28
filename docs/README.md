# FASTpass® SPKD — Documentation

**Version**: v2.41.0
**Last Updated**: 2026-03-27

---

## Essential Documents (Start Here)

### Development & Operations
- **[DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md)** — **START HERE**
  - Credentials (PostgreSQL, Oracle, LDAP)
  - Helper scripts, daily workflow
  - Multi-DBMS support (PostgreSQL + Oracle)
  - Build process, testing (438 test cases), troubleshooting

- **[DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md)** — 통합 배포 가이드
  - Local (WSL2, Docker), Production (10.0.0.220, RHEL 9, Podman), Luckfox (ARM64)
  - 서버 설정, CI/CD 파이프라인, Build SOP, 트러블슈팅

- **[LDAP_QUERY_GUIDE.md](LDAP_QUERY_GUIDE.md)** — LDAP Operations

### API & Integration
- **[API_DEVELOPER_MANUAL.md](API_DEVELOPER_MANUAL.md)** — REST API 개발자 매뉴얼
  - 전체 API 레퍼런스 (인증서, PA, AI, 동기화, ICAO, CSR 관리, 코드 마스터)

- **[PA_API_GUIDE.md](PA_API_GUIDE.md)** — Passive Authentication API
  - 12 endpoints, 8-step verification, DSC auto-registration, DG2 face image

- **[API_CLIENT_GUIDE.md](API_CLIENT_GUIDE.md)** — API Client 통합 가이드
  - 관리자: API Key 관리, 권한, Rate Limiting
  - 사용자: 외부 연동 가이드, Python/Java/C#/curl 예제
  - Certificate Search Quick Start

### Architecture & Design
- **[ARCHITECTURE.md](ARCHITECTURE.md)** — 시스템 아키텍처 & 설계 원칙
  - 마이크로서비스 아키텍처, DB 스키마, LDAP DIT
  - DDD, ServiceContainer, Handler Pattern, Query Helpers

- **[FRONTEND_GUIDE.md](FRONTEND_GUIDE.md)** — 프론트엔드 통합 가이드
  - Design System: 색상, 타이포그래피, 컴포넌트, 토큰
  - 29개 페이지 전체 기능 명세, 권한, 라우팅

### ICAO Compliance & Security
- **[ICAO_COMPLIANCE_MANUAL.md](ICAO_COMPLIANCE_MANUAL.md)** — ICAO 규격 준수 매뉴얼
  - ePassport Trust Chain, Doc 9303 Compliance Checks
  - BSI TR-03110 Algorithm Support, DSC Non-Conformant Handling
  - PKD 인증서 분석 결과, PII 암호화 (개인정보보호법)

### Certificate & AI
- **[CERTIFICATE_PROCESSING_GUIDE.md](CERTIFICATE_PROCESSING_GUIDE.md)** — 인증서 처리 통합 가이드
  - Certificate Source Management (5가지 소스 타입)
  - Master List Processing (CMS SignedData 파싱)

- **[AI_ANALYSIS_DASHBOARD_GUIDE.md](AI_ANALYSIS_DASHBOARD_GUIDE.md)** — AI Dashboard Guide
- **[COMPETITIVE_ANALYSIS.md](COMPETITIVE_ANALYSIS.md)** — Competitive Analysis

### Reference
- **[ICAO_PKD_COST_ANALYSIS.md](ICAO_PKD_COST_ANALYSIS.md)** — ICAO PKD Cost Analysis
- **[EAC_SERVICE_IMPLEMENTATION_PLAN.md](EAC_SERVICE_IMPLEMENTATION_PLAN.md)** — EAC Service (Experimental)
- **[DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md)** — Build Cache Troubleshooting
- **[VERSION_HISTORY.md](VERSION_HISTORY.md)** — 전체 버전 이력

### Legal
- **[EULA.md](EULA.md)** — End User License Agreement
- **[LICENSE_COMPLIANCE.md](LICENSE_COMPLIANCE.md)** — License Compliance

---

## API Specifications

**Location**: [openapi/](openapi/)

| Spec | Service | Version |
|------|---------|---------|
| [pkd-management.yaml](openapi/pkd-management.yaml) | PKD Management | v2.36.0 |
| [pa-service.yaml](openapi/pa-service.yaml) | PA Service | v2.1.7 |
| [pkd-relay-service.yaml](openapi/pkd-relay-service.yaml) | PKD Relay | v2.36.0 |
| [monitoring-service.yaml](openapi/monitoring-service.yaml) | Monitoring | v1.2.0 |

**Swagger UI**: http://localhost:18090

---

## Archived Documents

**Location**: [archive/](archive/)

170+ historical implementation documents including:
- Sprint planning and completion summaries
- Phase completion reports (Security, Oracle migration, Repository pattern)
- Feature design and implementation plans (completed)
- Fix reports, security audit reports, load test results, troubleshooting records
- Merged source documents (pre-consolidation individual files)

These documents are for historical reference only. All current information is captured in the active reference docs above and in [CLAUDE.md](../CLAUDE.md).

---

## Quick Reference

### Common Tasks
- **Start development**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#daily-commands)
- **Build a service**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#2-build-and-deploy)
- **Deploy to Production**: [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md#3-production-100220-rhel-9-podman)
- **Deploy to Luckfox**: [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md#4-luckfox-arm64)
- **Troubleshoot build**: [DOCKER_BUILD_CACHE.md](DOCKER_BUILD_CACHE.md)
- **Use PA API**: [PA_API_GUIDE.md](PA_API_GUIDE.md)
- **Switch DB (PostgreSQL/Oracle)**: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md#switching-databases)
- **SSL setup**: [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md#39-ssl-인증서)

### Related
- **[../CLAUDE.md](../CLAUDE.md)** - Project quick reference (v2.41.0)
  - Architecture overview, API endpoints, version history
  - Helper scripts, common issues, key decisions
