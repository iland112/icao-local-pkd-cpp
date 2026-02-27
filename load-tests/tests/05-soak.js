/**
 * Phase 5: Soak Test
 *
 * 목적: 메모리 누수, 커넥션 누수, 점진적 성능 저하 감지
 * VUs: 500 (constant) | Duration: 2시간
 * 기준: P95 안정 (상승 추세 없음), error < 1%, 메모리 20% 이상 증가 없음
 *
 * 실행:
 *   k6 run --insecure-skip-tls-verify tests/05-soak.js
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
import { icaoStatus } from '../scenarios/read/icao-status.js';
import { syncStatus } from '../scenarios/read/sync-status.js';
import { healthCheck } from '../scenarios/read/health-check.js';
import { paLookup } from '../scenarios/write/pa-lookup.js';
import { authLogin } from '../scenarios/write/auth-login.js';
import { syncCheck } from '../scenarios/write/sync-check.js';
import { weightedRandom } from '../lib/helpers.js';

export const options = {
  scenarios: {
    read_soak: {
      executor: 'constant-vus',
      exec: 'readMix',
      vus: 350,           // 70% of 500
      duration: '2h',
    },
    write_soak: {
      executor: 'constant-vus',
      exec: 'writeMix',
      vus: 150,           // 30% of 500
      duration: '2h',
    },
  },
  thresholds: {
    http_req_duration: ['p(95)<3000'],
    http_req_failed: ['rate<0.01'],
    checks: ['rate>0.98'],
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
