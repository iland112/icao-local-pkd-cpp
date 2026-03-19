# ICAO Local PKD - Architecture & Design Principles

**Version**: 2.37.0
**Last Updated**: 2026-03-18
**Status**: Active Standard

---

## 핵심 디자인 원칙

### 1. Domain-Driven Design (DDD)
도메인 중심 설계를 통한 비즈니스 로직의 명확한 분리

### 2. Microservice Architecture
독립적으로 배포 가능한 서비스 단위로 시스템 구성

### 3. Strategy Pattern
파일 타입과 인증서 타입에 따른 비즈니스 로직 분리

### 4. Single Responsibility Principle (SRP)
각 클래스와 모듈은 단 하나의 책임만 가짐

### 5. ServiceContainer + Pimpl Pattern (v2.12.0+)
중앙 DI 컨테이너로 모든 의존성 관리. `struct Impl` pimpl 패턴으로 헤더 의존성 최소화

### 6. Handler Extraction Pattern (v2.13.0+)
main.cpp를 최소화하고 모든 HTTP 핸들러를 독립 클래스로 추출

---

## Microservice Architecture

### 서비스 구성

```
services/
├── pkd-management/        # PKD 관리 서비스 (Port: 8081)
│   ├── File Upload & Processing (LDIF, Master List)
│   ├── Certificate Search & Export
│   ├── ICAO Auto Sync
│   └── API Client Authentication (X-API-Key)
│
├── pa-service/            # Passive Authentication 서비스 (Port: 8082)
│   ├── SOD Verification
│   ├── DG1/DG2 Parsing
│   └── CSCA LDAP Lookup
│
├── pkd-relay-service/     # PKD Relay 서비스 (Port: 8083)
│   ├── DB-LDAP Synchronization
│   ├── Auto Reconciliation
│   └── Sync Status Monitoring
│
├── monitoring-service/    # 모니터링 서비스 (Port: 8084)
│   ├── System Resource Monitoring (CPU, Memory, Disk, Network)
│   ├── Service Health Checks (HTTP Probes)
│   └── DB-Independent Architecture (no PostgreSQL/Oracle dependency)
│
└── ai-analysis/           # AI 분석 서비스 (Port: 8085)
    ├── ML Anomaly Detection (Isolation Forest + LOF)
    ├── Forensic Risk Scoring
    └── Pattern Analysis (Python/FastAPI)
```

### 서비스 통신

```
┌──────────────┐
│   Frontend   │ :3000
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ API Gateway  │ :8080 (Nginx)
└──────┬───────┘
       │
       ├──────────────────┬──────────────────┬──────────────────┬──────────────────┐
       ▼                  ▼                  ▼                  ▼                  ▼
┌──────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│     PKD      │   │      PA      │   │  PKD Relay   │   │ Monitoring   │   │     AI       │
│  Management  │   │   Service    │   │   Service    │   │   Service    │   │  Analysis    │
│    :8081     │   │    :8082     │   │    :8083     │   │    :8084     │   │    :8085     │
└──────┬───────┘   └──────┬───────┘   └──────┬───────┘   └──────────────┘   └──────┬───────┘
       │                  │                  │                                      │
       └──────────────────┴──────────────────┴──────────────────────────────────────┘
                         │
                         ▼
              ┌──────────────────────┐
              │    PostgreSQL DB     │
              │    OpenLDAP Cluster  │
              └──────────────────────┘
```

---

## DDD (Domain-Driven Design) 구조

### PKD Management Service 레이어 구조

