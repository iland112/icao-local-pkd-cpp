import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { readLatency } from '../../lib/metrics.js';

export function aiStatistics() {
  const resp = http.get(`${BASE_URL}/api/ai/statistics`, tagRequest('ai_stats'));
  readLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'ai_stats');
  thinkTime();
}
