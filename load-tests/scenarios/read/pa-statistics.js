import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { readLatency, dbQueryLatency } from '../../lib/metrics.js';

export function paStatistics() {
  const resp = http.get(`${BASE_URL}/api/pa/statistics`, tagRequest('pa_stats'));
  readLatency.add(resp.timings.duration);
  dbQueryLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'pa_stats');
  thinkTime();
}
