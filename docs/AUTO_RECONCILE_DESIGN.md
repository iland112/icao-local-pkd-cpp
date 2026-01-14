# Auto Reconcile 기능 설계 문서

**작성일**: 2026-01-14
**상태**: 설계 완료 / 구현 예정
**버전**: 1.0.0

---

## 1. 개요

Auto Reconcile는 PostgreSQL과 OpenLDAP 간 인증서 데이터 불일치를 **자동으로 해소**하는 기능입니다.

### 목적
- DB-LDAP 동기화 상태 모니터링 중 발견된 불일치를 자동으로 조정
- PostgreSQL을 Single Source of Truth로 사용하여 LDAP 데이터 정합성 보장
- 수동 개입 없이 데이터 일관성 유지

### 적용 범위
- CSCA (Country Signing CA) 인증서
- DSC (Document Signer Certificate) 인증서
- DSC_NC (Non-Conformant DSC) 인증서
- CRL (Certificate Revocation List)

---

## 2. 시스템 아키텍처

### 2.1 현재 구현 상태

| 컴포넌트 | 상태 | 위치 |
|---------|------|------|
| **Frontend UI** | ✅ 완료 | `frontend/src/pages/SyncDashboard.tsx` |
| **설정 API** | ✅ 완료 | `services/sync-service/src/main.cpp` |
| **데이터베이스 스키마** | ✅ 완료 | `sync_config` 테이블 |
| **Reconciliation 로직** | ❌ 미구현 | TODO (main.cpp:1130) |
| **Audit Log** | ❌ 미구현 | 새 테이블 필요 |

### 2.2 동작 흐름

```
┌─────────────────────────────────────────────────────────────────┐
│                    Sync Status Check                             │
│  (매일 자동 실행 또는 수동 트리거)                                 │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
                 ┌───────────────┐
                 │ DISCREPANCY?  │
                 └───────┬───────┘
                         │
                 ┌───────┴───────┐
                 │               │
            No   │               │ Yes
                 ▼               ▼
         ┌──────────┐    ┌─────────────────┐
         │  SYNCED  │    │ Auto Reconcile  │
         │  (완료)   │    │   Enabled?      │
         └──────────┘    └────────┬────────┘
                                  │
                          ┌───────┴───────┐
                          │               │
                     No   │               │ Yes
                          ▼               ▼
                  ┌──────────┐    ┌─────────────────┐
                  │   SKIP   │    │  Reconciliation │
                  │ (수동 조정)│    │   Process       │
                  └──────────┘    └────────┬────────┘
                                           │
                                           ▼
                                  ┌─────────────────┐
                                  │ DB → LDAP Sync  │
                                  │  (Batch 처리)    │
                                  └────────┬────────┘
                                           │
                                           ▼
                                  ┌─────────────────┐
                                  │  Audit Logging  │
                                  └────────┬────────┘
                                           │
                                           ▼
                                  ┌─────────────────┐
                                  │ Status Update   │
                                  │  → SYNCED       │
                                  └─────────────────┘
```

---

## 3. 불일치 처리 전략

### 3.1 PostgreSQL → LDAP (Primary Strategy)

**원칙**: PostgreSQL이 Single Source of Truth

| 불일치 유형 | 처리 방법 | 비고 |
|-----------|---------|------|
| **DB에만 존재** | LDAP에 추가 | `ldap_add_ext_s()` |
| **LDAP에만 존재** | LDAP에서 삭제 | `ldap_delete_ext_s()` |
| **양쪽 모두 존재하지만 내용 다름** | LDAP 업데이트 (삭제 후 재추가) | `ldap_modify_ext_s()` 또는 delete+add |

### 3.2 Batch 처리

대량의 불일치를 한 번에 처리하면 시스템 부하 발생 가능. 배치 단위로 분할 처리:

```cpp
// Configuration
int maxReconcileBatchSize = 100;  // 한 번에 처리할 최대 개수
```

**처리 순서**:
1. CSCA (가장 중요 - Trust Chain의 Root)
2. DSC (CSCA에 의존)
3. DSC_NC
4. CRL

---

## 4. 데이터베이스 설계

### 4.1 기존 테이블 (sync_config)

