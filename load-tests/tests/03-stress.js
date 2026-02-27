/**
 * Phase 3: Stress Test
 *
 * 목적: 예상 용량 초과, 병목 지점 식별
 * VUs: 500 → 2000 (15분 ramp + 10분 hold + 5분 cooldown)
 * 기준: P95 < 10s, error < 10%, 크래시 없음
 * 서버 튜닝: nginx + DB pool + LDAP pool + Drogon thread 전부 필요
 *
 * 실행:
 *   k6 run --insecure-skip-tls-verify tests/03-stress.js
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
import { STRESS_THRESHOLDS } from '../config/thresholds.js';
import { weightedRandom } from '../lib/helpers.js';

export const options = {
  scenarios: {
    read_stress: {
      executor: 'ramping-vus',
      exec: 'readMix',
      startVUs: 350,
      stages: [
        { duration: '5m',  target: 700 },
        { duration: '5m',  target: 1050 },
        { duration: '5m',  target: 1400 },   // 70% of 2000
        { duration: '10m', target: 1400 },    // hold
        { duration: '5m',  target: 0 },       // cooldown
      ],
    },
    write_stress: {
      executor: 'ramping-vus',
      exec: 'writeMix',
      startVUs: 150,
      stages: [
        { duration: '5m',  target: 300 },
        { duration: '5m',  target: 450 },
        { duration: '5m',  target: 600 },     // 30% of 2000
        { duration: '10m', target: 600 },      // hold
        { duration: '5m',  target: 0 },        // cooldown
      ],
    },
  },
  thresholds: STRESS_THRESHOLDS,
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
