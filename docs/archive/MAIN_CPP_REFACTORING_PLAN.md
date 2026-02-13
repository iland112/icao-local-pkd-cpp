# Main.cpp Refactoring Plan - Front Controller Pattern

**Version**: 1.0.0
**Created**: 2026-01-29
**Status**: 🚧 Planning
**Current main.cpp**: 9,313 lines

---

## 문제점 분석

### 현재 상태 (Anti-Pattern)

```
main.cpp (9,313 lines)
├── Configuration
├── Utility Functions
├── Database Connection Logic
├── LDAP Connection Logic
├── Business Logic (Upload, Validation, Search)
├── HTTP Controllers (40+ endpoints)
├── CORS, Authentication, Middleware
└── Application Initialization
```

**문제점**:
1. ❌ **God Class**: main.cpp가 모든 책임을 가짐
2. ❌ **SRP 위반**: 설정, 비즈니스 로직, 컨트롤러가 혼재
3. ❌ **테스트 불가능**: 9,313줄의 monolithic 파일
4. ❌ **유지보수 어려움**: 코드 변경 시 전체 파일 재컴파일
5. ❌ **DDD 위반**: 도메인 로직이 컨트롤러에 직접 구현됨

---

## 목표 상태 (Front Controller Pattern + DDD)

### 아키텍처 변경

```
main.cpp (< 500 lines) - Front Controller Only
├── Application Initialization
├── Route Registration
├── Middleware Configuration
└── Service Dependency Injection

services/
├── UploadService          (파일 업로드, 파싱, 검증)
├── CertificateService     (인증서 검색, 조회, export)
├── ValidationService      (재검증, trust chain)
├── AuditService          (audit log 조회 및 통계)
├── ProgressService       (SSE progress stream)
└── StatisticsService     (통계 데이터)

controllers/
├── UploadController       (Upload 엔드포인트)
├── CertificateController  (Certificate 엔드포인트)
├── ValidationController   (Validation 엔드포인트)
├── AuditController       (Audit 엔드포인트)
└── StatisticsController  (Statistics 엔드포인트)
```

**개선 효과**:
1. ✅ **SRP 준수**: 각 클래스가 단일 책임만 가짐
2. ✅ **테스트 가능**: 각 Service를 독립적으로 테스트
3. ✅ **유지보수 용이**: 변경 범위 최소화
4. ✅ **DDD 준수**: 비즈니스 로직이 Service Layer에 위치
5. ✅ **코드 재사용**: Service를 다른 Controller에서도 사용 가능

---

## 엔드포인트 분류

### 1. Upload Management (15개 엔드포인트)

**Service**: `UploadService`

| Method | Endpoint | 책임 |
|--------|----------|------|
| POST | `/api/upload/ldif` | LDIF 파일 업로드 |
| POST | `/api/upload/masterlist` | Master List 파일 업로드 |
| POST | `/api/upload/{id}/parse` | 파일 파싱 트리거 (MANUAL 모드) |
| POST | `/api/upload/{id}/validate` | 검증 트리거 (MANUAL 모드) |
| GET | `/api/upload/history` | 업로드 이력 조회 |
| GET | `/api/upload/detail/{id}` | 개별 업로드 상세 조회 |
| GET | `/api/upload/{id}/validations` | 업로드별 검증 결과 조회 |
| GET | `/api/upload/{id}/issues` | 업로드 이슈 (중복) 조회 |
| GET | `/api/upload/statistics` | 업로드 통계 |
| GET | `/api/upload/changes` | 업로드 변경 사항 계산 |
| GET | `/api/upload/countries` | 국가별 통계 |
| GET | `/api/upload/countries/detailed` | 국가별 상세 통계 |
| DELETE | `/api/upload/{id}` | 실패한 업로드 삭제 |
| GET | `/api/progress/stream/{id}` | SSE progress stream |
| GET | `/api/progress/status/{id}` | Progress 상태 조회 |

**비즈니스 로직**:
- LDIF/Master List 파싱
- 인증서 검증 (Trust Chain, CRL)
- DB 저장 (PostgreSQL)
- LDAP 저장
- 중복 감지 및 추적
- 통계 계산

### 2. Certificate Management (8개 엔드포인트)

**Service**: `CertificateService` (기존 확장)

