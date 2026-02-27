import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime, randomPage } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { readLatency } from '../../lib/metrics.js';
import { randomCountry, randomCertType } from '../../lib/random.js';

export function aiAnomalies() {
  const page = randomPage(5);
  let url = `${BASE_URL}/api/ai/anomalies?page=${page}&pageSize=20`;

  // Random filters (30% chance each)
  if (Math.random() < 0.3) url += `&country=${randomCountry()}`;
  if (Math.random() < 0.3) url += `&type=${randomCertType()}`;

  const resp = http.get(url, tagRequest('ai_anomalies'));
  readLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'ai_anomalies');
  thinkTime();
}
