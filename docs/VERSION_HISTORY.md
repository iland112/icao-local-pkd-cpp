# ICAO Local PKD - Version History

**Current Version**: v2.27.1
**Period**: 2026-01-21 ~ 2026-03-04
**Total Releases**: 55+

---

## Version Summary

| Version | Date | Category | Summary |
|---------|------|----------|---------|
| v2.27.1 | 03-04 | Fix | API Client Rate Limit 분리 + Podman DNS Resolver 영구 수정 |
| v2.27.0 | 03-04 | Feature | FAILED 이어하기 재처리 + COMPLETED 재업로드 차단 + SAVEPOINT 에러 격리 |
| v2.26.1 | 03-03 | Performance | 3단계 성능 최적화 (X.509 이중 파싱 제거 + Statement 캐시 + 배치 커밋) |
| v2.26.0 | 03-03 | Performance | Oracle 업로드 성능 최적화 Phase 2 (Fingerprint 프리캐시) |
| v2.25.9 | 03-02 | Performance | Oracle 업로드 성능 최적화 (CSCA 캐시 + Regex 사전컴파일) |
| v2.25.8 | 03-02 | Performance | SQL 인덱스 최적화 (Oracle 패리티 + 복합 인덱스) |
| v2.25.7 | 03-02 | Quality | 안정성 강화 + AI 벡터화 + 코드 품질 개선 |
| v2.25.6 | 03-02 | Fix | Stepper 깜빡임 수정 + DSC_NC ICAO 준수 판정 수정 |
| v2.25.5 | 02-28 | Feature | 마이크로서비스 리소스 동적 확장성 |
| v2.25.4 | 02-28 | Performance | 서버 리소스 최적화 (환경별 튜닝) |
| v2.25.3 | 02-28 | Fix | Oracle XE 안정화 + XEPDB1 Healthcheck 개선 |
| v2.25.2 | 02-28 | Feature | 전체 서비스 운영 감사 로그 확장 |
| v2.25.1 | 02-28 | Fix | API 클라이언트 사용량 추적 + 감사 로그 Oracle 수정 |
| v2.25.0 | 02-28 | Feature | MANUAL 모드 제거 + AUTO 모드 LDAP 복원력 + 실패 업로드 재시도 |
| v2.24.1 | 02-27 | Fix | Podman 스크립트 안정성 + HTTPS Client 인증서 관리 개선 |
| v2.24.0 | 02-27 | Feature | Production Podman Migration + Oracle Schema Consolidation |
| v2.23.0 | 02-26 | Security | DoS 방어 보강 + 대시보드 통계 수정 + Admin 권한 UI |
| v2.22.1 | 02-25 | Fix | PA auth_request Backward Compatibility Fix + UsageDialog UX |
| v2.22.0 | 02-25 | Feature | API Client Usage Tracking + PA nginx auth_request |
| v2.21.0 | 02-24 | Feature | API Client Authentication (X-API-Key) |
| v2.20.2 | 02-22 | Fix | Oracle CRL Report BLOB Read Fix |
| v2.20.1 | 02-22 | Fix | AI Analysis Multi-DBMS Compatibility Fix |
| v2.20.0 | 02-22 | Feature | AI Certificate Forensic Analysis Engine Enhancement |
| v2.19.0 | 02-21 | Feature | HTTPS Support (Private CA) + Frontend Proxy + AI Dashboard UX |
| v2.18.1 | 02-21 | Feature | PA History Anonymous User IP/User-Agent Display |
| v2.18.0 | 02-20 | Feature | AI Certificate Analysis Engine |
| v2.17.0 | 02-20 | Feature | Doc 9303 Compliance Checklist |
| v2.16.0 | 02-20 | Feature | Code Master Table (Centralized Code/Status Management) |
| v2.15.2 | 02-20 | Feature | Trust Chain Path Distribution + PA Structured Error Messages |
| v2.15.1 | 02-19 | Feature | Trust Chain Demo + CRL Download + PA Conformance |
| v2.15.0 | 02-18 | Feature | CRL Report Page |
| v2.14.1 | 02-18 | Fix | Trust Chain Success Rate Fix + Upload History Duplicate Flow |
| v2.14.0 | 02-18 | Feature | Per-Certificate ICAO Compliance DB Storage + SSE Validation |
| v2.13.0 | 02-17 | Refactor | main.cpp Minimization: 9,752 → 1,261 lines (-87.1%) |
| v2.12.0 | 02-17 | Refactor | Architecture Rewrite: ServiceContainer, Handler Extraction |
| v2.11.0 | 02-16 | Refactor | Validation Library Extraction (icao::validation) |
| v2.10.5 | 02-15 | Security | Security Hardening (Full Audit + OWASP) |
| v2.10.4 | 02-15 | Feature | DSC_NC Non-Conformant Certificate Report Page |
| v2.10.3 | 02-14 | Feature | DSC Non-Conformant (nc-data) Support |
| v2.10.2 | 02-14 | Feature | Lightweight PA Lookup API + PA Trust Chain Multi-CSCA Fix |
| v2.10.1 | 02-14 | Feature | Validation Reason Tracking + Upload UX + PA CRL Date Fix |
| v2.10.0 | 02-14 | Feature | ICAO Auto Scheduler + Upload Processing UX + Event Log |
| v2.9.2 | 02-13 | Feature | Full Certificate Export + PA CRL Expiration Check |
| v2.9.1 | 02-13 | DevOps | ARM64 CI/CD Pipeline + Luckfox Full Deployment |
| v2.9.0 | 02-12 | Feature | DSC Auto-Registration + Certificate Source Filter & Dashboard |
| v2.8.0 | 02-12 | Feature | PA Verification UX + DG2 JPEG2000 Face Image Support |
| v2.7.1 | 02-12 | Fix | Monitoring Service DB-Free + Oracle Compatibility Fixes |
| v2.7.0 | 02-12 | Feature | Individual Certificate Upload + Preview-before-Save |
| v2.6.3 | 02-11 | Fix | Oracle Audit Log Complete + ICAO Endpoint Fix |
| v2.6.2 | 02-10 | Fix | Oracle Statistics & Full Data Upload |
| v2.6.1 | 02-09 | Fix | Master List Upload Oracle Support |
| v2.6.0-alpha | 02-08 | Feature | Oracle Authentication Complete |
| v2.5.0 | 02-06 | Feature | All Services Oracle Support |
| v2.5.0-dev | 02-04 | Feature | Oracle Migration Phase 1-4 |
| v2.4.3 | 02-04 | Feature | LDAP Connection Pool Migration Complete |
| v2.4.2 | 02-04 | Feature | Shared Database Connection Pool Library |
| v2.4.0 | 02-03 | Refactor | PKD Relay Repository Pattern Complete |
| v2.3.x | 02-02 | Feature | UI/UX Enhancements & System Stabilization |
| v2.2.x | 01-30 | Feature | Enhanced Metadata & Critical Fixes |
| v2.1.x | 01-26 | Refactor | Repository Pattern & Trust Chain |
| v2.0.x | 01-21 | Refactor | Service Separation & Security |
| v1.8-1.9 | — | Security | JWT Authentication + RBAC |

