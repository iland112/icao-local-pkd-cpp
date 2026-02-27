# ICAO Local PKD — 부하 테스트 결과 보고서

**테스트 일자**: 2026-02-27
**테스트 환경**: Production 서버 (10.0.0.220, RHEL 9, Podman 5.6.0)
**테스트 도구**: k6 (Grafana) v1.6.1
**테스트 대상**: https://pkd.smartcoreinc.com (http://localhost)

---

## 1. 시스템 환경

| 구성 요소 | 사양 |
|-----------|------|
| OS | RHEL 9 (Linux 5.14) |
| CPU | 8 vCPU |
| RAM | 15 GB |
| DB | Oracle XE 21c (SGA 2GB 제한) |
| LDAP | OpenLDAP (단일 노드) |
| 데이터 | 31,212 인증서, 69 CRL, 139 국가 |
| 컨테이너 | 11개 (Podman) |

### 주요 제약 사항 (테스트 시점)

| 구분 | 현재 값 |
|------|---------|
| nginx `worker_connections` | 1024 |
| nginx `limit_conn` per IP | 20 |
| nginx `limit_req` API | 100r/s per IP |
| nginx `limit_req` login | 5r/m per IP |
| Oracle OCI Session Pool | sessMax=10 (하드코딩) |
| LDAP Connection Pool | maxSize=10 |
| Drogon Thread | 기본 4개 |
| AI Service DB Pool | pool_size=5 |

---

## 2. 테스트 결과 요약

### Phase 0: Smoke Test (5 VUs, 1분)

**목적**: 모든 엔드포인트 정상 응답 확인

| 항목 | 결과 | 판정 |
|------|------|------|
| 성공률 | 98.98% | PASS |
| P95 Latency | 203ms | PASS |
| 에러율 | 1.05% | PASS |
| 총 요청 | 95 (1.5 req/s) | - |

**상세**:
- 11개 엔드포인트 중 9개 100% 성공
- `login`: 80% (nginx `login_limit 5r/m` rate limiting으로 1건 503)
- 모든 데이터 엔드포인트 정상 응답 확인

---

### Phase 1: Baseline Test (50 VUs, 5분) — 튜닝 없음

**목적**: 현재 설정에서의 기준 성능 측정

| 항목 | 결과 | 판정 |
|------|------|------|
| 성공률 | 93.47% | WARN |
| P95 Latency | 370ms | PASS |
| 에러율 | 6.53% | FAIL (>5%) |
| 총 요청 | 4,900 (16.1 req/s) | - |
| Server errors (5xx) | 320건 | - |

**503 오류 분석** (320건):

| 원인 | 건수 | 비율 |
|------|------|------|
| `/api/auth/login` (rate limit) | 294 | 91.9% |
| 기타 엔드포인트 (conn limit) | 26 | 8.1% |

**핵심 발견**:
- Backend 순수 성능은 우수 (P95=370ms)
- **nginx rate limit (5r/m)이 주요 병목** — login 요청 322건 중 294건 차단
- `limit_conn 20` (IP당 동시 20연결)도 가끔 차단 발생
- 실제 Backend 처리 능력은 50 VU에서 충분

**엔드포인트별 레이턴시** (P95):

| 엔드포인트 | P95 | 유형 |
|-----------|-----|------|
| health | ~5ms | 경량 |
| sync_status | ~200ms | DB 조회 |
| cert_search | ~235ms | LDAP 검색 |
| country_list | ~235ms | LDAP 검색 |
| pa_lookup | ~435ms | DB 조회 |
| upload_stats | ~248ms | DB GROUP BY |
| dsc_nc_report | ~381ms | CPU 집약 |
| crl_report | ~381ms | CPU 집약 |
| ai_stats | ~215ms | Python ML |

---

### Phase 2: Ramp-Up Test (50→500 VUs, 17분) — nginx 튜닝 적용

**서버 튜닝 적용 항목**:
- `worker_connections`: 1024 → 16384
- `limit_conn`: 20 → 5000
- `api_limit`: 100r/s → 10000r/s
- `login_limit`: 5r/m → 500r/m
- OS kernel: `somaxconn=65535`, `tcp_tw_reuse=1`