| Method | Endpoint | 책임 |
|--------|----------|------|
| GET | `/api/certificates/search` | 인증서 검색 (LDAP) |
| GET | `/api/certificates/detail` | 인증서 상세 조회 |
| GET | `/api/certificates/validation` | Fingerprint로 검증 결과 조회 |
| GET | `/api/certificates/export/file` | 단일 인증서 파일 export |
| GET | `/api/certificates/export/country` | 국가별 인증서 ZIP export |
| GET | `/api/certificates/countries` | 사용 가능한 국가 목록 |
| GET | `/api/link-certs/search` | Link Certificate 검색 |
| GET | `/api/link-certs/{id}` | Link Certificate 상세 조회 |

**비즈니스 로직**:
- LDAP 검색 (필터, 페이징)
- 인증서 상세 정보 조회
- PEM/DER 파일 생성
- ZIP 아카이브 생성
- 국가 목록 캐싱

### 3. Validation Management (2개 엔드포인트)

**Service**: `ValidationService` (신규)

| Method | Endpoint | 책임 |
|--------|----------|------|
| POST | `/api/validation/revalidate` | DSC 재검증 |
| POST | `/api/validate/link-cert` | Link Certificate 검증 |

**비즈니스 로직**:
- Trust Chain 재검증
- Link Certificate 검증
- 검증 결과 DB 저장
- 검증 통계 업데이트

### 4. Audit Log Management (2개 엔드포인트)

**Service**: `AuditService` (신규)

| Method | Endpoint | 책임 |
|--------|----------|------|
| GET | `/api/audit/operations` | Audit log 목록 조회 |
| GET | `/api/audit/operations/stats` | Audit log 통계 |

**비즈니스 로직**:
- Audit log 필터링 및 페이징
- 통계 계산 (성공/실패, 사용자별, 작업별)
- 평균 응답 시간 계산

### 5. ICAO Sync (이미 분리됨 ✅)

**Handler**: `IcaoHandler` (기존)
**Service**: `IcaoSyncService` (기존)

| Method | Endpoint | 책임 |
|--------|----------|------|
| POST | `/api/icao/check-updates` | ICAO 버전 확인 |
| GET | `/api/icao/status` | 동기화 상태 조회 |
| GET | `/api/icao/latest` | 최신 버전 조회 |
| GET | `/api/icao/history` | 감지 이력 조회 |

**상태**: ✅ 이미 Handler/Service 패턴으로 분리됨

### 6. PA (Passive Authentication) (3개 엔드포인트)

**Note**: PA Service는 별도 마이크로서비스 (pa-service)로 분리됨
**pkd-management에서는 Proxy 역할만 수행**

| Method | Endpoint | 책임 |
|--------|----------|------|
| POST | `/api/pa/verify` | PA 검증 (Proxy) |
| GET | `/api/pa/statistics` | PA 통계 (Proxy) |
| GET | `/api/pa/history` | PA 이력 (Proxy) |

**상태**: ✅ 별도 마이크로서비스로 분리됨 (리팩토링 불필요)

### 7. Auth (이미 분리됨 ✅)

**Handler**: `AuthHandler` (기존)
**Service**: `JwtService`, `PasswordHashService` (기존)

| Method | Endpoint | 책임 |
|--------|----------|------|
| POST | `/api/auth/login` | 로그인 |
| POST | `/api/auth/logout` | 로그아웃 |
| POST | `/api/auth/refresh` | 토큰 갱신 |

**상태**: ✅ 이미 Handler/Service 패턴으로 분리됨

### 8. Health & Utility (2개 엔드포인트)

**Service**: `HealthService` (신규)

| Method | Endpoint | 책임 |
|--------|----------|------|
| GET | `/api/ldap/health` | LDAP health check |
| GET | `/api/openapi.yaml` | OpenAPI specification |

**비즈니스 로직**:
- LDAP 연결 상태 확인
- OpenAPI YAML 파일 제공

### 9. Internal/Migration (1개 엔드포인트)

**Service**: `MigrationService` (신규)

| Method | Endpoint | 책임 |
|--------|----------|------|
| POST | `/api/internal/migrate-ldap-dns` | LDAP DN v2 마이그레이션 |

**비즈니스 로직**:
- 대량 DN 마이그레이션
- 진행 상황 추적
- Rollback 지원