---

## 2026-03 (March)

### v2.27.1 (2026-03-04) - API Client Rate Limit 분리 + Podman DNS Resolver 영구 수정

**nginx Rate Limit 분리**
- `/api/auth/api-clients` 별도 location 블록 추가 — API 클라이언트 관리 엔드포인트에 `api_limit`(100r/s) 적용
- **Root cause**: `/api/auth` location이 `login_limit`(5r/m)을 사용하여 API 클라이언트 관리 포함 모든 auth 엔드포인트에 로그인 rate limit 적용 → 페이지 로드 GET이 rate limit 소모 → POST 발급 요청 차단
- Frontend 에러 표시: ApiClientManagement CreateDialog에 에러 state + 빨간 배너 추가, Regenerate 핸들러에 alert 추가 (기존 `console.error` 사일런트 실패 → 사용자 피드백)

**Podman DNS Resolver 영구 수정**
- `scripts/lib/common.sh`에 `generate_podman_nginx_conf()` 공유 함수 추가 — Podman aardvark-dns 게이트웨이 IP 자동 감지 후 nginx resolver 치환
- **Root cause**: Production Podman 환경에서 nginx `resolver 127.0.0.11`(Docker DNS) 사용으로 `auth_request` 서브리퀘스트 DNS 해석 타임아웃 → PA 서비스 500 오류
- `start.sh`, `restart.sh`, `clean-and-init.sh` 모두 `generate_podman_nginx_conf()` 공유 함수 사용으로 인라인 DNS 감지 코드 제거 (~50줄 중복 삭제)
- restart.sh api-gateway 재시작 시에도 `generate_podman_nginx_conf()` 자동 호출 (DNS resolver 누락 방지)
- `docker-compose.podman.yaml` 기본값을 존재하지 않는 `api-gateway-podman-ssl.conf` → 생성 파일 `.docker-data/nginx/api-gateway.conf`로 변경
- Docker clean-and-init SSL 보존: `.docker-data` 삭제 시 SSL 인증서 백업 후 복원

