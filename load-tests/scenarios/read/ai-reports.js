import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime, randomItem } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { readLatency } from '../../lib/metrics.js';

const AI_REPORT_ENDPOINTS = [
  '/api/ai/reports/country-maturity',
  '/api/ai/reports/algorithm-trends',
  '/api/ai/reports/key-size-distribution',
  '/api/ai/reports/risk-distribution',
  '/api/ai/reports/forensic-summary',
  '/api/ai/reports/issuer-profiles',
];

export function aiReports() {
  const endpoint = randomItem(AI_REPORT_ENDPOINTS);
  const resp = http.get(`${BASE_URL}${endpoint}`, tagRequest('ai_reports'));
  readLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'ai_reports');
  thinkTime();
}