---

## 리팩토링 단계별 계획

### Phase 1: Service 클래스 추출 (Week 1)

#### Step 1.1: UploadService 추출 (Day 1-2)

**파일 생성**:
```
services/pkd-management/src/
├── services/
│   ├── upload_service.h
│   └── upload_service.cpp
```

**클래스 구조**:
```cpp
// services/upload_service.h
#pragma once

#include <string>
#include <memory>
#include <libpq-fe.h>
#include <ldap.h>
#include "processing_strategy.h"

namespace services {

class UploadService {
public:
    // Constructor with dependency injection
    UploadService(PGconn* dbConn, LDAP* ldapConn);

    // LDIF upload
    struct LdifUploadResult {
        bool success;
        std::string uploadId;
        std::string message;
        int certificateCount;
        int crlCount;
    };
    LdifUploadResult uploadLdif(
        const std::string& fileName,
        const std::vector<uint8_t>& fileContent,
        const std::string& uploadMode,  // "AUTO" or "MANUAL"
        const std::string& uploadedBy
    );

    // Master List upload
    struct MasterListUploadResult {
        bool success;
        std::string uploadId;
        std::string message;
        int mlscCount;
        int cscaCount;
    };
    MasterListUploadResult uploadMasterList(
        const std::string& fileName,
        const std::vector<uint8_t>& fileContent,
        const std::string& uploadMode,
        const std::string& uploadedBy
    );

    // Trigger parsing (MANUAL mode)
    bool triggerParsing(const std::string& uploadId);

    // Trigger validation (MANUAL mode)
    bool triggerValidation(const std::string& uploadId);

    // Get upload history with pagination
    struct UploadHistoryFilter {
        int page = 0;
        int size = 10;
        std::string sort = "createdAt";
        std::string direction = "DESC";
    };
    Json::Value getUploadHistory(const UploadHistoryFilter& filter);

    // Get upload detail
    Json::Value getUploadDetail(const std::string& uploadId);

    // Get upload validations
    struct ValidationFilter {
        int limit = 50;
        int offset = 0;
        std::string status;    // "VALID", "INVALID", "PENDING"
        std::string certType;  // "DSC", "DSC_NC"
    };
    Json::Value getUploadValidations(
        const std::string& uploadId,
        const ValidationFilter& filter
    );

    // Get upload issues (duplicates)
    Json::Value getUploadIssues(const std::string& uploadId);

    // Delete upload
    bool deleteUpload(const std::string& uploadId);

    // Statistics
    Json::Value getUploadStatistics();
    Json::Value getCountryStatistics();
    Json::Value getDetailedCountryStatistics(int limit);

private:
    PGconn* dbConn_;
    LDAP* ldapConn_;

    // Helper methods
    std::string generateUploadId();
    void recordUploadToDatabase(/* ... */);
    void updateUploadStatus(const std::string& uploadId, const std::string& status);
};

} // namespace services
```

**비즈니스 로직 이동**:
- `main.cpp` lines 6017-6267 → `UploadService::uploadLdif()`
- `main.cpp` lines 6267-6625 → `UploadService::uploadMasterList()`
- `main.cpp` lines 6791-6892 → `UploadService::getUploadHistory()`
- 기타 upload 관련 로직

#### Step 1.2: ValidationService 생성 (Day 2-3)

**파일 생성**:
```
services/pkd-management/src/
├── services/
│   ├── validation_service.h
│   └── validation_service.cpp
```

**클래스 구조**:
```cpp
// services/validation_service.h
#pragma once

#include <string>
#include <memory>
#include <libpq-fe.h>
#include <openssl/x509.h>

namespace services {

class ValidationService {
public:
    ValidationService(PGconn* dbConn);

    // Re-validate DSC certificates
    struct RevalidateResult {
        bool success;
        int totalProcessed;
        int validCount;
        int invalidCount;
        int pendingCount;
        std::string message;
    };
    RevalidateResult revalidateDscCertificates();

    // Validate single certificate
    struct ValidationResult {
        bool trustChainValid;
        std::string trustChainMessage;
        std::string trustChainPath;
        bool signatureValid;
        bool crlChecked;
        bool revoked;
    };
    ValidationResult validateCertificate(X509* cert);

    // Get validation by fingerprint
    Json::Value getValidationByFingerprint(const std::string& fingerprint);

private:
    PGconn* dbConn_;

    // Helper methods
    void saveValidationResult(/* ... */);
    std::string buildTrustChainPath(/* ... */);
};

} // namespace services
```

