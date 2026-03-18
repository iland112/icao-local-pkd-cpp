# ICAO Local PKD - Version History

**Current Version**: v2.36.0
**Period**: 2026-01-21 ~ 2026-03-17
**Total Releases**: 76+

---

## Version Summary

| Version | Date | Category | Summary |
|---------|------|----------|---------|
| v2.36.0 | 03-17~18 | UX | 브랜드 리네이밍 FastSPKD + UI/UX 전면 일관성 개선 + GlossaryTerm + CountryFlag + LC 버그 수정 |
| v2.35.0 | 03-16 | Feature | ICAO PKD CSR 관리 (생성/Import/인증서 등록/암호화/감사 로그) |
| v2.34.0 | 03-15 | Refactor | DB 초기화 스크립트 통합 (PostgreSQL 18→8, Oracle 13→9) |
| v2.33.5 | 03-15 | Fix | ICAO Doc 9303 미준수 카테고리 필터 + DSC_NC 부적합 사유 표시 수정 |
| v2.33.3 | 03-15 | Fix | RSA-PSS 해시 알고리즘 추출 + ICAO 미준수 상세 다이얼로그 수정 |
| v2.33.2 | 03-15 | Fix | 전체 코드 멱등성 전수 수정 (Idempotency Hardening) |
| v2.33.0 | 03-14 | Feature | EAC Service Phase 4 완료 (BSI TR-03110 OID + Frontend + Oracle) |
| v2.32.0 | 03-12 | Refactor | SOD 파서 ICAO Doc 9303 준수 리팩토링 + PA 검증 Step 순서 수정 |
| v2.31.3 | 03-12 | Fix | Reconciliation 후 동기화 상태 자동 갱신 |
| v2.31.2 | 03-11 | Feature | 개인정보 암호화 (AES-256-GCM, 개인정보보호법 제29조) |
| v2.31.1 | 03-11 | Feature | 전체 프론트엔드 반응형 디자인 개선 (13개 페이지) |
| v2.31.0 | 03-10 | Feature | DSC 등록 승인 워크플로우 (PA 자동 등록 → 관리자 승인) |
| v2.30.0 | 03-09 | Feature | 로그인 페이지 모던 리디자인 + 사이드바 4섹션 재구성 + 클라이언트 정렬 |
| v2.29.8 | 03-09 | Feature | 로그인 페이지 리브랜딩 + 동적 통계 + 업로드 중복 파일 UX 개선 |
| v2.29.7 | 03-09 | Fix | SSE Heartbeat + Recharts null guard + HTML 표준 접근성 개선 |
| v2.29.6 | 03-07 | Feature | 시스템 모니터링 고유 접속자 + 전체 대시보드 그리드 높이 정렬 |
| v2.29.5 | 03-07 | Feature | 인증서 업로드 결과 다이얼로그 + ICAO 버전 확인 다이얼로그 |
| v2.29.4 | 03-06 | Fix | OpenSSL 메모리 누수 수정 (CRITICAL 1건 + HIGH 4건) |
| v2.29.3 | 03-06 | Fix | 5차 코드 보안 + 안정성 강화 (CRITICAL 6건 + HIGH 8건 + MEDIUM 7건) |
| v2.29.2 | 03-06 | Fix | 4차 코드 안정성 강화 + SSE HTTP/2 완전 수정 + 프론트엔드 UI 수정 |
| v2.29.1 | 03-05 | Fix | SSE HTTP/2 프로토콜 에러 수정 + 업로드 통계 totalCertificates 누락 수정 |
| v2.29.0 | 03-05 | Feature | 실시간 알림 시스템 (SSE) + DSC 재검증 + source_type 수정 |
| v2.28.2 | 03-05 | Fix | 코드 안정성 강화 3차 (CRITICAL 4건 + HIGH 4건) |
| v2.28.1 | 03-04 | Fix | 메모리 안전 + 예외 처리 + 보안 강화 (CMS 누수, LDAP 즉시 해제, OEM 포트 제거) |
| v2.28.0 | 03-04 | Quality | 전체 코드 품질 개선 (139건 이슈 수정) + 테스트 인프라 구축 (438 test cases) |
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

### v2.30.0 (2026-03-09) - 로그인 페이지 모던 리디자인 + 사이드바 4섹션 재구성 + 클라이언트 정렬