```sql
CREATE TABLE sync_config (
    id SERIAL PRIMARY KEY,
    auto_reconcile BOOLEAN NOT NULL DEFAULT true,
    max_reconcile_batch_size INTEGER NOT NULL DEFAULT 100,
    daily_sync_enabled BOOLEAN NOT NULL DEFAULT true,
    daily_sync_hour INTEGER NOT NULL DEFAULT 2,
    daily_sync_minute INTEGER NOT NULL DEFAULT 0,
    revalidate_certs_on_sync BOOLEAN NOT NULL DEFAULT true,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 4.2 새 테이블 (reconciliation_log) - 구현 예정

```sql
CREATE TABLE reconciliation_log (
    id SERIAL PRIMARY KEY,
    started_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at TIMESTAMP,
    trigger_type VARCHAR(20) NOT NULL,  -- 'AUTO' | 'MANUAL'
    cert_type VARCHAR(10) NOT NULL,     -- 'CSCA' | 'DSC' | 'DSC_NC' | 'CRL'
    operation VARCHAR(10) NOT NULL,     -- 'ADD' | 'DELETE' | 'UPDATE'
    country_code CHAR(3),
    cert_subject TEXT,
    cert_issuer TEXT,
    status VARCHAR(20) NOT NULL,        -- 'SUCCESS' | 'FAILED'
    error_message TEXT,
    duration_ms INTEGER
);

CREATE INDEX idx_reconciliation_log_started_at ON reconciliation_log(started_at DESC);
CREATE INDEX idx_reconciliation_log_cert_type ON reconciliation_log(cert_type);
CREATE INDEX idx_reconciliation_log_status ON reconciliation_log(status);
```

### 4.3 Reconciliation Summary (reconciliation_summary)

```sql
CREATE TABLE reconciliation_summary (
    id SERIAL PRIMARY KEY,
    executed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    trigger_type VARCHAR(20) NOT NULL,
    total_processed INTEGER NOT NULL DEFAULT 0,
    csca_added INTEGER NOT NULL DEFAULT 0,
    csca_deleted INTEGER NOT NULL DEFAULT 0,
    dsc_added INTEGER NOT NULL DEFAULT 0,
    dsc_deleted INTEGER NOT NULL DEFAULT 0,
    dsc_nc_added INTEGER NOT NULL DEFAULT 0,
    dsc_nc_deleted INTEGER NOT NULL DEFAULT 0,
    crl_added INTEGER NOT NULL DEFAULT 0,
    crl_deleted INTEGER NOT NULL DEFAULT 0,
    success_count INTEGER NOT NULL DEFAULT 0,
    failed_count INTEGER NOT NULL DEFAULT 0,
    duration_ms INTEGER NOT NULL,
    status VARCHAR(20) NOT NULL  -- 'COMPLETED' | 'PARTIAL' | 'FAILED'
);