#### Step 1.3: AuditService 생성 (Day 3)

**파일 생성**:
```
services/pkd-management/src/
├── services/
│   ├── audit_service.h
│   └── audit_service.cpp
```

**클래스 구조**:
```cpp
// services/audit_service.h
#pragma once

#include <string>
#include <memory>
#include <libpq-fe.h>
#include <json/json.h>

namespace services {

class AuditService {
public:
    AuditService(PGconn* dbConn);

    // List audit operations
    struct AuditFilter {
        int limit = 50;
        int offset = 0;
        std::string operationType;
        std::string status;
        std::string username;
        std::string startDate;
        std::string endDate;
    };
    Json::Value listAuditOperations(const AuditFilter& filter);

    // Get audit statistics
    Json::Value getAuditStatistics();

private:
    PGconn* dbConn_;
};

} // namespace services
```

#### Step 1.4: StatisticsService 생성 (Day 4)

**파일 생성**:
```
services/pkd-management/src/
├── services/
│   ├── statistics_service.h
│   └── statistics_service.cpp
```

**클래스 구조**:
```cpp
// services/statistics_service.h
#pragma once

#include <string>
#include <memory>
#include <libpq-fe.h>
#include <json/json.h>

namespace services {

class StatisticsService {
public:
    StatisticsService(PGconn* dbConn);

    // Upload statistics
    Json::Value getUploadStatistics();

    // Country statistics
    Json::Value getCountryStatistics();
    Json::Value getDetailedCountryStatistics(int limit);

    // Certificate statistics
    Json::Value getCertificateStatistics();

private:
    PGconn* dbConn_;

    // Cache management
    void refreshCache();
};

} // namespace services
```

---

### Phase 2: Controller 클래스 생성 (Week 1, Day 5-7)

#### Controller 구조

```cpp
// controllers/upload_controller.h
#pragma once

#include <drogon/HttpController.h>
#include "../services/upload_service.h"

namespace controllers {

class UploadController : public drogon::HttpController<UploadController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UploadController::uploadLdif, "/api/upload/ldif", Post);
    ADD_METHOD_TO(UploadController::uploadMasterList, "/api/upload/masterlist", Post);
    ADD_METHOD_TO(UploadController::getHistory, "/api/upload/history", Get);
    ADD_METHOD_TO(UploadController::getDetail, "/api/upload/detail/{1}", Get);
    ADD_METHOD_TO(UploadController::getValidations, "/api/upload/{1}/validations", Get);
    ADD_METHOD_TO(UploadController::getIssues, "/api/upload/{1}/issues", Get);
    ADD_METHOD_TO(UploadController::deleteUpload, "/api/upload/{1}", Delete);
    METHOD_LIST_END

    // Handler methods
    void uploadLdif(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    );

    void uploadMasterList(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    );

    void getHistory(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    );

    // ... other handlers

private:
    std::shared_ptr<services::UploadService> uploadService_;
};

} // namespace controllers
```

**Controller 책임**:
1. HTTP 요청 파싱
2. 파라미터 검증
3. Service 메서드 호출
4. HTTP 응답 생성
5. 에러 핸들링

**Controller가 하지 않는 것**:
- ❌ 비즈니스 로직 (Service가 담당)
- ❌ DB 접근 (Repository가 담당)
- ❌ LDAP 접근 (Repository가 담당)

---

### Phase 3: main.cpp 리팩토링 (Week 2, Day 1-3)

#### 최종 main.cpp 구조 (< 500 lines)

