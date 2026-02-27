import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime } from '../../lib/helpers.js';
import { checkJsonResponse, checkHasData } from '../../lib/checks.js';
import { readLatency } from '../../lib/metrics.js';

export function countryList() {
  const resp = http.get(`${BASE_URL}/api/certificates/countries`, tagRequest('country_list'));
  readLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'country_list');
  checkHasData(resp, 'country_list');
  thinkTime();
}
