/**
 * Phase 4: Peak Load (5000 VU Target)
 *
 * 목적: 5000 동시 접속 목표 검증
 * VUs: 2000 → 5000 (10분 ramp + 5분 hold + 5분 cooldown)
 * 기준: 시스템 크래시 없음, 읽기 응답 유지 (저하 허용), graceful 429/503
 * 요구사항: 전체 서버 튜닝 + nginx rate limit 제거
 *
 * 실행:
 *   k6 run --insecure-skip-tls-verify tests/04-peak.js
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
import { PEAK_THRESHOLDS } from '../config/thresholds.js';
import { weightedRandom } from '../lib/helpers.js';

export const options = {
  scenarios: {
    read_peak: {
      executor: 'ramping-vus',
      exec: 'readMix',
      startVUs: 1400,
      stages: [
        { duration: '3m',  target: 2100 },
        { duration: '3m',  target: 2800 },
        { duration: '4m',  target: 3500 },   // 70% of 5000
        { duration: '5m',  target: 3500 },   // hold at peak
        { duration: '5m',  target: 0 },      // cooldown
      ],
    },
    write_peak: {
      executor: 'ramping-vus',
      exec: 'writeMix',
      startVUs: 600,
      stages: [
        { duration: '3m',  target: 900 },
        { duration: '3m',  target: 1200 },
        { duration: '4m',  target: 1500 },   // 30% of 5000
        { duration: '5m',  target: 1500 },   // hold at peak
        { duration: '5m',  target: 0 },      // cooldown
      ],
    },
  },
  thresholds: PEAK_THRESHOLDS,
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