- **로그인 폼 모던 리디자인**: 카드 래퍼 제거(flat layout), `ring-1 ring-gray-200` 입력 필드, `group-focus-within:text-[#02385e]` 아이콘 포커스 색상, 단색 `bg-[#02385e]` 버튼 + `active:scale-[0.98]` 프레스 효과, `radial-gradient` 도트 패턴 배경
- **로그인 텍스트 변경**: "시스템 로그인" → "FastSPKD 로그인", 하단 부제 제거, 버전 번호 제거, 푸터 `© 2026 SmartCore Inc.` 만 표시
- **Hero 배경 가시성 향상**: `hero-bg.svg` 전 레이어 opacity ~2.5배 증가 (0.03~0.06 → 0.08~0.25)
- **사이드바 섹션 collapsible 전환**: 인증서 관리/위·변조 검사/보고서 & 분석/시스템 관리 4개 섹션을 접기/펼치기 가능한 버튼으로 변환
- **사이드바 섹션 아이콘**: FolderKey, Fingerprint, ClipboardList, Settings — Home 메뉴와 동일한 스타일 통일
- **사이드바 서브메뉴 인덴트**: `ml-3 pl-3 border-l border-gray-200` 트리형 시각 계층 구조
- **클라이언트 사이드 테이블 정렬**: SortableHeader 컴포넌트 기반 12개 페이지 컬럼 정렬 (문자열/숫자/날짜/상태)
- **페이지네이션 z-index 수정**: 8개 다이얼로그 `z-50` → `z-[70]`
- ~21 files changed (0 new, ~21 modified)

---

### v2.29.8 (2026-03-09) - 로그인 페이지 리브랜딩 + 업로드 중복 파일 UX 개선

- **로그인 페이지 제목 변경**: "ePassport 인증서 관리 시스템" → "전자여권 위·변조 검사 시스템"
- **통계 카드 동적 데이터**: 로그인 페이지 "현재 등록 국가" / "현재 관리 인증서" 카드가 API에서 실시간 데이터 조회
- **업로드 버튼 disabled 확장**: 처리 중 외에 완료(FINALIZED) 시에도 비활성화
- **중복 파일 경고 다이얼로그**: 동일 파일(SHA-256 해시 일치) 재업로드 시 빨간 경고 다이얼로그 표시
- 3 files changed

---

### v2.29.7 (2026-03-09) - SSE Heartbeat + Recharts null guard + HTML 표준 접근성 개선

- **SSE Heartbeat**: NotificationManager에 30초 간격 heartbeat 스레드 추가 — `ERR_HTTP2_PROTOCOL_ERROR` 방지
- **Recharts Tooltip null guard**: 6개소 `payload[0].payload` 접근 시 가드 추가
- **HTML 표준 접근성**: 전체 프론트엔드 폼 요소에 `id`/`name`/`htmlFor` 속성 추가 (WCAG 2.1)
- **Frontend SSE 안정화**: NotificationListener 재연결 최대 20회 제한, 딜레이 조정
- ~15 files changed

---

### v2.29.6 (2026-03-07) - 시스템 모니터링 고유 접속자 + 대시보드 그리드 높이 정렬

- **모니터링 고유 접속자 수**: nginx access log 기반 최근 5분 고유 IP 카운트
- **전체 대시보드 그리드 높이 정렬** (8개 페이지): h-full flex flex-col 적용
- **auditApi JWT 인터셉터 누락 수정**
- ~15 files changed

---

### v2.29.5 (2026-03-07) - 인증서 업로드 결과 다이얼로그 + ICAO 버전 확인 다이얼로그

- **인증서 업로드 결과 다이얼로그**: 업로드 완료 후 인라인 결과 Dialog 표시
- **중복 인증서 UX 개선**: 전체 중복 시 주황 배너 "이미 등록된 인증서입니다"
- **ICAO 버전 확인 결과 다이얼로그**: "업데이트 확인" 결과를 Dialog로 표시
- **DSC Trust Chain 보고서 간소화**: QuickLookupPanel 제거
- **AI 분석 폴링 안정화**: 연속 10회 실패 시 에러 토스트 + 폴링 중단
- 6 files changed

---

### v2.29.4 (2026-03-06) - OpenSSL 메모리 누수 수정 (CRITICAL 1건 + HIGH 4건)

