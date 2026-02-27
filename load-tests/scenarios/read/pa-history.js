import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime, randomPage } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { readLatency, dbQueryLatency } from '../../lib/metrics.js';

export function paHistory() {
  const page = randomPage(5);
  const resp = http.get(
    `${BASE_URL}/api/pa/history?page=${page}&pageSize=20`,
    tagRequest('pa_history')
  );
  readLatency.add(resp.timings.duration);
  dbQueryLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'pa_history');
  thinkTime();
}