```
services/pkd-management/src/
│
├── infrastructure/               # 인프라 레이어 (v2.12.0+)
│   ├── service_container.h/.cpp  # ServiceContainer — 중앙 DI (pimpl 패턴)
│   ├── app_config.h              # AppConfig — 환경변수 파싱
│   ├── http/http_client.h        # HTTP 클라이언트
│   └── notification/email_sender.h
│
├── handlers/                     # 핸들러 레이어 (v2.12.0~v2.13.0 main.cpp에서 추출)
│   ├── upload_handler.h/.cpp     # 업로드 API (10 endpoints)
│   ├── upload_stats_handler.h/.cpp # 통계/이력 API (11 endpoints)
│   ├── certificate_handler.h/.cpp  # 인증서 API (20 endpoints)
│   ├── auth_handler.h/.cpp       # 인증 API
│   ├── icao_handler.h/.cpp       # ICAO 동기화 API
│   ├── code_master_handler.h/.cpp  # 코드 마스터 API (6 endpoints)
│   ├── api_client_handler.h/.cpp   # API 클라이언트 관리 (7 endpoints)
│   ├── api_client_request_handler.h/.cpp # API 클라이언트 요청 (5 endpoints)
│   ├── csr_handler.h/.cpp        # CSR 관리 API (6 endpoints)
│   └── misc_handler.h/.cpp       # Health, Audit, Validation, PA proxy, Info
│
├── middleware/                   # 인증 미들웨어
│   └── auth_middleware.h/.cpp    # JWT + API Key 인증
│
├── repositories/                 # 저장소 레이어 (16개 리포지토리)
│   ├── upload_repository.h/.cpp
│   ├── certificate_repository.h/.cpp
│   ├── validation_repository.h/.cpp
│   ├── crl_repository.h/.cpp
│   ├── ldap_certificate_repository.h/.cpp
│   ├── code_master_repository.h/.cpp
│   ├── api_client_repository.h/.cpp
│   ├── pending_dsc_repository.h/.cpp
│   ├── csr_repository.h/.cpp
│   └── ...
│
├── services/                     # 서비스 레이어
│   ├── upload_service.h/.cpp
│   ├── validation_service.h/.cpp
│   ├── certificate_service.h/.cpp
│   ├── icao_sync_service.h/.cpp
│   ├── ldap_storage_service.h/.cpp  # LDAP 저장 (인증서/CRL/ML)
│   └── csr_service.h/.cpp
│
├── adapters/                     # icao::validation 어댑터
│   ├── db_csca_provider.cpp      # DB 기반 CSCA Provider
│   ├── db_crl_provider.cpp       # DB 기반 CRL Provider
│   ├── ldap_csca_provider.cpp    # LDAP 기반 CSCA Provider (PA Lookup)
│   └── ldap_crl_provider.cpp     # LDAP 기반 CRL Provider (PA Lookup)
│
├── auth/                         # 인증 모듈
│   ├── personal_info_crypto.h/.cpp  # PII 암호화 (AES-256-GCM)
│   └── password_hash.h/.cpp     # PBKDF2-HMAC-SHA256
│
├── processing_strategy.h         # 전략 패턴 (AUTO)
├── ldif_processor.h/.cpp         # LDIF 처리기
├── common/
│   ├── masterlist_processor.h    # Master List 처리기
│   └── progress_manager.h/.cpp   # SSE 진행률 관리
│
└── main.cpp                      # 진입점 (최소화: ~430줄, v2.13.0)
```

### DDD 레이어 책임

#### 1. Domain Layer (도메인 레이어)
**책임**: 비즈니스 규칙과 핵심 로직
- **Models**: 비즈니스 엔티티 (Certificate, ICAO Version 등)
- **Domain Services**: 도메인 로직 (검증, 계산 등)

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
    // 비즈니스 규칙: Self-signed 여부 확인
    bool isSelfSigned() const;

    // 비즈니스 규칙: Link Certificate 여부 확인
    bool isLinkCertificate() const;
};
```

#### 2. Repository Layer (저장소 레이어)
**책임**: 데이터 영속성 (DB, LDAP 접근)
- PostgreSQL 데이터 접근
- LDAP 데이터 접근
- 도메인 모델 ↔ 데이터베이스 변환

```cpp
// repositories/ldap_certificate_repository.h
class LdapCertificateRepository {
public:
    // LDAP에서 인증서 조회
    std::optional<Certificate> findByFingerprint(const std::string& fingerprint);