- **CRITICAL**: CMS_ContentInfo 예외 경로 누수 — cms 선언을 try 블록 밖으로 이동
- **HIGH**: X509 벡터 예외 경로 누수 (인증서 업로드 + 미리보기), X509_CRL RAII 전환, PA allCscas 예외 경로 누수
- 4 files changed

---

### v2.29.3 (2026-03-06) - 5차 코드 보안 + 안정성 강화 (CRITICAL 6건 + HIGH 8건 + MEDIUM 7건)

- **CRITICAL**: SQL 인젝션 방어 (PGconn 파라미터화), detached thread `this` 캡처 제거, EVP_Digest 반환값 검증, Oracle OCI 예외 경로 메모리 누수, dl_parser OID 버퍼 오버플로, HSTS 만료시간
- **HIGH**: PA JSON 필드 검증, data_group stoi 예외 안전, Frontend EditDialog/DeleteDialog 에러 표시, JWT 디코드 안전, Python GC/캐시 동시성, Swagger CORS
- **MEDIUM**: ZIP export 경로 탐색 방지, tolower signed char UB, SSE 알림 필드 검증, THREAD_NUM 범위 제한, Luckfox CORS, AI Dockerfile CMD
- ~15 files changed

---

### v2.29.2 (2026-03-06) - 4차 코드 안정성 강화 + SSE HTTP/2 완전 수정 + 프론트엔드 UI 수정

- **SSE HTTP/2 완전 수정**: `proxy_hide_header Transfer-Encoding;` 추가 (RFC 7540)
- **nginx HTTP/2 문법 업데이트**: `listen 443 ssl http2;` → `listen 443 ssl;` + `http2 on;`
- **다이얼로그 z-index 수정**: 8개 파일 `z-50` → `z-[70]`
- **Recharts payload null guard**: 3개 파일 커스텀 YAxis tick
- **ASN1_TIME 버퍼 안전성**, **DG2 JPEG2000 이미지 크기 검증**, **JWT 만료시간 검증**
- **IP 파서 안전 강화**, **ReDoS 방어**, **페이지네이션 상한**, **catch-all 로깅**
- 33 files changed, +145 / -62 lines

---

### v2.29.1 (2026-03-05) - SSE HTTP/2 프로토콜 에러 수정 + 업로드 통계 누락 수정

**nginx SSE + HTTP/2 호환성 수정**
- **증상**: HTTPS(HTTP/2) 환경에서 SSE 스트림 연결 시 `ERR_HTTP2_PROTOCOL_ERROR 200 (OK)` 콘솔 에러
- **원인**: nginx HTTP/2에서 SSE의 chunked transfer encoding이 호환되지 않음
- **수정**: SSE location 블록 4곳(`/api/progress`, `/api/sync/notifications`)에 `chunked_transfer_encoding off;` 추가
- 대상 파일: `api-gateway-ssl.conf`, `api-gateway.conf`

**LDIF 업로드 totalCertificates 통계 수정**
- **증상**: 이미 업로드된 LDIF 파일 재업로드 시 결과 카드에 "파일 전체: 0, 중복: 30,047, 신규 처리: 0"으로 표시 — 상세 통계 섹션 전체 미표시
- **원인**: `ldif_processor.cpp`의 fingerprint 캐시 히트(중복) 조기 반환 경로에서 `enhancedStats.totalCertificates++` 누락 (v2.26.0 성능 최적화에서 유입)
- **수정**: 중복 인증서 조기 반환 전 `totalCertificates++` 추가 → 모든 인증서가 중복이어도 정확한 총 수 표시
- 3 files changed

### v2.29.0 (2026-03-05) - 실시간 알림 시스템 + DSC 재검증 + source_type 수정

백엔드 이벤트를 프론트엔드에 실시간 전달하는 SSE 알림 시스템, DSC 인증서 만료 상태 재검증 기능, 그리고 인증서 출처 타입 저장 버그 수정

