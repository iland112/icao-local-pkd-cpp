/**
 * PA Lookup Load Test — 동시 접속 3000+ 클라이언트 목표
 *
 * 목적: PA lookup (POST /api/certificates/pa-lookup) 전용 부하 테스트
 *       - 50% subject DN 조회, 50% fingerprint 조회
 *       - 실제 외부 클라이언트(여권 리더기, 출입국 시스템)의 동시 요청 시뮬레이션
 *
 * VUs: 50 → 500 → 1000 → 2000 → 3000 (ramp 12분 + hold 10분 + cooldown 3분)
 * 총 시간: ~25분
 * 기준: P95 < 3s, P99 < 5s, error < 5%
 *
 * 실행:
 *   k6 run --insecure-skip-tls-verify -e BASE_URL=https://10.0.0.220 tests/pa-service.js
 *
 * 사전 준비:
 *   1. nginx 부하 테스트 설정 적용 (server-prep/tune-server.sh apply)
 *   2. 테스트 데이터 시드 (data/seed-data.sh https://10.0.0.220)
 */

import { paLookup } from '../scenarios/write/pa-lookup.js';

export const options = {
  scenarios: {
    pa_lookup_load: {
      executor: 'ramping-vus',
      exec: 'paLookupTest',
      startVUs: 50,
      stages: [
        { duration: '2m',  target: 500 },    // warm-up
        { duration: '2m',  target: 1000 },   // ramp to 1000
        { duration: '3m',  target: 2000 },   // ramp to 2000
        { duration: '3m',  target: 3000 },   // ramp to 3000
        { duration: '10m', target: 3000 },   // hold at peak
        { duration: '3m',  target: 1000 },   // gradual cooldown
        { duration: '2m',  target: 0 },      // full cooldown
      ],
    },
  },
  thresholds: {
    http_req_failed: ['rate<0.05'],                           // 5% error budget
    http_req_duration: ['p(95)<3000', 'p(99)<5000'],          // P95 < 3s, P99 < 5s
    checks: ['rate>0.95'],                                    // 95%+ checks pass
    'http_req_duration{endpoint:pa_lookup}': ['p(95)<3000'],  // per-endpoint P95
  },
};

export function paLookupTest() {
  paLookup();
}
