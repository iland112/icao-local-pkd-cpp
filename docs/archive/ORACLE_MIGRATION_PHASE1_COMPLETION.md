# Oracle Migration Phase 1: Database Abstraction Layer 완료

**작업 일자**: 2026-02-04
**상태**: ✅ Phase 1 완료 (95%) - 개발 환경 구축 완료, main.cpp 적용 대기
**브랜치**: `feature/oracle-migration`

---

## 작업 개요

PostgreSQL에서 Oracle로 데이터베이스를 마이그레이션하기 위한 첫 번째 단계로, Strategy Pattern을 사용한 Database Abstraction Layer를 구현했습니다.

---

## 완료된 작업

### 1. Database Abstraction Layer 설계 ✅

**파일 생성**:
- `shared/lib/database/db_connection_interface.h` - 추상 인터페이스
  - `IDbConnection`: 데이터베이스 연결 추상화
  - `IDbConnectionPool`: 커넥션 풀 추상화

**핵심 설계**:
```cpp
class IDbConnection {
    virtual bool isValid() const = 0;
    virtual std::string getDatabaseType() const = 0;
    virtual bool execute(const std::string& sql) = 0;
    virtual void release() = 0;
};

class IDbConnectionPool {
    virtual bool initialize() = 0;
    virtual std::unique_ptr<IDbConnection> acquireGeneric() = 0;
    virtual Stats getStats() const = 0;
    virtual void shutdown() = 0;
    virtual std::string getDatabaseType() const = 0;
};
```

---

### 2. Factory Pattern 구현 ✅

**파일 생성**:
- `shared/lib/database/db_connection_pool_factory.h`
- `shared/lib/database/db_connection_pool_factory.cpp`

**주요 기능**:
- `DbPoolConfig` 구조체 - 데이터베이스 설정 통합 관리
- `DbConnectionPoolFactory::create()` - 타입별 풀 생성
- `DbConnectionPoolFactory::createFromEnv()` - 환경 변수 기반 자동 생성
- 데이터베이스 타입 정규화 (postgres/postgresql/pg → postgres, oracle/ora → oracle)

**환경 변수 지원**:
```bash
# PostgreSQL
DB_TYPE=postgres
DB_HOST=localhost
DB_PORT=5432
DB_NAME=localpkd
DB_USER=pkd
DB_PASSWORD=pkd123

# Oracle
DB_TYPE=oracle
ORACLE_HOST=oracle-xe-dev
ORACLE_PORT=1521
ORACLE_SERVICE_NAME=LOCALPKD
ORACLE_USER=pkd
ORACLE_PASSWORD=pkd123
```

---

### 3. Oracle Connection Pool 구현 ✅

**파일 생성**:
- `shared/lib/database/oracle_connection_pool.h`
- `shared/lib/database/oracle_connection_pool.cpp`
- `shared/lib/database/external/otl/otlv4.h` (OTL 라이브러리 v4.0.498)

**구현 특징**:
- **OTL (Oracle Template Library)** 기반 - 헤더 온리 라이브러리
- **Thread-safe RAII Pattern** - OracleConnection 클래스
- **자동 재연결** - 연결 끊김 시 자동 재시도
- **Health Check** - 주기적 연결 상태 확인
- **Connection String Format**: `user/password@host:port/service_name`

**코드 예시**:
```cpp
OracleConnectionPool pool(connString, minSize, maxSize, timeout);
pool.initialize();

auto conn = pool.acquire();  // RAII wrapper
if (conn.isValid()) {
    // Use connection
    conn.get()->...
}
// Connection automatically released on scope exit
```

---

### 4. PostgreSQL Connection Pool 인터페이스 확장 ✅

**파일 수정**:
- `shared/lib/database/db_connection_pool.h` - IDbConnectionPool 상속
- `shared/lib/database/db_connection_pool.cpp` - 인터페이스 메서드 구현

**변경 사항**:
- `DbConnection::getDatabaseType()` 추가 → "postgres" 반환
- `DbConnection::execute()` 추가 - 간단한 SQL 실행
- `DbConnectionPool::acquireGeneric()` 추가 - 추상 인터페이스 호환
- `DbConnectionPool::getDatabaseType()` 추가 → "postgres" 반환