#### 실시간 알림 시스템
- **NotificationManager** (singleton, thread-safe): SSE 클라이언트 관리 + broadcast, copy-release-execute 패턴 (ProgressManager 패턴 재사용)
- **NotificationHandler**: `GET /api/sync/notifications/stream` SSE 엔드포인트 (Drogon AsyncStreamResponse)
- **SyncScheduler 알림 통합**: Daily Sync, Revalidation, Reconciliation 완료 시 `NotificationManager::broadcast()` 호출
- **알림 타입 4종**: `SYNC_CHECK_COMPLETE`, `REVALIDATION_COMPLETE`, `RECONCILE_COMPLETE`, `DAILY_SYNC_COMPLETE`
- **Header Bell UI**: Lucide Bell 아이콘 + unread 뱃지(빨간 원) + 드롭다운 알림 패널 (최대 50건, 읽음 처리, 전체 삭제)
- **NotificationListener**: 전역 SSE 리스너 (Layout에 mount, 자동 재연결 최대 3회 → 30초 간격)
- **notificationStore** (Zustand): notifications[], unreadCount, add/markRead/clear + toast.info() 연동
- **nginx SSE 설정**: `/api/sync/notifications` location 블록 (proxy_buffering off, proxy_cache off, read_timeout 3600s)

#### DSC 만료 상태 재검증
- **Relay ValidationService**: `revalidateExpiredDsc()` — 만료된 DSC 인증서 trust chain 재평가
- **CSCA/CRL Provider 어댑터**: `RelayCscaProvider`, `RelayCrlProvider` — Relay 서비스의 DB 기반 CSCA/CRL 조회 어댑터
- **Trust chain 재구축**: `icao::validation` 라이브러리 활용, CSCA 조회 → trust chain 검증 → CRL 확인 → validation_result DB 업데이트
- **`POST /api/sync/revalidate`** 엔드포인트: 만료 DSC 일괄 재검증 트리거
- **SyncDashboard UI**: "만료 상태 갱신" 버튼 + 결과 다이얼로그 (처리 건수, 상태 변경 건수, 처리 시간)
- **DB 스키마**: `revalidation_trust_chain` 테이블 신규 (PostgreSQL + Oracle)

#### 버그 수정
- **source_type 컬럼 누락**: `saveCertificateWithDuplicateCheck()` INSERT에 `source_type` 컬럼 미포함 → 모든 인증서가 DB DEFAULT `FILE_UPLOAD`로 저장. `sourceType` 파라미터 추가하여 LDIF→`LDIF_PARSED`, ML→`ML_PARSED`, 개별업로드→`FILE_UPLOAD` 정확 전달
- **Reconciliation 버그**: `reconciliation_log` INSERT 컬럼명 오류 (`certificate_type` → `entity_type`) + UUID 시퀀스 문제 수정

#### 기타
- **Frontend 수정**: CrlReport/DscNcReport `fetchReport` 스코프 오류 수정, "인증서 재검증" → "만료 상태 갱신" 용어 통일
- **문서**: `CERTIFICATE_SOURCE_MANAGEMENT.md` 신규, `DSC_NC_HANDLING.md`/`LDAP_QUERY_GUIDE.md`/`PA_API_GUIDE.md` 업데이트, `PAGE_FUNCTIONALITY_GUIDE.md` 신규
- 48 files changed (20 new, 28 modified), +3,240 / -114 lines

### v2.28.2 (2026-03-05) - 코드 안정성 강화 (3차 코드 리뷰: CRITICAL 4건 + HIGH 4건)

3차 코드 리뷰에서 발견된 안전성 이슈 수정

#### CRITICAL
- **BN_bn2hex null 체크**: `certificate_utils.cpp` — OOM 시 nullptr dereference 방지
- **LDAP 풀 race condition**: `ldap_connection_pool.cpp` `acquire()` — mutex 재획득 후 `totalConnections_` 재검증
- **nginx POST 재시도 차단**: `proxy_params` 기본값 `proxy_next_upstream off` — 데이터 중복 실행 방지
- **detached thread this 캡처 제거**: `upload_handler.cpp` — 핸들러 소멸 후 dangling pointer 방지

#### HIGH
- **std::stoi() 예외 안전 (14개소)**: 환경변수 파싱 전체 try-catch + 기본값 폴백
- **Oracle asInt() → scalarToInt() 전환**: `processing_strategy.cpp` 8개소
- **Frontend AbortController 적용 (3개 페이지)**: DscNcReport, CrlReport, MonitoringDashboard
- **AI 데이터 캐시 TTL + 결과 저장 per-row 에러 격리**
- 25 files changed (0 new, 25 modified), +400 / -205 lines

### v2.28.1 (2026-03-04) - 메모리 안전 + 예외 처리 + 보안 강화

2차 코드 리뷰에서 발견된 메모리 안전, 예외 처리, 보안 이슈 수정

