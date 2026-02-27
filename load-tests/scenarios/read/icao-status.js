import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { readLatency } from '../../lib/metrics.js';

export function icaoStatus() {
  const resp = http.get(`${BASE_URL}/api/icao/status`, tagRequest('icao_status'));
  readLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'icao_status');
  thinkTime();
}
