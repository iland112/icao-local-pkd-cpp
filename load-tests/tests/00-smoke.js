/**
 * Phase 0: Smoke Test
 *
 * 목적: 모든 엔드포인트 정상 응답, TLS 연결, 테스트 데이터 유효성 확인
 * VUs: 5 | Duration: 1분
 * 기준: 100% 성공률, 모든 엔드포인트 200 응답
 *
 * 실행:
 *   k6 run --insecure-skip-tls-verify tests/00-smoke.js
 */

import { healthCheck } from '../scenarios/read/health-check.js';
import { certificateSearch } from '../scenarios/read/certificate-search.js';
import { countryList } from '../scenarios/read/country-list.js';
import { uploadStatistics } from '../scenarios/read/upload-statistics.js';
import { uploadCountries } from '../scenarios/read/upload-countries.js';
import { paHistory } from '../scenarios/read/pa-history.js';
import { aiStatistics } from '../scenarios/read/ai-statistics.js';
import { icaoStatus } from '../scenarios/read/icao-status.js';
import { syncStatus } from '../scenarios/read/sync-status.js';
import { paLookup } from '../scenarios/write/pa-lookup.js';
import { authLogin } from '../scenarios/write/auth-login.js';
import { SMOKE_THRESHOLDS } from '../config/thresholds.js';

export const options = {
  vus: 5,
  duration: '1m',
  thresholds: SMOKE_THRESHOLDS,
};

// All scenarios in round-robin
const scenarios = [
  healthCheck,
  certificateSearch,
  countryList,
  uploadStatistics,
  uploadCountries,
  paHistory,
  aiStatistics,
  icaoStatus,
  syncStatus,
  paLookup,
  authLogin,
];

export default function () {
  const idx = __ITER % scenarios.length;
  scenarios[idx]();
}