#### CRITICAL
- **CMS_ContentInfo 메모리 누수**: Master List CMS 성공 경로에서 `CMS_ContentInfo_free(cms)` 미호출 — `if (!cms)` 블록 밖으로 이동하여 모든 경로에서 해제
- **BIO_new_mem_buf size_t→int 오버플로**: `content.size()` > `INT_MAX` 사전 검증 추가

#### HIGH
- **stoi() 예외 안전 2건**: `MAX_CONCURRENT_UPLOADS` static 초기화 + `parseHexBinary()` hex 파싱에 try-catch 추가
- **LDAP message 즉시 해제**: reconciliation_engine에서 `ldap_search_ext_s()` 직후 `ldap_msgfree()` 호출 — 이후 예외 발생 시 메모리 누수 방지 (인증서 + CRL 2개소)
- **Oracle OEM 포트 제거**: `15500:5500` 매핑 제거 — 관리 인터페이스 외부 노출 방지 (Docker + Podman)
- **SSE stale closure**: `isProcessingRef` (useRef) 도입 — 재연결 시 최신 처리 상태 참조

#### MEDIUM
- **Audit catch 로깅**: 9개 빈 `catch (...) {}` 블록에 `spdlog::warn` 추가 (api_client_handler 4건, auth_handler 4건, pa_handler 1건)

**수정 파일**: upload_handler.cpp, reconciliation_engine.cpp, api_client_handler.cpp, auth_handler.cpp, pa_handler.cpp, docker-compose.yaml, docker-compose.podman.yaml, FileUpload.tsx
**검증**: 4개 서비스 빌드 성공, AI 201 테스트 통과, 11개 컨테이너 healthy

### v2.28.0 (2026-03-04) - 전체 코드 품질 개선 + 테스트 인프라 구축

**코드 분석 139건 이슈 체계적 수정** (CRITICAL 13, HIGH 37, MEDIUM 59, LOW 30) + 테스트 인프라 구축

#### Security & Stability (CRITICAL)
- C++ ProgressManager mutex 교착 위험 해소 — 콜백을 lock 밖으로 이동
- C++ ReconciliationEngine LDAP raw 연결 → `ldapPool_->acquire()` 전환
- C++ OracleQueryExecutor 원시 메모리 → RAII + 데드 코드 587줄 제거
- Frontend React.lazy() 코드 스플리팅 (22개 페이지 lazy load)
- Python async sync I/O 블로킹 → `asyncio.to_thread()` 래핑 (~15개 엔드포인트)
- Python `_compliance_cache` 스레드 안전, 분석 작업 race condition 수정
- 인프라 하드코딩 비밀번호 제거, Oracle 과잉 권한 제거

#### Thread Safety & Architecture (HIGH)
- C++ `localtime`/`gmtime` → `localtime_r`/`gmtime_r` (13+ 파일)
- C++ `s_activeProcessingCount` RAII guard, `g_services` null 체크
- Frontend PrivateRoute JWT 만료 검사, `alert()` → ConfirmDialog, AbortController 적용
- Python SQL 파라미터화, DB URL.create(), JSONB generic 타입
- Infra HTTP nginx PA auth_request, POST 재시도 방지, nginx 이미지 태그 고정

#### Code Quality (MEDIUM)
- C++ ApiRateLimiter cleanup, IPv6 정규화, ProgressManager deque 전환, EmailSender 제거
- Frontend 폴링 stale closure 수정, MANUAL 모드 제거, console.error DEV 가드, unique key 14건
- Python 데드 코드 10+ 함수 제거, Pydantic v2 ConfigDict 전환, DB 필터 최적화
- Infra TIMESTAMPTZ 전환, Docker/Podman 스크립트 통합, Legacy Dockerfile 삭제

#### Polish (LOW)
- `__APP_VERSION__` 컴파일 타임 버전, 접근성 aria-label, SHA-256 라벨 수정
- health 엔드포인트 DB 체크 확장, `isExpired` TODO 구현

#### 테스트 인프라 (신규)
- Frontend: Vitest + RTL — 11 파일, 114 passed + 1 skipped
- AI Analysis: pytest — 8 파일, 201 passed
- C++ certificate-parser: GTest — 5 파일, 122 test cases
- **총 438 테스트 케이스 작성**

125 files changed (14 new, 110 modified, 1 deleted), +4,063 / -2,226 lines

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
