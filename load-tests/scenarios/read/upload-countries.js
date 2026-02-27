import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { readLatency, dbQueryLatency } from '../../lib/metrics.js';

export function uploadCountries() {
  const resp = http.get(`${BASE_URL}/api/upload/countries`, tagRequest('upload_countries'));
  readLatency.add(resp.timings.duration);
  dbQueryLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'upload_countries');
  thinkTime();
}