    // LDAP에 인증서 저장
    void save(const Certificate& cert);
};
```

#### 3. Application Service Layer (애플리케이션 서비스 레이어)
**책임**: 유즈케이스 조율 (트랜잭션, 여러 도메인 서비스 조합)
- 업로드 처리 흐름 관리
- 트랜잭션 경계 설정
- 외부 API 호출

```cpp
// services/upload_service.h
class UploadService {
public:
    // 유즈케이스: 파일 업로드 처리
    UploadResult processFileUpload(
        const FileUploadRequest& request,
        ProcessingStrategy* strategy
    );
};
```

#### 4. Infrastructure Layer (인프라스트럭처 레이어)
**책임**: 외부 시스템 연동 (HTTP, 파일 시스템, 외부 API)
- ServiceContainer (중앙 DI)
- Drogon HTTP 핸들러 (handlers/)
- 파일 시스템 접근
- LDAP/PostgreSQL 연결 관리

---

## ServiceContainer Pattern (중앙 DI)

### 설계 원칙 (v2.12.0+)

ServiceContainer는 모든 애플리케이션 의존성을 소유하고 의존성 주입을 제공하는 중앙 컨테이너이다.
기존 main.cpp의 17개 전역 `shared_ptr` 변수를 단일 컨테이너 인스턴스로 대체.

### Pimpl 패턴

헤더에서 `struct Impl` 전방 선언만 노출하여 컴파일 의존성 최소화:

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

    // Non-owning pointer accessors (소유권은 Impl이 보유)
    common::IQueryExecutor* queryExecutor() const;
    repositories::CertificateRepository* certificateRepository() const;
    services::UploadService* uploadService() const;
    handlers::UploadHandler* uploadHandler() const;
    // ... 기타 접근자

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

    // Repositories (16개)
    std::shared_ptr<repositories::UploadRepository> uploadRepository;
    std::shared_ptr<repositories::CertificateRepository> certificateRepository;
    // ...

    // Services (8개)
    std::shared_ptr<services::UploadService> uploadService;
    // ...

    // Handlers (9개)
    std::shared_ptr<handlers::UploadHandler> uploadHandler;
    // ...
};
```

### 초기화 순서

엄격한 의존성 순서로 초기화:

```
Phase 0: PII Encryption (개인정보보호법)
Phase 1: LDAP Connection Pool
Phase 2: Certificate Service (LDAP-based search)
Phase 3: Database Connection Pool + Query Executor
Phase 4: Repositories (16개, 모두 QueryExecutor 의존)
Phase 4.5: LDAP Storage Service
Phase 5: ICAO Sync Module
Phase 6: Business Logic Services
Phase 7: Handlers (9개)
Phase 8: Ensure Admin User
```

### 리소스 해제

`shutdown()` 메서드에서 역순으로 모든 리소스 해제 (소멸자가 자동 호출):

```
Handlers → Services → LDAP Providers → Repositories → QueryExecutor → LDAP Pool → DB Pool
```

### 4개 서비스 적용 현황

| 서비스 | ServiceContainer | 주요 구성 |
|--------|:----------------:|-----------|
| PKD Management | O | 16 repos, 8 services, 9 handlers |
| PA Service | O | 4 repos, 2 parsers, 3 services |
| PKD Relay | O | 5 repos, 3 services, SyncScheduler |
| Monitoring | - | DB 비의존 (직접 HTTP/system 호출) |

---

## main.cpp 최소화 패턴 (v2.13.0+)

### 원칙

main.cpp는 최소한의 오케스트레이션 레이어로만 구성:

```
설정 로드 → ServiceContainer 초기화 → 라우트 등록 → 서버 실행
```

### 구조 (PKD Management, ~430줄)

```cpp
// main.cpp — 최소화된 진입점
infrastructure::ServiceContainer* g_services = nullptr;

namespace {
    AppConfig appConfig;

    void initializeLogging() { /* spdlog 설정 */ }
    void printBanner() { /* 배너 출력 */ }
    Json::Value checkDatabase() { /* DB 헬스 체크 */ }
    Json::Value checkLdap() { /* LDAP 헬스 체크 */ }

    void registerRoutes() {
        auto& app = drogon::app();

        // 각 핸들러의 registerRoutes() 호출
        if (g_services && g_services->authHandler())
            g_services->authHandler()->registerRoutes(app);
        if (g_services && g_services->uploadHandler())
            g_services->uploadHandler()->registerRoutes(app);
        if (g_services && g_services->certificateHandler())
            g_services->certificateHandler()->registerRoutes(app);
        // ... 기타 핸들러

        // MiscHandler (health, audit 등 — ServiceContainer 미관리)
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

### Handler Extraction 패턴

모든 핸들러는 `registerRoutes(HttpAppFramework&)` 메서드를 제공:

```cpp
// handlers/upload_handler.h
class UploadHandler {
public:
    UploadHandler(UploadService*, ValidationService*, ...);

    // main.cpp에서 호출
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    void handleLdifUpload(const HttpRequestPtr&, ResponseCallback&&);
    void handleMasterListUpload(const HttpRequestPtr&, ResponseCallback&&);
    // ... 10 endpoints
};
```

### 4개 서비스 main.cpp 규모

| 서비스 | v2.11.0 | v2.13.0 | 감소율 |
|--------|---------|---------|--------|
| PKD Management | 8,095줄 | ~430줄 | -94.7% |
| PA Service | 2,800줄 | ~281줄 | -90.0% |
| PKD Relay | 1,644줄 | ~457줄 | -72.2% |
| Monitoring | 586줄 | ~93줄 | -84.1% |

---

## Strategy Pattern (전략 패턴)

### 1. Processing Strategy - 파일 처리 모드

```cpp
// processing_strategy.h

