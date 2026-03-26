# PKD Relay Service

**Port**: 8083 (via API Gateway :8080/api)
**Language**: C++20, Drogon Framework
**Role**: 외부 ICAO PKD 연계 — LDIF/ML 파일 임포트, ICAO PKD LDAP 자동 동기화, ICAO 버전 감지

---

## API Endpoints

### Upload Management (pkd-management에서 이동, v2.41.0)
- `POST /api/upload/ldif` — LDIF 업로드
- `POST /api/upload/masterlist` — Master List 업로드
- `GET /api/upload/history` — 업로드 이력 (paginated)
- `GET /api/upload/detail/{id}` — 업로드 상세
- `POST /api/upload/{id}/retry` — 실패 업로드 재시도
- `DELETE /api/upload/{id}` — 업로드 삭제
- `GET /api/upload/statistics` — 업로드 통계
- `GET /api/upload/countries` — 국가 통계
- `GET /api/upload/{id}/validations` — Trust Chain 검증 결과
- `GET /api/upload/{id}/issues` — 중복 인증서
- `GET /api/upload/{id}/ldif-structure` — LDIF/ASN.1 구조
- `GET /api/progress/{id}` — SSE 진행 상황 스트림

### ICAO Version Detection (pkd-management에서 이동, v2.41.0)
- `GET /api/icao/check-updates` — ICAO 포털 버전 확인
- `GET /api/icao/latest` — 최신 감지 버전
- `GET /api/icao/history` — 버전 감지 이력
- `GET /api/icao/status` — 버전 비교 (감지 vs 업로드)

### ICAO PKD LDAP Sync
- `POST /api/sync/icao-ldap/trigger` — ICAO PKD 동기화 트리거
- `GET /api/sync/icao-ldap/status` — 동기화 상태
- `GET /api/sync/icao-ldap/history` — 동기화 이력
- `GET/PUT /api/sync/icao-ldap/config` — 동기화 설정
- `POST /api/sync/icao-ldap/test` — ICAO LDAP 연결 테스트

> **Note**: DB-LDAP Sync/Reconciliation은 pkd-management로 이동 (v2.41.0)

---

## ICAO PKD LDAP 동기화 파이프라인

ML→CSCA → **CRL** → DSC(Trust Chain+CRL+Doc9303) → DSC_NC (4단계)
- `IcaoLdapClient`: Simple Bind / TLS + SASL EXTERNAL 이중 모드
- fingerprint 캐시(`unordered_set`) → X.509 메타데이터 22개 추출 → Trust Chain+CRL+ICAO 검증 → DB/LDAP 저장
- Master List CMS SignedData에서 CSCA/MLSC 추출 (ICAO PKD DIT에 o=csca 없음)
- `processEntry()` → `EntryResult(NEW/SKIPPED/FAILED)` 3-state
- 연속 50건 실패 시 자동 중단, 재시작 시 fingerprint 기반 자동 resume
- **성능**: fingerprint 캐시 + duplicate 배치(500건) + validation 배치(100건) → 전체 skip 2.9분

## 코드 구조

```
src/
├── handlers/           # HealthHandler, IcaoLdapHandler(6)
├── repositories/       # Certificate, CRL, SyncStatus, Reconciliation, Validation (relay용)
├── services/           # SyncService, ReconciliationService, ValidationService (relay용, 추후 정리)
├── relay/icao-ldap/    # IcaoLdapClient, IcaoLdapSyncService (ML 추출 + LDAP 동기화)
├── relay/sync/         # ReconciliationEngine, LdapOperations (추후 정리)
├── infrastructure/     # ServiceContainer, SyncScheduler, RelayOperations
├── upload/             # Upload 모듈 (pkd-management에서 이동)
│   ├── handlers/       # UploadHandler, UploadStatsHandler, IcaoHandler
│   ├── services/       # UploadService, ValidationService, LdifStructureService, LdapStorageService, IcaoSyncService
│   ├── repositories/   # Upload, Certificate, CRL, Validation, LdifStructure, DeviationList, IcaoVersion
│   ├── common/         # certificate_utils, masterlist_processor, progress_manager, x509_metadata 등
│   └── domain/models/  # certificate, validation_result, icao_version
└── common/             # NotificationManager (SSE)
```
