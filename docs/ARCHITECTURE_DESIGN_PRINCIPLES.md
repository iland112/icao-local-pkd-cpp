# ICAO Local PKD - Architecture & Design Principles

**Version**: 1.0.0
**Created**: 2026-01-29
**Status**: ✅ Active Standard

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

---

## Microservice Architecture

### 서비스 구성

```
services/
├── pkd-management/        # PKD 관리 서비스 (Port: 8081)
│   ├── File Upload & Processing (LDIF, Master List)
│   ├── Certificate Search & Export
│   └── ICAO Auto Sync
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
└── monitoring-service/    # 모니터링 서비스 (미구현)
    └── Health Check & Metrics
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
       ├──────────────────┬──────────────────┬──────────────────┐
       ▼                  ▼                  ▼                  ▼
┌──────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│     PKD      │   │      PA      │   │  PKD Relay   │   │ Monitoring   │
│  Management  │   │   Service    │   │   Service    │   │   Service    │
│    :8081     │   │    :8082     │   │    :8083     │   │    :8084     │
└──────┬───────┘   └──────┬───────┘   └──────┬───────┘   └──────────────┘
       │                  │                  │
       └──────────────────┴──────────────────┘
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
├── domain/                    # 도메인 레이어 (비즈니스 핵심)
│   ├── models/                # 도메인 모델 (엔티티)
│   │   ├── certificate.h      # Certificate 엔티티
│   │   └── icao_version.h     # ICAO Version 엔티티
│   │
│   └── services/              # 도메인 서비스
│       └── certificate_service.h
│
├── repositories/              # 인프라스트럭처 레이어 (데이터 접근)
│   ├── ldap_certificate_repository.h
│   └── postgres_certificate_repository.h
│
├── services/                  # 애플리케이션 서비스 레이어
│   └── upload_service.h
│
├── processing_strategy.h      # 전략 패턴 (비즈니스 로직 분리)
├── ldif_processor.h          # LDIF 처리 프로세서
├── common/
│   └── masterlist_processor.h # Master List 처리 프로세서
│
└── main.cpp                  # 애플리케이션 진입점 (컨트롤러)
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
- Drogon HTTP 컨트롤러 (main.cpp)
- 파일 시스템 접근
- LDAP/PostgreSQL 연결 관리

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

// 구체 전략 1: AUTO 모드
class AutoProcessingStrategy : public ProcessingStrategy {
    // 한 번에 처리: Parse → Validate → DB → LDAP
};

// 구체 전략 2: MANUAL 모드
class ManualProcessingStrategy : public ProcessingStrategy {
    // 2단계 처리: Stage 1 (Parse) → Stage 2 (Validate + DB + LDAP)
};

// Factory Pattern
class ProcessingStrategyFactory {
    static std::unique_ptr<ProcessingStrategy> create(const std::string& mode);
};
```

**사용 예시**:
```cpp
// main.cpp
auto strategy = ProcessingStrategyFactory::create(uploadMode); // "AUTO" or "MANUAL"
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

#### ✅ GOOD: 책임 분리
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

#### ❌ BAD: 책임 혼재
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
| `ManualProcessingStrategy` | MANUAL 모드 처리 흐름 |
| `Certificate` (Domain Model) | 인증서 도메인 규칙 |
| `LdapCertificateRepository` | LDAP 데이터 접근 |
| `CertificateValidator` | 인증서 검증 로직 |
| `TrustChainBuilder` | Trust Chain 구축 |
| `CscaCache` | CSCA 캐시 관리 |

---

## 코드 구조 규칙

### 1. 파일 조직 규칙

```
서비스명/
├── src/
│   ├── domain/              # 도메인 레이어 (비즈니스 핵심)
│   │   ├── models/          # 엔티티
│   │   └── services/        # 도메인 서비스
│   │
│   ├── repositories/        # 데이터 접근 (LDAP, PostgreSQL)
│   │
│   ├── services/            # 애플리케이션 서비스
│   │
│   ├── common/              # 유틸리티 (공통 함수)
│   │
│   ├── {strategy_name}_strategy.h   # 전략 패턴
│   ├── {processor_name}_processor.h # 프로세서
│   │
│   └── main.cpp            # 애플리케이션 진입점 (컨트롤러)
│
└── tests/                  # 단위 테스트
```

### 2. 네이밍 규칙

#### 클래스 네이밍
- **Domain Model**: `Certificate`, `IcaoVersion` (명사)
- **Repository**: `LdapCertificateRepository`, `PostgresCertificateRepository`
- **Service**: `UploadService`, `CertificateService`
- **Strategy**: `AutoProcessingStrategy`, `ManualProcessingStrategy`
- **Processor**: `LdifProcessor`, `MasterListProcessor`
- **Validator**: `CertificateValidator`, `TrustChainValidator`

#### 함수 네이밍
- **도메인 규칙**: `isSelfSigned()`, `isLinkCertificate()`
- **Repository**: `findByFingerprint()`, `save()`, `findAll()`
- **Service**: `processUpload()`, `validateCertificate()`
- **Strategy**: `processLdifEntries()`, `processMasterListContent()`

### 3. 의존성 방향

```
┌─────────────────────────────────────────┐
│        main.cpp (Controller)            │
│         (Presentation Layer)            │
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
- Controller는 모든 레이어 의존 가능

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

### 3. 새로운 Microservice 추가 시

```cpp
// 1. services/{new-service-name}/ 디렉토리 생성
// 2. DDD 구조 적용 (domain/, repositories/, services/)
// 3. Dockerfile, CMakeLists.txt 작성
// 4. docker-compose.yaml에 서비스 추가
// 5. API Gateway (nginx.conf)에 라우팅 추가
```

---

## 안티패턴 (피해야 할 것)

### ❌ God Class (신 클래스)
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

### ❌ Anemic Domain Model (빈약한 도메인 모델)
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

### ❌ Tight Coupling (강결합)
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

---

## 코드 리뷰 체크리스트

새로운 코드 작성 시 다음을 확인:

### DDD 준수
- [ ] 도메인 로직이 Domain Layer에 위치하는가?
- [ ] Repository는 데이터 접근만 담당하는가?
- [ ] Application Service는 유즈케이스 조율만 하는가?

### Strategy Pattern 준수
- [ ] 파일 타입별로 Processor가 분리되어 있는가?
- [ ] 인증서 타입별로 검증 로직이 분리되어 있는가?
- [ ] ProcessingStrategy를 통해 모드별 처리가 분리되어 있는가?

### SRP 준수
- [ ] 각 클래스가 단 하나의 책임만 가지는가?
- [ ] 클래스 이름이 책임을 명확히 표현하는가?
- [ ] 함수가 한 가지 일만 하는가?

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

- **[CLAUDE.md](../CLAUDE.md)** - 프로젝트 개요 및 현재 버전
- **[SPRINT3_COMPLETION_SUMMARY.md](SPRINT3_COMPLETION_SUMMARY.md)** - Sprint 3 구현 세부사항
- **[DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md)** - 개발 가이드

---

**Document Status**: ✅ Active Standard
**Last Updated**: 2026-01-29
**Reviewed By**: Development Team
**Approved By**: Project Lead
