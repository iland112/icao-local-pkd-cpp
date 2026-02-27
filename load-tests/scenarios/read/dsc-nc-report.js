import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { readLatency, cpuHeavyLatency } from '../../lib/metrics.js';
import { randomCountry } from '../../lib/random.js';

export function dscNcReport() {
  // Randomly add country filter (50% chance)
  let url = `${BASE_URL}/api/certificates/dsc-nc/report?page=1&pageSize=20`;
  if (Math.random() < 0.5) {
    url += `&country=${randomCountry()}`;
  }

  const resp = http.get(url, tagRequest('dsc_nc_report'));
  readLatency.add(resp.timings.duration);
  cpuHeavyLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'dsc_nc_report');
  thinkTime();
}
