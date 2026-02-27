# ICAO Local PKD — k6 부하 테스트

5000 동시 접속자 부하 테스트 스위트 (k6 기반)

## 빠른 시작

### 1. k6 설치

```bash
# Ubuntu/Debian
sudo gpg -k
sudo gpg --no-default-keyring --keyring /usr/share/keyrings/k6-archive-keyring.gpg \
  --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys C5AD17C747E3415A3642D57D77C6C491D6AC1D68
echo "deb [signed-by=/usr/share/keyrings/k6-archive-keyring.gpg] https://dl.k6.io/deb stable main" | \
  sudo tee /etc/apt/sources.list.d/k6.list
sudo apt-get update && sudo apt-get install k6

# macOS
brew install k6

# Docker
docker run --rm -i grafana/k6 version
```

### 2. 테스트 데이터 준비

```bash
cd load-tests
./data/seed-data.sh https://pkd.smartcoreinc.com
```

### 3. Smoke 테스트 실행

```bash
k6 run --insecure-skip-tls-verify tests/00-smoke.js
```

## 테스트 Phase

| Phase | 파일 | VUs | 시간 | 목적 |
|-------|------|-----|------|------|
| 0 Smoke | `tests/00-smoke.js` | 5 | 1분 | 엔드포인트 정상 확인 |
| 1 Baseline | `tests/01-baseline.js` | 50 | 5분 | 기준 레이턴시 측정 |
| 2 Ramp-Up | `tests/02-ramp-up.js` | 50→500 | 17분 | 첫 성능 저하 지점 |
| 3 Stress | `tests/03-stress.js` | 500→2000 | 30분 | 병목 지점 식별 |
| 4 Peak | `tests/04-peak.js` | 2000→5000 | 20분 | 5000 VU 목표 |
| 5 Soak | `tests/05-soak.js` | 500 | 2시간 | 메모리 누수 감지 |
| 6 Spike | `tests/06-spike.js` | 100→5000→100 | 13분 | 복구 능력 검증 |
| Full | `tests/full-suite.js` | 0→5000→0 | ~60분 | 전체 통합 |

## 워크로드 비율 (70% Read / 30% Write)

**Read (70%)**:
- 인증서 검색 (20%), 업로드 통계 (8%), 국가 통계 (7%)
- 국가 목록 (5%), Health (5%), DSC_NC 보고서 (5%), CRL 보고서 (5%)
- PA 이력 (5%), AI 통계 (5%), AI 이상치 (5%), AI 보고서 (3%)
- ICAO 상태 (3%), Sync 상태 (2%), Doc 9303 (2%)

**Write (30%)**:
- PA Lookup (20%), 로그인 (7%), Sync Check (3%)

## 서버 튜닝 (Phase 3+ 필요)

```bash
# Production 서버에서 실행
sudo ./server-prep/tune-server.sh apply

# 테스트 후 복구
sudo ./server-prep/restore-production.sh
```

### 주요 변경 사항

| 항목 | 기본값 | 테스트 값 |
|------|--------|----------|
| nginx worker_connections | 1024 | 16384 |
| nginx limit_conn/IP | 20 | 5000 |
| nginx api_limit | 100r/s | 10000r/s |
| Oracle sessMax | 10 | 50 (코드 변경) |
| LDAP maxSize | 10 | 50 (코드 변경) |
| Drogon Thread | 4 | 16 |

## 모니터링

```bash
# 테스트 중 서버 메트릭 수집 (5초 간격)
./monitoring/collect-metrics.sh &

# 테스트 후 분석
./monitoring/analyze-results.sh
```

## 실행 예시

```bash
# 환경변수로 타겟 지정
k6 run --insecure-skip-tls-verify \
  -e BASE_URL=https://pkd.smartcoreinc.com \
  --out json=reports/stress-result.json \
  --summary-export=reports/stress-summary.json \
  tests/03-stress.js

# Docker로 실행
docker run --rm -i \
  -v $(pwd):/load-tests \
  -w /load-tests \
  grafana/k6 run --insecure-skip-tls-verify tests/00-smoke.js
```

## Pass/Fail 기준

| Phase | P95 | Error Rate | 특이사항 |
|-------|-----|-----------|---------|
| Smoke | < 1s | 0% | 모든 엔드포인트 200 |
| Baseline | < 2s | 0% | 안정적 |
| Ramp-Up | < 3s | < 2% | 503 없음 |
| Stress | < 10s | < 10% | 크래시 없음 |
| Peak | < 15s | < 15% | graceful 429/503 |
| Soak | < 3s | < 1% | 메모리 증가 20% 이내 |

## 현실적 예상

| 조건 | 예상 최대 VU |
|------|-------------|
| 튜닝 없음 | ~100-200 |
| nginx만 튜닝 | ~300-500 |
| 전체 튜닝 | ~1,000-2,000 |
| Oracle SE/EE | ~3,000-5,000 |

Oracle XE 21c (2GB SGA)가 최종 병목. 테스트로 실제 한계를 측정합니다.