**Files**: nginx 3개 + scripts 4개 + compose 1개 + frontend 1개 = 10 files changed

---

### v2.27.0 (2026-03-04) - FAILED 이어하기 재처리 + COMPLETED 재업로드 차단 + SAVEPOINT 에러 격리

**FAILED 이어하기 재처리**
- FAILED 업로드 retry 시 기존 데이터 유지, fingerprint 캐시 기반으로 이미 처리된 인증서 스킵 (~15초 vs 기존 3분 35초)
- Early fingerprint cache check: `parseCertificateEntry()`에서 fingerprint 계산 직후 캐시 확인 → 히트 시 X.509 파싱/검증/DB/LDAP 전체 스킵
- DB 통계 재집계: resume 모드 처리 완료 후 DB에서 certificate/validation_result 기반 정확한 최종 통계 재계산

**COMPLETED 재업로드 차단**
- COMPLETED 파일 retry 불가(400), 동일 파일 재업로드 불가(409 DUPLICATE_FILE)
- 재처리 확인 다이얼로그: 재처리 클릭 시 확인 팝업 표시 (파일명, 상태, 진행률, 이어하기 모드 안내)

**SAVEPOINT 에러 격리**
- PostgreSQL 배치 트랜잭션 내 개별 엔트리 실패 시 `SAVEPOINT`/`ROLLBACK TO SAVEPOINT`로 전체 트랜잭션 보호 (cascade abort 방지)
- `IQueryExecutor` 인터페이스에 `savepoint()`, `rollbackToSavepoint()` 가상 메서드 추가
- `validation_result` 중복 방지: PostgreSQL `ON CONFLICT DO NOTHING`, Oracle `MERGE INTO` 적용

**Files**: 10 files changed (0 new, 10 modified)

---

### v2.26.1 (2026-03-03) - 3단계 성능 최적화

**X.509 이중 파싱 제거 + Statement 캐시 + 배치 커밋**

| 최적화 | 내용 |
|--------|------|
| X.509 이중 파싱 제거 | `d2i_X509()` + `extractMetadata()` 2회 → 1회 |
| OracleQueryExecutor 배치 모드 | 세션 고정, Statement 캐시, COMMIT 지연 |
| PostgreSQLQueryExecutor 배치 모드 | 커넥션 고정, `BEGIN`/`COMMIT` 트랜잭션 래핑 |
| ldif_processor 배치 호출 | 500건마다 `endBatch()`/`beginBatch()` 중간 커밋 |

**성능 결과** (30,114건 기준):

| Metric | v2.26.0 | v2.26.1 | 개선 |
|--------|---------|---------|------|
| 건당 처리시간 | 11.1ms | 7.3ms | 1.53x |
| 처리 속도 | 90건/초 | 137.5건/초 | 1.53x |
| 총 처리 시간 | 5분 35초 | 3분 39초 | -2분 |
| 최초 대비 | — | — | **21.2x** |

**Files**: 12 files changed

---

### v2.26.0 (2026-03-03) - Oracle 업로드 성능 최적화 Phase 2 (Fingerprint 프리캐시)

- LDIF 처리 시작 전 전체 인증서 fingerprint (~31K건)를 1회 벌크 로드 → `unordered_map` 캐시
- 매 인증서 중복체크 SELECT 제거 (30K 쿼리 → 1회)
- 캐시 히트 시 `d2i_X509` + 27개 메타데이터 파라미터 구성 완전 스킵
- 신규 INSERT 후 캐시 동기화 (같은 배치 내 중복 방지)

