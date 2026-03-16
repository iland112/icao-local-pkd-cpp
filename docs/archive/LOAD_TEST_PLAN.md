# ICAO Local PKD — 부하 테스트 작업 계획서

**문서 버전**: v1.0
**작성일**: 2026-02-27
**대상 시스템**: ICAO Local PKD v2.24.1
**대상 서버**: Production (10.0.0.220, RHEL 9, Podman 5.6.0)

---

## 1. 개요

### 1.1 목적

ICAO Local PKD 시스템의 동시 접속 처리 능력을 검증하고, 최대 수용 가능 동시 접속자 수와 병목 지점을 파악한다.

### 1.2 목표

| 항목 | 목표 |
|------|------|
| **동시 접속자 수** | 5,000 클라이언트 |
| **워크로드 비율** | 70% 읽기 / 30% 쓰기 |
| **P95 응답시간** | 경량 API < 1s, 중량 API < 5s |
| **오류율** | < 5% (정상 부하), < 15% (피크 부하) |
| **가용성** | 시스템 크래시 0건 |

### 1.3 테스트 도구

| 도구 | 버전 | 용도 |
|------|------|------|
| **k6 (Grafana)** | latest | 부하 생성 |
| **podman stats** | 5.6.0 | 컨테이너 리소스 모니터링 |
| **ss / mpstat / free** | OS 내장 | 서버 리소스 모니터링 |
| **nginx access.log** | - | 응답 코드/시간 분석 |

---

## 2. 시스템 현황

### 2.1 아키텍처

```
클라이언트 (k6)
    │
    ▼
nginx API Gateway (:80/:443)
    │
    ├── PKD Management (C++ Drogon, :8081)
    │     └── Oracle XE 21c + OpenLDAP
    ├── PA Service (C++ Drogon, :8082)
    │     └── Oracle XE 21c + OpenLDAP
    ├── PKD Relay (C++ Drogon, :8083)
    │     └── Oracle XE 21c + OpenLDAP
    ├── Monitoring (C++ Drogon, :8084)
    └── AI Analysis (Python FastAPI, :8085)
          └── Oracle XE 21c
```

### 2.2 Production 서버 사양

| 항목 | 스펙 |
|------|------|
| **OS** | RHEL 9 |
| **컨테이너** | Podman 5.6.0 |
| **DB** | Oracle XE 21c (SGA 2GB 제한) |
| **LDAP** | OpenLDAP (단일 노드 운영) |
| **도메인** | pkd.smartcoreinc.com |
| **IP** | 10.0.0.220 |

### 2.3 현재 리소스 제한

| 구분 | 현재 값 | 영향 |
|------|---------|------|
| nginx `worker_connections` | 1,024 | worker당 최대 동시 연결 |
| nginx `limit_conn` per IP | 20 | **단일 IP에서 20 연결 초과 불가** |
| nginx `limit_req` API | 100 r/s per IP | 초당 100 요청 제한 |
| nginx `limit_req` PA verify | 2 r/m per user | PA 검증 분당 2회 |
| nginx `limit_req` login | 5 r/m per IP | 로그인 분당 5회 |
| Oracle OCI Session Pool | sessMax=10 (하드코딩) | 동시 DB 쿼리 10개 |
| LDAP Connection Pool | maxSize=10 | 동시 LDAP 쿼리 10개 |
| Drogon Thread | 4 (기본) | HTTP 동시 처리 스레드 |
| 동시 업로드 처리 | 최대 3건 | atomic counter |
| Oracle XE SGA | 2GB | DB 메모리 상한 |

### 2.4 Production 데이터

| 인증서 유형 | 건수 | LDAP | 비율 |
|-------------|------|------|------|
| CSCA | 845 | 845 | 100% |
| DSC | 29,838 | 29,838 | 100% |
| DSC_NC | 502 | 502 | 100% |
| MLSC | 27 | 27 | 100% |
| CRL | 69 | 69 | 100% |
| **합계** | **31,212** | **31,212** | **100%** |

---

## 3. 테스트 범위

### 3.1 대상 엔드포인트 (17개)

모든 대상은 **Public 엔드포인트** (JWT 불필요)로, 실제 사용자 트래픽 패턴을 반영한다.

#### Read 엔드포인트 (14개, 70%)

