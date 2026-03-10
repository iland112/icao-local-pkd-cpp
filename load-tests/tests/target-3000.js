/**
 * Target 3000 VU Load Test
 *
 * 목적: 동시 접속자 3000명 목표 부하 테스트
 * VUs: 100 → 1000 → 2000 → 3000 (ramp 15분 + hold 10분 + cooldown 5분)
 * 총 시간: ~30분
 * 기준: P95 < 15s, error < 15%, 크래시 없음
 *
 * 실행:
 *   k6 run --insecure-skip-tls-verify -e BASE_URL=https://10.0.0.220 tests/target-3000.js
 */

import { certificateSearch } from '../scenarios/read/certificate-search.js';
import { countryList } from '../scenarios/read/country-list.js';
import { uploadStatistics } from '../scenarios/read/upload-statistics.js';
import { uploadCountries } from '../scenarios/read/upload-countries.js';
import { dscNcReport } from '../scenarios/read/dsc-nc-report.js';
import { crlReport } from '../scenarios/read/crl-report.js';
import { paHistory } from '../scenarios/read/pa-history.js';
import { aiStatistics } from '../scenarios/read/ai-statistics.js';
import { aiAnomalies } from '../scenarios/read/ai-anomalies.js';
import { aiReports } from '../scenarios/read/ai-reports.js';
import { icaoStatus } from '../scenarios/read/icao-status.js';
import { syncStatus } from '../scenarios/read/sync-status.js';
import { healthCheck } from '../scenarios/read/health-check.js';
import { paLookup } from '../scenarios/write/pa-lookup.js';
import { authLogin } from '../scenarios/write/auth-login.js';
import { syncCheck } from '../scenarios/write/sync-check.js';
import { weightedRandom } from '../lib/helpers.js';

export const options = {
  scenarios: {
    read_load: {
      executor: 'ramping-vus',
      exec: 'readMix',
      startVUs: 70,
      stages: [
        { duration: '3m',  target: 700 },    // warm-up
        { duration: '3m',  target: 1400 },   // ramp to 1400
        { duration: '3m',  target: 2100 },   // ramp to 2100 (70% of 3000)
        { duration: '10m', target: 2100 },   // hold at peak
        { duration: '3m',  target: 1050 },   // gradual cooldown
        { duration: '2m',  target: 0 },      // full cooldown
      ],
    },
    write_load: {
      executor: 'ramping-vus',
      exec: 'writeMix',
      startVUs: 30,
      stages: [
        { duration: '3m',  target: 300 },    // warm-up
        { duration: '3m',  target: 600 },    // ramp to 600
        { duration: '3m',  target: 900 },    // ramp to 900 (30% of 3000)
        { duration: '10m', target: 900 },    // hold at peak
        { duration: '3m',  target: 450 },    // gradual cooldown
        { duration: '2m',  target: 0 },      // full cooldown
      ],
    },
  },
  thresholds: {
    http_req_failed: ['rate<0.15'],          // 15% error budget
    http_req_duration: ['p(95)<15000'],       // P95 < 15s
    checks: ['rate>0.85'],                    // 85%+ checks pass
  },
};

const readScenarios = [
  { value: certificateSearch, weight: 20 },
  { value: uploadStatistics,  weight: 8 },
  { value: uploadCountries,   weight: 7 },
  { value: countryList,       weight: 5 },
  { value: healthCheck,       weight: 5 },
  { value: dscNcReport,       weight: 5 },
  { value: crlReport,         weight: 5 },
  { value: paHistory,         weight: 5 },
  { value: aiStatistics,      weight: 5 },
  { value: aiAnomalies,       weight: 5 },
  { value: aiReports,         weight: 3 },
  { value: icaoStatus,        weight: 3 },
  { value: syncStatus,        weight: 2 },
];

const writeScenarios = [
  { value: paLookup,   weight: 20 },
  { value: authLogin,  weight: 7 },
  { value: syncCheck,  weight: 3 },
];

export function readMix() {
  const fn = weightedRandom(readScenarios);
  fn();
}

export function writeMix() {
  const fn = weightedRandom(writeScenarios);
  fn();
}
