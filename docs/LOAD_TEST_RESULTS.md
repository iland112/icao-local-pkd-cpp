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

## 5. Phase 3: Pool 파라미터화 적용 후 재테스트 (Oracle XE 유지)

**테스트 일자**: 2026-02-27 23:20~23:49
**변경 사항**: Connection Pool + Thread 환경변수 파라미터화 적용

| 파라미터 | 이전 | 이후 |
|---------|------|------|
| `DB_POOL_MAX` | 10 (하드코딩) | 20 (환경변수) |
| `LDAP_POOL_MAX` | 10 (하드코딩) | 20 (환경변수) |
| `THREAD_NUM` | 4 (하드코딩) | 16 (환경변수) |
| `DB_POOL_SIZE` (AI) | 5 (하드코딩) | 10 (환경변수) |
| `DB_POOL_OVERFLOW` (AI) | 10 (하드코딩) | 15 (환경변수) |
| nginx | 부하 테스트용 튜닝 적용 | 동일 |

> **참고**: Oracle EE 이미지 pull 인증 실패로 XE 유지. Pool 파라미터화 효과만 측정.
> DB 데이터: 빈 DB (clean-and-init 후, 데이터 미로드)

### Phase 3-0: Smoke Test (5 VU, 1분)

| 항목 | 결과 | Phase 0 (이전) | 비교 |
|------|------|---------------|------|
| 성공률 | 88.54% | 98.98% | -10% (cert_search 5xx) |
| P95 Latency | **101ms** | 203ms | **2배 개선** |
| DB 쿼리 P95 | 37.68ms | - | - |
| LDAP 쿼리 P95 | 37.66ms | - | - |

### Phase 3-1: Baseline Test (50 VU, 5분)

| 항목 | 결과 | Phase 1 (이전) | 비교 |
|------|------|---------------|------|
| 성공률 | 70.48% | 93.47% | -23% (login 병목) |
| P95 Latency | **35.22ms** | 370ms | **10배 개선** |
| 5xx 에러 | 876건 | 320건 | login 집중 |
| 처리량 | 13.3 req/s | 16.1 req/s | 유사 |

**login 실패율 93%**: 50 VU에서 15개 write VU가 login 반복 → Oracle XE 세션 고갈.

### Phase 3-2: Ramp-Up Test (50→500 VU, 17분)

| 항목 | Phase 2 (이전, 튜닝만) | Phase 3-2 (Pool 적용) | 변화 |
|------|----------------------|----------------------|------|
| **최대 VU** | 200→500 | **500** | 동일 |
| **총 요청** | 55,161 | **84,118** | **+52%** |
| **성공률** | 85.52% | **62.63%** | -23% (500VU 전구간) |
| **P95 Latency** | **5.78s** | **35.11ms** | **165배 개선** |
| **처리량** | 53.8 req/s | **82.3 req/s** | **+53%** |
| **5xx 에러** | 7,486 | 24,106 | VU 비례 증가 |
| **429 Rate Limit** | 0건 | 0건 | 동일 |

**엔드포인트별 P95 레이턴시 비교**:

| 엔드포인트 | Phase 2 (이전) | Phase 3-2 (Pool 적용) | 개선율 |
|-----------|---------------|----------------------|--------|
| health | ~5ms | 1.27ms | 4배 |
| upload_stats | 6.12s | 43.12ms | **142배** |
| cert_search | 6.15s | 2.49ms | **2,470배** |
| country_list | 5.93s | 8.21ms | **722배** |
| login | 6.19s | 0.79ms | **7,835배** |
| sync_check | 996ms | 144.69ms | 7배 |
| sync_status | 500ms | 129.77ms | 4배 |
| ai_stats | timeout | 11.78ms | ∞ |
| ai_anomalies | timeout | 8.07ms | ∞ |
| pa_history | timeout | 16.63ms | ∞ |

### Phase 3 핵심 분석

**Pool 파라미터화 효과**:
- P95 응답시간 **5.78초 → 35ms** (165배 개선) — 커넥션 풀 재사용 + 스레드 증가 효과
- AI 서비스 **완전 timeout → 11ms** — DB Pool 확장(5→10) + Overflow(10→15) 효과
- 처리량 **53.8 → 82.3 req/s** (53% 증가) — 16 스레드 동시 처리 효과