---

### 5. CMake 빌드 시스템 통합 ✅

**파일 수정**:
- `shared/lib/database/CMakeLists.txt`
  - oracle_connection_pool.cpp 추가
  - OTL 헤더 경로 추가 (`external/otl`)
  - Oracle SDK include 경로 추가 (PRIVATE, 충돌 방지)

**Oracle SDK Include 경로 설정**:
```cmake
# Oracle SDK include path (PRIVATE to avoid conflicts with OpenLDAP)
if(DEFINED ENV{ORACLE_HOME})
    target_include_directories(icao-database PRIVATE
        $ENV{ORACLE_HOME}/sdk/include
    )
endif()
```

**중요**: PRIVATE 설정으로 OpenLDAP 헤더와의 충돌 방지

---

### 6. Docker 환경 구성 ✅

#### 6.1. Dockerfile 수정

**파일**: `services/pkd-management/Dockerfile`

**Oracle Instant Client 설치**:
- Version: 21.13 (Oracle 11g~21c 호환)
- Basic + SDK 패키지 설치
- 심볼릭 링크 생성 (libclntsh.so, libocci.so)

**환경 변수 설정**:
```dockerfile
ENV ORACLE_HOME=/opt/oracle/instantclient_21_13
ENV LD_LIBRARY_PATH=$ORACLE_HOME:$LD_LIBRARY_PATH
ENV PATH=$ORACLE_HOME:$PATH
# Note: C_INCLUDE_PATH는 설정하지 않음 (OpenLDAP 충돌 방지)
```

#### 6.2. 개발 환경 Compose 파일

**파일**: `docker/docker-compose.dev.yaml`

**프로젝트 설정**:
```yaml
version: '3.8'
name: icao-dev  # 프로덕션과 분리

networks:
  pkd-network:
    name: pkd-dev-network
    driver: bridge
```

**서비스 구성**:
1. **oracle-xe-dev** - Oracle XE 21c
   - 포트: `11521:1521` (외부:내부)
   - 포트: `15500:5500` (EM)
   - 데이터베이스: LOCALPKD
   - 사용자: pkd/pkd123

2. **pkd-management-dev**
   - 포트: `18091:18091`
   - DB_TYPE: oracle
   - ORACLE_HOST: oracle-xe-dev
   - ORACLE_PORT: 1521

**포트 규칙**: 개발 환경 포트는 앞에 `1`을 추가 (18091, 11521, 15500)

#### 6.3. Oracle 초기화 스크립트

**파일**: `docker/db/oracle-init/01-init-schema.sql`

**내용**:
- UUID 생성 함수 (uuid_generate_v4)
- 11개 테이블 생성 (PostgreSQL → Oracle 타입 변환)
  - uploaded_file, certificate, crl, validation_result
  - duplicate_certificate, sync_status, reconciliation_summary
  - reconciliation_log, users, auth_audit_log, operation_audit_log
  - icao_version_history

**타입 변환**:
- UUID → VARCHAR2(36)
- BYTEA → BLOB
- TEXT → CLOB
- BOOLEAN → NUMBER(1)
- TIMESTAMP → TIMESTAMP(6)

---

### 7. 빌드 오류 해결 ✅

#### 7.1. OTL 예외 타입 변환

**문제**: `e.msg`가 `unsigned char[1000]`로 `std::string`과 직접 연결 불가

**해결**: `reinterpret_cast<const char*>(e.msg)` 사용

**수정 파일**: `shared/lib/database/oracle_connection_pool.cpp` (2곳)
```cpp
throw std::runtime_error(std::string("Oracle connection failed: ")
    + reinterpret_cast<const char*>(e.msg));
```

#### 7.2. Oracle SDK Include 경로 충돌

**문제**: 전역 `C_INCLUDE_PATH`에 Oracle SDK 추가 → OpenLDAP 빌드 실패

**원인**: Oracle의 `ldap.h`가 OpenLDAP의 `ldap.h`를 덮어씀

**해결**:
1. Dockerfile에서 전역 경로 제거
2. CMakeLists.txt에서 PRIVATE로 Oracle SDK 경로 추가 (oracle_connection_pool.cpp만 사용)

