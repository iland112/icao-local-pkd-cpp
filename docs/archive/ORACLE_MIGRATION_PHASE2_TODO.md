# Oracle Migration Phase 2: TODO List

**시작 예정**: 다음 세션
**예상 소요**: 2-3시간
**목표**: Factory Pattern 적용 및 Oracle 연결 테스트 완료

---

## Phase 2 작업 목록

### 1. main.cpp에 Factory Pattern 적용 ⏳

**우선순위**: 🔴 높음
**예상 시간**: 30분

**작업 내용**:
- [ ] `db_connection_pool_factory.h` include 추가
- [ ] `buildPostgresConnInfo()` 함수 제거 (또는 주석 처리)
- [ ] `DbConnectionPoolFactory::createFromEnv()` 호출로 변경
- [ ] Pool 초기화 확인 및 에러 처리
- [ ] getDatabaseType() 로그 추가 (어떤 DB 사용 중인지 확인)

**수정 위치**:
```cpp
// services/pkd-management/src/main.cpp
// 함수: initializeServices() 또는 main()

// BEFORE (현재 코드)
std::string conninfo = buildPostgresConnInfo();
g_dbPool = std::make_shared<common::DbConnectionPool>(conninfo, 5, 20);

// AFTER (변경 예정)
auto pool = common::DbConnectionPoolFactory::createFromEnv();
if (!pool) {
    spdlog::error("Failed to create database connection pool");
    return 1;
}

if (!pool->initialize()) {
    spdlog::error("Failed to initialize database connection pool");
    return 1;
}

spdlog::info("Database connection pool initialized (type={})",
             pool->getDatabaseType());
```

**검증 방법**:
```bash
# 컨테이너 재시작
docker compose -f docker/docker-compose.dev.yaml restart pkd-management-dev

# 로그 확인 (Oracle 사용 확인)
docker logs icao-pkd-management-dev 2>&1 | grep -i oracle

# 예상 로그 출력
# [info] Database connection pool initialized (type=oracle)
# [info] OTL initialized successfully
# [info] Created new Oracle connection (total=1)
```

---

### 2. Repository 인터페이스 타입 변경 ⏳

**우선순위**: 🟡 중간
**예상 시간**: 1시간

**작업 내용**:
- [ ] UploadRepository 헤더 수정
- [ ] CertificateRepository 헤더 수정
- [ ] ValidationRepository 헤더 수정
- [ ] AuditRepository 헤더 수정
- [ ] StatisticsRepository 헤더 수정

**변경 패턴**:
```cpp
// BEFORE
class Repository {
    std::shared_ptr<common::DbConnectionPool> dbPool_;
public:
    Repository(std::shared_ptr<common::DbConnectionPool> dbPool);
};

// AFTER
class Repository {
    std::shared_ptr<common::IDbConnectionPool> dbPool_;
public:
    Repository(std::shared_ptr<common::IDbConnectionPool> dbPool);
};
```

**주의사항**:
- Repository 구현 파일(.cpp)의 생성자도 함께 수정
- `dbPool_->acquire()` 호출은 변경 불필요 (인터페이스 호환)
- PostgreSQL 전용 메서드 사용 시 주의 (현재는 없음)

---

### 3. Repository acquire() 호환성 확인 ⏳

**우선순위**: 🟡 중간
**예상 시간**: 30분

**작업 내용**:
- [ ] 모든 Repository의 `dbPool_->acquire()` 호출 확인
- [ ] PostgreSQL 전용 `DbConnection` 타입 사용 여부 확인
- [ ] `IDbConnection` 인터페이스로 변경 필요 시 수정

**현재 패턴**:
```cpp
// Repository 내부
auto conn = dbPool_->acquire();  // DbConnection 반환 (PostgreSQL 전용)
if (!conn.isValid()) {
    return error;
}
PGconn* pgConn = conn.get();  // PostgreSQL 전용!
```

**변경 필요 시**:
```cpp
// Option 1: 타입 확인 후 캐스팅 (비추천)
auto conn = dbPool_->acquireGeneric();  // IDbConnection 반환
if (dbPool_->getDatabaseType() == "postgres") {
    // PostgreSQL 전용 처리
}

// Option 2: SQL만 사용 (추천)
auto conn = dbPool_->acquireGeneric();
conn->execute("SELECT ...");
```

**결정 사항**:
- 현재 Repository가 libpq 함수를 직접 사용하는지 확인 필요
- 만약 사용한다면 Repository를 두 가지 버전으로 분리해야 할 수도 있음
- 또는 IDbConnection::execute() 인터페이스만 사용하도록 수정

---

### 4. Oracle 연결 테스트 ⏳

**우선순위**: 🔴 높음
**예상 시간**: 30분

**작업 내용**:
- [ ] 서비스 재시작 후 로그 확인
- [ ] OTL 초기화 성공 확인
- [ ] Oracle Connection Pool 생성 확인
- [ ] Health check 엔드포인트 테스트
- [ ] 간단한 SELECT 쿼리 테스트

**테스트 명령**:
```bash
# 1. 서비스 재시작
docker compose -f docker/docker-compose.dev.yaml restart pkd-management-dev

# 2. 로그 모니터링
docker logs -f icao-pkd-management-dev | grep -E "(OTL|Oracle|oracle)"

# 3. Health check
curl http://localhost:18091/api/health | jq .

# 4. Oracle 직접 연결 테스트
docker exec icao-oracle-xe-dev sqlplus pkd/pkd123@LOCALPKD <<EOF
SELECT COUNT(*) FROM uploaded_file;
SELECT table_name FROM user_tables;
EXIT;
EOF

# 5. 업로드 히스토리 조회 (Repository 테스트)
curl http://localhost:18091/api/upload/history | jq .
```