| # | 엔드포인트 | 비중 | 리소스 유형 | P95 기준 |
|---|-----------|------|-----------|---------|
| 1 | `GET /api/certificates/search` | 20% | LDAP | 3s |
| 2 | `GET /api/upload/statistics` | 8% | DB | 2s |
| 3 | `GET /api/upload/countries` | 7% | DB | 2s |
| 4 | `GET /api/certificates/countries` | 5% | LDAP | 1s |
| 5 | `GET /api/health` | 5% | 경량 | 200ms |
| 6 | `GET /api/certificates/dsc-nc/report` | 5% | CPU 집약 | 10s |
| 7 | `GET /api/certificates/crl/report` | 5% | CPU 집약 | 10s |
| 8 | `GET /api/pa/history` | 5% | DB | 3s |
| 9 | `GET /api/pa/statistics` | 5% | DB | 3s |
| 10 | `GET /api/ai/statistics` | 5% | Python | 5s |
| 11 | `GET /api/ai/anomalies` | 5% | Python | 5s |
| 12 | `GET /api/ai/reports/*` | 3% | Python | 5s |
| 13 | `GET /api/icao/status` | 3% | DB | 2s |
| 14 | `GET /api/sync/status` | 2% | DB | 2s |

#### Write 엔드포인트 (3개, 30%)

| # | 엔드포인트 | 비중 | 리소스 유형 | P95 기준 |
|---|-----------|------|-----------|---------|
| 15 | `POST /api/certificates/pa-lookup` | 20% | DB 인덱스 | 3s |
| 16 | `POST /api/auth/login` | 7% | bcrypt | 2s |
| 17 | `POST /api/sync/check` | 3% | DB+LDAP | 5s |

### 3.2 제외 범위

| 엔드포인트 | 제외 사유 |
|-----------|----------|
| `POST /api/pa/verify` | 실제 SOD/DG 바이너리 데이터 필요, 분당 2회 제한 |
| `POST /api/upload/ldif` | 파일 업로드, 동시 3건 제한 |
| `GET /api/certificates/export/all` | 31,212건 ZIP (수 GB), 메모리 위험 |
| `GET /api/certificates/doc9303-checklist` | 지문 데이터 의존 (seed 후 포함 가능) |

---

## 4. 테스트 Phase

### Phase 0: Smoke Test

| 항목 | 값 |
|------|-----|
| **VUs** | 5 |
| **Duration** | 1분 |
| **목적** | 모든 엔드포인트 정상 응답, TLS 연결, 테스트 데이터 유효성 확인 |
| **서버 튜닝** | 불필요 |
| **Pass 기준** | 100% 성공률, 모든 엔드포인트 200 응답 |

### Phase 1: Baseline

| 항목 | 값 |
|------|-----|
| **VUs** | 50 (35 read + 15 write) |
| **Duration** | 5분 |
| **목적** | 엔드포인트별 기준 레이턴시 측정 (최소 부하) |
| **서버 튜닝** | 불필요 |
| **Pass 기준** | P95 < 2s, error rate 0% |

### Phase 2: Ramp-Up

| 항목 | 값 |
|------|-----|
| **VUs** | 50 → 500 (점진적 증가) |
| **Duration** | 17분 (10분 ramp + 5분 hold + 2분 cooldown) |
| **목적** | 첫 번째 성능 저하 지점 발견 |
| **서버 튜닝** | nginx rate limit 완화 필요 |
| **Pass 기준** | P95 < 3s, error rate < 2%, 503 없음 |

```
VU ▲
500│          ┌────────┐
   │         /│        │
350│        / │        │
   │       /  │        │
200│      /   │        │
   │     /    │        │
100│    /     │        │\
 50│───┘      │        │ \___
   └──┴──┴──┴──┴──┴──┴──┴──► Time
   0  2  5  8  10 12 15 17
```

### Phase 3: Stress

| 항목 | 값 |
|------|-----|
| **VUs** | 500 → 2,000 |
| **Duration** | 30분 (15분 ramp + 10분 hold + 5분 cooldown) |
| **목적** | 예상 용량 초과, 병목 지점 식별 |
| **서버 튜닝** | nginx + Oracle + LDAP + Drogon 전체 튜닝 |
| **Pass 기준** | P95 < 10s, error rate < 10%, 크래시 없음 |