**결과**: OpenLDAP 라이브러리 정상 빌드

---

### 8. 개발 환경 시작 성공 ✅

**컨테이너 상태**:
```bash
$ docker ps --filter "label=com.docker.compose.project=icao-dev"
NAMES                     STATUS                    PORTS
icao-pkd-management-dev   Up (healthy)             18091:18091
icao-oracle-xe-dev        Up (healthy)             11521:1521, 15500:5500
```

**환경 변수 확인**:
```bash
DB_TYPE=oracle
ORACLE_HOST=oracle-xe-dev
ORACLE_PORT=1521
ORACLE_SERVICE_NAME=LOCALPKD
ORACLE_USER=pkd
ORACLE_PASSWORD=pkd123
```

**로그 확인**:
- ✅ 서비스 정상 시작 (포트 18091)
- ✅ Repository Pattern 초기화 완료
- ⚠️  현재 PostgreSQL Connection Pool 사용 중 (Factory Pattern 미적용)

---

## 다음 세션 작업 (Phase 2)

### 1. main.cpp에 Factory Pattern 적용 🔄

**목표**: 환경 변수 기반 자동 데이터베이스 선택

**현재 코드**:
```cpp
// PostgreSQL Connection Pool 직접 생성
std::string conninfo = buildPostgresConnInfo();
g_dbPool = std::make_shared<common::DbConnectionPool>(conninfo, 5, 20);
```

**변경 예정**:
```cpp
// Factory Pattern으로 자동 선택
#include "db_connection_pool_factory.h"

auto pool = common::DbConnectionPoolFactory::createFromEnv();
if (!pool) {
    spdlog::error("Failed to create database connection pool");
    return 1;
}

if (!pool->initialize()) {
    spdlog::error("Failed to initialize database connection pool");
    return 1;
}

spdlog::info("Database connection pool initialized (type={})", pool->getDatabaseType());

// Repository에 전달
g_uploadRepo = std::make_shared<repositories::UploadRepository>(pool);
// ... 다른 Repository도 동일
```

**수정 대상 파일**:
- `services/pkd-management/src/main.cpp`
  - initializeServices() 함수
  - 약 10-15줄 수정 예상

**예상 소요 시간**: 30분

---

### 2. Repository 타입 호환성 확인 🔄

**현재 상황**:
- Repository들이 `std::shared_ptr<common::DbConnectionPool>` 사용 (PostgreSQL 전용)

**필요한 변경**:
- `std::shared_ptr<common::IDbConnectionPool>` 사용 (추상 인터페이스)

**수정 대상**:
- Repository 헤더 파일 (5개)
  - UploadRepository
  - CertificateRepository
  - ValidationRepository
  - AuditRepository
  - StatisticsRepository

**Repository 수정 예시**:
```cpp
// Before
class UploadRepository {
    std::shared_ptr<common::DbConnectionPool> dbPool_;
public:
    UploadRepository(std::shared_ptr<common::DbConnectionPool> dbPool);
};

// After
class UploadRepository {
    std::shared_ptr<common::IDbConnectionPool> dbPool_;
public:
    UploadRepository(std::shared_ptr<common::IDbConnectionPool> dbPool);
};
```

**예상 소요 시간**: 1시간

---

### 3. Oracle 연결 테스트 🔄

**테스트 항목**:
1. ✅ Oracle 데이터베이스 연결 확인
2. ✅ OTL 초기화 성공 확인
3. ✅ Connection Pool 생성 확인
4. ⏳ 간단한 SELECT 쿼리 테스트
5. ⏳ Health check 엔드포인트 확인

**테스트 명령**:
```bash
# 서비스 재시작
docker compose -f docker/docker-compose.dev.yaml restart pkd-management-dev

# 로그 확인
docker logs -f icao-pkd-management-dev | grep -i oracle

# Health check
curl http://localhost:18091/api/health

# Oracle 연결 확인
docker exec icao-oracle-xe-dev sqlplus pkd/pkd123@LOCALPKD <<EOF
SELECT COUNT(*) FROM uploaded_file;
EXIT;
EOF
```

**예상 소요 시간**: 30분

---

### 4. 통합 테스트 🔄