```cpp
// main.cpp
#include <drogon/drogon.h>
#include "controllers/upload_controller.h"
#include "controllers/certificate_controller.h"
#include "controllers/validation_controller.h"
#include "controllers/audit_controller.h"
#include "controllers/statistics_controller.h"
#include "handlers/icao_handler.h"
#include "handlers/auth_handler.h"
#include "middleware/auth_middleware.h"

// Global services (Dependency Injection Container 역할)
std::shared_ptr<services::UploadService> uploadService;
std::shared_ptr<services::CertificateService> certificateService;
std::shared_ptr<services::ValidationService> validationService;
std::shared_ptr<services::AuditService> auditService;
std::shared_ptr<services::StatisticsService> statisticsService;

// Global handlers (기존 유지)
std::shared_ptr<handlers::IcaoHandler> icaoHandler;
std::shared_ptr<handlers::AuthHandler> authHandler;

int main() {
    // 1. Configuration
    AppConfig config = AppConfig::fromEnvironment();

    // 2. Initialize Database Connection
    PGconn* dbConn = initializeDatabaseConnection(config);

    // 3. Initialize LDAP Connection
    LDAP* ldapConn = initializeLdapConnection(config);

    // 4. Initialize Services (Dependency Injection)
    uploadService = std::make_shared<services::UploadService>(dbConn, ldapConn);
    certificateService = std::make_shared<services::CertificateService>(dbConn, ldapConn);
    validationService = std::make_shared<services::ValidationService>(dbConn);
    auditService = std::make_shared<services::AuditService>(dbConn);
    statisticsService = std::make_shared<services::StatisticsService>(dbConn);

    // 5. Initialize Handlers (기존 방식 유지)
    icaoHandler = std::make_shared<handlers::IcaoHandler>(/* ... */);
    authHandler = std::make_shared<handlers::AuthHandler>(/* ... */);

    // 6. Configure Middleware
    app().registerPreRoutingAdvice([](const HttpRequestPtr& req) {
        // CORS
        // Authentication
        // Logging
    });

    // 7. Register Controllers (자동 라우팅)
    // Drogon이 METHOD_LIST_BEGIN/END를 기반으로 자동 등록

    // 8. Register Handler Routes (기존 방식)
    icaoHandler->registerRoutes(app());
    authHandler->registerRoutes(app());

    // 9. Start Server
    app().addListener("0.0.0.0", config.serverPort)
        .setThreadNum(config.threadNum)
        .run();

    return 0;
}
```

**개선 효과**:
- ✅ main.cpp: 9,313 lines → **< 500 lines** (95% 감소)
- ✅ 비즈니스 로직이 Service Layer로 이동
- ✅ 라우팅만 main.cpp/Controller에 위치
- ✅ 테스트 가능한 구조
- ✅ DDD 및 SRP 준수

---

### Phase 4: 통합 테스트 (Week 2, Day 4-5)

#### 테스트 체크리스트

**Unit Tests**:
- [ ] UploadService::uploadLdif() - LDIF 파일 업로드
- [ ] UploadService::uploadMasterList() - Master List 업로드
- [ ] ValidationService::revalidateDscCertificates() - DSC 재검증
- [ ] AuditService::listAuditOperations() - Audit log 조회
- [ ] StatisticsService::getCountryStatistics() - 통계 조회

**Integration Tests**:
- [ ] POST /api/upload/ldif - End-to-end LDIF 업로드
- [ ] GET /api/upload/history - 업로드 이력 조회
- [ ] POST /api/validation/revalidate - DSC 재검증
- [ ] GET /api/audit/operations - Audit log 조회
- [ ] GET /api/certificates/search - 인증서 검색

**Performance Tests**:
- [ ] 컴파일 시간 비교 (Before vs After)
- [ ] 메모리 사용량 비교
- [ ] API 응답 시간 비교 (변경 없어야 함)

---

## 파일 구조 변경

### Before (현재)

```
services/pkd-management/src/
├── main.cpp (9,313 lines) ❌
├── common/
├── domain/
├── repositories/
├── services/ (일부만 존재)
└── handlers/ (ICAO, Auth만)
```

### After (목표)