**성능**: v2.25.9(31.5ms) → v2.26.0(11.1ms) = **2.84x 개선**, 처리 시간 15분 49초 → 5분 35초 (-10분)

**Files**: 3 files changed

---

### v2.25.9 (2026-03-02) - Oracle 업로드 성능 최적화 (CSCA 캐시 + Regex 사전컴파일)

- CSCA 인메모리 캐시: LDIF 처리 시작 전 전체 CSCA (~845건) 벌크 로드 → DSC 29,838건 각각의 CSCA DB 조회 제거
- Oracle LOB 세션 드롭 완화: CSCA BLOB SELECT → OCI 세션 파괴 반복이 캐시로 근본 해결
- Regex 사전 컴파일: PostgreSQL→Oracle SQL 변환 패턴 8개를 static 1회 컴파일 (~150K 컴파일 제거)

**예상 성능**: 182ms/건 → ~32ms/건 (5.7x 개선)

**Files**: 6 files changed

---

### v2.25.8 (2026-03-02) - SQL 인덱스 최적화

- Oracle 패리티 인덱스 2개 추가 (`certificate.subject_dn`, `certificate.issuer_dn`)
- 복합 인덱스 7개 추가 (양쪽 DB): 동기화, 통계, CSCA 조회, CRL, 검증, 감사, AI 이상 탐지 쿼리 최적화
- 마이그레이션 스크립트: PostgreSQL `CREATE INDEX IF NOT EXISTS` + Oracle PL/SQL 예외 처리

**Files**: 8 files changed (2 new, 6 modified)

---

### v2.25.7 (2026-03-02) - 안정성 강화 + AI 벡터화 + 코드 품질 개선

**Phase 1 — 안정성**: OpenSSL null 체크, optional 안전화, LDAP RAII 가드, 임시 파일 RAII 가드, 리소스 제한
**Phase 2 — AI 성능**: feature_engineering, risk_scorer, extension_rules_engine, issuer_profiler 벡터화
**Phase 3 — 코드 품질**: Shell 공유 라이브러리 (`common.sh`, 319줄 14 함수), API 응답 헬퍼

**Files**: ~20 files changed, ~1,010 lines changed

---

### v2.25.6 (2026-03-02) - Stepper 깜빡임 수정 + DSC_NC ICAO 준수 판정 수정

- **Stepper fix**: `useRef`로 마지막 active step 기억 → 단계 전환 중 상세 패널 유지
- **DSC_NC fix**: DSC_NC는 ICAO PKD에서 표준 미준수 분류 → 기술 체크와 무관하게 항상 `NON_CONFORMANT` 반환

---

## 2026-02-28 (Late February)

### v2.25.5 - 마이크로서비스 리소스 동적 확장성

5개 신규 환경변수로 하드코딩된 리소스 파라미터 외부화:
- `LDAP_NETWORK_TIMEOUT`(5s), `LDAP_HEALTH_CHECK_TIMEOUT`(2s), `LDAP_WRITE_TIMEOUT`(10s)
- `MAX_CONCURRENT_UPLOADS`(3), `MAX_BODY_SIZE_MB`(100/50)

### v2.25.4 - 서버 리소스 최적화 (환경별 튜닝)

- Production (16코어): DB Pool max 10→20, LDAP Pool max 10→20, AI workers 1→4
- Local (8코어): THREAD_NUM 16→8, AI workers 1→2
- Oracle XE SGA 검증: 1GB가 안정 최대값

### v2.25.3 - Oracle XE 안정화 + XEPDB1 Healthcheck 개선

- **CRITICAL FIX**: EE 파라미터 삭제 → `ORA-56752` 기동 실패 근본 원인 제거
- Healthcheck CDB→XEPDB1(PDB) 변경, start.sh에 XEPDB1 대기 로직 추가

### v2.25.2 - 전체 서비스 운영 감사 로그 확장

- 15개 신규 OperationType (API_CLIENT, CODE_MASTER, USER, UPLOAD_RETRY 등)
- 총 ~25개 감사 로그 엔드포인트 (기존 ~9개 + 16개 추가)

### v2.25.1 - API 클라이언트 사용량 추적 + 감사 로그 Oracle 수정