CREATE INDEX idx_reconciliation_summary_executed_at ON reconciliation_summary(executed_at DESC);
```

---

## 5. API 설계

### 5.1 기존 API (구현 완료)

#### GET `/api/sync/config`
현재 설정 조회

**Response**:
```json
{
  "autoReconcile": true,
  "maxReconcileBatchSize": 100,
  "dailySyncEnabled": true,
  "dailySyncHour": 2,
  "dailySyncMinute": 0,
  "dailySyncTime": "02:00",
  "revalidateCertsOnSync": true
}
```

#### PUT `/api/sync/config`
설정 변경

**Request**:
```json
{
  "autoReconcile": true,
  "maxReconcileBatchSize": 100
}
```

### 5.2 새 API (구현 예정)

#### POST `/api/sync/reconcile`
수동 Reconciliation 트리거

**Query Parameters**:
- `dryRun` (boolean, optional): `true`일 경우 실제 변경 없이 시뮬레이션만 실행

**Response**:
```json
{
  "success": true,
  "message": "Reconciliation completed",
  "summary": {
    "totalProcessed": 245,
    "cscaAdded": 10,
    "cscaDeleted": 5,
    "dscAdded": 150,
    "dscDeleted": 80,
    "dscNcAdded": 0,
    "dscNcDeleted": 0,
    "crlAdded": 0,
    "crlDeleted": 0,
    "successCount": 243,
    "failedCount": 2,
    "durationMs": 12345
  },
  "failures": [
    {
      "certType": "DSC",
      "operation": "ADD",
      "countryCode": "KOR",
      "subject": "CN=...",
      "error": "LDAP connection timeout"
    }
  ]
}
```

#### GET `/api/sync/reconcile/history`
Reconciliation 이력 조회

**Query Parameters**:
- `limit` (integer, default: 50)
- `offset` (integer, default: 0)

**Response**:
```json
{
  "total": 123,
  "history": [
    {
      "id": 1,
      "executedAt": "2026-01-14T10:30:00Z",
      "triggerType": "AUTO",
      "totalProcessed": 245,
      "successCount": 243,
      "failedCount": 2,
      "durationMs": 12345,
      "status": "COMPLETED"
    }
  ]
}
```

#### GET `/api/sync/reconcile/{id}`
특정 Reconciliation 상세 정보

**Response**:
```json
{
  "id": 1,
  "executedAt": "2026-01-14T10:30:00Z",
  "completedAt": "2026-01-14T10:30:12Z",
  "triggerType": "MANUAL",
  "summary": {
    "totalProcessed": 245,
    "cscaAdded": 10,
    "dscAdded": 150,
    "successCount": 243,
    "failedCount": 2
  },
  "logs": [
    {
      "certType": "CSCA",
      "operation": "ADD",
      "countryCode": "KOR",
      "subject": "CN=Korea CSCA",
      "status": "SUCCESS",
      "durationMs": 45
    },
    {
      "certType": "DSC",
      "operation": "DELETE",
      "countryCode": "USA",
      "subject": "CN=US DSC 001",
      "status": "FAILED",
      "errorMessage": "LDAP connection timeout",
      "durationMs": 5000
    }
  ]
}
```

---

## 6. 구현 계획

### 6.1 Phase 1: Core Reconciliation Logic

**파일**: `services/sync-service/src/main.cpp`

**구현 내용**:
```cpp
struct ReconciliationResult {
    int totalProcessed = 0;
    int cscaAdded = 0;
    int cscaDeleted = 0;
    int dscAdded = 0;
    int dscDeleted = 0;
    int dscNcAdded = 0;
    int dscNcDeleted = 0;
    int crlAdded = 0;
    int crlDeleted = 0;
    int successCount = 0;
    int failedCount = 0;
    int durationMs = 0;
    std::vector<ReconciliationFailure> failures;
};

// Main reconciliation function
ReconciliationResult performReconciliation(bool dryRun = false);