// 추상 전략 인터페이스
class ProcessingStrategy {
public:
    virtual void processLdifEntries(...) = 0;
    virtual void processMasterListContent(...) = 0;
};

// 구체 전략: AUTO 모드 (v2.25.0+ MANUAL 모드 제거)
class AutoProcessingStrategy : public ProcessingStrategy {
    // 한 번에 처리: Parse → Validate → DB → LDAP
};

// Factory Pattern
class ProcessingStrategyFactory {
    static std::unique_ptr<ProcessingStrategy> create(const std::string& mode);
};
```

**사용 예시**:
```cpp
// upload_handler.cpp
auto strategy = ProcessingStrategyFactory::create("AUTO");
strategy->processLdifEntries(uploadId, entries, conn, ld);
```

### 2. Certificate Type Strategy - 인증서 타입별 검증

```cpp
// 각 인증서 타입별로 검증 전략 분리

// CSCA 검증 전략
ValidationResult validateCscaCertificate(X509* cert) {
    // CSCA 검증 로직 (Self-signed, CA:TRUE, keyCertSign)
}

// DSC 검증 전략
ValidationResult validateDscCertificate(X509* cert, PGconn* conn) {
    // DSC 검증 로직 (Trust Chain, CRL check)
}

// Link Certificate 검증 전략
bool isLinkCertificate(X509* cert) {
    // Link Certificate 감지 로직 (CA:TRUE, keyCertSign, not self-signed)
}
```

### 3. File Type Strategy - 파일 타입별 처리

```cpp
// LDIF 파일 처리
class LdifProcessor {
    ProcessingCounts process(const std::string& content);
};

// Master List 파일 처리
class MasterListProcessor {
    MasterListStats process(const std::vector<uint8_t>& content);
};
```

---

## Single Responsibility Principle (SRP)

### 단일 책임 예시

#### GOOD: 책임 분리
```cpp
// 1. LDIF 파싱만 담당
class LdifParser {
    std::vector<LdifEntry> parse(const std::string& content);
};

// 2. 인증서 검증만 담당
class CertificateValidator {
    ValidationResult validate(X509* cert);
};

// 3. LDAP 저장만 담당
class LdapCertificateRepository {
    void save(const Certificate& cert);
};

