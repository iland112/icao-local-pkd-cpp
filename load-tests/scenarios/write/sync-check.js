import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { writeLatency } from '../../lib/metrics.js';

export function syncCheck() {
  const resp = http.post(
    `${BASE_URL}/api/sync/check`,
    JSON.stringify({}),
    tagRequest('sync_check')
  );

  writeLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'sync_check');
  thinkTime();
}