**예상 로그**:
```
[info] OTL initialized successfully
[info] Created new Oracle connection (total=1)
[info] Created new Oracle connection (total=2)
[info] Database connection pool initialized (type=oracle)
[info] Repository Pattern initialization complete - Ready for Oracle migration
```

---

### 5. 오류 처리 및 디버깅 ⏳

**우선순위**: 🟡 중간
**예상 시간**: 1시간 (문제 발생 시)

**예상 가능한 문제**:

#### 5.1. Oracle 연결 실패
**증상**: "Oracle connection failed: ORA-12154"
**원인**: TNS 이름 해석 실패
**해결**:
```cpp
// Connection String 형식 확인
// 올바른 형식: "user/password@host:port/service_name"
// 예: "pkd/pkd123@oracle-xe-dev:1521/LOCALPKD"
```

#### 5.2. OTL 초기화 실패
**증상**: "OTL initialization failed"
**원인**: Oracle Instant Client 경로 문제
**해결**:
```bash
# Dockerfile에서 환경 변수 확인
ENV LD_LIBRARY_PATH=$ORACLE_HOME:$LD_LIBRARY_PATH
```

#### 5.3. Repository SQL 오류
**증상**: SQL 실행 시 Oracle 문법 오류
**원인**: PostgreSQL과 Oracle SQL 문법 차이
**해결**:
- LIMIT → ROWNUM 또는 FETCH FIRST
- BOOLEAN → NUMBER(1)
- :: 캐스팅 → CAST() 함수

---

### 6. 통합 테스트 (선택 사항) ⏸️

**우선순위**: 🟢 낮음
**예상 시간**: 1-2시간

**작업 내용**:
- [ ] 최소 LDIF 파일 업로드 테스트
- [ ] 인증서 검색 API 테스트
- [ ] 통계 API 테스트
- [ ] PostgreSQL vs Oracle 결과 비교

**연기 사유**:
- SQL 쿼리 변환이 필요할 수 있음 (Phase 3 작업)
- 우선 연결 확인이 중요

---

## 진행 체크리스트

### 필수 작업 (Phase 2 완료 조건)
- [ ] main.cpp Factory Pattern 적용 완료
- [ ] 컴파일 성공 (빌드 오류 없음)
- [ ] 서비스 재시작 성공
- [ ] 로그에서 "type=oracle" 확인
- [ ] Health check 응답 확인
- [ ] Oracle 데이터베이스 직접 연결 확인

### 선택 작업 (Phase 3으로 연기 가능)
- [ ] Repository 인터페이스 타입 변경
- [ ] Repository acquire() 호환성 확인
- [ ] 통합 테스트

---

## 빠른 시작 가이드 (다음 세션)

```bash
# 1. 프로젝트 디렉토리로 이동
cd /home/kbjung/projects/c/icao-local-pkd

# 2. 개발 환경 상태 확인
docker ps --filter "label=com.docker.compose.project=icao-dev"

# 3. main.cpp 수정 (Factory Pattern 적용)
vim services/pkd-management/src/main.cpp

# 4. 재빌드
docker compose -f docker/docker-compose.dev.yaml build pkd-management-dev

# 5. 재시작
docker compose -f docker/docker-compose.dev.yaml restart pkd-management-dev

# 6. 로그 확인
docker logs -f icao-pkd-management-dev | grep -E "(OTL|Oracle|type=)"

# 7. Health check
curl http://localhost:18091/api/health
```

---

## 참고 파일

### 수정 대상 파일
- `services/pkd-management/src/main.cpp` - Factory Pattern 적용
- `services/pkd-management/src/repositories/*.h` - 인터페이스 타입 변경 (선택)

### 참고 파일
- `shared/lib/database/db_connection_pool_factory.h` - Factory 사용법
- `shared/lib/database/oracle_connection_pool.cpp` - Oracle 구현
- `docs/ORACLE_MIGRATION_PHASE1_COMPLETION.md` - Phase 1 완료 보고서

---

## 예상 결과

### 성공 시 로그 출력
```
[2026-02-04 XX:XX:XX] [info] Initializing Repository Pattern...
[2026-02-04 XX:XX:XX] [info] OTL initialized successfully
[2026-02-04 XX:XX:XX] [info] Created new Oracle connection (total=1)
[2026-02-04 XX:XX:XX] [info] Created new Oracle connection (total=2)
[2026-02-04 XX:XX:XX] [info] Database connection pool initialized (type=oracle)
[2026-02-04 XX:XX:XX] [info] Repositories initialized with Connection Pool
[2026-02-04 XX:XX:XX] [info] Repository Pattern initialization complete
[2026-02-04 XX:XX:XX] [info] Server starting on http://0.0.0.0:18091
```

### Health Check 응답
```json
{
  "status": "healthy",
  "database": {
    "type": "oracle",
    "available": 2,
    "total": 2,
    "status": "connected"
  },
  "version": "v2.4.3",
  "timestamp": "2026-02-04T09:00:00Z"
}
```

---

## 작성자

- **작성자**: Claude (Anthropic AI)
- **작성일**: 2026-02-04
- **문서 버전**: 1.0