// 4. 전체 흐름 조율 (Orchestration)
class UploadService {
    void processUpload() {
        auto entries = ldifParser.parse(content);      // 파싱
        auto result = validator.validate(cert);        // 검증
        repository.save(cert);                         // 저장
    }
};
```

#### BAD: 책임 혼재
```cpp
// 한 클래스에서 파싱, 검증, 저장 모두 수행 (SRP 위반)
class EverythingProcessor {
    void processEverything(const std::string& content) {
        // 파싱
        auto entries = /* parse LDIF */;

        // 검증
        auto result = /* validate certificate */;

        // DB 저장
        /* save to PostgreSQL */;

        // LDAP 저장
        /* save to LDAP */;
    }
};
```

### 클래스 책임 정의

| 클래스/모듈 | 단일 책임 |
|------------|---------|
| `LdifProcessor` | LDIF 파일 파싱 |
| `MasterListProcessor` | Master List CMS 파싱 |
| `AutoProcessingStrategy` | AUTO 모드 처리 흐름 |
| `Certificate` (Domain Model) | 인증서 도메인 규칙 |
| `LdapCertificateRepository` | LDAP 데이터 접근 |
| `CertificateValidator` | 인증서 검증 로직 |
| `TrustChainBuilder` | Trust Chain 구축 |
| `CscaCache` | CSCA 캐시 관리 |
| `ServiceContainer` | 의존성 소유 및 초기화 |
| `UploadHandler` | 업로드 HTTP 핸들러 |
| `LdapStorageService` | LDAP 쓰기 연산 |

---

## 코드 구조 규칙

### 1. 파일 조직 규칙

```
서비스명/
├── src/
│   ├── infrastructure/         # 인프라 레이어 (ServiceContainer, AppConfig)
│   │
│   ├── handlers/               # HTTP 핸들러 (registerRoutes 패턴)
│   │
│   ├── middleware/              # 인증 미들웨어
│   │
│   ├── domain/                 # 도메인 레이어 (비즈니스 핵심)
│   │   ├── models/             # 엔티티
│   │   └── services/           # 도메인 서비스
│   │
│   ├── repositories/           # 데이터 접근 (LDAP, PostgreSQL)
│   │
│   ├── services/               # 애플리케이션 서비스
│   │
│   ├── adapters/               # Provider 어댑터 (icao::validation)
│   │
│   ├── auth/                   # 인증/암호화 모듈
│   │
│   ├── common/                 # 유틸리티 (공통 함수)
│   │
│   ├── {strategy_name}_strategy.h   # 전략 패턴
│   ├── {processor_name}_processor.h # 프로세서
│   │
│   └── main.cpp               # 최소 진입점 (~430줄)
│
└── tests/                     # 단위 테스트
```

### 2. 네이밍 규칙

#### 클래스 네이밍
- **Domain Model**: `Certificate`, `IcaoVersion` (명사)
- **Repository**: `LdapCertificateRepository`, `CrlRepository`
- **Service**: `UploadService`, `CertificateService`
- **Handler**: `UploadHandler`, `CertificateHandler` (v2.12.0+)
- **Strategy**: `AutoProcessingStrategy`
- **Processor**: `LdifProcessor`, `MasterListProcessor`
- **Validator**: `CertificateValidator`, `TrustChainValidator`
- **Container**: `ServiceContainer` (DI 컨테이너)

#### 함수 네이밍
- **도메인 규칙**: `isSelfSigned()`, `isLinkCertificate()`
- **Repository**: `findByFingerprint()`, `save()`, `findAll()`
- **Service**: `processUpload()`, `validateCertificate()`
- **Handler**: `handleLdifUpload()`, `handleCertificateSearch()`
- **Route Registration**: `registerRoutes(HttpAppFramework&)`
- **Strategy**: `processLdifEntries()`, `processMasterListContent()`

### 3. 의존성 방향

```
┌─────────────────────────────────────────┐
│     main.cpp (Minimal Orchestration)    │
│    config → DI → routes → run          │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│   ServiceContainer (DI + Handlers)      │
│     Owns all dependencies (pimpl)       │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│      Application Service Layer          │
│     (UploadService, etc.)               │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│         Domain Layer                    │
│  (Certificate, CertificateValidator)    │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│    Infrastructure Layer                 │
│  (LdapRepository, PostgresRepository)   │
└─────────────────────────────────────────┘
```

**의존성 규칙**:
- Domain Layer는 다른 레이어를 의존하지 않음 (Pure Business Logic)
- Application Service는 Domain Layer 의존
- Infrastructure는 Domain Layer 의존 (Dependency Inversion)
- Handler는 Service + Repository 의존
- ServiceContainer는 모든 레이어를 소유하되, Pimpl로 헤더 노출 최소화
- main.cpp는 ServiceContainer만 의존

---

## Query Executor Pattern (Database Abstraction)

Multi-DBMS 지원(PostgreSQL + Oracle)을 위한 데이터베이스 추상화 패턴.

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

### 구현체
- **PostgreSqlQueryExecutor**: libpq 기반 (10-50x faster)
- **OracleQueryExecutor**: OCI Session Pool 기반

### 런타임 선택
```cpp
// Factory Pattern으로 DB_TYPE 환경변수에 따라 런타임 선택
auto executor = QueryExecutorFactory::create(DB_TYPE);  // "postgres" or "oracle"
```

모든 Repository는 `IQueryExecutor`를 통해 SQL을 실행하므로 DB 교체가 가능하다.

---

## Query Helpers Pattern (common::db:: 유틸리티)

DB별 SQL 문법 차이를 추상화하는 헬퍼 함수 (v2.12.0+).

### 제공 함수

```cpp
// shared/lib/database/query_helpers.h
namespace common::db {
    // JSON → 정수 안전 변환 (Oracle은 모든 값을 문자열로 반환)
    int scalarToInt(const Json::Value& val, int defaultVal = 0);

    // DB별 불리언 리터럴 ("TRUE"/"FALSE" vs "1"/"0")
    std::string boolLiteral(const std::string& dbType, bool value);

    // DB별 페이지네이션 ("LIMIT $N OFFSET $M" vs "OFFSET $M ROWS FETCH NEXT $N ROWS ONLY")
    std::string paginationClause(const std::string& dbType, int limit, int offset);

    // DB별 hex 접두사 ("\\x" vs 빈 문자열)
    std::string hexPrefix(const std::string& dbType);