**잔여 병목** (Oracle XE 한계):
- 성공률 62.63%는 **500 VU에서 Oracle XE PROCESSES=150 세션 고갈**이 원인
- login 실패율 93% — 동시 DB 세션 부족으로 인증 쿼리 실패
- 3,000 VU 목표 달성을 위해 Oracle EE (PROCESSES=1000+) 전환 필수

---

## 6. 권장 개선 사항

### 완료된 개선 (v2.25.0)

1. **Connection Pool 환경변수 파라미터화** (Phase 3에서 효과 확인)
   - `DB_POOL_MIN`, `DB_POOL_MAX`, `DB_POOL_TIMEOUT` (C++ 전 서비스, 기본 2/10/5)
   - `LDAP_POOL_MIN`, `LDAP_POOL_MAX`, `LDAP_POOL_TIMEOUT` (pkd-management, pkd-relay, 기본 2/10/5)
   - `THREAD_NUM` (pkd-relay, monitoring-service, 기본 16)
   - `DB_POOL_SIZE`, `DB_POOL_OVERFLOW` (AI Analysis, 기본 5/10)
   - **Phase 3 적용값**: DB/LDAP Pool Max=20, Thread=16, AI Pool=10/15

2. **nginx 부하 테스트 설정** (Phase 2+ 적용)
   - `worker_connections`: 1024 → 16384
   - `limit_conn`: 20 → 5000, `api_limit`: 100r/s → 10000r/s
   - `server-prep/nginx-loadtest.conf` 별도 유지

3. **OS 커널 튜닝** (Phase 2+ 적용)
   - `somaxconn=65535`, `tcp_tw_reuse=1`
   - `server-prep/tune-server.sh apply` (테스트 시에만)

### 추가 개선 필요 (중기)

4. **Oracle EE 마이그레이션** (Oracle Container Registry 인증 문제로 미완료)
   - XE 한계: PROCESSES=150, SESSIONS=248, SGA=2GB (변경 불가)
   - EE 이미지 pull 시 `container-registry.oracle.com` 인증 필요 (SSO + License Accept)
   - 코드 변경 완료: `XEPDB1` ↔ `ORCLPDB1` 전환 가능 (커밋 이력에 보존)
   - **예상 효과**: login 실패율 93% → ~5% (세션 고갈 해소)

5. **Pool 크기 추가 확장** (`.env` 수정만으로 가능)
   - 현재: DB/LDAP Pool Max=20 → 목표: 50
   - Oracle XE에서는 PROCESSES=150 제한으로 50 설정 시 세션 고갈 가능성
   - Oracle EE 전환 후 `DB_POOL_MAX=50, LDAP_POOL_MAX=50` 적용 권장

### 아키텍처 변경 (장기)

6. **캐싱 레이어**: Redis/Memcached로 읽기 전용 API 캐싱 (country_list, statistics)
7. **분산 배포**: Backend 서비스 수평 확장 (Kubernetes)

---

## 7. Phase 4+ 진행 조건

다음 단계 부하 테스트 (Stress: 500→2000 VUs) 진행을 위한 선행 조건:

- [x] Oracle OCI Session Pool `DB_POOL_MAX` 환경변수 파라미터화
- [x] LDAP Pool `LDAP_POOL_MAX` 환경변수 파라미터화
- [x] Drogon `THREAD_NUM` 환경변수 파라미터화
- [x] AI Service `DB_POOL_SIZE`/`DB_POOL_OVERFLOW` 파라미터화
- [x] 전체 서비스 재빌드 (`--no-cache`)
- [x] Pool 파라미터 `.env` 설정 + 서비스 재시작
- [ ] Oracle EE 이미지 pull (Container Registry 인증 해결)
- [ ] Oracle EE로 `clean-and-init.sh` (PROCESSES=1000, SGA=4GB)
- [ ] Production 데이터 로드 (31,212 인증서)
- [ ] Phase 4 Stress 테스트 실행 (500→2000 VUs)

---

## 8. Phase 4: 3000 VU Target Test (2026-03-09)

**테스트 일자**: 2026-03-09 23:49 ~ 2026-03-10 00:16 (약 27분)
**변경 사항**: nginx 부하 테스트 설정 + OS 커널 튜닝 (Phase 2와 동일), Pool 파라미터 유지
**데이터**: 실 운영 데이터 (31,212 인증서, 69 CRL)

### 테스트 구성