### Phase 4: Peak (5,000 VU 목표)

| 항목 | 값 |
|------|-----|
| **VUs** | 2,000 → 5,000 |
| **Duration** | 20분 (10분 ramp + 5분 hold + 5분 cooldown) |
| **목적** | 5,000 동시 접속 목표 검증 |
| **서버 튜닝** | 전체 튜닝 필수 |
| **Pass 기준** | 시스템 크래시 없음, 읽기 응답 유지, graceful 429/503 |

### Phase 5: Soak

| 항목 | 값 |
|------|-----|
| **VUs** | 500 (상수) |
| **Duration** | 2시간 |
| **목적** | 메모리 누수, 커넥션 누수, 점진적 성능 저하 감지 |
| **서버 튜닝** | Phase 3 수준 |
| **Pass 기준** | P95 안정 (상승 추세 없음), error < 1%, 메모리 증가 20% 이내 |

### Phase 6: Spike

| 항목 | 값 |
|------|-----|
| **VUs** | 100 → 5,000 → 100 (급격한 전환) |
| **Duration** | 13분 |
| **목적** | 급격한 부하 증가/감소 시 시스템 복구 능력 검증 |
| **서버 튜닝** | Phase 4 수준 |
| **Pass 기준** | 스파이크 종료 후 3분 내 Baseline 성능 복구 |

---

## 5. 서버 튜닝 계획

Phase 2 이상 실행 전, 다음 튜닝을 순차 적용한다.

### 5.1 nginx 튜닝 (Phase 2+)

별도 `nginx-loadtest.conf`를 배포하여 Rate Limit를 완화한다.

| 항목 | Production | Load Test | 비고 |
|------|-----------|----------|------|
| `worker_connections` | 1,024 | 16,384 | x16 |
| `worker_rlimit_nofile` | (없음) | 65,535 | 추가 |
| `limit_conn` per IP | 20 | 5,000 | x250 |
| `api_limit` | 100 r/s | 10,000 r/s | x100 |
| `login_limit` | 5 r/m | 500 r/m | x100 |
| `pa_verify_limit` | 2 r/m | 1,000 r/m | x500 |
| `upload_limit` | 5 r/m | 500 r/m | x100 |
| `export_limit` | 1 r/m | 100 r/m | x100 |
| upstream `keepalive` | 32 | 128 | x4 |

### 5.2 OS 커널 튜닝 (Phase 2+)

```bash
sysctl -w net.core.somaxconn=65535
sysctl -w net.ipv4.tcp_max_syn_backlog=65535
sysctl -w net.ipv4.ip_local_port_range="1024 65535"
sysctl -w net.core.netdev_max_backlog=65535
sysctl -w net.ipv4.tcp_tw_reuse=1
sysctl -w fs.file-max=655350
```

### 5.3 Backend 서비스 튜닝 (Phase 3+, 코드 변경 필요)

| 항목 | 현재 | 목표 | 파일 |
|------|------|------|------|
| Oracle sessMax | 10 (하드코딩) | 50 | `oracle_query_executor.cpp:1144` |
| LDAP maxSize | 10 (하드코딩) | 50 | `ldap_connection_pool.h:128` |
| Drogon Thread | 4 | 16 | `THREAD_NUM` 환경변수 |

### 5.4 Oracle XE 튜닝 (Phase 3+)

```sql
ALTER SYSTEM SET PROCESSES=300 SCOPE=SPFILE;
ALTER SYSTEM SET SESSIONS=300 SCOPE=SPFILE;
-- Oracle 재시작 필요
```

> **참고**: Oracle XE 21c는 2GB SGA 하드 제한이 있어 5,000 VU 달성의 최종 병목이 될 수 있다.

---

## 6. 모니터링 항목

### 6.1 k6 메트릭 (클라이언트 측)

| 메트릭 | 설명 | 임계값 |
|--------|------|--------|
| `http_req_duration` P50/P90/P95/P99 | 응답 시간 분포 | Phase별 상이 |
| `http_req_failed` | 오류율 | < 5% (정상) |
| `http_req_blocked` P95 | 연결 대기 시간 | < 100ms |
| `rate_limit_429` | Rate Limit 횟수 | < 100 |
| `server_errors_5xx` | 서버 오류 수 | < 50 |
| `read_latency` / `write_latency` | R/W별 레이턴시 | - |
| `vus` | 동시 접속 VU 수 | 목표 5,000 |
| `iterations` | 총 요청 수 | - |

