/**
 * Phase 2: Ramp-Up Test
 *
 * 목적: 첫 번째 성능 저하 지점 발견
 * VUs: 50 → 500 (15분 ramp + 5분 hold + 2분 cooldown)
 * 기준: P95 < 3s, error < 2%, 503 없음
 * 서버 튜닝: nginx rate limit 완화 필요
 *
 * 실행:
 *   k6 run --insecure-skip-tls-verify tests/02-ramp-up.js
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
import { THRESHOLDS } from '../config/thresholds.js';
import { weightedRandom } from '../lib/helpers.js';

export const options = {
  scenarios: {
    // 70% Read
    read_ramp: {
      executor: 'ramping-vus',
      exec: 'readMix',
      startVUs: 35,
      stages: [
        { duration: '2m',  target: 70 },
        { duration: '3m',  target: 140 },
        { duration: '3m',  target: 245 },
        { duration: '2m',  target: 350 },   // 70% of 500
        { duration: '5m',  target: 350 },   // hold
        { duration: '2m',  target: 0 },     // cooldown
      ],
    },
    // 30% Write
    write_ramp: {
      executor: 'ramping-vus',
      exec: 'writeMix',
      startVUs: 15,
      stages: [
        { duration: '2m',  target: 30 },
        { duration: '3m',  target: 60 },
        { duration: '3m',  target: 105 },
        { duration: '2m',  target: 150 },   // 30% of 500
        { duration: '5m',  target: 150 },   // hold
        { duration: '2m',  target: 0 },     // cooldown
      ],
    },
  },
  thresholds: Object.assign({}, THRESHOLDS, {
    http_req_duration: ['p(95)<3000'],
    http_req_failed: ['rate<0.02'],
  }),
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