**시나리오**:
1. LDIF 파일 업로드 (최소 파일로 테스트)
2. PostgreSQL과 Oracle 결과 비교
3. 성능 측정

**예상 소요 시간**: 1시간

---

## 아키텍처 다이어그램

```
┌─────────────────────────────────────────────────────┐
│                   Application                        │
│                    (main.cpp)                        │
└────────────────┬────────────────────────────────────┘
                 │
                 │ createFromEnv()
                 ▼
┌─────────────────────────────────────────────────────┐
│          DbConnectionPoolFactory                     │
│                                                       │
│  ┌──────────────────────────────────────────────┐  │
│  │ DbPoolConfig::fromEnvironment()              │  │
│  │ - Reads DB_TYPE, ORACLE_*, DB_* env vars    │  │
│  └──────────────────────────────────────────────┘  │
│                                                       │
│  ┌──────────────────────────────────────────────┐  │
│  │ create(config)                                │  │
│  │ - if (dbType == "oracle")                     │  │
│  │     return OracleConnectionPool              │  │
│  │ - else if (dbType == "postgres")             │  │
│  │     return DbConnectionPool (PostgreSQL)     │  │
│  └──────────────────────────────────────────────┘  │
└────────────────┬────────────────┬───────────────────┘
                 │                │
     ┌───────────┘                └───────────┐
     │                                        │
     ▼                                        ▼
┌─────────────────┐              ┌─────────────────────┐
│ DbConnectionPool│              │OracleConnectionPool │
│  (PostgreSQL)   │              │     (OTL-based)     │
│                 │              │                     │
│ - libpq         │              │ - OTL 4.0.498       │
│ - PGconn*       │              │ - otl_connect*      │
└─────────────────┘              └─────────────────────┘
     │                                        │
     │ implements                             │ implements
     ▼                                        ▼
┌──────────────────────────────────────────────────────┐
│            IDbConnectionPool                          │
│                                                        │
│  - initialize()                                        │
│  - acquireGeneric() → IDbConnection                   │
│  - getStats()                                          │
│  - shutdown()                                          │
│  - getDatabaseType()                                   │
└────────────────────────────────────────────────────────┘
```

---

## 주요 설계 결정

### 1. Strategy Pattern 선택 이유
- **장점**: 런타임에 데이터베이스 타입 전환 가능
- **단점**: 약간의 추상화 오버헤드
- **결론**: 유연성이 성능 오버헤드보다 중요

### 2. OTL 라이브러리 선택 이유
- **장점**: 헤더 온리, 경량, 안정적
- **단점**: Oracle 전용 (다른 DB 지원 불가)
- **대안**: SOCI, ODBC (범용성 있지만 복잡함)
- **결론**: Oracle 전용 프로젝트에 최적

### 3. Factory Pattern vs Service Locator
- **Factory**: 객체 생성 책임 분리
- **Service Locator**: 전역 레지스트리
- **선택**: Factory (더 명시적, 테스트 용이)

---

## 파일 변경 요약

### 생성된 파일 (13개)
```
shared/lib/database/
├── db_connection_interface.h              (NEW)
├── db_connection_pool_factory.h           (NEW)
├── db_connection_pool_factory.cpp         (NEW)
├── oracle_connection_pool.h               (NEW)
├── oracle_connection_pool.cpp             (NEW)
└── external/otl/otlv4.h                   (NEW, 973KB)

docker/
└── db/oracle-init/01-init-schema.sql      (NEW)

docker/docker-compose.dev.yaml             (NEW)
```

### 수정된 파일 (6개)
```
shared/lib/database/
├── db_connection_pool.h                   (Modified - 인터페이스 추가)
├── db_connection_pool.cpp                 (Modified - 메서드 구현)
└── CMakeLists.txt                         (Modified - Oracle 설정)

services/pkd-management/
└── Dockerfile                             (Modified - Oracle Client)

docker/
└── docker-compose.dev.yaml                (Modified - 포트, 네트워크)
```

---

## 빌드 및 배포

### 빌드 명령
```bash
# 전체 빌드 (최초 1회 또는 의존성 변경 시)
docker compose -f docker/docker-compose.dev.yaml build --no-cache

# 빠른 재빌드 (소스 코드만 변경 시)
docker compose -f docker/docker-compose.dev.yaml build

# 서비스 시작
docker compose -f docker/docker-compose.dev.yaml up -d

# 로그 확인
docker logs -f icao-pkd-management-dev
```