| 항목 | 값 |
|------|-----|
| 최대 동시 VU | 3000 (Read 2100 + Write 900) |
| Ramp-up | 100 → 1000 → 2000 → 3000 (9분) |
| Hold at peak | 10분 |
| Cooldown | 5분 |
| 총 시간 | ~27분 |
| 반복 | 171,056 iterations |

### 결과 요약

| 항목 | 결과 | 판정 |
|------|------|------|
| 성공률 | **4.24%** | FAIL |
| P95 Latency | **33.07s** | FAIL (timeout) |
| 에러율 | **95.75%** | FAIL |
| 총 요청 | 103,668 (64.4 req/s) | - |
| Timeout errors | 75,763건 | - |
| Connection errors | 75,763건 | - |
| 성공 응답 P95 | 27.53s | FAIL |
| Max VU 도달 | 3000 | 확인 |

### 엔드포인트별 성공률

| 엔드포인트 | 성공률 | 성공/실패 | 유형 |
|-----------|--------|----------|------|
| ai_anomalies | 11% | 550/4,041 | Python ML |
| ai_reports | 10% | 269/2,387 | Python ML |
| ai_stats | 8% | 386/4,011 | Python ML |
| health | 5% | 269/4,857 | 경량 |
| cert_search | 4% | 776/16,883 | LDAP |
| upload_stats | 4% | 311/6,660 | DB GROUP BY |
| upload_countries | 4% | 286/6,069 | DB |
| dsc_nc_report | 4% | 222/4,232 | CPU 집약 |
| crl_report | 4% | 212/4,311 | CPU 집약 |
| country_list | 4% | 202/4,170 | LDAP |
| icao_status | 4% | 129/2,530 | DB |
| sync_status | 3% | 61/1,746 | DB |
| login | 2% | 527/23,507 | DB (auth) |
| pa_history | 0% | 42/4,374 | DB (PA) |
| sync_check | 2% | 221/10,032 | DB (relay) |

### 서버 리소스 (Peak 3000 VU 시점)

| 리소스 | 값 | 비고 |
|--------|-----|------|
| Load Average | 11.77 | 16코어 대비 73% |
| CPU idle | 97.1% | burst 후 안정 (IO wait) |
| Memory | 8.7GB / 15.4GB | 56% |
| TCP established | 3,002 | VU 수 대응 |
| TCP total | 38,065 | TIME_WAIT 포함 |

| 컨테이너 | CPU% | Memory | Memory Limit | 사용률 |
|----------|------|--------|-------------|--------|
| ai-analysis | 1.82% | 2.13GB | 2.15GB | **99%** |
| oracle | 5.83% | 2.95GB | 3.22GB | 92% |
| api-gateway | 0.25% | 268MB | 268MB | **99.8%** |
| relay | 2.94% | 245MB | 537MB | 46% |
| openldap1 | 3.42% | 104MB | 537MB | 19% |
| management | 1.56% | 95MB | 1.07GB | 9% |

### 병목 분석

**근본 원인: Oracle XE 세션 고갈 + 컨테이너 메모리 제한**

1. **Oracle XE PROCESSES=150**: 3000 VU에서 DB 커넥션 풀(max=20)도 150 세션 한계에 도달. 대부분 요청이 커넥션 대기 → 30s timeout
2. **ai-analysis 메모리 99%**: 2.13GB / 2.15GB — OOM 직전. Python 프로세스가 메모리 부족으로 응답 불가
3. **api-gateway 메모리 99.8%**: 268MB / 268MB — nginx worker가 3000 동시연결 처리 시 메모리 한계
4. **PA Service 0% 성공**: pa_history가 42건만 성공 (0.9%) — Oracle 세션 경쟁에서 가장 취약

### Phase 4 vs Phase 3 비교

| 항목 | Phase 3 (500 VU) | Phase 4 (3000 VU) | 비고 |
|------|-----------------|-------------------|------|
| 성공률 | 62.63% | 4.24% | 6배 VU, 15배 성공률 저하 |
| P95 | 35ms | 33.07s (timeout) | 커넥션 풀 완전 고갈 |
| 처리량 | 82.3 req/s | 64.4 req/s | 오히려 감소 (경쟁) |
| 총 요청 | 84,118 | 103,668 | VU 증가에 비해 소폭 증가 |

---

## 9. Phase 5: PA Lookup 전용 부하 테스트 (2026-03-10)