### 6.2 서버 메트릭 (서버 측)

| 메트릭 | 도구 | 임계값 |
|--------|------|--------|
| Container CPU | `podman stats` | > 90% 지속 = 경고 |
| Container Memory | `podman stats` | Oracle > 2GB, 서비스 > 1GB |
| Container 재시작 | `podman ps` | 1회라도 = **실패** |
| nginx 429 비율 | access.log | > 5% = rate limit 병목 |
| nginx 502/503 | access.log | > 1% = backend 장애 |
| Host CPU | `mpstat` | > 85% 지속 |
| Host Memory | `free -m` | 가용 < 500MB |
| TCP TIME_WAIT | `ss -s` | > 10,000 = 포트 고갈 |
| Oracle 세션 | `v$session` | > 250 = 포화 |

### 6.3 수집 방법

- 서버: `monitoring/collect-metrics.sh` (5초 간격 자동 수집, CSV 출력)
- k6: `--out json=reports/<phase>.json --summary-export=reports/<phase>-summary.json`

---

## 7. 실행 절차

### 7.1 사전 준비

```
1. [ ] 모든 컨테이너 정상 확인: podman-health.sh (8/8 healthy)
2. [ ] DB 데이터 확인: 31,212 certificates
3. [ ] LDAP 데이터 확인: ldap_count_all
4. [ ] k6 설치 확인: k6 version
5. [ ] 테스트 데이터 추출: ./data/seed-data.sh https://pkd.smartcoreinc.com
6. [ ] nginx access log 초기화
7. [ ] 기준 컨테이너 메모리 기록: podman stats --no-stream
```

### 7.2 실행 순서

```
Phase 0: Smoke Test (5 VUs, 1분)
    ↓ Pass → 계속 / Fail → 중단, 원인 분석
Phase 1: Baseline (50 VUs, 5분)
    ↓ 기준 레이턴시 기록
nginx 튜닝 적용 (tune-server.sh apply)
    ↓
Phase 2: Ramp-Up (50→500, 17분)
    ↓ Pass → 계속 / 성능 저하 지점 기록
[Optional] Backend 튜닝 (코드 변경 + 재빌드)
    ↓
Phase 3: Stress (500→2000, 30분)
    ↓ 병목 지점 식별
Phase 4: Peak (2000→5000, 20분)
    ↓ 최대 수용량 확인
Phase 5: Soak (500 VUs, 2시간)
    ↓ 안정성 검증
Phase 6: Spike (100→5000→100, 13분)
    ↓ 복구 능력 검증
Production 설정 복구 (restore-production.sh)
```

### 7.3 실행 명령

```bash
# 테스트 시작 전
cd load-tests
./monitoring/collect-metrics.sh &    # 서버 메트릭 수집

# Phase 0
k6 run --insecure-skip-tls-verify tests/00-smoke.js

# Phase 1
k6 run --insecure-skip-tls-verify \
  --out json=reports/01-baseline.json \
  --summary-export=reports/01-baseline-summary.json \
  tests/01-baseline.js

# Phase 2 (nginx 튜닝 후)
k6 run --insecure-skip-tls-verify \
  --out json=reports/02-ramp-up.json \
  --summary-export=reports/02-ramp-up-summary.json \
  tests/02-ramp-up.js

# ... (Phase 3~6 동일 패턴)

# 테스트 종료 후
./monitoring/collect-metrics.sh stop
./monitoring/analyze-results.sh
sudo ./server-prep/restore-production.sh
```

---

## 8. 결과 분석 계획

### 8.1 분석 항목

| 항목 | 분석 방법 |
|------|----------|
| **병목 지점** | Phase별 P95 레이턴시 비교 → "무릎" 지점 식별 |
| **리소스 상관관계** | 레이턴시 스파이크 ↔ CPU/Memory 스파이크 매핑 |
| **DB Pool 포화** | P95 증가 + 5xx 증가 + Oracle 세션 > 250 |
| **LDAP 병목** | 인증서 검색 P95만 급증 |
| **Rate Limit 병목** | 429 응답 > 5% |
| **메모리 누수** | Soak 테스트에서 컨테이너 메모리 선형 증가 |