- **CRITICAL FIX**: CIDR 매칭 — 비트 연산 기반으로 교체 (`/8`, `/16` 매칭 오류 수정)
- nginx: auth_request에 `X-API-Key` 헤더 전달 누락 수정
- Oracle CLOB: `TO_CHAR()` 래핑으로 LOB/non-LOB 혼합 fetch 수정

### v2.25.0 - MANUAL 모드 제거 + AUTO 모드 LDAP 복원력

- MANUAL 모드 완전 제거 (~470줄 + ~200줄 삭제, net -948 lines)
- LDAP 연결 실패 시 차단→경고 (DB-only 저장 + Reconciliation 동기화)
- `POST /api/upload/{id}/retry` 재시도 엔드포인트 추가

---

## 2026-02-27

### v2.24.1 - Podman 스크립트 안정성

- stop.sh `--profile` 추가, health.sh port 수정, restart.sh 전체 재시작 개선
- Production 5회 반복 테스트 + 7개 스크립트 전수 테스트 통과

### v2.24.0 - Production Podman Migration + Oracle Schema Consolidation

- Docker CE → Podman 5.6.0 전환 (RHEL 9 Production)
- SELinux Rootless Podman: 2단계 `chcon` 사전 라벨링
- LDAP init: `podman exec`로 직접 초기화 (Docker init 컨테이너 불필요)
- Oracle schema 리팩토링: 독립 실행 가능한 스크립트 구조

---

## 2026-02-25 ~ 02-26

### v2.23.0 - DoS 방어 보강 + 대시보드 통계 수정 + Admin 권한 UI

**Security**: 파일 크기 제한, 동시 처리 제한(3개), LDAP 쓰기 타임아웃, 캐시 만료, 분포 map 제한, nginx per-IP
**Bug fixes**: 검증 실패 중복 계수, PA 이력 상태 카드 전체 카운트
**Frontend**: AdminRoute 컴포넌트, 권한 기반 Sidebar 메뉴

### v2.22.1 - PA auth_request Backward Compatibility Fix

- **CRITICAL FIX**: 미등록 API Key → 200 OK 반환 (기존 클라이언트 호환성)

### v2.22.0 - API Client Usage Tracking + PA nginx auth_request

- nginx `auth_request` 모듈로 PA 서비스 API Key 추적
- `GET /api/auth/internal/check` 내부 엔드포인트
- Frontend UsageDialog: 사용량 차트 + 상세 테이블

---

## 2026-02-24

### v2.21.0 - API Client Authentication (X-API-Key)

- 외부 클라이언트 API Key 인증 (M2M, server-to-server)
- API Key format: `icao_{prefix}_{random}` (46 chars), SHA-256 해시 저장
- 10개 granular permissions, 3-tier Rate Limiting, IP/CIDR whitelist
- 7개 관리 API 엔드포인트 (admin JWT required)

---

## 2026-02-22

### v2.20.2 - Oracle CRL Report BLOB Read Fix

- **CRITICAL FIX**: OCI LOB locator BLOB truncation → `RAWTOHEX(DBMS_LOB.SUBSTR())` 우회

### v2.20.1 - AI Analysis Multi-DBMS Compatibility Fix

- **CRITICAL FIX**: PostgreSQL `varchar = uuid` JOIN 오류 → `c.id = v.certificate_id`
- `safe_json_loads()`, `safe_isna()` 헬퍼 통합

### v2.20.0 - AI Certificate Forensic Analysis Engine Enhancement

- Feature engineering 25→45, 인증서 유형별 모델, Extension rules engine
- Issuer profiling, Forensic risk scoring (10 categories)
- 5 new API endpoints, Frontend 포렌식 탭

---

## 2026-02-20 ~ 02-21

### v2.19.0 - HTTPS Support (Private CA) + Frontend Proxy

- Private CA 기반 TLS (RSA 4096 CA + RSA 2048 서버), HTTP+HTTPS dual-listen
- nginx 프론트엔드 프록시, 동적 CORS origin
- AI Dashboard UX 전면 리디자인

### v2.18.1 - PA History Anonymous User IP/User-Agent Display

### v2.18.0 - AI Certificate Analysis Engine

- Python FastAPI ML-based anomaly detection (Isolation Forest + LOF)
- 25 ML features, Composite risk scoring (0~100), APScheduler 스케줄링
- 12 API endpoints, Frontend 대시보드