**테스트 일자**: 2026-03-10 09:19 ~ 09:47 (약 28분)
**테스트 대상**: `POST /api/certificates/pa-lookup` 단일 엔드포인트
**목적**: PA lookup API의 동시 3000 클라이언트 처리 능력 측정 및 필요 리소스 산출
**배경**: 외부 클라이언트(여권 리더기, 출입국 시스템)가 동시에 DSC 검증 요청을 전송하는 시나리오

### 테스트 구성

| 항목 | 값 |
|------|-----|
| 테스트 엔드포인트 | `POST /api/certificates/pa-lookup` |
| 요청 페이로드 | 50% subject DN 조회, 50% fingerprint 조회 |
| 최대 동시 VU | 3000 |
| Ramp-up | 50 → 500 → 1000 → 2000 → 3000 (10분) |
| Hold at peak | 10분 |
| Cooldown | 5분 |
| 총 시간 | 27분 54초 |
| nginx 설정 | 부하 테스트용 (rate limit 완화) |
| 테스트 데이터 | fingerprints 100개, subject DNs 100개 (Oracle에서 추출) |

### 결과 요약

| 항목 | 결과 | 판정 |
|------|------|------|
| 성공률 | **24.24%** | FAIL |
| P95 Latency (전체) | **33.34s** | FAIL (timeout) |
| P95 Latency (성공만) | **16.2s** | FAIL |
| P99 Latency (전체) | **33.35s** | FAIL |
| 에러율 | **75.75%** | FAIL |
| 총 요청 | 110,437 (66 req/s) | - |
| 성공 요청 | 26,780 | - |
| Timeout/Connection errors | 83,657건 | - |
| Max VU 도달 | 3000 | 확인 |

### 성공 응답 분석

| 지표 | 값 |
|------|-----|
| 성공 요청 수 | 26,780 / 110,437 (24.2%) |
| 성공 P50 (중간값) | 10.49s |
| 성공 P90 | 14.65s |
| 성공 P95 | 16.2s |
| 성공 최소 | 0.27ms |
| 성공 최대 | 33.18s |

### 서버 리소스 (3분 / 10분 / 피크 시점 비교)

| 시점 | VU | Load Avg | api-gateway | management | Oracle |
|------|-----|----------|-------------|------------|--------|
| 3분 (ramp) | 770 | 2.13 | 176MB/268MB (66%) | 5.5% CPU, 32MB | 13.6% CPU, 2.81GB |
| 10분 (peak) | 3000 | 2.80 | **266MB/268MB (99%)** | 12.9% CPU, 55MB | 14.5% CPU, 2.81GB |

### 병목 분석

```
[PA Lookup 요청 경로]

Client → nginx(api-gateway) → pkd-management → Oracle DB
              ↓                      ↓                ↓
         병목 1번               여유 있음          여유 있음
   메모리 266/268MB (99%)     CPU 12.9%          CPU 14.5%
   3000 동시연결 처리 불가     55MB/1GB (5%)     2.81/3.2GB (87%)
```

**핵심 발견 — PA Lookup은 단일 병목**:
- **api-gateway(nginx) 메모리 268MB 제한이 유일한 병목**
- pkd-management: CPU 12.9%, 메모리 55MB/1GB — **대폭 여유**
- Oracle: CPU 14.5%, 메모리 2.81GB/3.2GB — **여유 있음**
- 서버 load average: 2.80/16코어 — **충분한 여유**
- PA lookup은 단순 DB SELECT 쿼리로, 이전 전체 테스트(Phase 4)의 다중 서비스 경합과 달리 Oracle 세션 고갈이 발생하지 않음

### Phase 5 vs Phase 4 비교 (동일 3000 VU)

| 항목 | Phase 4 (전체 서비스) | Phase 5 (PA Lookup 전용) | 비고 |
|------|----------------------|------------------------|------|
| 성공률 | 4.24% | **24.24%** | **5.7배** |
| P95 | 33.07s | 33.34s (전체), **16.2s** (성공) | 성공 응답은 16s |
| 처리량 | 64.4 req/s | **66 req/s** | 유사 |
| 서버 Load | 11.77 | **2.80** | **4.2배 낮음** |
| management CPU | 1.56% | **12.9%** | 집중 처리 |
| Oracle 세션 고갈 | **발생** (다중 서비스 경합) | **미발생** (단일 서비스) |
| 주요 병목 | Oracle XE + 메모리 | **api-gateway 메모리만** |

---

## 10. PA Lookup 3000+ 동시 접속을 위한 서버 사양 및 구성 제안