```
services/pkd-management/src/
├── main.cpp (< 500 lines) ✅
│
├── controllers/              # NEW - HTTP Layer
│   ├── upload_controller.h
│   ├── upload_controller.cpp
│   ├── certificate_controller.h
│   ├── certificate_controller.cpp
│   ├── validation_controller.h
│   ├── validation_controller.cpp
│   ├── audit_controller.h
│   ├── audit_controller.cpp
│   ├── statistics_controller.h
│   └── statistics_controller.cpp
│
├── services/                 # Application Service Layer
│   ├── upload_service.h
│   ├── upload_service.cpp
│   ├── certificate_service.h (기존 확장)
│   ├── certificate_service.cpp
│   ├── validation_service.h     # NEW
│   ├── validation_service.cpp   # NEW
│   ├── audit_service.h          # NEW
│   ├── audit_service.cpp        # NEW
│   ├── statistics_service.h     # NEW
│   └── statistics_service.cpp   # NEW
│
├── domain/                   # Domain Layer (기존 유지)
│   ├── models/
│   └── services/
│
├── repositories/             # Infrastructure Layer (기존 유지)
│   ├── ldap_certificate_repository.h
│   └── postgres_certificate_repository.h
│
├── handlers/                 # 기존 Handler (유지)
│   ├── icao_handler.h
│   ├── icao_handler.cpp
│   ├── auth_handler.h
│   └── auth_handler.cpp
│
├── middleware/               # 기존 유지
│   ├── auth_middleware.h
│   └── permission_filter.h
│
└── common/                   # Utilities (기존 유지)
    ├── ldap_utils.h
    ├── audit_log.h
    └── masterlist_processor.h
```

---

## 마이그레이션 전략

### 점진적 마이그레이션 (Strangler Fig Pattern)

1. **Phase 1**: Service 클래스 생성 (Week 1)
   - main.cpp에서 로직 복사 → Service 클래스로 이동
   - main.cpp의 기존 코드는 유지 (주석 처리)

2. **Phase 2**: Controller 생성 및 전환 (Week 1)
   - Controller 클래스 생성
   - 일부 엔드포인트부터 Controller로 전환
   - 기존 main.cpp 엔드포인트와 병행 운영

3. **Phase 3**: main.cpp 정리 (Week 2)
   - 모든 엔드포인트가 Controller로 전환되면
   - main.cpp에서 중복 코드 제거
   - main.cpp를 Front Controller로 최종 정리

4. **Phase 4**: 검증 및 배포 (Week 2)
   - 통합 테스트 수행
   - 성능 검증
   - Production 배포

**장점**:
- ✅ 점진적 마이그레이션으로 리스크 최소화
- ✅ 각 단계마다 테스트 및 검증 가능
- ✅ 문제 발생 시 Rollback 용이

---

## 리팩토링 규칙

### 1. Service 클래스 설계 원칙

**DO**:
- ✅ 단일 책임 원칙 준수 (예: UploadService는 Upload만)
- ✅ Constructor Dependency Injection 사용
- ✅ 비즈니스 로직에만 집중
- ✅ Repository 패턴 사용 (DB/LDAP 접근)
- ✅ 도메인 모델 활용

**DON'T**:
- ❌ HTTP 요청/응답 처리 (Controller가 담당)
- ❌ Session 관리 (Middleware가 담당)
- ❌ CORS 처리 (Middleware가 담당)
- ❌ 직접 SQL 실행 (Repository가 담당)

### 2. Controller 클래스 설계 원칙

**DO**:
- ✅ HTTP 요청 파싱 및 검증
- ✅ Service 메서드 호출
- ✅ HTTP 응답 생성 (JSON)
- ✅ 에러 핸들링 및 HTTP 상태 코드 설정

**DON'T**:
- ❌ 비즈니스 로직 구현 (Service가 담당)
- ❌ DB 직접 접근 (Service → Repository)
- ❌ 복잡한 데이터 변환 (Service가 담당)

### 3. 네이밍 규칙

**Service**:
- `{Domain}Service` (예: UploadService, CertificateService)
- 메서드: `{verb}{Noun}()` (예: `uploadLdif()`, `getCertificate()`)

**Controller**:
- `{Domain}Controller` (예: UploadController)
- 메서드: HTTP verb + 명사 (예: `uploadLdif()`, `getHistory()`)

**Repository**:
- `{Technology}{Domain}Repository` (예: LdapCertificateRepository)
- 메서드: CRUD 표준 (find, save, update, delete)

---

## 예상 효과

### 정량적 개선

| 지표 | Before | After | 개선율 |
|------|--------|-------|--------|
| main.cpp 라인 수 | 9,313 | < 500 | 95% ↓ |
| 컴파일 시간 | ~30초 | ~10초 | 67% ↓ |
| 테스트 가능 클래스 | 0 | 10+ | ∞ |
| 코드 재사용성 | 낮음 | 높음 | - |
| 유지보수 난이도 | 높음 | 낮음 | - |

