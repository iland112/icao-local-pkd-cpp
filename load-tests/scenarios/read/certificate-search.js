import http from 'k6/http';
import { BASE_URL, PAGE_SIZE } from '../../config/base.js';
import { tagRequest, thinkTime, randomPage } from '../../lib/helpers.js';
import { checkJsonResponse, checkHasData } from '../../lib/checks.js';
import { readLatency, ldapQueryLatency } from '../../lib/metrics.js';
import { randomCountry, randomCertType } from '../../lib/random.js';

export function certificateSearch() {
  const country = randomCountry();
  const certType = randomCertType();
  const page = randomPage(10);

  const url = `${BASE_URL}/api/certificates/search?country=${country}&type=${certType}&page=${page}&pageSize=${PAGE_SIZE}`;
  const resp = http.get(url, tagRequest('cert_search'));

  readLatency.add(resp.timings.duration);
  ldapQueryLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'cert_search');
  checkHasData(resp, 'cert_search');
  thinkTime();
}