| 항목 | 결과 | 판정 |
|------|------|------|
| 성공률 | 85.52% | FAIL |
| P95 Latency | **5.78s** | FAIL (>3s) |
| 에러율 | **14.47%** | FAIL (>2%) |
| 총 요청 | 55,161 (53.8 req/s) | - |
| Rate limit 429 | **0건** | nginx 튜닝 효과 확인 |
| Server errors (5xx) | 7,486건 | - |
| Connection errors | 463건 | - |

**nginx 응답 코드 분석**:

| 응답 코드 | 건수 | 비율 | 의미 |
|-----------|------|------|------|
| 200 | 47,424 | 86% | 정상 |
| **502** | **5,133** | **9.3%** | **Backend upstream timeout** |
| **500** | **2,435** | **4.4%** | **Backend 내부 오류** |
| 499 | 505 | 0.9% | Client timeout |

**완전 실패한 엔드포인트 (0% 성공)**:

| 엔드포인트 | 실패 원인 | 실패 건수 |
|-----------|----------|----------|
| `ai_anomalies` | Python AI 서비스 timeout (5s) | 2,535 |
| `ai_stats` | Python AI 서비스 timeout (5s) | 2,516 |
| `pa_history` | PA 서비스 timeout (5s) | 2,435 |

**성능 저하 엔드포인트** (97-99% 성공, 고 레이턴시):

| 엔드포인트 | 성공률 | P95 | P90 |
|-----------|--------|-----|-----|
| cert_search | 98% | 6.15s | 4.93s |
| country_list | 98% | 5.93s | 4.88s |
| upload_stats | 98% | 6.12s | 5.09s |
| dsc_nc_report | 98% | 6.54s | 5.25s |
| login | 98% | 6.19s | 4.92s |
| pa_lookup | 99% | 6.31s | 4.95s |

**안정적 엔드포인트** (100% 성공):

| 엔드포인트 | P95 |
|-----------|-----|
| sync_check | 996ms |
| sync_status | 500ms |

---

## 3. 병목 지점 분석

### 계층별 병목

```
[1단계] nginx Rate Limit     → Phase 1에서 주요 병목 (login 5r/m)
                                → nginx 튜닝으로 해결됨 (Phase 2에서 429 = 0건)

[2단계] Backend Connection Pool → Phase 2에서 주요 병목
  ├── Oracle OCI Session Pool (max=10)  → 502 Bad Gateway
  ├── LDAP Connection Pool (max=10)     → 502 Bad Gateway
  ├── Drogon Thread (default 4)         → 요청 큐 대기
  └── AI Service DB Pool (5)            → 500 Internal Error / Timeout

[3단계] Oracle XE 21c SGA     → 미도달 (Phase 3+ 필요)
  └── 2GB SGA 하드 제한        → 세션/쿼리 처리량 상한
```

### 성능 저하 시작점

| VU 수 | 상태 |
|-------|------|
| 5 VU | 안정 (P95=203ms, 99% 성공) |
| 50 VU | 안정 (P95=370ms, 93.5% — login rate limit만 문제) |
| ~150 VU | 성능 저하 시작 (추정) |
| ~250 VU | 뚜렷한 레이턴시 증가 (P95 > 2s) |
| 500 VU | **과부하** (P95=5.78s, 85.5% 성공) |

### 서비스별 취약점

| 서비스 | 병목 | 영향 |
|--------|------|------|
| AI Analysis (Python) | DB pool 5개, 동기 처리 | 150 VU+ 에서 완전 timeout |
| PA Service (C++) | LDAP pool 10, Drogon 4 thread | 250 VU+ 에서 timeout |
| PKD Management (C++) | Oracle pool 10, LDAP pool 10 | 300 VU+ 에서 레이턴시 급증 |
| PKD Relay (C++) | 경량 (sync check) | 500 VU에서도 안정 |

---

## 4. 현실적 동시 접속자 수 평가

| 튜닝 수준 | 예상 최대 VU | 비고 |
|-----------|-------------|------|
| **현재 (튜닝 없음)** | **~50** | nginx rate limit 병목 |
| **nginx 튜닝만** | **~150-200** | Backend pool 포화 시작 |
| + DB/LDAP pool 확장 (50) | ~500-1,000 | 코드 변경 필요 |
| + Drogon 스레드 증가 (16) | ~1,000-1,500 | docker-compose 환경변수 |
| + AI 서비스 최적화 | ~1,500-2,000 | Python async 개선 |
| + Oracle SE/EE (SGA 해제) | ~2,000-3,000 | 라이선스 필요 |
| + 캐싱 레이어 (Redis) | ~3,000-5,000 | 아키텍처 변경 |