### 8.2 최종 보고서 포함 사항

1. Phase별 P50/P90/P95/P99 레이턴시 비교표
2. 엔드포인트별 성능 순위
3. 최대 수용 동시 접속자 수 (오류율 5% 기준)
4. 식별된 병목 지점과 원인
5. 수평 확장(스케일링) 권장 사항
6. 인프라 투자 필요 사항 (Oracle SE/EE, Redis 캐시 등)

---

## 9. 리스크 및 대응 방안

| 리스크 | 영향 | 대응 |
|--------|------|------|
| Oracle XE 2GB SGA 한계 | 2,000+ VU에서 DB 병목 | 테스트로 한계 측정, SE/EE 전환 권장 |
| LDAP 단일 노드 | 읽기 부하 집중 | openldap2 복구 권장 |
| 컨테이너 OOM Kill | 서비스 중단 | `podman stats` 실시간 감시, OOM 즉시 중단 |
| Production 서비스 영향 | 실 사용자 접속 장애 | 비업무 시간(야간) 실행, 즉시 복구 스크립트 준비 |
| 부하 생성기 한계 | k6 자체 리소스 부족 | 별도 머신에서 k6 실행 권장 |

---

## 10. 파일 구조

```
load-tests/
├── README.md                          # Quick Start
├── config/                            # 설정
│   ├── base.js                        # URL, TLS, think time
│   ├── thresholds.js                  # Pass/Fail 기준
│   └── prod.js                        # Production 환경
├── data/                              # 테스트 데이터
│   ├── countries.json                 # 95+ 국가 코드
│   ├── fingerprints.json              # 인증서 지문
│   ├── subject_dns.json               # DSC Subject DN
│   └── seed-data.sh                   # 데이터 추출 스크립트
├── lib/                               # 공통 라이브러리
│   ├── helpers.js                     # think time, 유틸
│   ├── auth.js                        # JWT 캐싱
│   ├── metrics.js                     # 커스텀 메트릭
│   ├── checks.js                      # 응답 검증
│   └── random.js                      # 랜덤 데이터
├── scenarios/                         # 개별 시나리오
│   ├── read/ (14개)                   # 읽기 시나리오
│   └── write/ (3개)                   # 쓰기 시나리오
├── tests/                             # 실행 파일
│   ├── 00-smoke.js ~ 06-spike.js      # Phase 0~6
│   └── full-suite.js                  # 전체 통합
├── server-prep/                       # 서버 준비
│   ├── nginx-loadtest.conf            # 부하 테스트 nginx 설정
│   ├── tune-server.sh                 # 튜닝 적용
│   ├── restore-production.sh          # 설정 복구
│   └── pre-test-checklist.md          # 체크리스트
├── monitoring/                        # 모니터링
│   ├── collect-metrics.sh             # 메트릭 수집
│   └── analyze-results.sh             # 결과 분석
└── reports/                           # 결과 출력
```

---

## 11. 현실적 도달 가능성 예측

| 튜닝 수준 | 예상 최대 동시 접속자 |
|----------|---------------------|
| 튜닝 없음 (현재) | ~100–200 |
| nginx Rate Limit만 완화 | ~300–500 |
| + DB/LDAP Pool 확장 | ~1,000–2,000 |
| + Drogon Thread 증가 | ~2,000–3,000 |
| + Oracle SE/EE (SGA 해제) | ~3,000–5,000 |
| + 캐싱 레이어 (Redis) | 5,000+ |

테스트를 통해 실제 한계를 측정하고, 단계별 인프라 투자 대비 수용량 증가를 정량화한다.

---

## 12. 일정

| 단계 | 소요 시간 | 비고 |
|------|----------|------|
| 사전 준비 (k6 설치, 데이터 추출) | 30분 | |
| Phase 0–1 (Smoke + Baseline) | 10분 | 튜닝 전 |
| nginx 튜닝 적용 | 5분 | |
| Phase 2 (Ramp-Up) | 20분 | |
| Phase 3 (Stress) | 35분 | |
| Phase 4 (Peak) | 25분 | |
| Phase 5 (Soak) | 2시간 | 선택 |
| Phase 6 (Spike) | 15분 | 선택 |
| 결과 분석 및 보고서 | 1시간 | |
| **총 소요** | **~3시간** (Soak 포함 5시간) | |