### 빌드 시간
- **--no-cache**: 20-30분 (vcpkg 의존성 설치)
- **Cached**: 2-3분 (소스 코드 재컴파일만)

---

## 검증 체크리스트

### Phase 1 완료 기준
- [x] IDbConnectionPool 인터페이스 정의
- [x] DbConnectionPoolFactory 구현
- [x] OracleConnectionPool 구현
- [x] PostgreSQL Connection Pool 인터페이스 확장
- [x] CMake 빌드 시스템 통합
- [x] Docker 환경 구성 (Oracle XE)
- [x] 개발 환경 빌드 성공
- [x] 개발 환경 컨테이너 시작 성공
- [ ] main.cpp Factory Pattern 적용 (Phase 2)
- [ ] Oracle 연결 테스트 (Phase 2)

### Phase 2 완료 기준 (다음 세션)
- [ ] main.cpp Factory Pattern 적용
- [ ] Repository 인터페이스 타입 변경
- [ ] Oracle 연결 성공 확인
- [ ] 간단한 SELECT 쿼리 테스트
- [ ] Health check 엔드포인트 동작 확인

---

## 트러블슈팅 가이드

### 1. 빌드 오류: `e.msg` 타입 불일치
**증상**: `std::string + e.msg` 컴파일 에러
**해결**: `reinterpret_cast<const char*>(e.msg)` 사용

### 2. 빌드 오류: `ldap_unbind_ext_s` not declared
**증상**: OpenLDAP 함수가 Oracle LDAP 헤더에 없음
**해결**: Oracle SDK를 전역이 아닌 PRIVATE로 추가

### 3. 포트 충돌: 8091 already in use
**증상**: 프로덕션과 포트 충돌
**해결**: 개발 환경 포트에 `1` prefix 추가 (18091)

### 4. 네트워크 충돌: pkd-network
**증상**: 프로덕션과 네트워크 공유
**해결**: `name: icao-dev` 설정으로 프로젝트 분리

### 5. 컨테이너 이름 충돌
**증상**: Container name already in use
**해결**: 기존 컨테이너 제거 후 재시작
```bash
docker rm -f icao-oracle-xe-dev icao-pkd-management-dev
```

---

## 참고 자료

### OTL (Oracle Template Library)
- 공식 사이트: http://otl.sourceforge.net/
- 버전: 4.0.498
- 라이선스: BSD-style

### Oracle Instant Client
- 버전: 21.13
- 호환성: Oracle 11g, 12c, 18c, 19c, 21c
- 다운로드: https://www.oracle.com/database/technologies/instant-client/downloads.html

### Docker Compose
- 프로젝트 분리: `name` 속성 (v2.x)
- 네트워크: 자동 생성 (external: false)

---

## Git Commit

```bash
git add .
git commit -m "feat(database): Complete Oracle migration Phase 1 - Database Abstraction Layer

- Implement Strategy Pattern for database abstraction (IDbConnectionPool interface)
- Add Factory Pattern (DbConnectionPoolFactory) for runtime DB selection
- Implement OracleConnectionPool using OTL library (thread-safe, RAII)
- Extend PostgreSQL DbConnectionPool with IDbConnectionPool interface
- Configure Docker development environment (Oracle XE 21c)
- Separate dev/prod environments (icao-dev project, ports 18091/11521/15500)
- Fix build errors (OTL exception types, Oracle SDK include conflicts)
- Create Oracle initialization scripts (11 tables, UUID support)

Files:
  Added: 13 files (db_connection_interface.h, factory, oracle_connection_pool, OTL lib)
  Modified: 6 files (db_connection_pool, CMakeLists.txt, Dockerfile, compose)

Next Phase: Apply Factory Pattern to main.cpp, test Oracle connections

Ref: docs/ORACLE_MIGRATION_PHASE1_COMPLETION.md"
```

---

## 작성자

- **작성자**: Claude (Anthropic AI)
- **검토자**: kbjung
- **작성일**: 2026-02-04
- **문서 버전**: 1.0