    // DB별 현재 시간 ("NOW()" vs "SYSTIMESTAMP")
    std::string currentTimestamp(const std::string& dbType);

    // DB별 대소문자 무시 조건 ("ILIKE" vs "UPPER() LIKE UPPER()")
    std::string ilikeCond(const std::string& dbType, const std::string& column, const std::string& paramRef);

    // DB별 LIMIT 절
    std::string limitClause(const std::string& dbType, int limit);

    // JSON 불리언 안전 읽기 (Oracle NUMBER(1) → bool)
    bool getBool(const Json::Value& row, const std::string& key, bool defaultVal = false);
}
```

### 사용 예시

```cpp
// repository에서 DB 분기 제거
std::string dbType = queryExecutor_->getDatabaseType();

std::string sql = "SELECT * FROM certificate WHERE stored_in_ldap = " +
    common::db::boolLiteral(dbType, false) +
    " ORDER BY created_at " +
    common::db::paginationClause(dbType, limit, offset);
```

### 효과
- 15개 리포지토리 across 3개 서비스에서 204개 인라인 DB 분기 제거
- 신규 리포지토리 작성 시 DB 호환성 자동 확보

---

## Provider/Adapter Pattern (Validation Infrastructure)

검증 로직과 데이터 소스(DB vs LDAP)를 분리하는 패턴.

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

### Adapter 구현

| 서비스 | CSCA Provider | CRL Provider |
|--------|--------------|-------------|
| PKD Management | `DbCscaProvider` → CertificateRepository | `DbCrlProvider` → CrlRepository |
| PKD Management (PA Lookup) | `LdapCscaProvider` → LdapPool | `LdapCrlProvider` → LdapPool |
| PA Service | `LdapCscaProvider` → LdapCertificateRepository | `LdapCrlProvider` → LdapCrlRepository |

각 서비스는 자체 데이터 접근 레이어에 맞는 Adapter를 구현한다. `icao::validation` 라이브러리는 인프라 코드를 포함하지 않는다.

---

## Shared Libraries (shared/lib/)

ICAO 인증서 관련 공통 기능을 공유 라이브러리로 추출하여 서비스 간 코드 중복 제거.

### 라이브러리 구성

| 라이브러리 | 네임스페이스 | 책임 |
|-----------|------------|------|
| `database` | `common::` / `common::db::` | DB 커넥션 풀, Query Executor (PostgreSQL + Oracle), Query Helpers |
| `ldap` | `common::` | Thread-safe LDAP 커넥션 풀 (min=2, max=10) |
| `icao-validation` | `icao::validation::` | ICAO 9303 인증서 검증 (trust chain, CRL, extensions, algorithm) |
| `certificate-parser` | `icao::` | X.509 인증서 파싱 |
| `cvc-parser` | `icao::cvc::` | BSI TR-03110 CVC 인증서 파싱 |
| `icao9303` | `icao::` | ICAO 9303 SOD/DG 파서 |
| `audit` | `icao::audit::` | 통합 감사 로깅 (operation_audit_log) |
| `config` | `icao::config::` | 설정 관리 |
| `exception` | `icao::` | 커스텀 예외 타입 |
| `logging` | `icao::logging::` | 구조화 로깅 (spdlog) |

### Shared Validation Library (icao::validation, v2.11.0+)

ICAO 9303 인증서 검증 로직을 공유 라이브러리로 추출한 패턴.

#### 설계 원칙
- **Pure Function**: 인프라 의존성 없이 OpenSSL X509 API만 사용
- **Idempotent**: 동일 입력 → 동일 출력 (멱등성 보장)
- **Provider Pattern**: 데이터 소스는 인터페이스를 통해 주입

#### 모듈 구성

| 모듈 | 책임 |
|------|------|
| `cert_ops` | Pure X509 연산 (지문, 유효성, DN 추출, 정규화) |
| `trust_chain_builder` | Multi-CSCA 신뢰 체인 구축 (키 전환 지원) |
| `crl_checker` | RFC 5280 CRL 폐기 확인 |
| `extension_validator` | X.509 확장 필드 검증 (key usage, critical extensions) |
| `algorithm_compliance` | ICAO Doc 9303 Part 12 알고리즘 요구사항 |

#### 테스트
86개 단위 테스트 (GTest). 모든 모듈에 멱등성 검증 포함 (50-100 iterations).

### Shared Database Library (icao::database)

#### Query Executor + Query Helpers
- `IQueryExecutor`: DB 추상화 인터페이스 (PostgreSQL + Oracle)
- `QueryHelpers` (`common::db::`): SQL 문법 차이 추상화 유틸리티
- `handler_utils.h`: `sendJsonSuccess()`, `notFound()` 응답 헬퍼

#### Connection Pooling
- `DbConnectionPool`: Thread-safe DB 커넥션 풀 (RAII)
- `DbConnectionPoolFactory`: 환경변수 기반 풀 생성

---

## 확장 시 고려사항

### 1. 새로운 파일 타입 추가 시

```cpp
// 1. Processor 생성 (SRP)
class NewFileTypeProcessor {
    ProcessingResult process(const std::vector<uint8_t>& content);
};

