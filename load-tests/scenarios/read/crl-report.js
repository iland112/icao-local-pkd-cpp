import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { readLatency, cpuHeavyLatency } from '../../lib/metrics.js';

export function crlReport() {
  const resp = http.get(
    `${BASE_URL}/api/certificates/crl/report?page=1&pageSize=20`,
    tagRequest('crl_report')
  );
  readLatency.add(resp.timings.duration);
  cpuHeavyLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'crl_report');
  thinkTime();
}