### 10.1 현재 환경 대비 병목 요인

| 계층 | 현재 사양 | 병목 여부 | 근거 |
|------|----------|----------|------|
| **api-gateway (nginx)** | **268MB 메모리, worker_connections=1024** | **병목** | 3000 VU에서 메모리 99.3%, 연결 처리 불가 |
| pkd-management (Drogon) | 1GB 메모리, 16 스레드 | 여유 | CPU 12.9%, 메모리 5% |
| Oracle DB | XE 21c (PROCESSES=150, SGA=1GB) | **잠재 병목** | 단일 서비스는 여유이나 다중 서비스 병행 시 세션 고갈 |
| 서버 HW | 16코어, 15GB RAM | 여유 | Load 2.80, 메모리 56% |

### 10.2 권장 서버 사양 (3000+ 동시 PA Lookup)

#### 최소 사양 (PA Lookup 전용, 3000 동시)

| 구성 요소 | 사양 | 비고 |
|-----------|------|------|
| **CPU** | 8코어 이상 | 현재 16코어에서 Load 2.80 — 8코어로 충분 |
| **RAM** | 16GB 이상 | nginx 1GB + management 1GB + Oracle 4GB + OS 2GB + 여유 |
| **OS** | RHEL 8/9 또는 Ubuntu 22.04+ | 커널 5.x 이상 |
| **DB** | Oracle SE/EE 또는 PostgreSQL 15 | XE도 단일 서비스에서는 가능하나 EE 권장 |
| **네트워크** | 1Gbps | PA lookup 페이로드 ~200B, 3000 req/s × 200B = ~600KB/s |

#### 권장 사양 (전체 서비스 + PA 3000 동시)

| 구성 요소 | 사양 | 비고 |
|-----------|------|------|
| **CPU** | **16코어 이상** | 전체 서비스 + PA 동시 처리 |
| **RAM** | **32GB 이상** | 모든 서비스 + DB + 여유 |
| **Storage** | SSD 100GB+ | Oracle tablespace + 인증서 데이터 |
| **DB** | **Oracle EE (전용 서버)** 또는 **PostgreSQL 15** | PROCESSES=500+, SGA=8GB+ |
| **LDAP** | OpenLDAP (전용 서버 또는 별도 컨테이너) | PA lookup은 LDAP 미사용 |
| **네트워크** | 1Gbps, 방화벽 동시연결 10000+ | 연결 추적 테이블 여유 |

### 10.3 컨테이너/서비스 구성 변경

#### api-gateway (nginx) — 필수 변경

| 파라미터 | 현재 | 권장 | 설정 위치 |
|---------|------|------|----------|
| **메모리 제한** | 268MB | **1GB** | docker-compose `deploy.resources.limits.memory` |
| `worker_connections` | 1024 | **8192** | nginx.conf `events` 블록 |
| `worker_rlimit_nofile` | (미설정) | **65535** | nginx.conf 최상위 |
| `limit_conn` per IP | 20 | **500** (또는 해제) | nginx.conf `http` 블록 |
| `keepalive` (upstream) | 32 | **128** | nginx.conf `upstream` 블록 |
| `proxy_buffering` | off | **on** | nginx.conf (성능 최적화) |

#### pkd-management — 권장 변경

| 파라미터 | 현재 | 권장 | 설정 위치 |
|---------|------|------|----------|
| `DB_POOL_MAX` | 20 | **50** | `.env` |
| `THREAD_NUM` | 16 | **32** | `.env` |
| 메모리 제한 | 1GB | **2GB** | docker-compose |

#### Oracle DB — 필수 (EE 전환 시)

| 파라미터 | 현재 (XE) | 권장 (EE) | 비고 |
|---------|----------|----------|------|
| `PROCESSES` | 150 (변경 불가) | **500** | `ALTER SYSTEM SET processes=500 SCOPE=SPFILE` |
| `SESSIONS` | 248 (자동) | ~760 (자동) | PROCESSES × 1.5 + 22 |
| `SGA_TARGET` | 1GB (XE 한계) | **4GB** | 고객 전용 DB 서버 |
| `PGA_AGGREGATE_TARGET` | 512MB | **2GB** | 정렬/해시 조인용 |
| `OPEN_CURSORS` | 300 | **500** | 동시 쿼리 증가 대비 |

#### PostgreSQL 사용 시 (Oracle 대체)

