/**
 * Phase 6: Spike Test
 *
 * 목적: 급격한 부하 증가/감소 시 시스템 복구 능력 검증
 * VUs: 100 → 5000 → 100 (급격한 전환)
 * 기준: 스파이크 종료 후 3분 내 Baseline 성능 복구
 *
 * 실행:
 *   k6 run --insecure-skip-tls-verify tests/06-spike.js
 */

import { certificateSearch } from '../scenarios/read/certificate-search.js';
import { countryList } from '../scenarios/read/country-list.js';
import { uploadStatistics } from '../scenarios/read/upload-statistics.js';
import { uploadCountries } from '../scenarios/read/upload-countries.js';
import { paHistory } from '../scenarios/read/pa-history.js';
import { aiStatistics } from '../scenarios/read/ai-statistics.js';
import { icaoStatus } from '../scenarios/read/icao-status.js';
import { syncStatus } from '../scenarios/read/sync-status.js';
import { healthCheck } from '../scenarios/read/health-check.js';
import { paLookup } from '../scenarios/write/pa-lookup.js';
import { authLogin } from '../scenarios/write/auth-login.js';
import { weightedRandom } from '../lib/helpers.js';

export const options = {
  stages: [
    // Warm-up
    { duration: '2m',  target: 100 },
    // Spike UP
    { duration: '30s', target: 5000 },
    // Hold spike
    { duration: '3m',  target: 5000 },
    // Spike DOWN
    { duration: '30s', target: 100 },
    // Recovery observation
    { duration: '5m',  target: 100 },
    // Cooldown
    { duration: '2m',  target: 0 },
  ],
  thresholds: {
    http_req_failed: ['rate<0.20'],      // Up to 20% during spike
    http_req_duration: ['p(95)<15000'],   // P95 < 15s during spike
  },
};

const allScenarios = [
  // Read (70%)
  { value: certificateSearch, weight: 20 },
  { value: uploadStatistics,  weight: 8 },
  { value: uploadCountries,   weight: 7 },
  { value: countryList,       weight: 5 },
  { value: healthCheck,       weight: 5 },
  { value: paHistory,         weight: 5 },
  { value: aiStatistics,      weight: 5 },
  { value: icaoStatus,        weight: 3 },
  { value: syncStatus,        weight: 2 },
  // Write (30%)
  { value: paLookup,          weight: 20 },
  { value: authLogin,         weight: 7 },
];

export default function () {
  const fn = weightedRandom(allScenarios);
  fn();
}