// Helper functions
std::vector<CertificateDiscrepancy> findCertificateDiscrepancies();
bool addCertificateToLdap(const Certificate& cert);
bool deleteCertificateFromLdap(const std::string& dn);
void logReconciliationOperation(const ReconciliationLog& log);
```

### 6.2 Phase 2: Database Schema Migration

**파일**: `docker/init-scripts/04-create-reconciliation-tables.sql` (신규)

**내용**:
- `reconciliation_log` 테이블 생성
- `reconciliation_summary` 테이블 생성
- 인덱스 생성

### 6.3 Phase 3: API Endpoints

**파일**: `services/sync-service/src/main.cpp`

**구현 내용**:
1. `POST /api/sync/reconcile` - 실제 reconciliation 실행
2. `GET /api/sync/reconcile/history` - 이력 조회
3. `GET /api/sync/reconcile/{id}` - 상세 조회

### 6.4 Phase 4: Frontend Integration

**파일**: `frontend/src/pages/SyncDashboard.tsx`

**구현 내용**:
1. Reconciliation History 섹션 추가
2. 수동 실행 버튼 동작 연결
3. Dry-run 모드 체크박스
4. 실행 결과 다이얼로그

### 6.5 Phase 5: Daily Scheduler Integration

**파일**: `services/sync-service/src/main.cpp`

**구현 내용**:
- Daily sync 완료 후 auto reconcile 자동 실행
- `dailySyncTask()` 함수에 통합

---

## 7. 테스트 계획

### 7.1 Unit Tests

- [ ] `findCertificateDiscrepancies()` - 불일치 감지 정확성
- [ ] `addCertificateToLdap()` - LDAP 추가 연산
- [ ] `deleteCertificateFromLdap()` - LDAP 삭제 연산
- [ ] Batch 처리 로직 (maxReconcileBatchSize)

### 7.2 Integration Tests

- [ ] PostgreSQL → LDAP 전체 동기화
- [ ] Daily scheduler 통합 동작
- [ ] API 엔드포인트 동작 확인

### 7.3 Scenario Tests

| 시나리오 | 초기 상태 | 예상 결과 |
|---------|---------|----------|
| DB에 새 CSCA 추가 | DB: 10, LDAP: 9 | LDAP에 1개 추가 → SYNCED |
| LDAP에 잘못된 DSC | DB: 100, LDAP: 101 | LDAP에서 1개 삭제 → SYNCED |
| 대량 불일치 (1000개) | Batch 처리 (100개씩) | 10회 반복 실행 → SYNCED |
| LDAP 연결 실패 | 중간에 LDAP 다운 | Partial 완료, 실패 로그 기록 |

---

## 8. 보안 고려사항

### 8.1 LDAP Write 권한
- Sync Service가 LDAP write 권한 보유 확인
- 환경 변수: `LDAP_BIND_DN`, `LDAP_BIND_PASSWORD`

### 8.2 Audit Trail
- 모든 reconciliation 작업은 `reconciliation_log`에 기록
- 삭제된 인증서도 복구 가능하도록 soft-delete 고려

### 8.3 Dry-run Mode
- 실제 변경 전 시뮬레이션 필수
- Frontend에서 dry-run 체크박스 제공

---

## 9. 성능 고려사항

### 9.1 Batch 처리
- 기본값: 100개씩 처리
- 대량 불일치 시 시스템 부하 분산

### 9.2 Transaction 관리
- PostgreSQL: 트랜잭션 단위 처리
- LDAP: 개별 연산 (트랜잭션 미지원)
- 실패 시 롤백 전략 필요

### 9.3 Concurrent Access
- Daily sync 중 reconciliation 실행 방지
- Mutex 또는 DB lock 사용

---

## 10. 에러 처리

### 10.1 LDAP 연결 실패
```cpp
if (!ldapConn) {
    spdlog::error("LDAP connection failed during reconciliation");
    return ReconciliationResult{
        .status = "FAILED",
        .errorMessage = "LDAP connection unavailable"
    };
}
```

### 10.2 부분 실패
- 일부 인증서 추가/삭제 실패 시:
  - 성공한 작업은 유지
  - 실패한 작업만 `reconciliation_log`에 기록
  - Status: `PARTIAL`

### 10.3 재시도 로직
```cpp
int maxRetries = 3;
int retryDelayMs = 1000;

for (int retry = 0; retry < maxRetries; retry++) {
    if (addCertificateToLdap(cert)) {
        break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
}
```

---

## 11. 모니터링 및 알림

### 11.1 메트릭
- Reconciliation 실행 횟수 (일별)
- 평균 처리 시간
- 성공률 (%)
- 실패 건수

### 11.2 알림 (향후 구현)
- Reconciliation 실패 시 이메일/Slack 알림
- 연속 실패 3회 시 긴급 알림

---

## 12. 향후 개선 사항

### 12.1 Bi-directional Sync (양방향 동기화)
- 현재: PostgreSQL → LDAP (단방향)
- 향후: LDAP 변경 사항도 PostgreSQL에 반영

### 12.2 Conflict Resolution
- 양쪽 모두 변경된 경우 충돌 해결 전략
- Last-write-wins vs Manual resolution

### 12.3 Incremental Sync
- 전체 스캔 대신 변경된 인증서만 동기화
- DB에 `last_synced_at` 컬럼 추가

---

## 참고 문서

- [DEPLOYMENT_PROCESS.md](DEPLOYMENT_PROCESS.md) - 배포 프로세스
- [CLAUDE.md](../CLAUDE.md) - 프로젝트 전체 문서
- [OpenLDAP Documentation](https://www.openldap.org/doc/) - LDAP API 참고

---

**작성자**: Claude (AI Assistant)
**검토자**: kbjung
**승인일**: 2026-01-14