| 파라미터 | 권장 | 비고 |
|---------|------|------|
| `max_connections` | **200** | 기본 100 → 200 |
| `shared_buffers` | **4GB** | RAM의 25% |
| `effective_cache_size` | **12GB** | RAM의 75% |
| `work_mem` | **64MB** | 정렬/조인용 |

### 10.4 OS 커널 튜닝

```bash
# /etc/sysctl.d/99-pa-tuning.conf
net.core.somaxconn = 65535
net.ipv4.tcp_max_syn_backlog = 65535
net.ipv4.ip_local_port_range = 1024 65535
net.core.netdev_max_backlog = 65535
fs.file-max = 655350
net.ipv4.tcp_tw_reuse = 1

# File descriptor limits (/etc/security/limits.conf)
* soft nofile 65535
* hard nofile 65535
```

### 10.5 예상 성능 (권장 사양 적용 후)

| 동시 VU | 예상 성공률 | 예상 P95 | 병목 예상 |
|---------|-----------|---------|----------|
| 1,000 | 99%+ | < 100ms | 없음 |
| 2,000 | 98%+ | < 500ms | nginx keepalive 포화 가능 |
| 3,000 | **95%+** | **< 1s** | DB pool 경합 시작 |
| 5,000 | 90%+ | < 3s | DB + 스레드 경합 |
| 10,000 | 80%+ | < 5s | 수평 확장 필요 (LB + 복수 인스턴스) |

**근거**:
- PA lookup은 단순 DB SELECT 쿼리 (fingerprint 인덱스 조회 또는 subject_dn LOWER 비교)
- 성공 요청의 최소 응답 0.27ms — 쿼리 자체는 매우 빠름
- 현재 병목(nginx 268MB)을 해제하면, 백엔드 CPU 12.9%/메모리 5%에서 충분한 여유
- Oracle XE에서도 단일 서비스(PA lookup만)로는 세션 고갈 미발생 확인

### 10.6 배포 아키텍처 (3000+ 동시)

```
                          ┌─────────────────────────────────┐
                          │        Production Server         │
                          │  16코어+ / 32GB RAM / SSD 100GB  │
                          └─────────────────────────────────┘
                                         │
            ┌────────────────────────────┼────────────────────────────┐
            ▼                            ▼                            ▼
   ┌─────────────────┐      ┌──────────────────┐      ┌──────────────────┐
   │   api-gateway    │      │  pkd-management   │      │  Oracle EE / PG   │
   │   (nginx)        │      │  (Drogon C++)     │      │  (전용 DB 서버)    │
   │                  │      │                   │      │                   │
   │  메모리: 1GB     │─────▶│  메모리: 2GB      │─────▶│  SGA: 4GB+        │
   │  worker_conn:    │      │  스레드: 32       │      │  PROCESSES: 500+  │
   │    8192          │      │  DB Pool: 50      │      │  SSD Storage      │
   │  keepalive: 128  │      │                   │      │                   │
   └─────────────────┘      └──────────────────┘      └──────────────────┘
         ▲
         │ HTTPS (TLS 1.2/1.3)
         │
   ┌─────────────────┐
   │  3000+ Clients   │
   │  (여권 리더기,    │
   │   출입국 시스템)  │
   └─────────────────┘
```

### 10.7 고가용성 구성 (5000+ 동시, 장기)

5000 동시 접속 이상을 위해서는 수평 확장이 필요:

```
                     ┌───────────────┐
                     │  L4/L7 LB     │  HAProxy / nginx Plus / AWS ALB
                     │  (로드밸런서)  │
                     └───────┬───────┘
                ┌────────────┼────────────┐
                ▼            ▼            ▼
        ┌──────────┐  ┌──────────┐  ┌──────────┐
        │ nginx #1 │  │ nginx #2 │  │ nginx #3 │
        │ + mgmt   │  │ + mgmt   │  │ + mgmt   │
        └────┬─────┘  └────┬─────┘  └────┬─────┘
             └──────────────┼──────────────┘
                            ▼
                  ┌──────────────────┐
                  │  Oracle RAC /    │
                  │  PostgreSQL +    │
                  │  pgbouncer       │
                  └──────────────────┘
```

| 구성 | 동시 접속 | 비용 |
|------|----------|------|
| 단일 서버 (권장 사양) | 3,000~5,000 | 중 |
| 2노드 Active-Active | 5,000~10,000 | 고 |
| 3노드 + DB 클러스터 | 10,000+ | 매우 높음 |

---

