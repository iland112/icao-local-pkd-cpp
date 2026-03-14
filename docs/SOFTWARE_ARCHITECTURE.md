# ICAO Local PKD - Software Architecture

**Version**: 2.30.0
**Last Updated**: 2026-03-09
**Status**: Production Ready (Multi-DBMS: PostgreSQL + Oracle)

---

## Table of Contents

1. [System Overview](#system-overview)
2. [High-Level Architecture](#high-level-architecture)
3. [Microservices Architecture](#microservices-architecture)
4. [Data Layer Architecture](#data-layer-architecture)
5. [Frontend Architecture](#frontend-architecture)
6. [API Gateway Architecture](#api-gateway-architecture)
7. [Component Details](#component-details)
8. [Data Flow Diagrams](#data-flow-diagrams)
9. [Deployment Architecture](#deployment-architecture)
10. [Security Architecture](#security-architecture)

---

## System Overview

ICAO Local PKD는 **마이크로서비스 아키텍처** 기반의 전자여권 인증서 관리 및 검증 통합 플랫폼입니다.

### Core Principles

- **Microservices**: 독립적으로 배포 가능한 5개 서비스 분리
- **Multi-DBMS**: PostgreSQL + Oracle 런타임 전환 (DB_TYPE 환경변수)
- **Data Consistency**: DB-LDAP 이중 저장 및 자동 동기화 (Reconciliation)
- **High Performance**: C++20 기반 고성능 백엔드
- **Modern UI**: React 19 + TypeScript + Tailwind CSS 4
- **Security First**: JWT 인증, RBAC, OWASP 보안 강화
- **Shared Validation**: icao::validation 공유 라이브러리 (86 단위 테스트)

### PKD와 EAC PKI의 역할 구분

본 시스템은 **ICAO 9303 Passive Authentication(PA)**에 특화되어 있으며, EU 여권의 EAC(Extended Access Control)와는 독립적인 별도 시스템이다.

| 구분 | 본 시스템 (PKD) | EAC PKI (별도) |
|------|----------------|----------------|
| **목적** | 칩 데이터 무결성 검증 (PA) | 보호된 생체정보 접근 (TA) |
| **인증서** | CSCA → DSC (X.509) | CVCA → DV → IS (CVC) |
| **보호 대상** | DG1(MRZ), DG2(얼굴) | DG3(지문), DG4(홍채) |
| **배포 방식** | ICAO PKD 서버 (공개) | 양자간 협정 (bilateral) |
| **EU 여권 처리** | **필수** (모든 국가 공통) | 지문/홍채 접근 시에만 필요 |

EU 여권 처리 시 PA(1단계)는 PKD로 처리하고, 필요 시 EAC TA(3단계)를 EAC PKI로 별도 처리한다.
두 시스템 간 인증서 공유는 없으며 인증 흐름도 독립적이다.
자세한 내용은 [EAC_SERVICE_IMPLEMENTATION_PLAN.md](EAC_SERVICE_IMPLEMENTATION_PLAN.md) §1.4 참조.

---

## Technical Architecture Diagram

### System Overview (v2.30.0)

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
    classDef titleStyle fill:#37474F,stroke:#263238,stroke-width:2px,color:#fff,font-weight:bold
    class L1Title,L2Title,L3Title,L4Title,L5Title titleStyle

    classDef external fill:#E3F2FD,stroke:#1976D2,stroke-width:2px,color:#000
    classDef dmz fill:#FFF3E0,stroke:#F57C00,stroke-width:2px,color:#000
    classDef app fill:#E8F5E9,stroke:#388E3C,stroke-width:2px,color:#000
    classDef data fill:#FCE4EC,stroke:#C2185B,stroke-width:2px,color:#000
    classDef infra fill:#F3E5F5,stroke:#7B1FA2,stroke-width:2px,color:#000

    class User,ICAOPortal external
    class Frontend,APIGateway dmz
    class PKD,PA,Relay,Monitoring,AI app
    class DB,LDAPCluster data
    class Docker infra

    classDef containerStyle fill:none,stroke:#546E7A,stroke-width:2px,stroke-dasharray:5 5
    class L1,L2,L3,L4,L5 containerStyle

    classDef nodeContainer fill:none,stroke:none
    class L1Nodes,L2Nodes,L3Nodes,L4Nodes nodeContainer
```

**Architecture Highlights**:

1. **5-Layer Hierarchy**: 명확한 계층 분리로 관심사 분리 (Separation of Concerns)
2. **Minimal Coupling**: 각 계층은 바로 아래 계층만 의존 (Vertical Flow)
3. **Gateway Pattern**: API Gateway (nginx)로 단일 진입점 제공, App-level LDAP SLB
4. **Data Abstraction**: LDAP MMR 클러스터로 2개 Master 노드 통합
5. **Simplified Topology**: 연결선 최소화로 시스템 복잡도 감소

### Layer Description

| Layer | Purpose | Components | Key Characteristics |
|-------|---------|------------|---------------------|
| **Layer 1: External** | 외부 접근 및 연계 | User, ICAO Portal | Public Internet |
| **Layer 2: DMZ** | 공개 서비스 영역 | Frontend, API Gateway | Ports 3080, 80/443/8080 |
| **Layer 3: Application** | 비즈니스 로직 처리 | PKD, PA, Relay, Monitoring (C++20), AI (Python) | Internal Network |
| **Layer 4: Data** | 데이터 영속성 | PostgreSQL/Oracle, LDAP MMR | Internal Storage + App-level SLB |
| **Layer 5: Infrastructure** | 컨테이너 런타임 | Docker Compose | Platform Layer |

### Data Flow Summary

**Request Flow** (Top → Bottom):
```
User → Frontend → API Gateway → Services (PKD/PA/Relay) → Data (PostgreSQL/LDAP)
```

**Service Architecture**:
- **5 Microservices**: PKD Management (:8081), PA Service (:8082), PKD Relay (:8083), Monitoring (:8084), AI Analysis (:8085)
- **2 Data Stores**: PostgreSQL/Oracle (31,212 certificates), LDAP MMR Cluster (Master 1+2)
- **1 Gateway Node**: API Gateway (HTTP :80 / HTTPS :443), App-level LDAP SLB
- **1 Frontend**: React 19 SPA (24 pages)

---

## Microservices Architecture

### 1. PKD Management Service (Port 8081)

```mermaid
flowchart LR
    subgraph API["API 레이어"]
        Upload["업로드 API<br/>LDIF/ML"]
        Cert["인증서 API<br/>검색/내보내기"]
        Health["헬스 API<br/>DB/LDAP"]
        ICAO["ICAO 동기화 API<br/>버전"]
    end

    subgraph Domain["도메인 레이어"]
        UploadDomain["업로드 도메인<br/>비즈니스 로직"]
        CertDomain["인증서 도메인<br/>검증"]
        IcaoDomain["ICAO 도메인<br/>버전 추적"]
    end

    subgraph Service["서비스 레이어"]
        UploadService["업로드 서비스<br/>파일 처리"]
        CertService["인증서 서비스<br/>LDAP 작업"]
        IcaoService["ICAO 서비스<br/>HTML 파싱"]
    end

    subgraph Repo["저장소 레이어"]
        UploadRepo["업로드 저장소<br/>PostgreSQL"]
        CertRepo["인증서 저장소<br/>LDAP"]
        IcaoRepo["ICAO 저장소<br/>PostgreSQL"]
    end

    subgraph Infra["인프라스트럭처"]
        LDIF["LDIF 처리기"]
        CMS["CMS 파서"]
        HTTP["HTTP 클라이언트"]
        HTML["HTML 파서"]
        Email["이메일 발송"]
    end

    subgraph Strategy["전략"]
        Auto["자동 처리"]
        Manual["수동 처리"]
    end

    subgraph Data["데이터 저장소"]
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

    style API fill:#1976D2,stroke:#0D47A1,stroke-width:2px,color:#fff
    style Domain fill:#388E3C,stroke:#1B5E20,stroke-width:2px,color:#fff
    style Service fill:#7B1FA2,stroke:#4A148C,stroke-width:2px,color:#fff
    style Repo fill:#E65100,stroke:#BF360C,stroke-width:2px,color:#fff
    style Infra fill:#0097A7,stroke:#006064,stroke-width:2px,color:#fff
    style Strategy fill:#5D4037,stroke:#3E2723,stroke-width:2px,color:#fff
    style Data fill:#C2185B,stroke:#880E4F,stroke-width:2px,color:#fff

    style ICAO fill:#FFA726,stroke:#F57C00,stroke-width:2px,color:#000
    style IcaoDomain fill:#FFA726,stroke:#F57C00,stroke-width:2px,color:#000
    style IcaoService fill:#FFA726,stroke:#F57C00,stroke-width:2px,color:#000
    style IcaoRepo fill:#FFA726,stroke:#F57C00,stroke-width:2px,color:#000
    style HTTP fill:#FFA726,stroke:#F57C00,stroke-width:2px,color:#000
    style HTML fill:#FFA726,stroke:#F57C00,stroke-width:2px,color:#000
    style Email fill:#FFA726,stroke:#F57C00,stroke-width:2px,color:#000
```

**Key Features**:
- Clean Architecture (6 Layers) with ServiceContainer (centralized DI, pimpl pattern)
- Handler Pattern: UploadHandler (10), UploadStatsHandler (11), CertificateHandler (12), AuthHandler, IcaoHandler, MiscHandler (health, audit, validation, PA proxy, info, ICAO)
- Strategy Pattern (AUTO/MANUAL Mode)
- Query Helpers (`common::db::`) — database-agnostic utility functions across 15 repositories
- ICAO Auto Sync with Daily Scheduler
- LDIF/Master List Parsing + Individual Certificate Upload (PEM/DER/P7B/DL/CRL)
- Trust Chain Validation (icao::validation shared library)
- Certificate Search & Export (DIT-structured ZIP)
- JWT Authentication + RBAC (admin/user)
- DSC_NC Report, PA Lookup API
- Multi-DBMS (PostgreSQL + Oracle)

---

### 2. PA Service (Port 8082)

```mermaid
flowchart LR
    subgraph API["API 레이어"]
        Verify["PA 검증 API<br/>SOD and DG"]
        ParseSOD["SOD 파싱<br/>메타데이터"]
        ParseDG1["DG1 파싱<br/>MRZ"]
        ParseDG2["DG2 파싱<br/>얼굴"]
        Stats["통계<br/>지표"]
    end

    subgraph Logic["비즈니스 로직"]
        SODVerify["SOD 검증기<br/>CMS"]
        HashVerify["해시 검증기<br/>DG"]
        ChainVerify["신뢰 체인<br/>CSCA-DSC"]
        MRZParser["MRZ 파서<br/>TD1/TD2/TD3"]
        ImageExtractor["이미지 추출<br/>JPEG"]
    end

    subgraph DataAccess["데이터 접근"]
        PARepo["PA 저장소<br/>PostgreSQL"]
        LDAPRepo["LDAP 저장소<br/>인증서"]
    end

    subgraph Crypto["암호화 레이어"]
        OpenSSL["OpenSSL 3.x<br/>CMS/X.509"]
    end

    subgraph DataStore["데이터 저장소"]
        DB[("PostgreSQL")]
        LDAPS[("LDAP")]
    end

    Verify --> SODVerify
    Verify --> HashVerify
    Verify --> ChainVerify

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

    style API fill:#1976D2,stroke:#0D47A1,stroke-width:2px,color:#fff
    style Logic fill:#388E3C,stroke:#1B5E20,stroke-width:2px,color:#fff
    style DataAccess fill:#E65100,stroke:#BF360C,stroke-width:2px,color:#fff
    style Crypto fill:#D32F2F,stroke:#B71C1C,stroke-width:2px,color:#fff
    style DataStore fill:#C2185B,stroke:#880E4F,stroke-width:2px,color:#fff

    style Verify fill:#66BB6A,stroke:#388E3C,stroke-width:2px,color:#000
    style OpenSSL fill:#FF7043,stroke:#D84315,stroke-width:2px,color:#fff
```

**Key Features**:
- ICAO 9303 PA Compliance (Part 10, 11, 12)
- SOD CMS Verification + DG Hash Validation
- Trust Chain Validation (icao::validation shared library)
- CRL Revocation Checking (RFC 5280)
- DSC Auto-Registration (PA_EXTRACTED source type)
- DSC Non-Conformant (nc-data) Support
- MRZ Parsing (TD1/TD2/TD3) + Face Image Extraction (JPEG2000 conversion)
- ServiceContainer (pImpl DI), Handler Pattern: PaHandler (9), HealthHandler (3), InfoHandler (4)
- Multi-DBMS (PostgreSQL + Oracle)

---

### 3. PKD Relay Service (Port 8083)

```mermaid
flowchart LR
    subgraph API["API 레이어"]
        RelayHealth["Relay Health<br/>상태"]
        RelayStatus["Relay Status<br/>통계"]
        IcaoCheck["ICAO Check<br/>버전"]
    end

    subgraph Domain["도메인 레이어"]
        IcaoDomain["ICAO 도메인<br/>버전 추적"]
        RelayDomain["Relay 도메인<br/>외부 연계"]
    end

    subgraph Service["서비스 레이어"]
        IcaoService["ICAO 서비스<br/>HTML 파싱"]
        RelayService["Relay 서비스<br/>요청 중계"]
    end

    subgraph Repo["저장소 레이어"]
        IcaoRepo["ICAO 저장소<br/>PostgreSQL"]
    end

    subgraph Infra["인프라스트럭처"]
        HTTP["HTTP 클라이언트"]
        HTML["HTML 파서"]
        Email["이메일 발송"]
    end

    subgraph Scheduler["스케줄러"]
        CronJob["Cron Job<br/>08:00 KST"]
    end

    subgraph DataStore["데이터 저장소"]
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

    style API fill:#1976D2,stroke:#0D47A1,stroke-width:2px,color:#fff
    style Domain fill:#388E3C,stroke:#1B5E20,stroke-width:2px,color:#fff
    style Service fill:#7B1FA2,stroke:#4A148C,stroke-width:2px,color:#fff
    style Repo fill:#E65100,stroke:#BF360C,stroke-width:2px,color:#fff
    style Infra fill:#0097A7,stroke:#006064,stroke-width:2px,color:#fff
    style Scheduler fill:#00796B,stroke:#004D40,stroke-width:2px,color:#fff
    style DataStore fill:#C2185B,stroke:#880E4F,stroke-width:2px,color:#fff

    style IcaoCheck fill:#FFA726,stroke:#F57C00,stroke-width:2px,color:#000
    style IcaoDomain fill:#FFA726,stroke:#F57C00,stroke-width:2px,color:#000
    style IcaoService fill:#FFA726,stroke:#F57C00,stroke-width:2px,color:#000
```

**Key Features**:
- ICAO PKD 외부 연계 (Version Detection)
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

### PostgreSQL Database Schema

```mermaid
erDiagram
    UPLOADED_FILE ||--o{ CERTIFICATE : contains
    UPLOADED_FILE ||--o{ CRL : contains
    UPLOADED_FILE ||--o{ MASTER_LIST : contains
    CERTIFICATE ||--o{ VALIDATION_RESULT : validates

    UPLOADED_FILE {
        uuid id PK
        varchar original_file_name
        varchar file_type
        bigint file_size
        varchar file_hash
        varchar processing_mode
        varchar status
        timestamp upload_timestamp
        int csca_count
        int dsc_count
        int dsc_nc_count
        int crl_count
        int ml_count
    }

    CERTIFICATE {
        uuid id PK
        uuid upload_id FK
        varchar country_code
        varchar cert_type
        bytea certificate_der
        varchar subject_dn
        varchar issuer_dn
        varchar serial_number
        varchar fingerprint_sha256
        varchar source_type
        varchar validation_status
        varchar signature_algorithm
        varchar public_key_algorithm
        int public_key_size
        boolean is_self_signed
        timestamp not_before
        timestamp not_after
        boolean stored_in_ldap
    }

    CRL {
        uuid id PK
        uuid upload_id FK
        varchar country_code
        bytea crl_der
        varchar issuer_dn
        timestamp this_update
        timestamp next_update
        boolean stored_in_ldap
    }

    MASTER_LIST {
        uuid id PK
        uuid upload_id FK
        varchar country_code
        bytea ml_der
        varchar subject_dn
        timestamp not_before
        timestamp not_after
        boolean stored_in_ldap
    }

    VALIDATION_RESULT {
        uuid id PK
        varchar certificate_id
        varchar validation_status
        varchar trust_chain_path
        varchar crl_check_status
        varchar csca_subject_dn
        varchar csca_fingerprint
        text message
        timestamp validated_at
    }

    AUTH_AUDIT_LOG {
        uuid id PK
        varchar event_type
        varchar username
        varchar ip_address
        varchar user_agent
        boolean success
        timestamp created_at
    }

    OPERATION_AUDIT_LOG {
        uuid id PK
        varchar operation_type
        varchar entity_type
        varchar entity_id
        varchar username
        varchar ip_address
        jsonb details
        timestamp created_at
    }

    DEVIATION_LIST {
        uuid id PK
        uuid upload_id FK
        varchar country_code
        bytea dl_binary
        varchar issuer_dn
        timestamp created_at
    }

    RECONCILIATION_LOG {
        uuid id PK
        uuid summary_id FK
        varchar entity_type
        varchar fingerprint
        varchar action
        varchar status
        text message
        timestamp created_at
    }

    PA_VERIFICATION {
        uuid id PK
        varchar country_code
        varchar sod_issuer
        varchar verification_status
        jsonb dg_hashes
        jsonb validation_steps
        timestamp verified_at
    }

    SYNC_STATUS {
        uuid id PK
        int db_csca_count
        int db_dsc_count
        int ldap_csca_count
        int ldap_dsc_count
        int total_discrepancy
        jsonb db_country_stats
        jsonb ldap_country_stats
        varchar status
        timestamp checked_at
    }

    RECONCILIATION_SUMMARY {
        uuid id PK
        varchar triggered_by
        varchar status
        int csca_added
        int dsc_added
        int failed_count
        int check_duration_ms
        timestamp started_at
        timestamp completed_at
    }

    ICAO_PKD_VERSIONS {
        uuid id PK
        varchar collection_type
        varchar file_name
        int file_version
        varchar status
        timestamp detected_at
        timestamp imported_at
        text import_upload_id FK
        boolean notification_sent
    }
```

**총 테이블**: 14개
- **Upload & Certificate**: uploaded_file, certificate, crl, master_list, deviation_list
- **Validation**: validation_result
- **PA**: pa_verification
- **Audit**: auth_audit_log, operation_audit_log
- **Sync**: sync_status, reconciliation_summary, reconciliation_log
- **ICAO Sync**: icao_pkd_versions

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

    style Root fill:#1976D2,stroke:#0D47A1,stroke-width:2px,color:#fff
    style Data fill:#43A047,stroke:#2E7D32,stroke-width:2px,color:#fff
    style NCData fill:#E53935,stroke:#C62828,stroke-width:2px,color:#fff
    style CSCA1 fill:#FFB74D,stroke:#F57C00,stroke-width:2px
    style DSC1 fill:#4DD0E1,stroke:#0097A7,stroke-width:2px
    style CRL1 fill:#BA68C8,stroke:#8E24AA,stroke-width:2px
    style ML1 fill:#81C784,stroke:#388E3C,stroke-width:2px
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

        subgraph "Page Components (21 Pages)"
            Dashboard[Dashboard.tsx<br/>시스템 개요]
            FileUpload[FileUpload.tsx<br/>파일 업로드]
            CertUpload[CertificateUpload.tsx<br/>개별 인증서]
            CertSearch[CertificateSearch.tsx<br/>인증서 조회]
            DscNcReport[DscNcReport.tsx<br/>DSC_NC 보고서]
            UploadHistory[UploadHistory.tsx<br/>업로드 이력]
            UploadDetail[UploadDetail.tsx<br/>업로드 상세]
            UploadDashboard[UploadDashboard.tsx<br/>통계 대시보드]
            PAVerify[PAVerify.tsx<br/>PA 검증 수행]
            PAHistory[PAHistory.tsx<br/>검증 이력]
            PADetail[PADetail.tsx<br/>PA 상세]
            PADashboard[PADashboard.tsx<br/>PA 통계]
            SyncDashboard[SyncDashboard.tsx<br/>동기화 상태]
            IcaoStatus[IcaoStatus.tsx<br/>ICAO 버전 상태]
            SystemMonitoring[MonitoringDashboard.tsx<br/>시스템 모니터링]
            Login[Login.tsx<br/>JWT 인증]
            UserMgmt[UserManagement.tsx<br/>사용자 관리]
            AuditLog[AuditLog.tsx<br/>인증 감사]
            OpAuditLog[OperationAuditLog.tsx<br/>운영 감사]
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
            FlagUtils[Flag SVG Utils<br/>국기 아이콘]
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

    style IcaoStatus fill:#FFD54F,stroke:#F57C00,stroke-width:3px
    style FlagUtils fill:#FFD54F,stroke:#F57C00,stroke-width:2px
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
    subgraph Gateway["Nginx API 게이트웨이 포트 8080"]
        subgraph Routes["라우팅 규칙"]
            Route1["PKD 라우트<br/>upload/cert/health/sync"]
            Route2["PA 라우트<br/>pa/*"]
            Route3["Relay 라우트<br/>relay/*"]
            Route4["API 문서<br/>api-docs"]
        end

        subgraph Features["주요 기능"]
            RateLimit["속도 제한<br/>100 req/s per IP"]
            Gzip["Gzip 압축<br/>80% 절감"]
            SSE["SSE 지원<br/>실시간"]
            Upload["파일 업로드<br/>최대 100MB"]
            Swagger["Swagger UI<br/>OpenAPI 3.0"]
        end

        subgraph Proxy["프록시 설정"]
            Timeout["타임아웃<br/>30s / 300s"]
            Buffer["버퍼<br/>8 x 16KB"]
            Keepalive["Keepalive<br/>32 연결"]
        end

        subgraph Errors["에러 처리"]
            Error502["502 Bad Gateway"]
            Error503["503 Unavailable"]
            Error504["504 Timeout"]
        end
    end

    Route1 --> PKD["PKD 관리<br/>8081"]
    Route2 --> PA["PA 서비스<br/>8082"]
    Route3 --> RelaySvc["Relay 서비스<br/>8083"]
    Route4 --> Swagger

    RateLimit -.-> Route1
    RateLimit -.-> Route2
    RateLimit -.-> Route3

    Gzip -.-> PKD
    Gzip -.-> PA
    Gzip -.-> RelaySvc

    SSE -.-> PKD
    Upload -.-> PKD

    style Route1 fill:#42A5F5,stroke:#1976D2,stroke-width:2px,color:#000
    style RateLimit fill:#FF5722,stroke:#D84315,stroke-width:2px,color:#fff
    style SSE fill:#9C27B0,stroke:#7B1FA2,stroke-width:2px,color:#fff
```

**Security Features**:
- ✅ Backend Service Isolation (Internal Network Only)
- ✅ Rate Limiting (DDoS Protection)
- ✅ Header Sanitization
- ✅ CORS Policy
- ✅ Request/Response Logging

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

    style Input fill:#E3F2FD,stroke:#1976D2,stroke-width:2px
    style Detect fill:#FFE0B2,stroke:#F57C00,stroke-width:2px
    style Output fill:#C8E6C9,stroke:#388E3C,stroke-width:2px
```

---

### ICAO Auto Sync Flow (v1.7.0)

```mermaid
sequenceDiagram
    participant Cron as Cron Job<br/>(매일 08:00)
    participant Script as Shell Script<br/>icao-version-check.sh
    participant API as API Gateway<br/>:8080
    participant PKD as PKD Management<br/>:8081
    participant HTTP as HTTP Client<br/>ICAO Portal
    participant Parser as HTML Parser<br/>Version Extractor
    participant DB as PostgreSQL<br/>icao_pkd_versions
    participant Dashboard as React Frontend<br/>ICAO 버전 상태

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
    subgraph External["🌐 External Access"]
        User["👤 User Browser<br/>Port 3000"]
        APIClient["🔌 API Client<br/>Port 8080"]
        LDAPClient["📂 LDAP Client<br/>Port 389"]
    end

    subgraph DockerNetwork["🐳 Docker Network: icao-network"]
        subgraph Presentation["📱 Presentation Layer"]
            Frontend["Frontend<br/>━━━━━━━━<br/>nginx:alpine<br/>React 19 SPA<br/>━━━━━━━━<br/>:3000"]
        end

        subgraph Gateway["🔀 Gateway Layer"]
            APIGateway["API Gateway<br/>━━━━━━━━<br/>nginx:1.25<br/>Reverse Proxy<br/>━━━━━━━━<br/>:8080"]
        end

        subgraph Application["🔧 Application Layer"]
            PKD["PKD Management<br/>━━━━━━━━<br/>Custom C++<br/>Drogon 1.9<br/>━━━━━━━━<br/>:8081"]
            PA["PA Service<br/>━━━━━━━━<br/>Custom C++<br/>Drogon 1.9<br/>━━━━━━━━<br/>:8082"]
            Relay["PKD Relay<br/>━━━━━━━━<br/>Custom C++<br/>Drogon 1.9<br/>━━━━━━━━<br/>:8083"]
            Mon["Monitoring<br/>━━━━━━━━<br/>Custom C++<br/>Drogon 1.9<br/>━━━━━━━━<br/>:8084"]
        end

        subgraph DataLayer["💾 Data Layer"]
            PG["PostgreSQL / Oracle<br/>━━━━━━━━<br/>postgres:15 / xe:21c<br/>Multi-DBMS<br/>━━━━━━━━<br/>:5432 / :1521"]
            LDAP1["OpenLDAP 1<br/>━━━━━━━━<br/>osixia:1.5.0<br/>MMR Primary<br/>━━━━━━━━<br/>:3891"]
            LDAP2["OpenLDAP 2<br/>━━━━━━━━<br/>osixia:1.5.0<br/>MMR Secondary<br/>━━━━━━━━<br/>:3892"]
        end
    end

    subgraph Storage["💿 Persistent Storage"]
        PGData[("📦 postgres<br/>Database Files")]
        LDAP1Data[("📦 openldap1<br/>Directory Data")]
        LDAP2Data[("📦 openldap2<br/>Directory Data")]
        UploadData[("📦 pkd-uploads<br/>LDIF/ML Files")]
        LogData[("📦 logs<br/>Service Logs")]
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
    style User fill:#E3F2FD,stroke:#1976D2,stroke-width:2px
    style APIClient fill:#E3F2FD,stroke:#1976D2,stroke-width:2px

    %% Styling - Presentation
    style Frontend fill:#81C784,stroke:#388E3C,stroke-width:3px

    %% Styling - Gateway
    style APIGateway fill:#FF9800,stroke:#F57C00,stroke-width:3px

    %% Styling - Application
    style PKD fill:#42A5F5,stroke:#1976D2,stroke-width:3px
    style PA fill:#42A5F5,stroke:#1976D2,stroke-width:3px
    style Relay fill:#42A5F5,stroke:#1976D2,stroke-width:3px
    style Mon fill:#42A5F5,stroke:#1976D2,stroke-width:3px

    %% Styling - Data Layer
    style PG fill:#7E57C2,stroke:#5E35B1,stroke-width:3px
    style LDAP1 fill:#26A69A,stroke:#00796B,stroke-width:3px
    style LDAP2 fill:#26A69A,stroke:#00796B,stroke-width:3px

    %% Styling - Storage
    style PGData fill:#F5F5F5,stroke:#9E9E9E,stroke-width:2px
    style LDAP1Data fill:#F5F5F5,stroke:#9E9E9E,stroke-width:2px
    style LDAP2Data fill:#F5F5F5,stroke:#9E9E9E,stroke-width:2px
    style UploadData fill:#F5F5F5,stroke:#9E9E9E,stroke-width:2px
    style LogData fill:#F5F5F5,stroke:#9E9E9E,stroke-width:2px
```

**Architecture Highlights**:

1. **Layered Design**: 명확한 4계층 구조 (Presentation → Gateway → Application → Data)
2. **Gateway Pattern**: API Gateway (nginx)로 서비스 라우팅 및 리버스 프록시
3. **App-Level SLB**: LDAP Software Load Balancing (서비스 내부 라운드로빈, HAProxy 제거)
4. **Microservices**: 4개의 독립적인 C++ 서비스 (PKD, PA, Relay, Monitoring)
5. **Multi-DBMS**: PostgreSQL 15 / Oracle XE 21c 런타임 전환 (DB_TYPE)
6. **MMR Replication**: OpenLDAP Multi-Master 복제로 고가용성 보장
7. **Bind Mounts**: 모든 데이터는 호스트 파일시스템에 영구 저장

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

**Total Resources**: 10 cores, 10.5GB RAM (PostgreSQL mode), Oracle 컨테이너 추가 시 +2GB

---

### Luckfox ARM64 Deployment

```mermaid
graph TB
    subgraph CICD["🚀 CI/CD Pipeline"]
        GHA["GitHub Actions<br/>━━━━━━━━<br/>ARM64 Build<br/>QEMU + Buildx<br/>━━━━━━━━<br/>~2 hours"]
        Artifacts["Artifacts<br/>━━━━━━━━<br/>OCI Format<br/>tar.gz<br/>━━━━━━━━<br/>30 days"]
        Convert["skopeo<br/>━━━━━━━━<br/>OCI → Docker<br/>override-arch<br/>━━━━━━━━<br/>~30 sec"]
    end

    subgraph Deploy["📦 Deployment"]
        Transfer["sshpass<br/>━━━━━━━━<br/>SCP Transfer<br/>to Luckfox<br/>━━━━━━━━<br/>~2 min"]
        Load["Docker Load<br/>━━━━━━━━<br/>Import Images<br/>docker load<br/>━━━━━━━━<br/>~1 min"]
    end

    subgraph Luckfox["🖥️ Luckfox Pico ARM64 - 192.168.100.11"]
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

        subgraph Storage3["💾 Persistent Storage"]
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
    style GHA fill:#4CAF50,stroke:#388E3C,stroke-width:3px
    style Artifacts fill:#2196F3,stroke:#1976D2,stroke-width:2px
    style Convert fill:#FF9800,stroke:#F57C00,stroke-width:2px

    %% Styling - Deploy
    style Transfer fill:#9C27B0,stroke:#7B1FA2,stroke-width:2px
    style Load fill:#F44336,stroke:#D32F2F,stroke-width:2px

    %% Styling - Services
    style Frontend3 fill:#81C784,stroke:#388E3C,stroke-width:2px
    style APIGateway3 fill:#FF9800,stroke:#F57C00,stroke-width:2px
    style PKD3 fill:#42A5F5,stroke:#1976D2,stroke-width:2px
    style PA3 fill:#42A5F5,stroke:#1976D2,stroke-width:2px
    style Relay3 fill:#42A5F5,stroke:#1976D2,stroke-width:2px
    style Mon3 fill:#42A5F5,stroke:#1976D2,stroke-width:2px
    style PG3 fill:#7E57C2,stroke:#5E35B1,stroke-width:2px
    style LDAP5 fill:#26A69A,stroke:#00796B,stroke-width:2px
    style LDAP6 fill:#26A69A,stroke:#00796B,stroke-width:2px

    %% Styling - Storage
    style ProjectDir fill:#F5F5F5,stroke:#9E9E9E,stroke-width:2px
    style PGData3 fill:#F5F5F5,stroke:#9E9E9E,stroke-width:2px
    style LDAP5Data fill:#F5F5F5,stroke:#9E9E9E,stroke-width:2px
    style LDAP6Data fill:#F5F5F5,stroke:#9E9E9E,stroke-width:2px
```

**Deployment Workflow**:

1. **GitHub Actions Build** (~2 hours)
   - Multi-stage Dockerfile with vcpkg caching
   - QEMU emulation for ARM64 cross-compilation
   - Output: OCI format images (tar.gz)

2. **Artifact Conversion** (~30 seconds)
   - `skopeo copy --override-arch arm64 oci-archive:... docker-archive:...`
   - OCI format → Docker loadable format

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
| **Image Format** | Docker native | OCI → Docker conversion |

---

## Security Architecture

### Authentication & Authorization

```mermaid
graph TB
    subgraph "보안 레이어 (v2.10.5 Full Audit)"
        subgraph "인증/인가 레이어"
            JWT["JWT Authentication<br/>HS256, 32+ byte secret"]
            RBAC["RBAC<br/>admin / user roles"]
            AuthMiddleware["Auth Middleware<br/>std::regex::optimize"]
            AuditLog["Dual Audit Logging<br/>auth_audit_log + operation_audit_log"]
        end

        subgraph "네트워크 레이어"
            Firewall[호스트 방화벽<br/>iptables]
            Docker[Docker 네트워크 격리<br/>icao-network]
            NginxHeaders["nginx Security Headers<br/>X-Content-Type-Options<br/>X-Frame-Options<br/>Referrer-Policy"]
        end

        subgraph "애플리케이션 레이어"
            RateLimit2[속도 제한<br/>100 req/s per IP]
            CORS[CORS 정책<br/>Same-Origin]
            CSP[콘텐츠 보안 정책<br/>frame-ancestors self]
            NoSystemCall["Command Injection 방지<br/>system()/popen() → Native C API"]
        end

        subgraph "데이터 레이어"
            DBAuth[DB 인증<br/>PostgreSQL / Oracle]
            LDAPAuth["LDAP 바인드<br/>cn=admin DN<br/>DN Escape (RFC 4514)"]
            Encryption[TLS/SSL 지원<br/>프로덕션]
        end

        subgraph "코드 레이어"
            Parameterized["매개변수화 쿼리<br/>100% parameterized SQL"]
            OrderByWhitelist["ORDER BY Whitelist<br/>SQL 인젝션 방지"]
            Base64Validation["Base64 입력 검증<br/>isValidBase64() pre-check"]
            NullChecks["OpenSSL Null Checks<br/>24 allocation sites"]
            BufferOverread["SOD Parser 보호<br/>end pointer boundary"]
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
    DBAuth --> Encryption
    LDAPAuth --> Encryption

    Encryption --> Parameterized
    Parameterized --> OrderByWhitelist
    OrderByWhitelist --> Base64Validation
    Base64Validation --> NullChecks
    NullChecks --> BufferOverread

    style JWT fill:#F44336,stroke:#C62828,stroke-width:2px,color:#fff
    style Encryption fill:#4CAF50,stroke:#388E3C,stroke-width:2px,color:#fff
    style Parameterized fill:#2196F3,stroke:#1976D2,stroke-width:2px,color:#fff
    style NoSystemCall fill:#FF5722,stroke:#E64A19,stroke-width:2px,color:#fff
```

**Security Checklist (v2.10.5 완료)**:
- ✅ JWT Authentication (HS256) + RBAC (admin/user)
- ✅ Dual audit logging (auth_audit_log + operation_audit_log)
- ✅ Backend services not exposed externally (API Gateway only)
- ✅ Rate limiting (DDoS protection)
- ✅ 100% parameterized SQL queries (all 3 services)
- ✅ ORDER BY whitelist validation
- ✅ LIKE query parameter escaping (`escapeSqlWildcards()`)
- ✅ Command injection eliminated — `system()`/`popen()` → Native C API
- ✅ XSS prevention (JSON serialization)
- ✅ CORS policy (configurable)
- ✅ nginx security headers (X-Content-Type-Options, X-Frame-Options, X-XSS-Protection, Referrer-Policy)
- ✅ JWT_SECRET minimum length validation (32 bytes)
- ✅ LDAP DN escape utility (RFC 4514)
- ✅ Base64 input validation (pre-check before decode)
- ✅ SOD parser buffer overread protection (end pointer boundary)
- ✅ OpenSSL null pointer checks (24 allocation sites)
- ✅ Frontend OWASP hardening (DEV-only console.error, credential guards, centralized JWT injection)
- ✅ Credential externalization (.env)
- ✅ File upload validation (MIME type, path sanitization)

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
| Artifact Conversion | skopeo | latest | OCI → Docker format |

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
| **Concurrent Users** | 100 | 1,000+ | Nginx workers × connections |
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

    style GW fill:#4CAF50,stroke:#388E3C,stroke-width:2px
    style Script fill:#FF9800,stroke:#F57C00,stroke-width:2px
    style MonDash fill:#42A5F5,stroke:#1976D2,stroke-width:2px
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

## Future Enhancements

### Completed (Previously Planned)

- ✅ JWT Authentication (v1.8.0)
- ✅ Role-Based Access Control — RBAC admin/user (v1.9.0)
- ✅ Multi-DBMS Support — PostgreSQL + Oracle (v2.6.0)
- ✅ Monitoring Service — DB-independent system metrics (v2.7.1)
- ✅ OWASP Security Hardening — Full audit (v2.10.5)
- ✅ ICAO 9303 Validation Library — 86 unit tests (v2.10.6)

### Phase 1 (Planned)

- 🔜 HTTPS/TLS Support (Let's Encrypt)
- 🔜 Horizontal Scaling (Multiple instances)
- 🔜 Redis Caching Layer

### Phase 2 (Research)

- 🔍 PKD Relay Tier 2 (Semi-automated download from ICAO)
- 🔍 PKD Relay Tier 3 (Full LDAP sync with ICAO membership)
- 🔍 Kubernetes Deployment
- 🔍 Prometheus + Grafana Monitoring
- 🔍 ELK Stack (Elasticsearch, Logstash, Kibana)

---

## Conclusion

ICAO Local PKD v2.13.0은 **마이크로서비스 아키텍처**, **Multi-DBMS**, **ICAO 9303 완전 준수**를 통해 높은 성능, 확장성, 보안성을 제공합니다.

**핵심 강점**:
- ✅ 4개 독립 마이크로서비스 (PKD Management, PA, Relay, Monitoring)
- ✅ Multi-DBMS 지원 — PostgreSQL 15 / Oracle XE 21c 런타임 전환
- ✅ JWT 인증 + RBAC (admin/user) + Dual Audit Logging
- ✅ OWASP 보안 강화 완료 (Command Injection 제거, SQL Injection 방지)
- ✅ icao::validation 공유 라이브러리 (86 단위 테스트, ICAO 9303 Part 12)
- ✅ DB-LDAP 데이터 일관성 보장 (31,212 인증서, 100% 동기화)
- ✅ C++20 고성능 백엔드 + React 19 모던 프론트엔드 (21 페이지)
- ✅ ARM64 CI/CD 파이프라인 (GitHub Actions → Luckfox 배포)
- ✅ Docker 기반 간편한 배포, 99.9% 업타임

---

**Document Created**: 2026-01-20
**Last Rewrite**: 2026-02-17
**Author**: ICAO Local PKD Development Team
**Organization**: SmartCore Inc.