### v2.17.0 - Doc 9303 Compliance Checklist

- Per-item compliance checklist (~28 checks), 인증서 유형별 체크
- Preview API + 전용 API, Frontend 컴포넌트

### v2.16.0 - Code Master Table

- 21 code categories, ~150 seed values
- CRUD API + `useCodeMaster()` TanStack Query 훅

### v2.15.2 - Trust Chain Path Distribution + PA Structured Error Messages

---

## 2026-02-18 ~ 02-19

### v2.15.1 - Trust Chain Demo + CRL Download + PA Conformance

- Trust Chain 통계 대시보드, CRL .crl 파일 다운로드, PA 비적합 DSC 경고

### v2.15.0 - CRL Report Page

- CRL 분석 대시보드: 통계, 차트, 폐기 인증서 목록, CSV export

### v2.14.1 - Trust Chain Success Rate Fix

### v2.14.0 - Per-Certificate ICAO Compliance DB Storage + SSE Validation

- ICAO 9303 compliance 8개 컬럼 DB 영구 저장
- Per-certificate validation log SSE 스트리밍
- ValidationSummaryPanel 컴포넌트화

---

## 2026-02-17

### v2.13.0 - main.cpp Minimization: 9,752 → 1,261 lines (-87.1%)

| Service | Before | After | Reduction |
|---------|--------|-------|-----------|
| PKD Management | 4,722 | 430 | -90.9% |
| PA Service | 2,800 | 281 | -90.0% |
| PKD Relay | 1,644 | 457 | -72.2% |
| Monitoring | 586 | 93 | -84.1% |

### v2.12.0 - Architecture Rewrite: ServiceContainer, Handler Extraction

- ServiceContainer (pImpl, 7-phase DI), 17개 global 변수 제거
- Handler extraction: Upload(10), UploadStats(11), Certificate(12) endpoints
- Query Helpers (`common::db::`), Frontend decomposition, echarts→recharts

---

## 2026-02-16

### v2.11.0 - Validation Library Extraction (icao::validation)

- 공유 라이브러리: trust_chain_builder, crl_checker, extension_validator, algorithm_compliance
- Provider/Adapter 패턴: DB vs LDAP 백엔드 추상화
- PKD Management -62%, PA Service -41% 코드 감소

---

## 2026-02-14 ~ 02-15

### v2.10.5 - Security Hardening (Full Audit + OWASP)

- **CRITICAL**: Command Injection 제거 (`system()`/`popen()` → native C API)
- **HIGH**: SQL Injection 방지, SOD Parser buffer overread 보호, OpenSSL null check (24개소)
- **MEDIUM**: nginx 보안 헤더, JWT_SECRET 최소 길이, LDAP DN escape

### v2.10.4 - DSC_NC Non-Conformant Certificate Report

### v2.10.3 - DSC Non-Conformant (nc-data) Support

### v2.10.2 - Lightweight PA Lookup API

- Subject DN/fingerprint 기반 pre-computed trust chain 조회 (5~20ms)

### v2.10.1 - Validation Reason Tracking + Upload UX

### v2.10.0 - ICAO Auto Scheduler + Upload Processing UX + Event Log

- ICAO PKD daily auto version check, PROCESSING 상태, EventLog 컴포넌트

---

## 2026-02-12 ~ 02-13

### v2.9.2 - Full Certificate Export

- LDAP DIT 구조 ZIP export (~31K certs + 69 CRLs + 27 MLs)

### v2.9.1 - ARM64 CI/CD Pipeline + Luckfox Deployment

- GitHub Actions ARM64 파이프라인, Luckfox 전체 배포

### v2.9.0 - DSC Auto-Registration + Certificate Source Filter

- PA 검증에서 DSC 자동 등록 (source_type='PA_EXTRACTED')

### v2.8.0 - PA Verification UX + DG2 JPEG2000 Support

- JPEG2000→JPEG 서버 변환 (OpenJPEG + libjpeg)

### v2.7.1 - Monitoring Service DB-Free

### v2.7.0 - Individual Certificate Upload + Preview-before-Save

- PEM, DER, P7B, DL, CRL 지원, Preview-before-save workflow

---

## 2026-02-08 ~ 02-11