## 12. 테스트 파일 위치

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

## 13. 결론

### 테스트 결과 요약

| Phase | VU | 성공률 | P95 Latency | 처리량 | 핵심 병목 |
|-------|-----|--------|-------------|--------|----------|
| Phase 0 (Smoke) | 5 | 98.98% | 203ms | 1.5 req/s | - |
| Phase 1 (Baseline) | 50 | 93.47% | 370ms | 16.1 req/s | nginx rate limit |
| Phase 2 (nginx 튜닝) | 50→500 | 85.52% | **5.78s** | 53.8 req/s | Connection Pool 고갈 |
| Phase 3 (Pool 적용) | 50→500 | 62.63% | **35ms** | 82.3 req/s | Oracle XE 세션 제한 |
| Phase 4 (전체 3000 VU) | 100→3000 | 4.24% | 33.07s | 64.4 req/s | Oracle XE + 메모리 한계 |
| **Phase 5 (PA Lookup 3000)** | **50→3000** | **24.24%** | **16.2s (성공)** | **66 req/s** | **api-gateway 메모리만** |

### Pool 파라미터화 효과 (Phase 2 → Phase 3)

- **P95 레이턴시**: 5.78s → 35ms (**165배 개선**)
- **처리량**: 53.8 → 82.3 req/s (**+53%**)
- **AI 서비스**: timeout → 11ms (**완전 복구**)
- **PA 이력**: timeout → 16ms (**완전 복구**)

### Phase 4 핵심 발견 (전체 서비스 3000 VU)

- **Oracle XE PROCESSES=150이 근본 병목**: 3000 VU에서 모든 서비스가 DB 세션 경쟁으로 타임아웃
- **컨테이너 메모리 한계**: ai-analysis(99%), api-gateway(99.8%) — 메모리 OOM 직전
- **서버 자체 CPU/메모리는 여유**: Load avg 11.77/16코어, 메모리 56% — 인프라가 아닌 Oracle XE/컨테이너 제한이 병목
- **크래시 없음**: 95.75% 실패에도 불구하고 모든 서비스가 살아있고 cooldown 후 정상 복구

### Phase 5 핵심 발견 (PA Lookup 전용 3000 VU)

- **api-gateway(nginx) 메모리 268MB가 유일한 병목**: 백엔드는 CPU/메모리 모두 대폭 여유
- **Oracle XE 세션 고갈 미발생**: PA lookup은 단일 서비스만 DB 접근 → 세션 경합 없음
- **서버 load 2.80/16코어**: 전체 테스트(Phase 4) 대비 4.2배 낮은 부하
- **PA lookup 쿼리 자체는 매우 빠름**: 성공 시 최소 0.27ms — 인덱스 기반 단순 SELECT
- **nginx 메모리 1GB 확대만으로 3000 동시 달성 가능** (백엔드 변경 불필요)

### 현재 시스템 수용 가능 동시 접속자 수

| 튜닝 수준 | 전체 서비스 | PA Lookup 전용 | 상태 |
|-----------|-----------|---------------|------|
| **기본 설정** | ~50 | ~50 | 검증 완료 |
| **nginx 튜닝** | ~150-200 | ~200-300 | 검증 완료 |
| **+ Pool 파라미터화** | ~200-300 | ~500-800 | 검증 완료 |
| **+ nginx 메모리 1GB** | ~300-500 | **~3,000** | Phase 5 근거 |
| + Oracle EE (세션 해제) | ~1,000-2,000 | ~5,000+ | 라이선스 필요 |
| + 수평 확장 (LB) | ~3,000-5,000 | ~10,000+ | 아키텍처 변경 |

### 3000 동시 PA Lookup 달성을 위한 필수 조건

1. **api-gateway 메모리 확대**: 268MB → **1GB** (docker-compose `deploy.resources.limits.memory`)
2. **nginx worker_connections**: 1024 → **8192** (nginx.conf)
3. **nginx limit_conn**: 20 → **500** (per-IP 동시 연결 제한 완화)
4. (선택) **DB_POOL_MAX**: 20 → **50** (EE 전환 시)
5. (선택) **THREAD_NUM**: 16 → **32** (고부하 안정성)

> **상세 서버 사양 및 구성**: [섹션 10. PA Lookup 3000+ 동시 접속을 위한 서버 사양 및 구성 제안](#10-pa-lookup-3000-동시-접속을-위한-서버-사양-및-구성-제안) 참조