// 2. Strategy에 메서드 추가
class ProcessingStrategy {
    virtual void processNewFileType(...) = 0;
};

// 3. 구체 전략 구현
class AutoProcessingStrategy {
    void processNewFileType(...) override;
};
```

### 2. 새로운 인증서 타입 추가 시

```cpp
// 1. CertificateType enum 확장
enum class CertificateType {
    CSCA, DSC, DSC_NC, MLSC, NEW_TYPE  // 추가
};

// 2. 검증 전략 추가 (SRP)
ValidationResult validateNewTypeCertificate(X509* cert) {
    // NEW_TYPE 검증 로직
}

// 3. LDAP DN 전략 추가
std::string buildLdapDnForNewType(const Certificate& cert) {
    // NEW_TYPE LDAP DN 생성 로직
}
```

### 3. 새로운 Handler 추가 시 (v2.12.0+ 패턴)

```cpp
// 1. handlers/new_handler.h/.cpp 생성
class NewHandler {
public:
    NewHandler(SomeService*, SomeRepository*, IQueryExecutor*);
    void registerRoutes(drogon::HttpAppFramework& app);
private:
    void handleSomeEndpoint(const HttpRequestPtr&, ResponseCallback&&);
};

// 2. ServiceContainer의 Impl에 멤버 추가
struct ServiceContainer::Impl {
    std::shared_ptr<handlers::NewHandler> newHandler;
};

// 3. ServiceContainer::initialize()에서 생성
impl_->newHandler = std::make_shared<handlers::NewHandler>(...);

// 4. ServiceContainer에 접근자 추가
handlers::NewHandler* newHandler() const;

// 5. main.cpp의 registerRoutes()에서 등록
if (g_services && g_services->newHandler())
    g_services->newHandler()->registerRoutes(app);
```

### 4. 새로운 Microservice 추가 시

```cpp
// 1. services/{new-service-name}/ 디렉토리 생성
// 2. DDD 구조 적용 (infrastructure/, handlers/, repositories/, services/)
// 3. ServiceContainer + AppConfig 패턴 적용
// 4. main.cpp 최소화 패턴 적용 (~100-500줄)
// 5. Dockerfile, CMakeLists.txt 작성
// 6. docker-compose.yaml에 서비스 추가
// 7. API Gateway (nginx.conf)에 라우팅 추가
```

---

## 안티패턴 (피해야 할 것)

### God Class (신 클래스)
```cpp
// BAD: 모든 책임을 하나의 클래스에 집중
class Everything {
    void parseFile();
    void validateCertificate();
    void saveToDatabase();
    void saveToLdap();
    void sendEmail();
    void logEverything();
};
```

### Fat main.cpp (비대한 진입점)
```cpp
// BAD: main.cpp에 모든 핸들러 로직 직접 구현 (v2.11.0 이전 패턴)
int main() {
    app.registerHandler("/api/upload/ldif", [](req, callback) {
        // 500줄의 업로드 핸들러 로직...
    });
    app.registerHandler("/api/certificates/search", [](req, callback) {
        // 300줄의 검색 로직...
    });
    // ... 8,095줄
}
```

**올바른 방법**: Handler 클래스로 추출 + ServiceContainer DI

### Global Variables (전역 변수)
```cpp
// BAD: 전역 shared_ptr 변수 17개 (v2.11.0 이전 패턴)
std::shared_ptr<UploadRepository> g_uploadRepo;
std::shared_ptr<CertificateRepository> g_certRepo;
// ... 15개 더
```

**올바른 방법**: ServiceContainer가 모든 의존성 소유

### Anemic Domain Model (빈약한 도메인 모델)
```cpp
// BAD: 도메인 모델이 데이터만 가지고 로직은 외부 서비스에 위치
class Certificate {
    std::string subjectDn;  // getter/setter만 존재
    std::string issuerDn;
};