---

## 5. 권장 개선 사항

### 완료된 개선 (v2.25.0)

1. **Oracle XE → EE 21c 마이그레이션** (하드 제한 해제)
   - PROCESSES: 150 → 1000, SESSIONS: 248 → 1500, SGA: 2GB → 4GB
   - PDB: XEPDB1 → ORCLPDB1 (EE 기본값)
   - `docker/db-oracle/init/00-ee-tuning.sql` 신규 생성

2. **DB Connection Pool 환경변수 파라미터화**
   - `DB_POOL_MIN`, `DB_POOL_MAX`, `DB_POOL_TIMEOUT` (C++ 전 서비스)
   - 기본값: min=2, max=10 → 테스트 시 `.env`에서 50으로 설정 가능

3. **LDAP Connection Pool 환경변수 파라미터화**
   - `LDAP_POOL_MIN`, `LDAP_POOL_MAX`, `LDAP_POOL_TIMEOUT` (pkd-management, pkd-relay)
   - 기본값: min=2, max=10 → 테스트 시 50으로 설정 가능

4. **Drogon Thread 수 환경변수 파라미터화**
   - `THREAD_NUM` (전 서비스, 기본값 16)
   - pkd-relay, monitoring-service 하드코딩(4) 제거

5. **AI Analysis Service Pool 파라미터화**
   - `DB_POOL_SIZE`, `DB_POOL_OVERFLOW` (기본 5/10 → 테스트 시 20/30)

### 즉시 적용 가능 (코드 변경 없음)

6. **nginx 부하 테스트 설정**: `server-prep/nginx-loadtest.conf` 별도 유지
7. **OS 커널 튜닝**: `server-prep/tune-server.sh apply` (테스트 시에만)

### 아키텍처 변경 (장기)

8. **캐싱 레이어**: Redis/Memcached로 읽기 전용 API 캐싱 (country_list, statistics)
9. **분산 배포**: Backend 서비스 수평 확장 (Kubernetes)

---

## 6. Phase 3+ 진행 조건

Phase 3 (Stress: 500→2000 VUs) 진행을 위해 다음 작업이 선행되어야 합니다:

- [x] Oracle XE → EE 21c 마이그레이션 (하드 제한 해제)
- [x] Oracle OCI Session Pool `DB_POOL_MAX` 환경변수 파라미터화
- [x] LDAP Pool `LDAP_POOL_MAX` 환경변수 파라미터화
- [x] Drogon `THREAD_NUM` 환경변수 파라미터화
- [x] AI Service `DB_POOL_SIZE`/`DB_POOL_OVERFLOW` 파라미터화
- [ ] 전체 서비스 재빌드 (`--no-cache`)
- [ ] Oracle EE 이미지 pull + `clean-and-init.sh`
- [ ] `.env` 부하 테스트용 값 설정 (`DB_POOL_MAX=50, LDAP_POOL_MAX=50`)
- [ ] 컨테이너 정상 확인 후 Phase 3 실행

---

## 7. 테스트 파일 위치

```
load-tests/
├── config/           # 설정 파일 (base.js, thresholds.js, prod.js)
├── data/             # 테스트 데이터 (countries, fingerprints, subject_dns)
├── lib/              # 공통 라이브러리 (helpers, auth, metrics, random, checks)
├── scenarios/        # 17개 시나리오 (14 read + 3 write)
├── tests/            # 7개 테스트 + 통합 테스트
├── server-prep/      # 서버 튜닝/복구 스크립트
├── monitoring/       # 메트릭 수집/분석 스크립트
└── reports/          # 테스트 결과 저장
```

---

## 8. 결론

현재 시스템은 **nginx 튜닝 없이 ~50 VU**, **nginx 튜닝 후 ~150-200 VU**를 안정적으로 처리할 수 있습니다. 5,000 VU 목표 달성을 위해서는 Backend 연결 풀 확장, 서비스 스레드 증가, AI 서비스 최적화, 그리고 장기적으로 Oracle 라이선스 업그레이드와 캐싱 레이어 도입이 필요합니다.

가장 비용 효율적인 개선 순서:
1. **Connection Pool 확장** (코드 변경) → ~500-1,000 VU 예상
2. **Drogon 스레드 증가** (설정 변경) → ~1,000-1,500 VU 예상
3. **캐싱 레이어 추가** (아키텍처 변경) → ~3,000-5,000 VU 예상
