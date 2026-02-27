/**
 * Phase 1: Baseline Performance
 *
 * 목적: 엔드포인트별 기준 레이턴시 측정 (최소 부하)
 * VUs: 50 (35 read + 15 write) | Duration: 5분
 * 기준: P95 < 500ms (경량), P95 < 2s (중량), error 0%
 *
 * 실행:
 *   k6 run --insecure-skip-tls-verify tests/01-baseline.js
 */

import { healthCheck } from '../scenarios/read/health-check.js';
import { certificateSearch } from '../scenarios/read/certificate-search.js';
import { countryList } from '../scenarios/read/country-list.js';
import { uploadStatistics } from '../scenarios/read/upload-statistics.js';
import { uploadCountries } from '../scenarios/read/upload-countries.js';
import { dscNcReport } from '../scenarios/read/dsc-nc-report.js';
import { crlReport } from '../scenarios/read/crl-report.js';
import { paHistory } from '../scenarios/read/pa-history.js';
import { paStatistics } from '../scenarios/read/pa-statistics.js';
import { aiStatistics } from '../scenarios/read/ai-statistics.js';
import { aiAnomalies } from '../scenarios/read/ai-anomalies.js';
import { icaoStatus } from '../scenarios/read/icao-status.js';
import { syncStatus } from '../scenarios/read/sync-status.js';
import { paLookup } from '../scenarios/write/pa-lookup.js';
import { authLogin } from '../scenarios/write/auth-login.js';
import { syncCheck } from '../scenarios/write/sync-check.js';
import { BASELINE_THRESHOLDS } from '../config/thresholds.js';
import { weightedRandom } from '../lib/helpers.js';

export const options = {
  scenarios: {
    read_mix: {
      executor: 'constant-vus',
      vus: 35,
      duration: '5m',
      exec: 'readMix',
    },
    write_mix: {
      executor: 'constant-vus',
      vus: 15,
      duration: '5m',
      exec: 'writeMix',
    },
  },
  thresholds: BASELINE_THRESHOLDS,
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
  { value: paStatistics,      weight: 5 },
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