### 정성적 개선

**개발자 경험**:
- ✅ 코드 변경 시 영향 범위 명확
- ✅ 새 기능 추가 시 어디에 코드를 작성할지 명확
- ✅ 비즈니스 로직 테스트 가능
- ✅ 코드 리뷰 용이 (파일 크기 감소)

**아키텍처**:
- ✅ DDD 원칙 준수
- ✅ SRP 준수 (각 클래스가 단일 책임)
- ✅ Dependency Inversion (Service ← Repository)
- ✅ Front Controller Pattern 적용

**향후 확장**:
- ✅ 새 엔드포인트 추가 용이 (Controller만 추가)
- ✅ 새 비즈니스 로직 추가 용이 (Service만 추가)
- ✅ Microservice로 분리 용이 (Service 단위로 분리 가능)

---

## 위험 요소 및 대응

### 위험 1: 컴파일 에러

**원인**: 함수 시그니처 변경, 헤더 의존성
**대응**:
- 점진적 마이그레이션 (한 번에 한 Service씩)
- 컴파일 에러 발생 시 즉시 수정
- CI/CD 파이프라인에서 자동 빌드 확인

### 위험 2: 런타임 에러

**원인**: Dependency Injection 누락, Null Pointer
**대응**:
- Service 초기화 시 nullptr 체크
- Integration Test로 검증
- Logging으로 초기화 과정 추적

### 위험 3: 성능 저하

**원인**: 함수 호출 오버헤드 증가
**대응**:
- 성능 테스트로 사전 검증
- Inline 함수 활용
- 컴파일러 최적화 옵션 확인

### 위험 4: 기존 기능 동작 변경

**원인**: 로직 이동 시 실수
**대응**:
- 코드 리뷰 철저히 수행
- Integration Test 전체 수행
- Staging 환경에서 충분히 테스트

---

## 일정

### Week 1: Service & Controller 추출

| Day | 작업 | 담당 | 완료 기준 |
|-----|------|------|----------|
| 1-2 | UploadService 추출 | Dev | Unit Test 통과 |
| 2-3 | ValidationService 생성 | Dev | Unit Test 통과 |
| 3 | AuditService 생성 | Dev | Unit Test 통과 |
| 4 | StatisticsService 생성 | Dev | Unit Test 통과 |
| 5-7 | Controller 생성 | Dev | Integration Test 통과 |

### Week 2: main.cpp 리팩토링 & 검증

| Day | 작업 | 담당 | 완료 기준 |
|-----|------|------|----------|
| 1-3 | main.cpp 정리 | Dev | 컴파일 성공 |
| 4-5 | 통합 테스트 | QA | All Tests Pass |
| 6 | 성능 테스트 | Dev | No Regression |
| 7 | 문서화 & 배포 | Team | Production Ready |

---

## 체크리스트

### Before Starting
- [ ] 현재 main.cpp 백업 (Git tag)
- [ ] 테스트 환경 준비
- [ ] 팀원들과 리팩토링 계획 공유
- [ ] CI/CD 파이프라인 확인

### During Refactoring
- [ ] UploadService 추출 완료
- [ ] ValidationService 생성 완료
- [ ] AuditService 생성 완료
- [ ] StatisticsService 생성 완료
- [ ] Controller 생성 완료
- [ ] main.cpp 정리 완료
- [ ] Unit Tests 작성 완료
- [ ] Integration Tests 통과
- [ ] Performance Tests 통과

### After Refactoring
- [ ] 문서 업데이트 (CLAUDE.md, ARCHITECTURE_DESIGN_PRINCIPLES.md)
- [ ] 코드 리뷰 완료
- [ ] Production 배포 완료
- [ ] 모니터링 확인 (에러 로그, 성능 지표)
- [ ] Retrospective 회의

---

## 참고 문서

- **[ARCHITECTURE_DESIGN_PRINCIPLES.md](ARCHITECTURE_DESIGN_PRINCIPLES.md)** - DDD, SRP, Strategy Pattern
- **[CLAUDE.md](../CLAUDE.md)** - 프로젝트 개요
- **[DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md)** - 개발 가이드

---

**Document Status**: 🚧 Planning
**Last Updated**: 2026-01-29
**Approved By**: Project Lead
