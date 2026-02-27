import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { readLatency } from '../../lib/metrics.js';

export function syncStatus() {
  const resp = http.get(`${BASE_URL}/api/sync/status`, tagRequest('sync_status'));
  readLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'sync_status');
  thinkTime();
}
