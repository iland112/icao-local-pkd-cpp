/**
 * Full Suite: 전체 통합 부하 테스트
 *
 * 모든 Phase를 순차적으로 실행 (Smoke → Baseline → Ramp → Stress → Peak → Cooldown)
 * 총 소요 시간: ~60분
 *
 * 실행:
 *   k6 run --insecure-skip-tls-verify \
 *     --out json=reports/full-suite.json \
 *     --summary-export=reports/full-suite-summary.json \
 *     tests/full-suite.js
 */

import { certificateSearch } from '../scenarios/read/certificate-search.js';
import { countryList } from '../scenarios/read/country-list.js';
import { uploadStatistics } from '../scenarios/read/upload-statistics.js';
import { uploadCountries } from '../scenarios/read/upload-countries.js';
import { dscNcReport } from '../scenarios/read/dsc-nc-report.js';
import { crlReport } from '../scenarios/read/crl-report.js';
import { doc9303Checklist } from '../scenarios/read/doc9303-checklist.js';
import { paHistory } from '../scenarios/read/pa-history.js';
import { paStatistics } from '../scenarios/read/pa-statistics.js';
import { aiStatistics } from '../scenarios/read/ai-statistics.js';
import { aiAnomalies } from '../scenarios/read/ai-anomalies.js';
import { aiReports } from '../scenarios/read/ai-reports.js';
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
    // Read workload (70% of total VUs at each stage)
    read_traffic: {
      executor: 'ramping-vus',
      exec: 'readMix',
      startVUs: 0,
      stages: [
        // Phase 0: Smoke (5 total → 4 read)
        { duration: '1m',  target: 4 },
        // Phase 1: Baseline (50 total → 35 read)
        { duration: '1m',  target: 35 },
        { duration: '5m',  target: 35 },
        // Phase 2: Ramp-up (→ 500 total → 350 read)
        { duration: '3m',  target: 100 },
        { duration: '3m',  target: 200 },
        { duration: '4m',  target: 350 },
        { duration: '5m',  target: 350 },   // hold at 500
        // Phase 3: Stress (→ 2000 total → 1400 read)
        { duration: '5m',  target: 700 },
        { duration: '5m',  target: 1050 },
        { duration: '5m',  target: 1400 },
        { duration: '5m',  target: 1400 },   // hold at 2000
        // Phase 4: Peak (→ 5000 total → 3500 read)
        { duration: '3m',  target: 2100 },
        { duration: '3m',  target: 2800 },
        { duration: '4m',  target: 3500 },
        { duration: '5m',  target: 3500 },   // hold at 5000
        // Cooldown
        { duration: '5m',  target: 0 },
      ],
    },

    // Write workload (30% of total VUs at each stage)
    write_traffic: {
      executor: 'ramping-vus',
      exec: 'writeMix',
      startVUs: 0,
      stages: [
        // Phase 0: Smoke
        { duration: '1m',  target: 1 },
        // Phase 1: Baseline
        { duration: '1m',  target: 15 },
        { duration: '5m',  target: 15 },
        // Phase 2: Ramp-up
        { duration: '3m',  target: 45 },
        { duration: '3m',  target: 90 },
        { duration: '4m',  target: 150 },
        { duration: '5m',  target: 150 },
        // Phase 3: Stress
        { duration: '5m',  target: 300 },
        { duration: '5m',  target: 450 },
        { duration: '5m',  target: 600 },
        { duration: '5m',  target: 600 },
        // Phase 4: Peak
        { duration: '3m',  target: 900 },
        { duration: '3m',  target: 1200 },
        { duration: '4m',  target: 1500 },
        { duration: '5m',  target: 1500 },
        // Cooldown
        { duration: '5m',  target: 0 },
      ],
    },
  },

  thresholds: THRESHOLDS,
};

// --- Read scenarios weighted by traffic proportion ---
const readScenarios = [
  { value: certificateSearch, weight: 20 },
  { value: uploadStatistics,  weight: 8 },
  { value: uploadCountries,   weight: 7 },
  { value: countryList,       weight: 5 },
  { value: healthCheck,       weight: 5 },
  { value: dscNcReport,       weight: 5 },
  { value: crlReport,         weight: 5 },
  { value: doc9303Checklist,  weight: 2 },
  { value: paHistory,         weight: 5 },
  { value: paStatistics,      weight: 5 },
  { value: aiStatistics,      weight: 5 },
  { value: aiAnomalies,       weight: 5 },
  { value: aiReports,         weight: 3 },
  { value: icaoStatus,        weight: 3 },
  { value: syncStatus,        weight: 2 },
];

// --- Write scenarios weighted by traffic proportion ---
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