class CertificateService {
    bool isSelfSigned(const Certificate& cert);  // 도메인 로직이 서비스에 위치
};
```

**올바른 방법**:
```cpp
// GOOD: 도메인 로직을 모델 내부에 배치
class Certificate {
private:
    std::string subjectDn;
    std::string issuerDn;

public:
    bool isSelfSigned() const {  // 도메인 로직이 모델 내부에 위치
        return subjectDn == issuerDn;
    }
};
```

### Tight Coupling (강결합)
```cpp
// BAD: 구체 클래스에 직접 의존
class UploadService {
    PostgresCertificateRepository repo;  // 구체 클래스에 의존

    void save(Certificate cert) {
        repo.save(cert);  // 교체 불가능
    }
};
```

**올바른 방법**:
```cpp
// GOOD: 인터페이스/추상 클래스에 의존 (Dependency Inversion)
class UploadService {
    CertificateRepository* repo;  // 추상 인터페이스에 의존

    UploadService(CertificateRepository* r) : repo(r) {}

    void save(Certificate cert) {
        repo->save(cert);  // 교체 가능 (LDAP/Postgres)
    }
};
```

### Inline DB Branching (인라인 DB 분기)
```cpp
// BAD: 리포지토리마다 DB별 if/else 반복 (v2.11.0 이전 패턴)
if (dbType == "oracle") {
    sql += " OFFSET " + std::to_string(offset) + " ROWS FETCH NEXT " + std::to_string(limit) + " ROWS ONLY";
} else {
    sql += " LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
}
```

**올바른 방법**:
```cpp
// GOOD: Query Helpers 사용
sql += common::db::paginationClause(dbType, limit, offset);
```

---

## 코드 리뷰 체크리스트

새로운 코드 작성 시 다음을 확인:

### DDD 준수
- [ ] 도메인 로직이 Domain Layer에 위치하는가?
- [ ] Repository는 데이터 접근만 담당하는가?
- [ ] Application Service는 유즈케이스 조율만 하는가?

### ServiceContainer 패턴 준수
- [ ] 새로운 의존성이 ServiceContainer에 등록되어 있는가?
- [ ] 전역 변수 대신 ServiceContainer 접근자를 사용하는가?
- [ ] 초기화 순서가 의존성 방향을 따르는가?

### Handler Extraction 준수
- [ ] HTTP 핸들러 로직이 main.cpp가 아닌 Handler 클래스에 있는가?
- [ ] Handler가 `registerRoutes(HttpAppFramework&)` 메서드를 제공하는가?
- [ ] main.cpp가 최소한의 오케스트레이션만 수행하는가?

### Strategy Pattern 준수
- [ ] 파일 타입별로 Processor가 분리되어 있는가?
- [ ] 인증서 타입별로 검증 로직이 분리되어 있는가?
- [ ] ProcessingStrategy를 통해 모드별 처리가 분리되어 있는가?

### SRP 준수
- [ ] 각 클래스가 단 하나의 책임만 가지는가?
- [ ] 클래스 이름이 책임을 명확히 표현하는가?
- [ ] 함수가 한 가지 일만 하는가?

### Multi-DBMS 준수
- [ ] Query Helpers (`common::db::`)를 사용하여 DB 분기를 제거했는가?
- [ ] `IQueryExecutor`를 통해 SQL을 실행하는가?
- [ ] Oracle/PostgreSQL 양쪽에서 테스트했는가?

### Microservice 준수
- [ ] 서비스 간 통신이 API Gateway를 통해 이루어지는가?
- [ ] 각 서비스가 독립적으로 배포 가능한가?
- [ ] 데이터베이스가 서비스별로 분리되어 있는가? (또는 명확히 분리된 테이블)

### 의존성 방향
- [ ] Domain Layer가 다른 레이어를 의존하지 않는가?
- [ ] Infrastructure가 Domain을 의존하는가? (Dependency Inversion)
- [ ] 순환 참조가 없는가?

---

## 참고 문서

- **[CLAUDE.md](../CLAUDE.md)** - 프로젝트 개요 및 현재 버전 (v2.37.0)
- **[SOFTWARE_ARCHITECTURE.md](SOFTWARE_ARCHITECTURE.md)** - 시스템 아키텍처
- **[SECURITY_AUDIT_REPORT.md](SECURITY_AUDIT_REPORT.md)** - 보안 감사 보고서
- **[DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md)** - 개발 가이드

---

**Document Status**: Active Standard
**Last Updated**: 2026-03-18
