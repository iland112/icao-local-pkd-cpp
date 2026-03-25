# PKD Relay Service

**Port**: 8083 (via API Gateway :8080/api)
**Language**: C++20, Drogon Framework
**Role**: DB-LDAP 동기화, Reconciliation, ICAO PKD LDAP 자동 동기화, 실시간 알림

---

## API Endpoints

### Sync Status
- `GET /api/sync/status` — DB-LDAP 동기화 상태
- `GET /api/sync/stats` — 동기화 통계
- `POST /api/sync/check` — 수동 동기화 체크
- `POST /api/sync/revalidate` — DSC 만료 상태 재검증

### Reconciliation
- `POST /api/sync/reconcile` — Reconciliation 실행
- `GET /api/sync/reconcile/history` — Reconciliation 이력
- `GET /api/sync/reconcile/{id}` — Reconciliation 상세
- `GET /api/sync/reconcile/stats` — Reconciliation 통계

### ICAO PKD LDAP Sync
- `POST /api/sync/icao-ldap/trigger` — ICAO PKD 동기화 트리거
- `GET /api/sync/icao-ldap/status` — 동기화 상태
- `GET /api/sync/icao-ldap/history` — 동기화 이력
- `GET/PUT /api/sync/icao-ldap/config` — 동기화 설정
- `POST /api/sync/icao-ldap/test` — ICAO LDAP 연결 테스트

### Notifications
- `GET /api/sync/notifications/stream` — SSE 실시간 알림 스트림

---

## Reconciliation 로직

1. DB에서 `stored_in_ldap=FALSE` 엔티티 검색
2. LDAP에서 실제 존재 여부 확인
3. 누락 시 LDAP에 추가 (부모 DN 자동 생성)
4. `stored_in_ldap=TRUE` 업데이트
5. reconciliation_log에 기록
- 범위: CSCA, DSC, CRL (DSC_NC 제외 — ICAO 2021년 nc-data 폐지)

## ICAO PKD LDAP 동기화 파이프라인

ML→CSCA → **CRL** → DSC(Trust Chain+CRL+Doc9303) → DSC_NC (4단계)
- `IcaoLdapClient`: Simple Bind / TLS + SASL EXTERNAL 이중 모드, `lastError()` 상세 에러 메시지
- fingerprint 캐시(`unordered_set`) → X.509 메타데이터 22개 추출 → Trust Chain+CRL+ICAO 검증 → DB/LDAP 저장
- Master List CMS SignedData에서 CSCA/MLSC 추출 (ICAO PKD DIT에 o=csca 없음)
- `processEntry()` → `EntryResult(NEW/SKIPPED/FAILED)` 3-state — 실패 정확 카운트
- `validateAndSaveResult`: Trust Chain + CRL 폐기 + ICAO Doc 9303 검사 → validation_result 28개 컬럼
- 연속 50건 실패 시 자동 중단, 재시작 시 fingerprint 기반 자동 resume
- **성능**: fingerprint 캐시 + duplicate 배치(500건) + validation 배치(100건) → 전체 skip 2.9분
- 연결 테스트: TLS 인증서 정보(Subject/Issuer/만료일) 반환
- 중복 인증서: `certificate_duplicates` 테이블에 기록 (배치 flush)

## 코드 구조

```
src/
├── handlers/         # SyncHandler(10), ReconciliationHandler(4), IcaoLdapHandler(6), NotificationHandler
├── repositories/     # Certificate, CRL, SyncStatus, Reconciliation, Validation
├── services/         # SyncService, ReconciliationService, ValidationService (DSC 재검증)
├── relay/icao-ldap/  # IcaoLdapClient, IcaoLdapSyncService (ML 추출 + 동기화)
├── relay/sync/       # ReconciliationEngine, LdapOperations
├── infrastructure/   # ServiceContainer, SyncScheduler, RelayOperations
└── common/           # NotificationManager (SSE, thread-safe)
```
