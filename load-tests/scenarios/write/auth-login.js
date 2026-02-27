import http from 'k6/http';
import { check } from 'k6';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime } from '../../lib/helpers.js';
import { writeLatency, rateLimitHits, successRate } from '../../lib/metrics.js';
import { ENV } from '../../config/prod.js';

export function authLogin() {
  const payload = JSON.stringify({
    username: ENV.username,
    password: ENV.password,
  });

  const resp = http.post(`${BASE_URL}/api/auth/login`, payload, tagRequest('login'));

  writeLatency.add(resp.timings.duration);
  if (resp.status === 429) rateLimitHits.add(1);
  successRate.add(resp.status === 200);

  check(resp, {
    'login: status 200': (r) => r.status === 200,
    'login: has token': (r) => {
      try {
        const body = JSON.parse(r.body);
        return !!body.access_token || !!(body.data && body.data.token) || !!body.token;
      } catch {
        return false;
      }
    },
  });

  thinkTime();
}