### v2.6.3 - Oracle Audit Log Complete

### v2.6.2 - Oracle Statistics & Full Data Upload

- Production 31,212건 인증서 업로드 검증 (100% DB-LDAP consistency)

### v2.6.1 - Master List Upload Oracle Support

### v2.6.0-alpha - Oracle Authentication Complete

- Pure OCI API 구현, 컬럼명 auto-lowercase, DB-aware boolean

---

## 2026-02-04 ~ 02-06

### v2.5.0 - All Services Oracle Support

- PA Service, PKD Relay, PKD Management 전부 Oracle 지원 완료

### v2.5.0-dev - Oracle Migration Phase 1-4

- Database abstraction (Strategy + Factory), OracleQueryExecutor, Repository migration
- Oracle XE 21c 스키마 (20 테이블), 성능 벤치마크

### v2.4.3 - LDAP Connection Pool Migration Complete

- 50x 성능 개선, 3개 서비스 전부 공유 `icao::ldap` 풀

### v2.4.2 - Shared Database Connection Pool Library

- `icao::database` 공유 라이브러리, 11개 repository 마이그레이션

---

## 2026-02-02 ~ 02-03

### v2.4.0 - PKD Relay Repository Pattern Complete

- 5 domain models, 4 repositories, 100% SQL 제거

### v2.3.x - UI/UX Enhancements & System Stabilization

- TreeViewer 리팩토링, 인증서 검색 UI, 감사 로그, PA 통합

---

## 2026-01-30 ~ 01-31

### v2.2.x - Enhanced Metadata & Critical Fixes

- X.509 메타데이터 추출 (22 fields), ICAO compliance checking
- LDIF 구조 시각화, ASN.1 파서

---

## 2026-01-26 ~ 01-30

### v2.1.x - Repository Pattern & Trust Chain

- Repository Pattern (12/12 endpoints, 100% SQL 제거)
- Trust chain validation (Link Certificate 지원)
- DN normalization, CSCA 캐시 최적화

---

## 2026-01-21 ~ 01-25

### v2.0.x - Service Separation & Security

- PKD Relay Service 분리 (monolith 해체)
- 100% parameterized queries, credential externalization
- CRL reconciliation, auto parent DN creation

---

## Earlier Versions

### v1.8.0 ~ v1.9.0 - Security Hardening

- JWT authentication + RBAC
- File upload security, audit logging

---

## Performance Milestones

### Oracle Upload Performance (30,114 certificates)

| Version | Per-entry | Speed | Total Time | Improvement |
|---------|-----------|-------|------------|-------------|
| v2.25.8 (baseline) | ~155ms | ~6.5/s | ~77min | — |
| v2.25.9 (CSCA cache) | ~32ms | ~31/s | ~16min | 5.7x |
| v2.26.0 (FP cache) | 11.1ms | 90/s | 5:35 | 13.9x |
| v2.26.1 (batch+stmt) | 7.3ms | 137.5/s | 3:39 | **21.2x** |

### Architecture Reduction

| Metric | Before | After | Reduction |
|--------|--------|-------|-----------|
| main.cpp total lines | 9,752 | 1,261 | -87.1% |
| Global variables | 17 | 0 | -100% |
| Inline SQL in handlers | 204 | 0 | -100% |
| MANUAL mode code | ~670 lines | 0 | -100% |

### Security Hardening

| Category | Count |
|----------|-------|
| Command Injection eliminated | 4 services |
| SQL Injection prevented | ORDER BY whitelist + parameterized LIKE |
| OpenSSL null checks added | 24 sites |
| Audit log endpoints | ~25 total |

---

## Technology Stack

| Layer | Technology |
|-------|-----------|
| Backend (C++) | C++20, Drogon Framework, OpenSSL, OpenLDAP |
| Backend (Python) | Python 3.12, FastAPI, scikit-learn, SQLAlchemy |
| Frontend | React 19, TypeScript, Vite, Tailwind CSS 4 |
| Database | PostgreSQL 15 / Oracle XE 21c (runtime switchable) |
| Directory | OpenLDAP (MMR cluster) |
| Gateway | nginx (rate limiting, auth_request, CSP) |
| Container | Docker / Podman 5.6 |
| CI/CD | GitHub Actions (ARM64 cross-build) |
