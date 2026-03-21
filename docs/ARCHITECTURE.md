# FASTpass(R) SPKD -- System Architecture & Design Principles

**Version**: v2.37.0 | **Last Updated**: 2026-03-18
**Status**: Production Ready (Multi-DBMS: PostgreSQL + Oracle)

---

## Table of Contents

### Part 1: System Architecture
1. [System Overview](#system-overview)
2. [Technical Architecture Diagram](#technical-architecture-diagram)
3. [Microservices Architecture](#microservices-architecture)
4. [Data Layer Architecture](#data-layer-architecture)
5. [Frontend Architecture](#frontend-architecture)
6. [API Gateway Architecture](#api-gateway-architecture)
7. [Component Details](#component-details)
8. [Data Flow Diagrams](#data-flow-diagrams)
9. [Deployment Architecture](#deployment-architecture)
10. [Security Architecture](#security-architecture)
11. [Technology Stack Summary](#technology-stack-summary)
12. [Performance Metrics](#performance-metrics)
13. [Monitoring & Observability](#monitoring--observability)

### Part 2: Design Principles & Patterns
14. [Core Design Principles](#core-design-principles)
15. [DDD (Domain-Driven Design) Structure](#ddd-domain-driven-design-structure)
16. [ServiceContainer Pattern (Centralized DI)](#servicecontainer-pattern-centralized-di)
17. [main.cpp Minimization Pattern](#maincpp-minimization-pattern)
18. [Strategy Pattern](#strategy-pattern)
19. [Single Responsibility Principle (SRP)](#single-responsibility-principle-srp)
20. [Query Executor Pattern (Database Abstraction)](#query-executor-pattern-database-abstraction)
21. [Query Helpers Pattern](#query-helpers-pattern)
22. [Provider/Adapter Pattern (Validation Infrastructure)](#provideradapter-pattern-validation-infrastructure)
23. [Shared Libraries](#shared-libraries)
24. [Code Structure Rules](#code-structure-rules)
25. [Extension Guidelines](#extension-guidelines)
26. [Anti-Patterns](#anti-patterns)
27. [Code Review Checklist](#code-review-checklist)

### Appendix
28. [Future Enhancements](#future-enhancements)

---

# Part 1: System Architecture

---

## System Overview

ICAO Local PKD is a **microservices architecture**-based integrated platform for e-Passport certificate management and verification.

### Core Principles

- **Microservices**: 5 independently deployable services
- **Multi-DBMS**: PostgreSQL + Oracle runtime switching (DB_TYPE environment variable)
- **Data Consistency**: DB-LDAP dual storage with automatic synchronization (Reconciliation)
- **High Performance**: C++20 high-performance backend
- **Modern UI**: React 19 + TypeScript + Tailwind CSS 4
- **Security First**: JWT authentication, RBAC, OWASP security hardening
- **Shared Validation**: icao::validation shared library (86 unit tests)

### PKD and EAC PKI Role Separation

This system is specialized for **ICAO 9303 Passive Authentication (PA)** and is independent from the EU EAC (Extended Access Control) system.

| Category | This System (PKD) | EAC PKI (Separate) |
|------|----------------|----------------|
| **Purpose** | Chip data integrity verification (PA) | Protected biometric access (TA) |
| **Certificates** | CSCA -> DSC (X.509) | CVCA -> DV -> IS (CVC) |
| **Protected Data** | DG1(MRZ), DG2(Face) | DG3(Fingerprint), DG4(Iris) |
| **Distribution** | ICAO PKD Server (Public) | Bilateral agreement |
| **EU Passport** | **Required** (All countries) | Only for fingerprint/iris access |

EU passport processing uses PKD for PA (Stage 1), and optionally EAC TA (Stage 3) via separate EAC PKI.
No certificate sharing between the two systems; authentication flows are independent.
See [EAC_SERVICE_IMPLEMENTATION_PLAN.md](EAC_SERVICE_IMPLEMENTATION_PLAN.md) Section 1.4 for details.

---

## Technical Architecture Diagram

### System Overview (v2.37.0)

```mermaid
graph TB
    %% Layer 1: External Access
    subgraph L1[" "]
        direction TB
        L1Title["Layer 1: External Access"]
        subgraph L1Nodes[" "]
            direction LR
            User["User Browser"]
            ICAOPortal["ICAO Portal<br/>pkd.icao.int"]
        end
    end

    %% Layer 2: DMZ
    subgraph L2[" "]
        direction TB
        L2Title["Layer 2: DMZ - Public Ports"]
        subgraph L2Nodes[" "]
            direction LR
            Frontend["Frontend<br/>React 19 SPA<br/>Port 3080"]
            APIGateway["API Gateway<br/>Nginx Reverse Proxy<br/>Port 80/443/8080"]
        end
    end

    %% Layer 3: Application Services
    subgraph L3[" "]
        direction TB
        L3Title["Layer 3: Application Services - C++ Microservices"]
        subgraph L3Nodes[" "]
            direction LR
            PKD["PKD Management<br/>Upload & Validation<br/>Port 8081"]
            PA["PA Service<br/>Passive Auth Verify<br/>Port 8082"]
            Relay["PKD Relay<br/>DB-LDAP Sync<br/>Port 8083"]
            Monitoring["Monitoring<br/>System Metrics<br/>Port 8084"]
            AI["AI Analysis<br/>ML Forensics<br/>Port 8085"]
        end
    end

    %% Layer 4: Data Storage
    subgraph L4[" "]
        direction TB
        L4Title["Layer 4: Data Storage - Persistent Layer"]
        subgraph L4Nodes[" "]
            direction LR
            DB[("PostgreSQL / Oracle<br/>31K Certificates<br/>Port 5432 / 1521")]
            LDAPCluster[("LDAP MMR Cluster<br/>Master 1 + Master 2<br/>Ports 3891/3892")]
        end
    end

    %% Layer 5: Infrastructure
    subgraph L5[" "]
        direction TB
        L5Title["Layer 5: Infrastructure - Docker Compose Runtime"]
        Docker["Docker Compose Network"]
    end

    %% Vertical connections
    User -->|1. HTTPS| Frontend
    User -->|2. REST API| APIGateway

    Frontend -->|3. Proxy| APIGateway

    APIGateway -->|4. Route| PKD
    APIGateway -.-> PA
    APIGateway -.-> Relay
    APIGateway -.-> Monitoring
    APIGateway -.-> AI

    PKD -->|5. Query| DB
    PA --> DB
    Relay --> DB

    PKD -->|6. LDAP SLB| LDAPCluster
    PA --> LDAPCluster
    Relay --> LDAPCluster

    DB -.->|7. Runtime| Docker
    LDAPCluster -.-> Docker

    ICAOPortal -.->|Scraping| Relay

    %% Styling
    classDef titleStyle fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-weight:bold,font-size:14px
    class L1Title,L2Title,L3Title,L4Title,L5Title titleStyle

    classDef external fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    classDef dmz fill:#0f4c75,stroke:#0ea5e9,stroke-width:2px,color:#f1f5f9,font-size:13px
    classDef app fill:#164e63,stroke:#22d3ee,stroke-width:2px,color:#f1f5f9,font-size:13px
    classDef data fill:#312e81,stroke:#818cf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    classDef infra fill:#3f3f46,stroke:#a1a1aa,stroke-width:2px,color:#f1f5f9,font-size:13px

    class User,ICAOPortal external
    class Frontend,APIGateway dmz
    class PKD,PA,Relay,Monitoring,AI app
    class DB,LDAPCluster data
    class Docker infra

    classDef containerStyle fill:none,stroke:#475569,stroke-width:1px,stroke-dasharray:5 5
    class L1,L2,L3,L4,L5 containerStyle

    classDef nodeContainer fill:none,stroke:none
    class L1Nodes,L2Nodes,L3Nodes,L4Nodes nodeContainer
```

**Architecture Highlights**:

1. **5-Layer Hierarchy**: Clear layer separation for Separation of Concerns
2. **Minimal Coupling**: Each layer depends only on the layer directly below (Vertical Flow)
3. **Gateway Pattern**: API Gateway (nginx) provides a single entry point, App-level LDAP SLB
4. **Data Abstraction**: LDAP MMR cluster integrates 2 Master nodes
5. **Simplified Topology**: Minimized connection lines to reduce system complexity

### Layer Description

| Layer | Purpose | Components | Key Characteristics |
|-------|---------|------------|---------------------|
| **Layer 1: External** | External access and integration | User, ICAO Portal | Public Internet |
| **Layer 2: DMZ** | Public service zone | Frontend, API Gateway | Ports 3080, 80/443/8080 |
| **Layer 3: Application** | Business logic processing | PKD, PA, Relay, Monitoring (C++20), AI (Python) | Internal Network |
| **Layer 4: Data** | Data persistence | PostgreSQL/Oracle, LDAP MMR | Internal Storage + App-level SLB |
| **Layer 5: Infrastructure** | Container runtime | Docker Compose | Platform Layer |

### Data Flow Summary

**Request Flow** (Top -> Bottom):
```
User -> Frontend -> API Gateway -> Services (PKD/PA/Relay) -> Data (PostgreSQL/LDAP)
```

**Service Architecture**:
- **5 Microservices**: PKD Management (:8081), PA Service (:8082), PKD Relay (:8083), Monitoring (:8084), AI Analysis (:8085)
- **2 Data Stores**: PostgreSQL/Oracle (31,212 certificates), LDAP MMR Cluster (Master 1+2)
- **1 Gateway Node**: API Gateway (HTTP :80 / HTTPS :443), App-level LDAP SLB
- **1 Frontend**: React 19 SPA (29 pages)

---

## Microservices Architecture

### Service Layout

```
services/
+-- pkd-management/        # PKD Management Service (Port: 8081)
|   +-- File Upload & Processing (LDIF, Master List)
|   +-- Certificate Search & Export
|   +-- ICAO Auto Sync
|   +-- API Client Authentication (X-API-Key)
|
+-- pa-service/            # Passive Authentication Service (Port: 8082)
|   +-- SOD Verification
|   +-- DG1/DG2 Parsing
|   +-- CSCA LDAP Lookup
|
+-- pkd-relay-service/     # PKD Relay Service (Port: 8083)
|   +-- DB-LDAP Synchronization
|   +-- Auto Reconciliation
|   +-- Sync Status Monitoring
|
+-- monitoring-service/    # Monitoring Service (Port: 8084)
|   +-- System Resource Monitoring (CPU, Memory, Disk, Network)
|   +-- Service Health Checks (HTTP Probes)
|   +-- DB-Independent Architecture (no PostgreSQL/Oracle dependency)
|
+-- ai-analysis/           # AI Analysis Service (Port: 8085)
    +-- ML Anomaly Detection (Isolation Forest + LOF)
    +-- Forensic Risk Scoring
    +-- Pattern Analysis (Python/FastAPI)
```

### Service Communication

```
+----------------+
|   Frontend     | :3000
+-------+--------+
        |
        v
+----------------+
| API Gateway    | :8080 (Nginx)
+-------+--------+
        |
        +------------------+------------------+------------------+------------------+
        v                  v                  v                  v                  v
+----------------+   +----------------+   +----------------+   +----------------+   +----------------+
|     PKD        |   |      PA        |   |  PKD Relay     |   | Monitoring     |   |     AI         |
|  Management    |   |   Service      |   |   Service      |   |   Service      |   |  Analysis      |
|    :8081       |   |    :8082       |   |    :8083       |   |    :8084       |   |    :8085       |
+-------+--------+   +-------+--------+   +-------+--------+   +----------------+   +-------+--------+
        |                     |                     |                                         |
        +---------------------+---------------------+-----------------------------------------+
                              |
                              v
               +------------------------+
               |    PostgreSQL DB       |
               |    OpenLDAP Cluster    |
               +------------------------+
```

### 1. PKD Management Service (Port 8081)

```mermaid
flowchart LR
    subgraph API["API Layer"]
        Upload["Upload API<br/>LDIF/ML"]
        Cert["Certificate API<br/>Search/Export"]
        Health["Health API<br/>DB/LDAP"]
        ICAO["ICAO Sync API<br/>Version"]
    end

    subgraph Domain["Domain Layer"]
        UploadDomain["Upload Domain<br/>Business Logic"]
        CertDomain["Certificate Domain<br/>Validation"]
        IcaoDomain["ICAO Domain<br/>Version Tracking"]
    end

    subgraph Service["Service Layer"]
        UploadService["Upload Service<br/>File Processing"]
        CertService["Certificate Service<br/>LDAP Operations"]
        IcaoService["ICAO Service<br/>HTML Parsing"]
    end

    subgraph Repo["Repository Layer"]
        UploadRepo["Upload Repository<br/>PostgreSQL"]
        CertRepo["Certificate Repository<br/>LDAP"]
        IcaoRepo["ICAO Repository<br/>PostgreSQL"]
    end

    subgraph Infra["Infrastructure"]
        LDIF["LDIF Processor"]
        CMS["CMS Parser"]
        HTTP["HTTP Client"]
        HTML["HTML Parser"]
        Email["Email Sender"]
    end

    subgraph Strategy["Strategy"]
        Auto["Auto Processing"]
        Manual["Manual Processing"]
    end

    subgraph Data["Data Store"]
        DB[("PostgreSQL")]
        LDAPS[("LDAP")]
    end

    Upload --> UploadDomain --> UploadService --> UploadRepo
    Cert --> CertDomain --> CertService --> CertRepo
    ICAO --> IcaoDomain --> IcaoService --> IcaoRepo
    Health --> CertService

    UploadService --> LDIF
    UploadService --> CMS
    UploadService --> Auto
    UploadService --> Manual

    IcaoService --> HTTP
    IcaoService --> HTML
    IcaoService --> Email

    UploadRepo --> DB
    CertRepo --> LDAPS
    IcaoRepo --> DB

    style API fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Domain fill:#14532d,stroke:#4ade80,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Service fill:#312e81,stroke:#818cf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Repo fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Infra fill:#164e63,stroke:#22d3ee,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Strategy fill:#3f3f46,stroke:#a1a1aa,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Data fill:#7f1d1d,stroke:#f87171,stroke-width:2px,color:#f1f5f9,font-size:13px

    style ICAO fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style IcaoDomain fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style IcaoService fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style IcaoRepo fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style HTTP fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style HTML fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Email fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
```

**Key Features**:
- Clean Architecture (6 Layers) with ServiceContainer (centralized DI, pimpl pattern)
- Handler Pattern: UploadHandler (10), UploadStatsHandler (11), CertificateHandler (12), AuthHandler, IcaoHandler, CsrHandler (7), MiscHandler
- Query Helpers (`common::db::`) -- database-agnostic utility functions across 16 repositories
- **CSR Management**: ICAO PKD CSR generation (RSA-2048 + SHA256withRSA), external CSR Import, ICAO-issued certificate registration
- **DSC Pending Approval**: DSC extracted from PA verification -> auto-registration -> admin approval workflow
- **PII Encryption**: AES-256-GCM authenticated encryption (Personal Information Protection Act Article 29 -- CSR private key, PII fields)
- ICAO Auto Sync with Daily Scheduler
- LDIF/Master List Parsing + Individual Certificate Upload (PEM/DER/P7B/DL/CRL)
- Trust Chain Validation (icao::validation shared library)
- Certificate Search & Export (DIT-structured ZIP)
- API Client Authentication (X-API-Key, SHA-256 hash, per-client Rate Limiting)
- JWT Authentication + RBAC (admin/user)
- DSC_NC Report, CRL Report, Certificate Quality Report, PA Lookup API
- Multi-DBMS (PostgreSQL + Oracle)

---

### 2. PA Service (Port 8082)

```mermaid
flowchart LR
    subgraph API["API Layer"]
        Verify["PA Verify API<br/>SOD and DG"]
        TrustMat["Trust Materials API<br/>Client PA"]
        TrustResult["Result Report API<br/>MRZ + Result"]
        ParseSOD["SOD Parsing<br/>Metadata"]
        ParseDG1["DG1 Parsing<br/>MRZ"]
        ParseDG2["DG2 Parsing<br/>Face"]
        Stats["Statistics<br/>Combined"]
    end

    subgraph Logic["Business Logic"]
        SODVerify["SOD Verifier<br/>CMS"]
        HashVerify["Hash Verifier<br/>DG"]
        ChainVerify["Trust Chain<br/>CSCA-DSC"]
        TrustMatSvc["Trust Material<br/>Service"]
        MRZParser["MRZ Parser<br/>TD1/TD2/TD3"]
        ImageExtractor["Image Extractor<br/>JPEG"]
    end

    subgraph DataAccess["Data Access"]
        PARepo["PA Repository<br/>PostgreSQL"]
        TMRepo["Trust Material<br/>Request Repo"]
        LDAPRepo["LDAP Repository<br/>Certificates"]
    end

    subgraph Crypto["Cryptography Layer"]
        OpenSSL["OpenSSL 3.x<br/>CMS/X.509"]
    end

    subgraph DataStore["Data Store"]
        DB[("PostgreSQL")]
        LDAPS[("LDAP")]
    end

    Verify --> SODVerify
    Verify --> HashVerify
    Verify --> ChainVerify

    TrustMat --> TrustMatSvc
    TrustResult --> TrustMatSvc
    TrustResult --> MRZParser
    TrustMatSvc --> LDAPRepo
    TrustMatSvc --> TMRepo
    Stats --> TMRepo

    ParseSOD --> SODVerify
    ParseDG1 --> MRZParser
    ParseDG2 --> ImageExtractor

    SODVerify --> OpenSSL
    HashVerify --> OpenSSL
    ChainVerify --> LDAPRepo
    ChainVerify --> OpenSSL

    Stats --> PARepo

    PARepo --> DB
    LDAPRepo --> LDAPS

    style API fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Logic fill:#14532d,stroke:#4ade80,stroke-width:2px,color:#f1f5f9,font-size:13px
    style DataAccess fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Crypto fill:#7f1d1d,stroke:#f87171,stroke-width:2px,color:#f1f5f9,font-size:13px
    style DataStore fill:#312e81,stroke:#818cf8,stroke-width:2px,color:#f1f5f9,font-size:13px

    style Verify fill:#14532d,stroke:#4ade80,stroke-width:2px,color:#f1f5f9,font-size:13px
    style OpenSSL fill:#7f1d1d,stroke:#f87171,stroke-width:2px,color:#f1f5f9,font-size:13px
```

**Key Features**:
- ICAO 9303 PA Compliance (Part 10, 11, 12)
- **Dual PA Mode**: Server PA (full 8-step) + Client PA (Trust Materials API)
- SOD CMS Verification + DG Hash Validation
- Trust Chain Validation (icao::validation shared library)
- CRL Revocation Checking (RFC 5280)
- Client PA: CSCA/CRL/LC DER Base64 제공 → 클라이언트 로컬 PA → 결과+암호화 MRZ 보고
- DSC Auto-Registration (PA_EXTRACTED source type)
- DSC Non-Conformant (nc-data) Support
- MRZ Parsing (TD1/TD2/TD3) + Face Image Extraction (JPEG2000 conversion)
- ServiceContainer (pImpl DI), Handler Pattern: PaHandler (9), HealthHandler (3), InfoHandler (4)
- Multi-DBMS (PostgreSQL + Oracle)

---

### 3. PKD Relay Service (Port 8083)

```mermaid
flowchart LR
    subgraph API["API Layer"]
        RelayHealth["Relay Health<br/>Status"]
        RelayStatus["Relay Status<br/>Statistics"]
        IcaoCheck["ICAO Check<br/>Version"]
    end

    subgraph Domain["Domain Layer"]
        IcaoDomain["ICAO Domain<br/>Version Tracking"]
        RelayDomain["Relay Domain<br/>External Integration"]
    end

    subgraph Service["Service Layer"]
        IcaoService["ICAO Service<br/>HTML Parsing"]
        RelayService["Relay Service<br/>Request Relay"]
    end

    subgraph Repo["Repository Layer"]
        IcaoRepo["ICAO Repository<br/>PostgreSQL"]
    end

    subgraph Infra["Infrastructure"]
        HTTP["HTTP Client"]
        HTML["HTML Parser"]
        Email["Email Sender"]
    end

    subgraph Scheduler["Scheduler"]
        CronJob["Cron Job<br/>08:00 KST"]
    end

    subgraph DataStore["Data Store"]
        DB[("PostgreSQL")]
    end

    RelayHealth --> RelayService
    RelayStatus --> IcaoService
    IcaoCheck --> IcaoDomain

    IcaoDomain --> IcaoService
    RelayDomain --> RelayService

    IcaoService --> HTTP
    IcaoService --> HTML
    IcaoService --> Email
    IcaoService --> IcaoRepo

    IcaoRepo --> DB

    CronJob --> IcaoCheck

    style API fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Domain fill:#14532d,stroke:#4ade80,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Service fill:#312e81,stroke:#818cf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Repo fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Infra fill:#164e63,stroke:#22d3ee,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Scheduler fill:#0f4c75,stroke:#0ea5e9,stroke-width:2px,color:#f1f5f9,font-size:13px
    style DataStore fill:#7f1d1d,stroke:#f87171,stroke-width:2px,color:#f1f5f9,font-size:13px

    style IcaoCheck fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style IcaoDomain fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style IcaoService fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
```

**Key Features**:
- ICAO PKD external integration (Version Detection)
- HTML Scraping (Table + Link Fallback)
- DB-LDAP Reconciliation (CSCA, DSC, CRL)
- Daily Auto Version Check Scheduler
- ServiceContainer (pImpl DI), Handler Pattern: SyncHandler (10), ReconciliationHandler (4), HealthHandler (1)
- SyncScheduler with callback-based DI
- Clean Architecture (4 Layers)
- Multi-DBMS (PostgreSQL + Oracle)

---

### 4. Monitoring Service (Port 8084)

**Key Features**:
- System Resource Monitoring (CPU, Memory, Disk, Network)
- Service Health Checks (HTTP Probes to all backend services)
- DB-Independent Architecture (no PostgreSQL/Oracle dependency)
- Handler Pattern: MonitoringHandler (3 endpoints + SystemMetricsCollector + ServiceHealthChecker)
- JSON Metrics API

---

## Data Layer Architecture

### Database Schema

> **Multi-DBMS**: PostgreSQL 15 / Oracle XE 21c 동시 지원. 아래 스키마는 DB-agnostic 표현이며, 런타임에 `DB_TYPE` 환경변수로 선택됩니다.

#### Core — 인증서 & 검증

```mermaid
erDiagram
    UPLOADED_FILE {
        UUID id PK
    }
    CERTIFICATE {
        UUID id PK
        UUID upload_id FK
    }
    CRL {
        UUID id PK
        UUID upload_id FK
    }
    MASTER_LIST {
        UUID id PK
        UUID upload_id FK
    }
    DEVIATION_LIST {
        UUID id PK
        UUID upload_id FK
    }
    VALIDATION_RESULT {
        UUID id PK
        UUID certificate_id FK
        UUID upload_id FK
    }
    LINK_CERTIFICATE {
        UUID id PK
        UUID upload_id FK
    }
    DUPLICATE_CERTIFICATE {
        UUID id PK
        UUID upload_id FK
    }
    REVOKED_CERTIFICATE {
        UUID id PK
        UUID crl_id FK
    }

    UPLOADED_FILE ||--o{ CERTIFICATE : "1:N"
    UPLOADED_FILE ||--o{ CRL : "1:N"
    UPLOADED_FILE ||--o{ MASTER_LIST : "1:N"
    UPLOADED_FILE ||--o{ DEVIATION_LIST : "1:N"
    UPLOADED_FILE ||--o{ LINK_CERTIFICATE : "1:N"
    UPLOADED_FILE ||--o{ DUPLICATE_CERTIFICATE : "1:N"
    CERTIFICATE ||--o{ VALIDATION_RESULT : "1:N"
    UPLOADED_FILE ||--o{ VALIDATION_RESULT : "1:N"
    CRL ||--o{ REVOKED_CERTIFICATE : "1:N"
```

#### PA & Sync & API

```mermaid
erDiagram
    PA_VERIFICATION {
        UUID id PK
    }
    PA_DATA_GROUP {
        UUID id PK
        UUID verification_id FK
    }
    PENDING_DSC_REGISTRATION {
        UUID id PK
    }
    SYNC_STATUS {
        INTEGER id PK
    }
    RECONCILIATION_SUMMARY {
        UUID id PK
        INTEGER sync_status_id FK
    }
    RECONCILIATION_LOG {
        INTEGER id PK
        UUID summary_id FK
    }
    USERS {
        UUID id PK
    }
    AUTH_AUDIT_LOG {
        UUID id PK
        UUID user_id FK
    }
    OPERATION_AUDIT_LOG {
        UUID id PK
        UUID user_id FK
    }
    API_CLIENTS {
        UUID id PK
        UUID created_by FK
    }
    API_CLIENT_USAGE_LOG {
        UUID id PK
        UUID client_id FK
    }
    API_CLIENT_REQUESTS {
        UUID id PK
        UUID reviewed_by FK
    }
    CSR_REQUEST {
        UUID id PK
    }
    AI_ANALYSIS_RESULT {
        UUID id PK
    }
    CVC_CERTIFICATE {
        UUID id PK
    }
    EAC_TRUST_CHAIN {
        UUID id PK
        UUID is_certificate_id FK
    }

    PA_VERIFICATION ||--o{ PA_DATA_GROUP : "1:N"
    SYNC_STATUS ||--o{ RECONCILIATION_SUMMARY : "1:N"
    RECONCILIATION_SUMMARY ||--o{ RECONCILIATION_LOG : "1:N"
    USERS ||--o{ AUTH_AUDIT_LOG : "1:N"
    USERS ||--o{ OPERATION_AUDIT_LOG : "1:N"
    USERS ||--o{ API_CLIENTS : "created_by"
    USERS ||--o{ API_CLIENT_REQUESTS : "reviewed_by"
    API_CLIENTS ||--o{ API_CLIENT_USAGE_LOG : "1:N"
    CVC_CERTIFICATE ||--o{ EAC_TRUST_CHAIN : "1:N"
```

**총 37개 테이블** — 15개 카테고리:

| 카테고리 | 테이블 | 설명 |
|---------|--------|------|
| **인증서 관리** | uploaded_file, certificate, crl, master_list, deviation_list, deviation_entry | 업로드, X.509 인증서, CRL, Master List, Deviation List |
| **인증서 검증** | validation_result, revoked_certificate | 신뢰 체인 검증, CRL 폐지 확인 |
| **중복 추적** | duplicate_certificate, certificate_duplicates | 업로드별 중복 인증서 추적 |
| **Link Certificate** | link_certificate, link_certificate_issuers | CSCA 키 전환 Link 인증서 |
| **PA 검증** | pa_verification, pa_data_group | Passive Authentication 요청/결과, DG 해시 |
| **DSC 등록** | pending_dsc_registration | PA 자동 등록 → 관리자 승인 워크플로 |
| **동기화** | sync_status, sync_config, reconciliation_summary, reconciliation_log, revalidation_history, crl_revocation_log | DB↔LDAP 동기화, 재검증, CRL 체크 |
| **LDAP 마이그레이션** | ldap_dn_migration_status | V2 DN 마이그레이션 진행 추적 |
| **감사** | audit_log, auth_audit_log, operation_audit_log | 인증, 업무 운영 감사 로그 |
| **사용자** | users | RBAC 사용자 계정 |
| **API 클라이언트** | api_clients, api_client_usage_log, api_client_requests | 외부 API 키 관리, 사용량 추적, 접근 신청 |
| **코드 관리** | code_master | 중앙 코드/상태/열거형 관리 (21개 카테고리) |
| **ICAO 버전** | icao_pkd_versions | ICAO PKD 자동 동기화 버전 추적 |
| **CSR** | csr_request | CSR 생성 + 암호화된 개인키 + 발급 인증서 |
| **AI 분석** | ai_analysis_result | ML 이상 탐지, 포렌식 위험도 |
| **EAC/CVC** | cvc_certificate, eac_trust_chain | BSI TR-03110 CVC 인증서 (실험적) |
| **모니터링** | system_metrics, service_health | 시스템 성능, 서비스 상태 |

---

#### 테이블 상세 명세

##### 1. Core — 인증서 관리

**uploaded_file** — 업로드 파일 추적

| 컬럼 | 타입 | 설명 |
|------|------|------|
| id | UUID PK | |
| file_name | VARCHAR(255) | 저장 파일명 |
| original_file_name | VARCHAR(255) | 원본 파일명 |
| file_size | BIGINT | 바이트 |
| file_hash | VARCHAR(64) | SHA-256 해시 (중복 감지) |
| file_format | VARCHAR(20) | LDIF, ML, PEM, DER, CER, P7B, DL, CRL |
| status | VARCHAR(30) | PENDING → PROCESSING → COMPLETED/FAILED |
| processing_mode | VARCHAR(10) | AUTO |
| upload_timestamp | TIMESTAMP | |
| csca_count / dsc_count / dsc_nc_count / crl_count / ml_count | INTEGER | 처리 통계 |
| validation_valid_count / invalid_count / pending_count / error_count | INTEGER | 검증 통계 |
| trust_chain_valid_count / invalid_count | INTEGER | 신뢰 체인 통계 |
| icao_compliant_count / non_compliant_count / warning_count | INTEGER | ICAO 준수 통계 |

**certificate** — X.509 인증서 (CSCA, DSC, DSC_NC, MLSC)

| 컬럼 | 타입 | 설명 |
|------|------|------|
| id | UUID PK | |
| upload_id | UUID FK | → uploaded_file |
| certificate_type | VARCHAR(20) | CSCA, DSC, DSC_NC, MLSC |
| country_code | VARCHAR(3) | ISO 3166-1 alpha-2/3 |
| fingerprint_sha256 | VARCHAR(64) UK | 고유 식별자 (type + fingerprint) |
| subject_dn / issuer_dn | TEXT | X.500 Distinguished Name |
| serial_number | VARCHAR(100) | |
| certificate_data | BLOB | DER 인코딩 바이너리 |
| not_before / not_after | TIMESTAMP | 유효 기간 |
| signature_algorithm | VARCHAR(50) | SHA256withRSA, SHA256withECDSA 등 |
| public_key_algorithm | VARCHAR(30) | RSA, EC |
| public_key_size | INTEGER | 2048, 4096, 256, 384 |
| public_key_curve | VARCHAR(50) | P-256, brainpoolP256r1 등 |
| is_self_signed / is_ca | BOOLEAN | 자체 서명 / CA 여부 |
| validation_status | VARCHAR(20) | VALID, INVALID, PENDING, EXPIRED, REVOKED |
| source_type | VARCHAR(50) | FILE_UPLOAD, PA_EXTRACTED, LDIF_PARSED, ML_PARSED, DL_PARSED |
| stored_in_ldap | BOOLEAN | LDAP 저장 여부 |
| ldap_dn_v2 | VARCHAR(512) | LDAP DN (V2 정규화) |

**crl** — 인증서 폐지 목록

| 컬럼 | 타입 | 설명 |
|------|------|------|
| id | UUID PK | |
| upload_id | UUID FK | → uploaded_file |
| country_code | VARCHAR(3) | |
| issuer_dn | TEXT | CRL 발행자 |
| fingerprint_sha256 | VARCHAR(64) UK | |
| this_update / next_update | TIMESTAMP | CRL 유효 기간 |
| crl_binary | BLOB | DER 인코딩 |
| stored_in_ldap | BOOLEAN | |

**master_list** — ICAO Master List (CMS SignedData)

| 컬럼 | 타입 | 설명 |
|------|------|------|
| id | UUID PK | |
| upload_id | UUID FK | → uploaded_file |
| signer_country | VARCHAR(3) | Master List 서명 국가 |
| fingerprint_sha256 | VARCHAR(64) | |
| csca_certificate_count | INTEGER | 포함 CSCA 수 |
| signer_certificate_id | UUID FK | → certificate (MLSC) |
| stored_in_ldap | BOOLEAN | |

**deviation_list / deviation_entry** — ICAO Deviation List

| 컬럼 | 타입 | 설명 |
|------|------|------|
| deviation_list.id | UUID PK | |
| issuer_country | VARCHAR(3) | |
| deviation_count | INTEGER | 포함 결함 수 |
| deviation_entry.defect_type_oid | VARCHAR(50) | OID |
| deviation_entry.defect_category | VARCHAR(20) | |

##### 2. 검증 & 신뢰 체인

**validation_result** — 인증서 신뢰 체인 검증 결과

| 컬럼 | 타입 | 설명 |
|------|------|------|
| id | UUID PK | |
| certificate_id | UUID FK | → certificate |
| upload_id | UUID FK | → uploaded_file |
| validation_status | VARCHAR(20) | VALID, INVALID, PENDING 등 |
| trust_chain_valid | BOOLEAN | CSCA → DSC 체인 |
| csca_found / signature_valid / validity_period_valid | BOOLEAN | 개별 검증 결과 |
| revocation_status | VARCHAR(20) | OK, REVOKED, UNKNOWN |
| icao_compliant | BOOLEAN | Doc 9303 준수 여부 |
| icao_violations | TEXT | 미준수 항목 |
| trust_chain_path | JSON | 체인 경로 |

**link_certificate** — CSCA 키 전환 Link Certificate

| 컬럼 | 타입 | 설명 |
|------|------|------|
| id | UUID PK | |
| country_code | VARCHAR(3) | |
| fingerprint_sha256 | VARCHAR(64) UK | |
| subject_dn / issuer_dn | TEXT | 신/구 CSCA DN |
| certificate_data | BLOB | |
| stored_in_ldap | BOOLEAN | |

##### 3. PA 검증

**pa_verification** — Passive Authentication 검증

| 컬럼 | 타입 | 설명 |
|------|------|------|
| id | UUID PK | |
| issuing_country | VARCHAR(3) | |
| document_number | VARCHAR(1024) | **PII 암호화** (AES-256-GCM) |
| verification_status | VARCHAR(30) | VALID, INVALID, ERROR, EXPIRED_VALID |
| trust_chain_valid / sod_signature_valid / dg_hashes_valid | BOOLEAN | 8단계 검증 결과 |
| crl_status | VARCHAR(20) | |
| dsc_fingerprint / csca_fingerprint | VARCHAR(64) | 사용된 인증서 |
| client_ip / user_agent | VARCHAR(1024) | **PII 암호화** |
| processing_time_ms | INTEGER | 처리 시간 |

**pa_data_group** — SOD Data Group 해시

| 컬럼 | 타입 | 설명 |
|------|------|------|
| verification_id | UUID FK | → pa_verification |
| dg_number | INTEGER | 1~16 |
| expected_hash / actual_hash | VARCHAR(128) | 기대값 vs 실제값 |
| hash_valid | BOOLEAN | |

##### 4. 동기화 & 감사

**sync_status** — DB↔LDAP 동기화 상태

| 컬럼 | 타입 | 설명 |
|------|------|------|
| db_csca_count / db_dsc_count / db_crl_count | INTEGER | DB 측 수량 |
| ldap_csca_count / ldap_dsc_count / ldap_crl_count | INTEGER | LDAP 측 수량 |
| total_discrepancy | INTEGER | 불일치 합계 |
| db_country_stats / ldap_country_stats | JSON | 국가별 통계 |

**auth_audit_log** — 인증 감사 로그

| 컬럼 | 타입 | 설명 |
|------|------|------|
| event_type | VARCHAR(50) | LOGIN, LOGOUT, TOKEN_REFRESH 등 |
| username | VARCHAR(255) | |
| ip_address | VARCHAR(45) | |
| success | BOOLEAN | |

**operation_audit_log** — 업무 운영 감사 로그

| 컬럼 | 타입 | 설명 |
|------|------|------|
| operation_type | VARCHAR(50) | UPLOAD, EXPORT, SYNC, CSR_CREATE 등 |
| resource_type / resource_id | VARCHAR | 대상 엔티티 |
| username | VARCHAR(255) | |
| metadata | JSON | 추가 상세 |

##### 5. API & 사용자

**api_clients** — 외부 API 클라이언트

| 컬럼 | 타입 | 설명 |
|------|------|------|
| api_key_hash | VARCHAR(64) UK | SHA-256 해시 (원본 키 미저장) |
| api_key_prefix | VARCHAR(16) | 식별용 접두사 |
| permissions | JSON | 12개 권한 배열 |
| rate_limit_per_minute / hour / day | INTEGER | Rate Limit 설정 |
| allowed_ips | JSON | IP 화이트리스트 |

**csr_request** — ICAO PKD CSR 관리

| 컬럼 | 타입 | 설명 |
|------|------|------|
| subject_dn | VARCHAR(1024) | CSR Subject |
| key_algorithm | VARCHAR(20) | RSA-2048, RSA-4096, EC-P256 등 |
| csr_pem / csr_der | TEXT / BLOB | CSR 데이터 |
| private_key_encrypted | TEXT | **AES-256-GCM 암호화** |
| issued_certificate_pem | TEXT | 발급된 인증서 (선택) |
| status | VARCHAR(20) | CREATED → SUBMITTED → ISSUED |

##### 6. AI & EAC

**ai_analysis_result** — ML 이상 탐지

| 컬럼 | 타입 | 설명 |
|------|------|------|
| certificate_fingerprint | VARCHAR(128) UK | |
| anomaly_score | FLOAT | Isolation Forest 점수 |
| risk_score / risk_level | FLOAT / VARCHAR | 포렌식 위험도 |
| forensic_findings | JSON | 위험 요인 상세 |

**cvc_certificate** — BSI TR-03110 CVC (실험적)

| 컬럼 | 타입 | 설명 |
|------|------|------|
| cvc_type | VARCHAR(20) | CVCA, DV, IS |
| car / chr | VARCHAR(255) | Certificate Authority/Holder Reference |
| chat_oid / chat_role | VARCHAR | 인증서 권한 |
| fingerprint_sha256 | VARCHAR(128) UK | |
| validation_status | VARCHAR(20) | |

---

### LDAP Directory Structure

```mermaid
graph TD
    Root[dc=ldap,dc=smartcoreinc,dc=com]
    PKD[dc=pkd]
    Download[dc=download]
    Data[dc=data]
    NCData[dc=nc-data]

    Root --> PKD
    PKD --> Download
    Download --> Data
    Download --> NCData

    subgraph "Data Branch"
        Data --> C1[c=KR]
        Data --> C2[c=US]
        Data --> C3[c=JP]
        Data --> CN[c=... 193 countries]

        C1 --> CSCA1[o=csca<br/>CSCA Certificates]
        C1 --> DSC1[o=dsc<br/>DSC Certificates]
        C1 --> CRL1[o=crl<br/>Certificate Revocation Lists]
        C1 --> ML1[o=ml<br/>Master Lists]
    end

    subgraph "NC-Data Branch"
        NCData --> NC1[c=KR]
        NCData --> NC2[c=US]
        NCData --> NCN[c=... countries]

        NC1 --> DSCNC[o=dsc<br/>Non-Conformant DSC]
    end

    style Root fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Data fill:#14532d,stroke:#4ade80,stroke-width:2px,color:#f1f5f9,font-size:13px
    style NCData fill:#7f1d1d,stroke:#f87171,stroke-width:2px,color:#f1f5f9,font-size:13px
    style CSCA1 fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style DSC1 fill:#164e63,stroke:#22d3ee,stroke-width:2px,color:#f1f5f9,font-size:13px
    style CRL1 fill:#312e81,stroke:#818cf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style ML1 fill:#0f4c75,stroke:#0ea5e9,stroke-width:2px,color:#f1f5f9,font-size:13px
```

**LDAP Schema**:
- **objectClass**: pkdDownload, cRLDistributionPoint
- **Attributes**: userCertificate;binary, cACertificate;binary, certificateRevocationList;binary
- **Total Entries**: 31,212 (845 CSCA + 27 MLSC + 29,838 DSC + 502 DSC_NC + 69 CRL)

---

## Frontend Architecture

```mermaid
graph TD
    subgraph "React Application Structure"
        subgraph "Entry Point"
            Main[main.tsx<br/>React 19 Root]
            App[App.tsx<br/>Router and Layout]
        end

        subgraph "Layout Components"
            Sidebar[Sidebar.tsx<br/>Navigation Menu]
            Header[Header.tsx<br/>User Info and Theme]
        end

        subgraph "Page Components (29 Pages)"
            Dashboard[Dashboard.tsx<br/>System Overview]
            FileUpload[FileUpload.tsx<br/>File Upload]
            CertUpload[CertificateUpload.tsx<br/>Individual Certificate]
            CertSearch[CertificateSearch.tsx<br/>Certificate Search]
            DscNcReport[DscNcReport.tsx<br/>DSC_NC Report]
            UploadHistory[UploadHistory.tsx<br/>Upload History]
            UploadDetail[UploadDetail.tsx<br/>Upload Detail]
            UploadDashboard[UploadDashboard.tsx<br/>Statistics Dashboard]
            PAVerify[PAVerify.tsx<br/>PA Verification]
            PAHistory[PAHistory.tsx<br/>Verification History]
            PADetail[PADetail.tsx<br/>PA Detail]
            PADashboard[PADashboard.tsx<br/>PA Statistics]
            SyncDashboard[SyncDashboard.tsx<br/>Sync Status]
            IcaoStatus[IcaoStatus.tsx<br/>ICAO Version Status]
            SystemMonitoring[MonitoringDashboard.tsx<br/>System Monitoring]
            Login[Login.tsx<br/>JWT Authentication]
            UserMgmt[UserManagement.tsx<br/>User Management]
            AuditLog[AuditLog.tsx<br/>Auth Audit]
            OpAuditLog[OperationAuditLog.tsx<br/>Operation Audit]
            CsrMgmt[CsrManagement.tsx<br/>CSR Management]
            PendingDsc[PendingDscApproval.tsx<br/>DSC Registration Approval]
            ApiClientMgmt[ApiClientManagement.tsx<br/>API Client]
            ApiClientReq[ApiClientRequest.tsx<br/>API Access Request]
            AiAnalysis[AiAnalysisDashboard.tsx<br/>AI Analysis]
            CrlReport2[CrlReport.tsx<br/>CRL Report]
            TrustChain[TrustChainValidationReport.tsx<br/>Trust Chain Report]
            CertQuality[CertificateQualityReport.tsx<br/>Certificate Quality]
            EacDash[EacDashboard.tsx<br/>EAC Certificates]
            ProfilePage[Profile.tsx<br/>User Profile]
        end

        subgraph "Shared Components"
            Button[Button.tsx]
            Card[Card.tsx]
            Table[Table.tsx]
            Modal[Modal.tsx]
            Badge[Badge.tsx]
            Alert[Alert.tsx]
        end

        subgraph "Utils & Hooks"
            CountryCode[countryCode.ts<br/>ISO 3166 Converter]
            FlagUtils[Flag SVG Utils<br/>Flag Icons]
            APIClient[API Client<br/>Axios Wrapper]
        end

        subgraph "Styling"
            TailwindCSS[TailwindCSS 4<br/>Utility-first CSS]
            DarkMode[Dark Mode Support<br/>Theme Provider]
        end
    end

    Main --> App
    App --> Sidebar
    App --> Header
    App --> Dashboard
    App --> FileUpload
    App --> CertSearch
    App --> UploadHistory
    App --> UploadDashboard
    App --> PAVerify
    App --> PAHistory
    App --> PADashboard
    App --> SyncDashboard
    App --> IcaoStatus
    App --> SystemMonitoring

    Dashboard --> Card
    FileUpload --> Button
    CertSearch --> Table
    UploadHistory --> Modal
    PAVerify --> Alert
    IcaoStatus --> Badge

    CertSearch --> CountryCode
    IcaoStatus --> FlagUtils
    PAVerify --> APIClient

    App --> TailwindCSS
    App --> DarkMode

    style IcaoStatus fill:#78350f,stroke:#fbbf24,stroke-width:3px,color:#f1f5f9,font-size:13px
    style FlagUtils fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
```

**Build Stack**:
- **Bundler**: Vite 5
- **Language**: TypeScript 5
- **UI Framework**: React 19
- **Styling**: TailwindCSS 4
- **Icons**: Lucide React
- **HTTP Client**: Axios
- **State Management**: React Hooks (useState, useEffect)

---

## API Gateway Architecture

```mermaid
graph TB
    subgraph Gateway["Nginx API Gateway Port 8080"]
        subgraph Routes["Routing Rules"]
            Route1["PKD Route<br/>upload/cert/health/sync"]
            Route2["PA Route<br/>pa/*"]
            Route3["Relay Route<br/>relay/*"]
            Route4["API Docs<br/>api-docs"]
        end

        subgraph Features["Key Features"]
            RateLimit["Rate Limiting<br/>100 req/s per IP"]
            Gzip["Gzip Compression<br/>80% reduction"]
            SSE["SSE Support<br/>Real-time"]
            Upload["File Upload<br/>Max 100MB"]
            Swagger["Swagger UI<br/>OpenAPI 3.0"]
        end

        subgraph Proxy["Proxy Configuration"]
            Timeout["Timeout<br/>30s / 300s"]
            Buffer["Buffer<br/>8 x 16KB"]
            Keepalive["Keepalive<br/>32 connections"]
        end

        subgraph Errors["Error Handling"]
            Error502["502 Bad Gateway"]
            Error503["503 Unavailable"]
            Error504["504 Timeout"]
        end
    end

    Route1 --> PKD["PKD Management<br/>8081"]
    Route2 --> PA["PA Service<br/>8082"]
    Route3 --> RelaySvc["Relay Service<br/>8083"]
    Route4 --> Swagger

    RateLimit -.-> Route1
    RateLimit -.-> Route2
    RateLimit -.-> Route3

    Gzip -.-> PKD
    Gzip -.-> PA
    Gzip -.-> RelaySvc

    SSE -.-> PKD
    Upload -.-> PKD

    style Route1 fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style RateLimit fill:#7f1d1d,stroke:#f87171,stroke-width:2px,color:#f1f5f9,font-size:13px
    style SSE fill:#312e81,stroke:#818cf8,stroke-width:2px,color:#f1f5f9,font-size:13px
```

**Security Features**:
- Backend Service Isolation (Internal Network Only)
- Rate Limiting (DDoS Protection)
- Header Sanitization
- CORS Policy
- Request/Response Logging

---

## Component Details

### LDIF Processor

```mermaid
graph LR
    Input[LDIF File<br/>30k+ entries]

    subgraph "Parsing Stage"
        Read[File Reader<br/>Stream Processing]
        Parse[LDIF Parser<br/>Entry Extraction]
        Validate[Entry Validator<br/>DN and Attributes]
    end

    subgraph "Classification Stage"
        Detect[Certificate Type Detector<br/>objectClass Analysis]
        Extract[Attribute Extractor<br/>Binary Data]
        Country[Country Code Extractor<br/>DN Parsing]
    end

    subgraph "Processing Stage"
        AutoProc[Auto Processing<br/>One-shot to DB and LDAP]
        ManualProc[Manual Processing<br/>3-Stage Workflow]
    end

    Output[(Database<br/>LDAP)]

    Input --> Read --> Parse --> Validate
    Validate --> Detect --> Extract --> Country
    Country --> AutoProc
    Country --> ManualProc
    AutoProc --> Output
    ManualProc --> Output

    style Input fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Detect fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Output fill:#14532d,stroke:#4ade80,stroke-width:2px,color:#f1f5f9,font-size:13px
```

---

### ICAO Auto Sync Flow (v1.7.0)

```mermaid
sequenceDiagram
    participant Cron as Cron Job<br/>(Daily 08:00)
    participant Script as Shell Script<br/>icao-version-check.sh
    participant API as API Gateway<br/>:8080
    participant PKD as PKD Management<br/>:8081
    participant HTTP as HTTP Client<br/>ICAO Portal
    participant Parser as HTML Parser<br/>Version Extractor
    participant DB as PostgreSQL<br/>icao_pkd_versions
    participant Dashboard as React Frontend<br/>ICAO Version Status

    Cron->>Script: Execute daily
    Script->>API: POST /api/icao/check-updates
    API->>PKD: Forward request

    PKD->>HTTP: Fetch ICAO portal HTML
    HTTP-->>PKD: HTML content

    PKD->>Parser: Parse HTML tables
    Parser-->>PKD: Version list<br/>(Collection 001/002/003)

    PKD->>DB: Query existing versions
    DB-->>PKD: Current version records

    PKD->>PKD: Compare versions

    alt New version detected
        PKD->>DB: INSERT new version<br/>status=DETECTED
        PKD->>Dashboard: Notify (SSE/Polling)
        Dashboard-->>Dashboard: Show UPDATE_NEEDED badge
    else No new version
        PKD->>Dashboard: Notify (no change)
        Dashboard-->>Dashboard: Show UP_TO_DATE badge
    end

    Script->>API: GET /api/icao/latest
    API->>PKD: Forward request
    PKD->>DB: Query latest versions
    DB-->>PKD: Latest version list
    PKD-->>API: JSON response
    API-->>Script: Latest versions
    Script->>Script: Log to file
```

---

## Data Flow Diagrams

### Upload Flow (AUTO Mode)

```mermaid
sequenceDiagram
    participant User
    participant Frontend
    participant Gateway as API Gateway
    participant PKD as PKD Management
    participant LDIF as LDIF Processor
    participant DB as PostgreSQL/Oracle
    participant LDAP as OpenLDAP

    User->>Frontend: Select LDIF file + AUTO mode
    Frontend->>Gateway: POST /api/upload/ldif<br/>(multipart/form-data)
    Gateway->>PKD: Forward upload

    PKD->>PKD: Generate UUID<br/>Save temp file
    PKD->>DB: INSERT uploaded_file<br/>status=PROCESSING
    PKD-->>Frontend: Upload ID + SSE URL

    Frontend->>Gateway: GET /api/progress/stream/{id}<br/>(SSE)

    PKD->>LDIF: Parse LDIF entries
    loop For each entry
        LDIF->>LDIF: Extract DN, attributes
        LDIF->>LDIF: Classify cert type
        LDIF-->>PKD: SSE progress update
        PKD-->>Frontend: PARSING_{percentage}
    end

    LDIF-->>PKD: Parsed entries (30k+)

    PKD->>DB: BEGIN TRANSACTION
    PKD->>DB: INSERT certificates (Batch 1000)
    PKD->>DB: INSERT crls
    PKD->>DB: INSERT master_lists

    PKD->>LDAP: LDAP BIND (write to primary)
    PKD->>LDAP: ADD entries (Batch 100)
    PKD->>DB: UPDATE stored_in_ldap=true

    PKD->>DB: Trust Chain Validation
    PKD->>DB: INSERT validation_result

    PKD->>DB: UPDATE uploaded_file<br/>status=COMPLETED
    PKD->>DB: COMMIT TRANSACTION

    PKD-->>Frontend: COMPLETED<br/>SSE close
    Frontend-->>User: Show success + stats
```

---

### PA Verification Flow

```mermaid
sequenceDiagram
    participant User
    participant Frontend
    participant Gateway
    participant PA as PA Service
    participant LDAP
    participant DB

    User->>Frontend: Upload SOD + DG files
    Frontend->>Gateway: POST /api/pa/verify<br/>(JSON payload)
    Gateway->>PA: Forward request

    PA->>PA: Step 1: Parse SOD<br/>(CMS d2i_CMS_bio)
    PA->>PA: Step 2: Extract signer info
    PA->>PA: Step 3: Extract DG hashes

    PA->>LDAP: Search DSC certificate<br/>by issuer DN
    LDAP-->>PA: DSC certificate DER

    PA->>PA: Step 4: Verify SOD signature<br/>(CMS_verify)

    PA->>LDAP: Search CSCA certificate<br/>by DSC issuer DN
    LDAP-->>PA: CSCA certificate DER

    PA->>PA: Step 5: Verify Trust Chain<br/>(X509_verify)

    PA->>PA: Step 6: Calculate DG hashes<br/>(SHA-256/SHA-384)

    PA->>PA: Step 7: Compare hashes<br/>(SOD vs Calculated)

    PA->>PA: Step 8: Check validity periods

    PA->>DB: INSERT pa_verification<br/>(all steps + results)

    PA-->>Gateway: JSON response<br/>(success + details)
    Gateway-->>Frontend: Verification result
    Frontend-->>User: Show step-by-step UI
```

---

## Deployment Architecture

### Docker Compose Architecture

```mermaid
graph TB
    subgraph External["External Access"]
        User["User Browser<br/>Port 3000"]
        APIClient["API Client<br/>Port 8080"]
        LDAPClient["LDAP Client<br/>Port 389"]
    end

    subgraph DockerNetwork["Docker Network: icao-network"]
        subgraph Presentation["Presentation Layer"]
            Frontend["Frontend<br/>nginx:alpine<br/>React 19 SPA<br/>:3000"]
        end

        subgraph Gateway["Gateway Layer"]
            APIGateway["API Gateway<br/>nginx:1.25<br/>Reverse Proxy<br/>:8080"]
        end

        subgraph Application["Application Layer"]
            PKD["PKD Management<br/>Custom C++<br/>Drogon 1.9<br/>:8081"]
            PA["PA Service<br/>Custom C++<br/>Drogon 1.9<br/>:8082"]
            Relay["PKD Relay<br/>Custom C++<br/>Drogon 1.9<br/>:8083"]
            Mon["Monitoring<br/>Custom C++<br/>Drogon 1.9<br/>:8084"]
        end

        subgraph DataLayer["Data Layer"]
            PG["PostgreSQL / Oracle<br/>postgres:15 / xe:21c<br/>Multi-DBMS<br/>:5432 / :1521"]
            LDAP1["OpenLDAP 1<br/>osixia:1.5.0<br/>MMR Primary<br/>:3891"]
            LDAP2["OpenLDAP 2<br/>osixia:1.5.0<br/>MMR Secondary<br/>:3892"]
        end
    end

    subgraph Storage["Persistent Storage"]
        PGData[("postgres<br/>Database Files")]
        LDAP1Data[("openldap1<br/>Directory Data")]
        LDAP2Data[("openldap2<br/>Directory Data")]
        UploadData[("pkd-uploads<br/>LDIF/ML Files")]
        LogData[("logs<br/>Service Logs")]
    end

    %% External to Docker Network
    User -->|HTTP/3000| Frontend
    APIClient -->|HTTP/8080| APIGateway

    %% Presentation to Gateway
    Frontend -->|proxy_pass| APIGateway

    %% Gateway to Application
    APIGateway -->|/api/upload<br/>/api/cert<br/>/api/auth<br/>/api/icao| PKD
    APIGateway -->|/api/pa/*| PA
    APIGateway -->|/api/sync/*| Relay
    APIGateway -->|/api/monitoring/*| Mon

    %% Application to Data Layer
    PKD -->|SQL| PG
    PA -->|SQL| PG
    Relay -->|SQL| PG

    PKD -->|LDAP Write| LDAP1
    PKD -->|LDAP Read<br/>App SLB| LDAP1
    PKD -->|LDAP Read<br/>App SLB| LDAP2
    PA -->|LDAP Read<br/>App SLB| LDAP1
    PA -->|LDAP Read<br/>App SLB| LDAP2
    Relay -->|LDAP Read<br/>App SLB| LDAP1
    Relay -->|LDAP Read<br/>App SLB| LDAP2

    %% Data Layer Replication
    LDAP1 <-->|MMR Sync| LDAP2

    %% Data Layer to Storage
    PG -->|bind mount| PGData
    LDAP1 -->|bind mount| LDAP1Data
    LDAP2 -->|bind mount| LDAP2Data
    PKD -->|bind mount| UploadData
    PKD -->|bind mount| LogData

    %% Styling - External
    style User fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style APIClient fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-size:13px

    %% Styling - Presentation
    style Frontend fill:#14532d,stroke:#4ade80,stroke-width:3px,color:#f1f5f9,font-size:13px

    %% Styling - Gateway
    style APIGateway fill:#78350f,stroke:#fbbf24,stroke-width:3px,color:#f1f5f9,font-size:13px

    %% Styling - Application
    style PKD fill:#164e63,stroke:#22d3ee,stroke-width:3px,color:#f1f5f9,font-size:13px
    style PA fill:#164e63,stroke:#22d3ee,stroke-width:3px,color:#f1f5f9,font-size:13px
    style Relay fill:#164e63,stroke:#22d3ee,stroke-width:3px,color:#f1f5f9,font-size:13px
    style Mon fill:#164e63,stroke:#22d3ee,stroke-width:3px,color:#f1f5f9,font-size:13px

    %% Styling - Data Layer
    style PG fill:#312e81,stroke:#818cf8,stroke-width:3px,color:#f1f5f9,font-size:13px
    style LDAP1 fill:#0f4c75,stroke:#0ea5e9,stroke-width:3px,color:#f1f5f9,font-size:13px
    style LDAP2 fill:#0f4c75,stroke:#0ea5e9,stroke-width:3px,color:#f1f5f9,font-size:13px

    %% Styling - Storage
    style PGData fill:#3f3f46,stroke:#a1a1aa,stroke-width:2px,color:#f1f5f9,font-size:13px
    style LDAP1Data fill:#3f3f46,stroke:#a1a1aa,stroke-width:2px,color:#f1f5f9,font-size:13px
    style LDAP2Data fill:#3f3f46,stroke:#a1a1aa,stroke-width:2px,color:#f1f5f9,font-size:13px
    style UploadData fill:#3f3f46,stroke:#a1a1aa,stroke-width:2px,color:#f1f5f9,font-size:13px
    style LogData fill:#3f3f46,stroke:#a1a1aa,stroke-width:2px,color:#f1f5f9,font-size:13px
```

**Architecture Highlights**:

1. **Layered Design**: Clear 4-layer structure (Presentation -> Gateway -> Application -> Data)
2. **Gateway Pattern**: API Gateway (nginx) for service routing and reverse proxy
3. **App-Level SLB**: LDAP Software Load Balancing (in-service round-robin, HAProxy removed)
4. **Microservices**: 4 independent C++ services (PKD, PA, Relay, Monitoring)
5. **Multi-DBMS**: PostgreSQL 15 / Oracle XE 21c runtime switching (DB_TYPE)
6. **MMR Replication**: OpenLDAP Multi-Master replication for high availability
7. **Bind Mounts**: All data permanently stored on host filesystem

**Container Details**:

| Container | Image | CPU | Memory | Restart |
|-----------|-------|-----|--------|---------|
| frontend | nginx:alpine + React build | 0.5 | 256MB | always |
| api-gateway | nginx:1.25-alpine | 0.5 | 256MB | always |
| pkd-management | Custom C++ (Debian) | 2.0 | 2GB | always |
| pa-service | Custom C++ (Debian) | 2.0 | 2GB | always |
| pkd-relay | Custom C++ (Debian) | 1.0 | 1GB | always |
| monitoring | Custom C++ (Debian) | 0.5 | 256MB | always |
| postgres | postgres:15-alpine | 2.0 | 2GB | always |
| openldap1 | osixia/openldap:1.5.0 | 1.0 | 1GB | always |
| openldap2 | osixia/openldap:1.5.0 | 1.0 | 1GB | always |

**Total Resources**: 10 cores, 10.5GB RAM (PostgreSQL mode), +2GB for Oracle container

---

### Luckfox ARM64 Deployment

```mermaid
graph TB
    subgraph CICD["CI/CD Pipeline"]
        GHA["GitHub Actions<br/>ARM64 Build<br/>QEMU + Buildx<br/>~2 hours"]
        Artifacts["Artifacts<br/>OCI Format<br/>tar.gz<br/>30 days"]
        Convert["skopeo<br/>OCI to Docker<br/>override-arch<br/>~30 sec"]
    end

    subgraph Deploy["Deployment"]
        Transfer["sshpass<br/>SCP Transfer<br/>to Luckfox<br/>~2 min"]
        Load["Docker Load<br/>Import Images<br/>docker load<br/>~1 min"]
    end

    subgraph Luckfox["Luckfox Pico ARM64 - 192.168.100.11"]
        subgraph Network["Host Network Mode"]
            Frontend3["Frontend<br/>:3000"]
            APIGateway3["API Gateway<br/>:8080"]
            PKD3["PKD Mgmt<br/>:8081"]
            PA3["PA Service<br/>:8082"]
            Relay3["PKD Relay<br/>:8083"]
            Mon3["Monitoring<br/>:8084"]
            PG3["PostgreSQL<br/>:5432"]
            LDAP5["OpenLDAP1<br/>:3891"]
            LDAP6["OpenLDAP2<br/>:3892"]
        end

        subgraph Storage3["Persistent Storage"]
            ProjectDir[("Project Directory<br/>icao-local-pkd-cpp-v2")]
            PGData3[("postgres-data<br/>localpkd DB")]
            LDAP5Data[("openldap1-data<br/>Directory")]
            LDAP6Data[("openldap2-data<br/>Directory")]
        end
    end

    %% CI/CD Flow
    GHA -->|Build Complete| Artifacts
    Artifacts -->|Download| Convert
    Convert -->|OCI Archive| Transfer
    Transfer -->|SSH/SCP| Load

    %% Deployment to Services
    Load -->|docker load| Frontend3
    Load -->|docker load| APIGateway3
    Load -->|docker load| PKD3
    Load -->|docker load| PA3
    Load -->|docker load| Relay3
    Load -->|docker load| Mon3

    %% Services to Storage
    PG3 -->|bind mount| PGData3
    LDAP5 -->|bind mount| LDAP5Data
    LDAP6 -->|bind mount| LDAP6Data
    PKD3 -->|bind mount| ProjectDir

    %% Styling - CI/CD
    style GHA fill:#14532d,stroke:#4ade80,stroke-width:3px,color:#f1f5f9,font-size:13px
    style Artifacts fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Convert fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px

    %% Styling - Deploy
    style Transfer fill:#312e81,stroke:#818cf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Load fill:#7f1d1d,stroke:#f87171,stroke-width:2px,color:#f1f5f9,font-size:13px

    %% Styling - Services
    style Frontend3 fill:#14532d,stroke:#4ade80,stroke-width:2px,color:#f1f5f9,font-size:13px
    style APIGateway3 fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style PKD3 fill:#164e63,stroke:#22d3ee,stroke-width:2px,color:#f1f5f9,font-size:13px
    style PA3 fill:#164e63,stroke:#22d3ee,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Relay3 fill:#164e63,stroke:#22d3ee,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Mon3 fill:#164e63,stroke:#22d3ee,stroke-width:2px,color:#f1f5f9,font-size:13px
    style PG3 fill:#312e81,stroke:#818cf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style LDAP5 fill:#0f4c75,stroke:#0ea5e9,stroke-width:2px,color:#f1f5f9,font-size:13px
    style LDAP6 fill:#0f4c75,stroke:#0ea5e9,stroke-width:2px,color:#f1f5f9,font-size:13px

    %% Styling - Storage
    style ProjectDir fill:#3f3f46,stroke:#a1a1aa,stroke-width:2px,color:#f1f5f9,font-size:13px
    style PGData3 fill:#3f3f46,stroke:#a1a1aa,stroke-width:2px,color:#f1f5f9,font-size:13px
    style LDAP5Data fill:#3f3f46,stroke:#a1a1aa,stroke-width:2px,color:#f1f5f9,font-size:13px
    style LDAP6Data fill:#3f3f46,stroke:#a1a1aa,stroke-width:2px,color:#f1f5f9,font-size:13px
```

**Deployment Workflow**:

1. **GitHub Actions Build** (~2 hours)
   - Multi-stage Dockerfile with vcpkg caching
   - QEMU emulation for ARM64 cross-compilation
   - Output: OCI format images (tar.gz)

2. **Artifact Conversion** (~30 seconds)
   - `skopeo copy --override-arch arm64 oci-archive:... docker-archive:...`
   - OCI format -> Docker loadable format

3. **Transfer to Luckfox** (~2 minutes)
   - `sshpass -p "luckfox" scp image.tar luckfox@192.168.100.11:`
   - Non-interactive SSH authentication

4. **Load and Deploy** (~1 minute)
   - `docker load < image.tar`
   - `docker compose -f docker-compose-luckfox.yaml up -d`
   - Health check verification

**Key Differences from Development Environment**:

| Aspect | Development (AMD64) | Luckfox (ARM64) |
|--------|---------------------|-----------------|
| **Network Mode** | bridge (icao-network) | host (direct port mapping) |
| **PostgreSQL DB** | pkd | localpkd |
| **Build Method** | Local build or Docker | GitHub Actions only |
| **Deployment** | docker-compose.yaml | docker-compose-luckfox.yaml |
| **Image Format** | Docker native | OCI -> Docker conversion |

---

## Security Architecture

### Authentication & Authorization

```mermaid
graph TB
    subgraph "Security Layers (v2.37.0)"
        subgraph "Auth Layer"
            JWT["JWT Authentication<br/>HS256, 32+ byte secret"]
            RBAC["RBAC<br/>admin / user roles"]
            AuthMiddleware["Auth Middleware<br/>std::regex::optimize"]
            AuditLog["Dual Audit Logging<br/>auth_audit_log + operation_audit_log"]
        end

        subgraph "Network Layer"
            Firewall[Host Firewall<br/>iptables]
            Docker[Docker Network Isolation<br/>icao-network]
            NginxHeaders["nginx Security Headers<br/>X-Content-Type-Options<br/>X-Frame-Options<br/>Referrer-Policy"]
        end

        subgraph "Application Layer"
            RateLimit2[Rate Limiting<br/>100 req/s per IP]
            CORS[CORS Policy<br/>Same-Origin]
            CSP[Content Security Policy<br/>frame-ancestors self]
            NoSystemCall["Command Injection Prevention<br/>system()/popen() to Native C API"]
        end

        subgraph "Data Layer"
            DBAuth[DB Authentication<br/>PostgreSQL / Oracle]
            LDAPAuth["LDAP Bind<br/>cn=admin DN<br/>DN Escape (RFC 4514)"]
            PII["PII Encryption<br/>AES-256-GCM<br/>CSR Private Key/PII"]
            Encryption[TLS/SSL Support<br/>Production]
        end

        subgraph "Code Layer"
            Parameterized["Parameterized Queries<br/>100% parameterized SQL"]
            OrderByWhitelist["ORDER BY Whitelist<br/>SQL Injection Prevention"]
            Base64Validation["Base64 Input Validation<br/>isValidBase64() pre-check"]
            NullChecks["OpenSSL Null Checks<br/>24 allocation sites"]
            BufferOverread["SOD Parser Protection<br/>end pointer boundary"]
        end
    end

    JWT --> RBAC
    RBAC --> AuthMiddleware
    AuthMiddleware --> AuditLog

    Firewall --> Docker
    Docker --> NginxHeaders

    NginxHeaders --> RateLimit2
    RateLimit2 --> CORS
    CORS --> CSP
    CSP --> NoSystemCall

    NoSystemCall --> DBAuth
    NoSystemCall --> LDAPAuth
    DBAuth --> PII
    LDAPAuth --> PII
    PII --> Encryption

    Encryption --> Parameterized
    Parameterized --> OrderByWhitelist
    OrderByWhitelist --> Base64Validation
    Base64Validation --> NullChecks
    NullChecks --> BufferOverread

    style JWT fill:#7f1d1d,stroke:#f87171,stroke-width:2px,color:#f1f5f9,font-size:13px
    style PII fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Encryption fill:#14532d,stroke:#4ade80,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Parameterized fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-size:13px
    style NoSystemCall fill:#7f1d1d,stroke:#f87171,stroke-width:2px,color:#f1f5f9,font-size:13px
```

**Security Checklist (v2.37.0)**:
- JWT Authentication (HS256) + RBAC (admin/user)
- API Client Authentication (X-API-Key, SHA-256 hash, per-client Rate Limiting, IP/Endpoint restriction)
- Dual audit logging (auth_audit_log + operation_audit_log) with request context (user, IP, path, User-Agent)
- **PII Encryption (Personal Information Protection Act Article 29)**: AES-256-GCM authenticated encryption -- CSR private key, passport number, PII fields
- **CSR Private Key Security**: AES-256-GCM encrypted storage, private key excluded from API response, permanent destruction on deletion
- Backend services not exposed externally (API Gateway only)
- Rate limiting (DDoS protection, per-IP connection limit)
- 100% parameterized SQL queries (all services)
- ORDER BY whitelist validation
- LIKE query parameter escaping (`escapeSqlWildcards()`)
- Command injection eliminated -- `system()`/`popen()` -> Native C API
- XSS prevention (JSON serialization, CSP headers)
- CORS policy (configurable, dynamic origin map)
- nginx security headers (X-Content-Type-Options, X-Frame-Options, X-XSS-Protection, Referrer-Policy, HSTS)
- JWT_SECRET minimum length validation (32 bytes)
- LDAP DN escape utility (RFC 4514)
- Base64 input validation (pre-check before decode)
- SOD parser buffer overread protection (end pointer boundary)
- OpenSSL null pointer checks (24+ allocation sites)
- Frontend OWASP hardening (DEV-only console.error, credential guards, centralized JWT injection)
- Logout audit logging even with expired JWT (best-effort payload decoding)
- Credential externalization (.env)
- File upload validation (MIME type, path sanitization)

---

## Technology Stack Summary

### Backend

| Component | Technology | Version | Purpose |
|-----------|-----------|---------|---------|
| Language | C++20 | GCC 11+ | High performance |
| Framework | Drogon | 1.9+ | Async HTTP server |
| Database | PostgreSQL / Oracle | 15 / XE 21c | Multi-DBMS (runtime switching) |
| LDAP | OpenLDAP | 2.6+ | Certificate storage (MMR cluster) |
| Crypto | OpenSSL | 3.x | X.509, CMS, Hash |
| JSON | nlohmann/json | 3.11+ | JSON parsing |
| Logging | spdlog | 1.12+ | Structured logging |
| Testing | Google Test | 1.14+ | Unit testing (86 tests) |
| Build | CMake + vcpkg | 3.20+ | Dependency management |

### Frontend

| Component | Technology | Version | Purpose |
|-----------|-----------|---------|---------|
| Language | TypeScript | 5.x | Type safety |
| Framework | React | 19 | UI library |
| Bundler | Vite | 5.x | Fast dev server |
| Styling | TailwindCSS | 4.x | Utility-first CSS |
| State | Zustand | latest | State management |
| Data Fetching | TanStack Query | latest | Server state |
| Charts | ECharts + Recharts | latest | Data visualization |
| Icons | Lucide React | latest | SVG icons |
| HTTP Client | Axios | latest | API requests |

### Infrastructure

| Component | Technology | Version | Purpose |
|-----------|-----------|---------|---------|
| API Gateway | Nginx | 1.25+ | Reverse proxy, security headers |
| LDAP LB | App-level SLB | - | Software load balancing (in-app) |
| Container | Docker | 24+ | Containerization |
| Orchestration | Docker Compose | 2.x | Multi-container apps |
| CI/CD | GitHub Actions | - | ARM64 cross-compilation |
| Artifact Conversion | skopeo | latest | OCI -> Docker format |

---

## Performance Metrics

### Throughput

| Metric | Value | Conditions |
|--------|-------|------------|
| **Certificate Search** | 2,222 req/s | 10k requests, 100 concurrent |
| **PA Verification** | 416 req/s | 1k requests, 50 concurrent |
| **PA Lookup** | 5~20ms | Pre-computed validation result |
| **API Latency** | <100ms | Average response time |
| **Database Query** | 40ms | PostgreSQL DISTINCT query (95 countries) |
| **LDAP Search** | <200ms | App-level SLB (round-robin) |

### Scalability

| Component | Current | Max Tested | Notes |
|-----------|---------|------------|-------|
| **Certificates** | 31,212 | 100,000+ | PostgreSQL/Oracle + LDAP |
| **Concurrent Users** | 100 | 1,000+ | Nginx workers x connections |
| **Upload File Size** | 100MB | 200MB | Nginx client_max_body_size |
| **Batch Size** | 1,000 | 10,000 | DB insert batch |

---

## Monitoring & Observability

### Health Checks

```mermaid
graph LR
    subgraph "Health Check Endpoints"
        GW[GET /health<br/>API Gateway]
        PKD_H[GET /api/health<br/>PKD Management]
        PA_H[GET /api/pa/health<br/>PA Service]
        Relay_H[GET /api/sync/health<br/>PKD Relay Service]
        Mon_H[GET /api/monitoring/health<br/>Monitoring Service]
        DB_H[GET /api/health/database<br/>Database Status]
        LDAP_H[GET /api/health/ldap<br/>LDAP Status]
    end

    subgraph "Monitoring Tools"
        Docker[Docker Healthcheck<br/>Container Status]
        Script[Health Check Script<br/>./docker-health.sh]
        MonDash[Monitoring Dashboard<br/>/monitoring]
    end

    GW --> Docker
    PKD_H --> Docker
    PA_H --> Docker
    Relay_H --> Docker
    Mon_H --> Docker

    DB_H --> Script
    LDAP_H --> Script
    MonDash --> Mon_H

    style GW fill:#14532d,stroke:#4ade80,stroke-width:2px,color:#f1f5f9,font-size:13px
    style Script fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#f1f5f9,font-size:13px
    style MonDash fill:#1e3a5f,stroke:#38bdf8,stroke-width:2px,color:#f1f5f9,font-size:13px
```

### Logging Strategy

| Component | Log Level | Destination | Retention |
|-----------|-----------|-------------|-----------|
| **PKD Management** | INFO | /var/log/pkd-management.log | 30 days |
| **PA Service** | INFO | /var/log/pa-service.log | 30 days |
| **PKD Relay Service** | INFO | /var/log/pkd-relay.log | 30 days |
| **Monitoring Service** | INFO | stdout (Docker logs) | 30 days |
| **Nginx Access** | COMBINED | /var/log/nginx/access.log | 30 days |
| **Nginx Error** | WARN | /var/log/nginx/error.log | 30 days |

---

# Part 2: Design Principles & Patterns

---

## Core Design Principles

### 1. Domain-Driven Design (DDD)
Domain-centric design for clear separation of business logic.

### 2. Microservice Architecture
System composed of independently deployable service units.

### 3. Strategy Pattern
Business logic separation based on file type and certificate type.

### 4. Single Responsibility Principle (SRP)
Each class and module has only one responsibility.

### 5. ServiceContainer + Pimpl Pattern (v2.12.0+)
Centralized DI container manages all dependencies. `struct Impl` pimpl pattern minimizes header dependencies.

### 6. Handler Extraction Pattern (v2.13.0+)
Minimize main.cpp and extract all HTTP handlers to independent classes.

---

## DDD (Domain-Driven Design) Structure

### PKD Management Service Layer Structure

```
services/pkd-management/src/
|
+-- infrastructure/               # Infrastructure Layer (v2.12.0+)
|   +-- service_container.h/.cpp  # ServiceContainer -- Centralized DI (pimpl pattern)
|   +-- app_config.h              # AppConfig -- Environment variable parsing
|   +-- http/http_client.h        # HTTP Client
|   +-- notification/email_sender.h
|
+-- handlers/                     # Handler Layer (v2.12.0~v2.13.0 extracted from main.cpp)
|   +-- upload_handler.h/.cpp     # Upload API (10 endpoints)
|   +-- upload_stats_handler.h/.cpp # Statistics/History API (11 endpoints)
|   +-- certificate_handler.h/.cpp  # Certificate API (20 endpoints)
|   +-- auth_handler.h/.cpp       # Auth API
|   +-- icao_handler.h/.cpp       # ICAO Sync API
|   +-- code_master_handler.h/.cpp  # Code Master API (6 endpoints)
|   +-- api_client_handler.h/.cpp   # API Client Management (7 endpoints)
|   +-- api_client_request_handler.h/.cpp # API Client Request (5 endpoints)
|   +-- csr_handler.h/.cpp        # CSR Management API (6 endpoints)
|   +-- misc_handler.h/.cpp       # Health, Audit, Validation, PA proxy, Info
|
+-- middleware/                   # Auth Middleware
|   +-- auth_middleware.h/.cpp    # JWT + API Key Auth
|
+-- repositories/                 # Repository Layer (16 repositories)
|   +-- upload_repository.h/.cpp
|   +-- certificate_repository.h/.cpp
|   +-- validation_repository.h/.cpp
|   +-- crl_repository.h/.cpp
|   +-- ldap_certificate_repository.h/.cpp
|   +-- code_master_repository.h/.cpp
|   +-- api_client_repository.h/.cpp
|   +-- pending_dsc_repository.h/.cpp
|   +-- csr_repository.h/.cpp
|   +-- ...
|
+-- services/                     # Service Layer
|   +-- upload_service.h/.cpp
|   +-- validation_service.h/.cpp
|   +-- certificate_service.h/.cpp
|   +-- icao_sync_service.h/.cpp
|   +-- ldap_storage_service.h/.cpp  # LDAP Storage (Certificates/CRL/ML)
|   +-- csr_service.h/.cpp
|
+-- adapters/                     # icao::validation Adapters
|   +-- db_csca_provider.cpp      # DB-based CSCA Provider
|   +-- db_crl_provider.cpp       # DB-based CRL Provider
|   +-- ldap_csca_provider.cpp    # LDAP-based CSCA Provider (PA Lookup)
|   +-- ldap_crl_provider.cpp     # LDAP-based CRL Provider (PA Lookup)
|
+-- auth/                         # Auth Module
|   +-- personal_info_crypto.h/.cpp  # PII Encryption (AES-256-GCM)
|   +-- password_hash.h/.cpp     # PBKDF2-HMAC-SHA256
|
+-- processing_strategy.h         # Strategy Pattern (AUTO)
+-- ldif_processor.h/.cpp         # LDIF Processor
+-- common/
|   +-- masterlist_processor.h    # Master List Processor
|   +-- progress_manager.h/.cpp   # SSE Progress Management
|
+-- main.cpp                      # Entry Point (minimized: ~430 lines, v2.13.0)
```

### DDD Layer Responsibilities

#### 1. Domain Layer
**Responsibility**: Business rules and core logic
- **Models**: Business entities (Certificate, ICAO Version, etc.)
- **Domain Services**: Domain logic (validation, computation, etc.)

```cpp
// domain/models/certificate.h
class Certificate {
private:
    std::string id;
    std::string subjectDn;
    std::string issuerDn;
    std::string country;
    CertificateType type;  // CSCA, DSC, DSC_NC, MLSC

public:
    // Business rule: Check self-signed
    bool isSelfSigned() const;

    // Business rule: Check Link Certificate
    bool isLinkCertificate() const;
};
```

#### 2. Repository Layer
**Responsibility**: Data persistence (DB, LDAP access)
- PostgreSQL data access
- LDAP data access
- Domain model <-> database conversion

```cpp
// repositories/ldap_certificate_repository.h
class LdapCertificateRepository {
public:
    // Query certificate from LDAP
    std::optional<Certificate> findByFingerprint(const std::string& fingerprint);

    // Save certificate to LDAP
    void save(const Certificate& cert);
};
```

#### 3. Application Service Layer
**Responsibility**: Use case orchestration (transactions, combining multiple domain services)
- Upload processing flow management
- Transaction boundary setting
- External API calls

```cpp
// services/upload_service.h
class UploadService {
public:
    // Use case: Process file upload
    UploadResult processFileUpload(
        const FileUploadRequest& request,
        ProcessingStrategy* strategy
    );
};
```

#### 4. Infrastructure Layer
**Responsibility**: External system integration (HTTP, filesystem, external API)
- ServiceContainer (centralized DI)
- Drogon HTTP handlers (handlers/)
- Filesystem access
- LDAP/PostgreSQL connection management

---

## ServiceContainer Pattern (Centralized DI)

### Design Principles (v2.12.0+)

ServiceContainer is a centralized container that owns all application dependencies and provides dependency injection.
Replaces 17 global `shared_ptr` variables from the original main.cpp with a single container instance.

### Pimpl Pattern

Only exposes `struct Impl` forward declaration in header to minimize compilation dependencies:

```cpp
// infrastructure/service_container.h
class ServiceContainer {
public:
    ServiceContainer();
    ~ServiceContainer();

    // Non-copyable, non-movable
    ServiceContainer(const ServiceContainer&) = delete;
    ServiceContainer& operator=(const ServiceContainer&) = delete;

    bool initialize(const AppConfig& config);
    void shutdown();

    // Non-owning pointer accessors (ownership held by Impl)
    common::IQueryExecutor* queryExecutor() const;
    repositories::CertificateRepository* certificateRepository() const;
    services::UploadService* uploadService() const;
    handlers::UploadHandler* uploadHandler() const;
    // ... other accessors

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

```cpp
// infrastructure/service_container.cpp
struct ServiceContainer::Impl {
    // Connection pools
    std::shared_ptr<common::IDbConnectionPool> dbPool;
    std::unique_ptr<common::IQueryExecutor> queryExecutor;
    std::shared_ptr<common::LdapConnectionPool> ldapPool;

    // Repositories (16)
    std::shared_ptr<repositories::UploadRepository> uploadRepository;
    std::shared_ptr<repositories::CertificateRepository> certificateRepository;
    // ...

    // Services (8)
    std::shared_ptr<services::UploadService> uploadService;
    // ...

    // Handlers (9)
    std::shared_ptr<handlers::UploadHandler> uploadHandler;
    // ...
};
```

### Initialization Order

Strict dependency-ordered initialization:

```
Phase 0: PII Encryption (Personal Information Protection Act)
Phase 1: LDAP Connection Pool
Phase 2: Certificate Service (LDAP-based search)
Phase 3: Database Connection Pool + Query Executor
Phase 4: Repositories (16, all depend on QueryExecutor)
Phase 4.5: LDAP Storage Service
Phase 5: ICAO Sync Module
Phase 6: Business Logic Services
Phase 7: Handlers (9)
Phase 8: Ensure Admin User
```

### Resource Release

`shutdown()` method releases all resources in reverse order (destructors called automatically):

```
Handlers -> Services -> LDAP Providers -> Repositories -> QueryExecutor -> LDAP Pool -> DB Pool
```

### ServiceContainer Adoption Across Services

| Service | ServiceContainer | Key Components |
|--------|:----------------:|-----------|
| PKD Management | O | 16 repos, 8 services, 9 handlers |
| PA Service | O | 5 repos, 2 parsers, 4 services |
| PKD Relay | O | 5 repos, 3 services, SyncScheduler |
| Monitoring | - | DB-independent (direct HTTP/system calls) |

---

## main.cpp Minimization Pattern

### Principles (v2.13.0+)

main.cpp is composed as a minimal orchestration layer only:

```
Load config -> Initialize ServiceContainer -> Register routes -> Run server
```

### Structure (PKD Management, ~430 lines)

```cpp
// main.cpp -- Minimized entry point
infrastructure::ServiceContainer* g_services = nullptr;

namespace {
    AppConfig appConfig;

    void initializeLogging() { /* spdlog setup */ }
    void printBanner() { /* banner output */ }
    Json::Value checkDatabase() { /* DB health check */ }
    Json::Value checkLdap() { /* LDAP health check */ }

    void registerRoutes() {
        auto& app = drogon::app();

        // Call each handler's registerRoutes()
        if (g_services && g_services->authHandler())
            g_services->authHandler()->registerRoutes(app);
        if (g_services && g_services->uploadHandler())
            g_services->uploadHandler()->registerRoutes(app);
        if (g_services && g_services->certificateHandler())
            g_services->certificateHandler()->registerRoutes(app);
        // ... other handlers

        // MiscHandler (health, audit, etc. -- not managed by ServiceContainer)
        static handlers::MiscHandler miscHandler(
            g_services->auditService(),
            g_services->validationService(),
            checkDatabase, checkLdap
        );
        miscHandler.registerRoutes(app);
    }
}

int main(int argc, char* argv[]) {
    printBanner();
    initializeLogging();
    appConfig = AppConfig::fromEnvironment();

    g_services = new infrastructure::ServiceContainer();
    if (!g_services->initialize(appConfig)) return 1;

    registerRoutes();
    drogon::app().run();

    delete g_services;
    return 0;
}
```

### Handler Extraction Pattern

All handlers provide a `registerRoutes(HttpAppFramework&)` method:

```cpp
// handlers/upload_handler.h
class UploadHandler {
public:
    UploadHandler(UploadService*, ValidationService*, ...);

    // Called from main.cpp
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    void handleLdifUpload(const HttpRequestPtr&, ResponseCallback&&);
    void handleMasterListUpload(const HttpRequestPtr&, ResponseCallback&&);
    // ... 10 endpoints
};
```

### main.cpp Size Across Services

| Service | v2.11.0 | v2.13.0 | Reduction |
|--------|---------|---------|--------|
| PKD Management | 8,095 lines | ~430 lines | -94.7% |
| PA Service | 2,800 lines | ~281 lines | -90.0% |
| PKD Relay | 1,644 lines | ~457 lines | -72.2% |
| Monitoring | 586 lines | ~93 lines | -84.1% |

---

## Strategy Pattern

### 1. Processing Strategy - File Processing Mode

```cpp
// processing_strategy.h

// Abstract strategy interface
class ProcessingStrategy {
public:
    virtual void processLdifEntries(...) = 0;
    virtual void processMasterListContent(...) = 0;
};

// Concrete strategy: AUTO mode (v2.25.0+ MANUAL mode removed)
class AutoProcessingStrategy : public ProcessingStrategy {
    // One-shot processing: Parse -> Validate -> DB -> LDAP
};

// Factory Pattern
class ProcessingStrategyFactory {
    static std::unique_ptr<ProcessingStrategy> create(const std::string& mode);
};
```

**Usage Example**:
```cpp
// upload_handler.cpp
auto strategy = ProcessingStrategyFactory::create("AUTO");
strategy->processLdifEntries(uploadId, entries, conn, ld);
```

### 2. Certificate Type Strategy - Per-Type Validation

```cpp
// Per-certificate-type validation strategy separation

// CSCA validation strategy
ValidationResult validateCscaCertificate(X509* cert) {
    // CSCA validation logic (Self-signed, CA:TRUE, keyCertSign)
}

// DSC validation strategy
ValidationResult validateDscCertificate(X509* cert, PGconn* conn) {
    // DSC validation logic (Trust Chain, CRL check)
}

// Link Certificate validation strategy
bool isLinkCertificate(X509* cert) {
    // Link Certificate detection logic (CA:TRUE, keyCertSign, not self-signed)
}
```

### 3. File Type Strategy - Per-File-Type Processing

```cpp
// LDIF file processing
class LdifProcessor {
    ProcessingCounts process(const std::string& content);
};

// Master List file processing
class MasterListProcessor {
    MasterListStats process(const std::vector<uint8_t>& content);
};
```

---

## Single Responsibility Principle (SRP)

### Good Example: Responsibility Separation
```cpp
// 1. Only responsible for LDIF parsing
class LdifParser {
    std::vector<LdifEntry> parse(const std::string& content);
};

// 2. Only responsible for certificate validation
class CertificateValidator {
    ValidationResult validate(X509* cert);
};

// 3. Only responsible for LDAP storage
class LdapCertificateRepository {
    void save(const Certificate& cert);
};

// 4. Orchestration of overall flow
class UploadService {
    void processUpload() {
        auto entries = ldifParser.parse(content);      // Parsing
        auto result = validator.validate(cert);        // Validation
        repository.save(cert);                         // Storage
    }
};
```

### Bad Example: Mixed Responsibilities
```cpp
// One class performs parsing, validation, and storage (SRP violation)
class EverythingProcessor {
    void processEverything(const std::string& content) {
        // Parse
        auto entries = /* parse LDIF */;

        // Validate
        auto result = /* validate certificate */;

        // DB save
        /* save to PostgreSQL */;

        // LDAP save
        /* save to LDAP */;
    }
};
```

### Class Responsibility Definitions

| Class/Module | Single Responsibility |
|------------|---------|
| `LdifProcessor` | LDIF file parsing |
| `MasterListProcessor` | Master List CMS parsing |
| `AutoProcessingStrategy` | AUTO mode processing flow |
| `Certificate` (Domain Model) | Certificate domain rules |
| `LdapCertificateRepository` | LDAP data access |
| `CertificateValidator` | Certificate validation logic |
| `TrustChainBuilder` | Trust Chain construction |
| `CscaCache` | CSCA cache management |
| `ServiceContainer` | Dependency ownership and initialization |
| `UploadHandler` | Upload HTTP handler |
| `LdapStorageService` | LDAP write operations |

---

## Query Executor Pattern (Database Abstraction)

Database abstraction pattern for Multi-DBMS support (PostgreSQL + Oracle).

### IQueryExecutor Interface

```cpp
// shared/lib/database/i_query_executor.h
class IQueryExecutor {
public:
    virtual QueryResult executeQuery(const std::string& sql,
                                      const std::vector<std::string>& params) = 0;
    virtual int executeNonQuery(const std::string& sql,
                                 const std::vector<std::string>& params) = 0;
    virtual DatabaseType getDatabaseType() const = 0;

    // Batch mode (v2.26.1+)
    virtual void beginBatch() {}
    virtual void endBatch() {}
    virtual void savepoint(const std::string& name) {}
    virtual void rollbackToSavepoint(const std::string& name) {}
};
```

### Implementations
- **PostgreSqlQueryExecutor**: libpq-based (10-50x faster)
- **OracleQueryExecutor**: OCI Session Pool-based

### Runtime Selection
```cpp
// Factory Pattern selects at runtime based on DB_TYPE environment variable
auto executor = QueryExecutorFactory::create(DB_TYPE);  // "postgres" or "oracle"
```

All repositories execute SQL through `IQueryExecutor`, enabling database replacement.

---

## Query Helpers Pattern

### common::db:: Utility Functions

Helper functions abstracting SQL syntax differences between databases (v2.12.0+).

```cpp
// shared/lib/database/query_helpers.h
namespace common::db {
    // JSON -> integer safe conversion (Oracle returns all values as strings)
    int scalarToInt(const Json::Value& val, int defaultVal = 0);

    // DB-specific boolean literal ("TRUE"/"FALSE" vs "1"/"0")
    std::string boolLiteral(const std::string& dbType, bool value);

    // DB-specific pagination ("LIMIT $N OFFSET $M" vs "OFFSET $M ROWS FETCH NEXT $N ROWS ONLY")
    std::string paginationClause(const std::string& dbType, int limit, int offset);

    // DB-specific hex prefix ("\\x" vs empty string)
    std::string hexPrefix(const std::string& dbType);

    // DB-specific current timestamp ("NOW()" vs "SYSTIMESTAMP")
    std::string currentTimestamp(const std::string& dbType);

    // DB-specific case-insensitive condition ("ILIKE" vs "UPPER() LIKE UPPER()")
    std::string ilikeCond(const std::string& dbType, const std::string& column, const std::string& paramRef);

    // DB-specific LIMIT clause
    std::string limitClause(const std::string& dbType, int limit);

    // JSON boolean safe reading (Oracle NUMBER(1) -> bool)
    bool getBool(const Json::Value& row, const std::string& key, bool defaultVal = false);
}
```

### Usage Example

```cpp
// Remove DB branching in repositories
std::string dbType = queryExecutor_->getDatabaseType();

std::string sql = "SELECT * FROM certificate WHERE stored_in_ldap = " +
    common::db::boolLiteral(dbType, false) +
    " ORDER BY created_at " +
    common::db::paginationClause(dbType, limit, offset);
```

### Impact
- Eliminated 204 inline DB branches across 15 repositories in 3 services
- New repositories automatically get DB compatibility

---

## Provider/Adapter Pattern (Validation Infrastructure)

Pattern separating validation logic from data sources (DB vs LDAP).

### Provider Interface

```cpp
// shared/lib/icao-validation/include/icao/validation/providers.h
class ICscaProvider {
public:
    virtual std::vector<X509*> findAllCscasByIssuerDn(const std::string& issuerDn) = 0;
    virtual X509* findCscaByIssuerDn(const std::string& issuerDn,
                                      const std::string& countryCode = "") = 0;
};

class ICrlProvider {
public:
    virtual X509_CRL* findCrlByCountry(const std::string& countryCode) = 0;
};
```

### Adapter Implementations

| Service | CSCA Provider | CRL Provider |
|--------|--------------|-------------|
| PKD Management | `DbCscaProvider` -> CertificateRepository | `DbCrlProvider` -> CrlRepository |
| PKD Management (PA Lookup) | `LdapCscaProvider` -> LdapPool | `LdapCrlProvider` -> LdapPool |
| PA Service | `LdapCscaProvider` -> LdapCertificateRepository | `LdapCrlProvider` -> LdapCrlRepository |

Each service implements Adapters matching its own data access layer. The `icao::validation` library contains no infrastructure code.

---

## Shared Libraries

### Library Structure (shared/lib/)

Common ICAO certificate functionality extracted into shared libraries to eliminate code duplication across services.

| Library | Namespace | Responsibility |
|-----------|------------|------|
| `database` | `common::` / `common::db::` | DB connection pool, Query Executor (PostgreSQL + Oracle), Query Helpers |
| `ldap` | `common::` | Thread-safe LDAP connection pool (min=2, max=10) |
| `icao-validation` | `icao::validation::` | ICAO 9303 certificate validation (trust chain, CRL, extensions, algorithm) |
| `certificate-parser` | `icao::` | X.509 certificate parsing |
| `cvc-parser` | `icao::cvc::` | BSI TR-03110 CVC certificate parsing |
| `icao9303` | `icao::` | ICAO 9303 SOD/DG parser |
| `audit` | `icao::audit::` | Unified audit logging (operation_audit_log) |
| `config` | `icao::config::` | Configuration management |
| `exception` | `icao::` | Custom exception types |
| `logging` | `icao::logging::` | Structured logging (spdlog) |

### Shared Validation Library (icao::validation, v2.11.0+)

Pattern extracting ICAO 9303 certificate validation logic into a shared library.

#### Design Principles
- **Pure Function**: Uses only OpenSSL X509 API without infrastructure dependencies
- **Idempotent**: Same input -> same output (idempotency guaranteed)
- **Provider Pattern**: Data sources injected through interfaces

#### Module Structure

| Module | Responsibility |
|------|------|
| `cert_ops` | Pure X509 operations (fingerprint, validity, DN extraction, normalization) |
| `trust_chain_builder` | Multi-CSCA trust chain construction (key rollover support) |
| `crl_checker` | RFC 5280 CRL revocation checking |
| `extension_validator` | X.509 extension field validation (key usage, critical extensions) |
| `algorithm_compliance` | ICAO Doc 9303 Part 12 algorithm requirements |

#### Testing
86 unit tests (GTest). All modules include idempotency verification (50-100 iterations).

### Shared Database Library (icao::database)

#### Query Executor + Query Helpers
- `IQueryExecutor`: DB abstraction interface (PostgreSQL + Oracle)
- `QueryHelpers` (`common::db::`): SQL syntax difference abstraction utilities
- `handler_utils.h`: `sendJsonSuccess()`, `notFound()` response helpers

#### Connection Pooling
- `DbConnectionPool`: Thread-safe DB connection pool (RAII)
- `DbConnectionPoolFactory`: Environment variable-based pool creation

---

## Code Structure Rules

### 1. File Organization Rules

```
service-name/
+-- src/
|   +-- infrastructure/         # Infrastructure Layer (ServiceContainer, AppConfig)
|   |
|   +-- handlers/               # HTTP Handlers (registerRoutes pattern)
|   |
|   +-- middleware/              # Auth Middleware
|   |
|   +-- domain/                 # Domain Layer (Business core)
|   |   +-- models/             # Entities
|   |   +-- services/           # Domain services
|   |
|   +-- repositories/           # Data access (LDAP, PostgreSQL)
|   |
|   +-- services/               # Application services
|   |
|   +-- adapters/               # Provider adapters (icao::validation)
|   |
|   +-- auth/                   # Auth/encryption module
|   |
|   +-- common/                 # Utilities (common functions)
|   |
|   +-- {strategy_name}_strategy.h   # Strategy pattern
|   +-- {processor_name}_processor.h # Processor
|   |
|   +-- main.cpp               # Minimal entry point (~430 lines)
|
+-- tests/                     # Unit tests
```

### 2. Naming Conventions

#### Class Naming
- **Domain Model**: `Certificate`, `IcaoVersion` (nouns)
- **Repository**: `LdapCertificateRepository`, `CrlRepository`
- **Service**: `UploadService`, `CertificateService`
- **Handler**: `UploadHandler`, `CertificateHandler` (v2.12.0+)
- **Strategy**: `AutoProcessingStrategy`
- **Processor**: `LdifProcessor`, `MasterListProcessor`
- **Validator**: `CertificateValidator`, `TrustChainValidator`
- **Container**: `ServiceContainer` (DI container)

#### Function Naming
- **Domain rules**: `isSelfSigned()`, `isLinkCertificate()`
- **Repository**: `findByFingerprint()`, `save()`, `findAll()`
- **Service**: `processUpload()`, `validateCertificate()`
- **Handler**: `handleLdifUpload()`, `handleCertificateSearch()`
- **Route Registration**: `registerRoutes(HttpAppFramework&)`
- **Strategy**: `processLdifEntries()`, `processMasterListContent()`

### 3. Dependency Direction

```
+-------------------------------------------+
|     main.cpp (Minimal Orchestration)      |
|    config -> DI -> routes -> run          |
+-------------------+---------------------+
                    |
                    v
+-------------------------------------------+
|   ServiceContainer (DI + Handlers)        |
|     Owns all dependencies (pimpl)         |
+-------------------+---------------------+
                    |
                    v
+-------------------------------------------+
|      Application Service Layer            |
|     (UploadService, etc.)                 |
+-------------------+---------------------+
                    |
                    v
+-------------------------------------------+
|         Domain Layer                      |
|  (Certificate, CertificateValidator)      |
+-------------------+---------------------+
                    |
                    v
+-------------------------------------------+
|    Infrastructure Layer                   |
|  (LdapRepository, PostgresRepository)     |
+-------------------------------------------+
```

**Dependency Rules**:
- Domain Layer does not depend on other layers (Pure Business Logic)
- Application Service depends on Domain Layer
- Infrastructure depends on Domain Layer (Dependency Inversion)
- Handler depends on Service + Repository
- ServiceContainer owns all layers but minimizes header exposure via Pimpl
- main.cpp depends only on ServiceContainer

---

## Extension Guidelines

### 1. Adding a New File Type

```cpp
// 1. Create Processor (SRP)
class NewFileTypeProcessor {
    ProcessingResult process(const std::vector<uint8_t>& content);
};

// 2. Add method to Strategy
class ProcessingStrategy {
    virtual void processNewFileType(...) = 0;
};

// 3. Implement concrete strategy
class AutoProcessingStrategy {
    void processNewFileType(...) override;
};
```

### 2. Adding a New Certificate Type

```cpp
// 1. Extend CertificateType enum
enum class CertificateType {
    CSCA, DSC, DSC_NC, MLSC, NEW_TYPE  // Added
};

// 2. Add validation strategy (SRP)
ValidationResult validateNewTypeCertificate(X509* cert) {
    // NEW_TYPE validation logic
}

// 3. Add LDAP DN strategy
std::string buildLdapDnForNewType(const Certificate& cert) {
    // NEW_TYPE LDAP DN generation logic
}
```

### 3. Adding a New Handler (v2.12.0+ Pattern)

```cpp
// 1. Create handlers/new_handler.h/.cpp
class NewHandler {
public:
    NewHandler(SomeService*, SomeRepository*, IQueryExecutor*);
    void registerRoutes(drogon::HttpAppFramework& app);
private:
    void handleSomeEndpoint(const HttpRequestPtr&, ResponseCallback&&);
};

// 2. Add member to ServiceContainer's Impl
struct ServiceContainer::Impl {
    std::shared_ptr<handlers::NewHandler> newHandler;
};

// 3. Create in ServiceContainer::initialize()
impl_->newHandler = std::make_shared<handlers::NewHandler>(...);

// 4. Add accessor to ServiceContainer
handlers::NewHandler* newHandler() const;

// 5. Register in main.cpp's registerRoutes()
if (g_services && g_services->newHandler())
    g_services->newHandler()->registerRoutes(app);
```

### 4. Adding a New Microservice

```cpp
// 1. Create services/{new-service-name}/ directory
// 2. Apply DDD structure (infrastructure/, handlers/, repositories/, services/)
// 3. Apply ServiceContainer + AppConfig pattern
// 4. Apply main.cpp minimization pattern (~100-500 lines)
// 5. Create Dockerfile, CMakeLists.txt
// 6. Add service to docker-compose.yaml
// 7. Add routing to API Gateway (nginx.conf)
```

---

## Anti-Patterns

### God Class
```cpp
// BAD: All responsibilities concentrated in one class
class Everything {
    void parseFile();
    void validateCertificate();
    void saveToDatabase();
    void saveToLdap();
    void sendEmail();
    void logEverything();
};
```

### Fat main.cpp
```cpp
// BAD: All handler logic implemented directly in main.cpp (pre-v2.11.0 pattern)
int main() {
    app.registerHandler("/api/upload/ldif", [](req, callback) {
        // 500 lines of upload handler logic...
    });
    app.registerHandler("/api/certificates/search", [](req, callback) {
        // 300 lines of search logic...
    });
    // ... 8,095 lines
}
```

**Correct approach**: Extract to Handler classes + ServiceContainer DI

### Global Variables
```cpp
// BAD: 17 global shared_ptr variables (pre-v2.11.0 pattern)
std::shared_ptr<UploadRepository> g_uploadRepo;
std::shared_ptr<CertificateRepository> g_certRepo;
// ... 15 more
```

**Correct approach**: ServiceContainer owns all dependencies

### Anemic Domain Model
```cpp
// BAD: Domain model has only data, logic is in external service
class Certificate {
    std::string subjectDn;  // Only getter/setter
    std::string issuerDn;
};

class CertificateService {
    bool isSelfSigned(const Certificate& cert);  // Domain logic in service
};
```

**Correct approach**:
```cpp
// GOOD: Domain logic placed inside model
class Certificate {
private:
    std::string subjectDn;
    std::string issuerDn;

public:
    bool isSelfSigned() const {  // Domain logic inside model
        return subjectDn == issuerDn;
    }
};
```

### Tight Coupling
```cpp
// BAD: Direct dependency on concrete class
class UploadService {
    PostgresCertificateRepository repo;  // Depends on concrete class

    void save(Certificate cert) {
        repo.save(cert);  // Not replaceable
    }
};
```

**Correct approach**:
```cpp
// GOOD: Depend on interface/abstract class (Dependency Inversion)
class UploadService {
    CertificateRepository* repo;  // Depends on abstract interface

    UploadService(CertificateRepository* r) : repo(r) {}

    void save(Certificate cert) {
        repo->save(cert);  // Replaceable (LDAP/Postgres)
    }
};
```

### Inline DB Branching
```cpp
// BAD: Repeated DB-specific if/else in every repository (pre-v2.11.0 pattern)
if (dbType == "oracle") {
    sql += " OFFSET " + std::to_string(offset) + " ROWS FETCH NEXT " + std::to_string(limit) + " ROWS ONLY";
} else {
    sql += " LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
}
```

**Correct approach**:
```cpp
// GOOD: Use Query Helpers
sql += common::db::paginationClause(dbType, limit, offset);
```

---

## Code Review Checklist

Verify the following when writing new code:

### DDD Compliance
- [ ] Is domain logic located in the Domain Layer?
- [ ] Does the Repository handle only data access?
- [ ] Does the Application Service only orchestrate use cases?

### ServiceContainer Pattern Compliance
- [ ] Are new dependencies registered in ServiceContainer?
- [ ] Are ServiceContainer accessors used instead of global variables?
- [ ] Does initialization order follow dependency direction?

### Handler Extraction Compliance
- [ ] Is HTTP handler logic in Handler classes, not main.cpp?
- [ ] Does the Handler provide a `registerRoutes(HttpAppFramework&)` method?
- [ ] Does main.cpp perform only minimal orchestration?

### Strategy Pattern Compliance
- [ ] Are Processors separated by file type?
- [ ] Is validation logic separated by certificate type?
- [ ] Is mode-specific processing separated through ProcessingStrategy?

### SRP Compliance
- [ ] Does each class have only one responsibility?
- [ ] Does the class name clearly express its responsibility?
- [ ] Does each function do only one thing?

### Multi-DBMS Compliance
- [ ] Are Query Helpers (`common::db::`) used to eliminate DB branching?
- [ ] Is SQL executed through `IQueryExecutor`?
- [ ] Has it been tested on both Oracle/PostgreSQL?

### Microservice Compliance
- [ ] Does inter-service communication go through the API Gateway?
- [ ] Is each service independently deployable?
- [ ] Are databases separated per service (or clearly separated tables)?

### Dependency Direction
- [ ] Does the Domain Layer not depend on other layers?
- [ ] Does Infrastructure depend on Domain? (Dependency Inversion)
- [ ] Are there no circular references?

---

# Appendix

---

## Future Enhancements

### Completed (Previously Planned)

- JWT Authentication (v1.8.0)
- Role-Based Access Control -- RBAC admin/user (v1.9.0)
- Multi-DBMS Support -- PostgreSQL + Oracle (v2.6.0)
- Monitoring Service -- DB-independent system metrics (v2.7.1)
- OWASP Security Hardening -- Full audit (v2.10.5)
- ICAO 9303 Validation Library -- 86 unit tests (v2.10.6)

### Phase 1 (Planned)

- HTTPS/TLS Support (Let's Encrypt)
- Horizontal Scaling (Multiple instances)
- Redis Caching Layer

### Phase 2 (Research / In Progress)

- ICAO PKD LDAP V3 Direct Connection -- Certificate auto-synchronization (March 2026~ ICAO transition)
- ICAO PKD REST API Integration -- HTTP-based certificate download
- Kubernetes Deployment
- Prometheus + Grafana Monitoring

---

## Conclusion

FASTpass(R) SPKD v2.37.0 provides high performance, scalability, and security through **microservices architecture**, **Multi-DBMS**, **full ICAO 9303 compliance**, and **CSR-based PKD integration**.

**Key Strengths**:
- 5 independent microservices (PKD Management, PA, Relay, Monitoring, AI Analysis)
- Multi-DBMS support -- PostgreSQL 15 / Oracle XE 21c runtime switching
- **ICAO PKD CSR Management** -- RSA-2048 CSR generation/Import, issued certificate registration, public key matching verification
- **DSC Registration Approval Workflow** -- PA-extracted DSC auto-registration -> admin approval/rejection
- JWT Authentication + RBAC (12 permissions, including pa:stats) + API Key Authentication (X-API-Key) + Dual Audit Logging
- **PII Encryption** (AES-256-GCM) -- CSR private key, passport number, PII fields
- OWASP security hardening complete (Command Injection elimination, SQL Injection prevention)
- AI certificate forensic analysis (Isolation Forest + LOF, 10-category forensic risk scoring)
- icao::validation shared library (ICAO 9303 Part 12)
- DB-LDAP data consistency guaranteed (31,212 certificates, 100% synchronization)
- C++20 high-performance backend + React 19 modern frontend (29 pages)
- ARM64 CI/CD pipeline (GitHub Actions -> Luckfox deployment)
- Docker / Podman flexible deployment (local, Production RHEL 9, ARM64)

---

**Document Created**: 2026-03-18
**Author**: ICAO Local PKD Development Team
**Organization**: SMARTCORE Inc.

---

## References

- **[CLAUDE.md](../CLAUDE.md)** - Project overview and current version (v2.37.0)
- **[SECURITY_AUDIT_REPORT.md](SECURITY_AUDIT_REPORT.md)** - Security audit report
- **[DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md)** - Development guide
