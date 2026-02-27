import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { writeLatency, dbQueryLatency } from '../../lib/metrics.js';
import { randomSubjectDn, randomFingerprint } from '../../lib/random.js';

export function paLookup() {
  let payload;

  // 50% subject DN lookup, 50% fingerprint lookup
  if (Math.random() < 0.5) {
    const dn = randomSubjectDn();
    if (!dn) {
      thinkTime();
      return;
    }
    payload = JSON.stringify({ subjectDn: dn });
  } else {
    const fp = randomFingerprint();
    if (!fp) {
      thinkTime();
      return;
    }
    payload = JSON.stringify({ fingerprint: fp });
  }

  const resp = http.post(
    `${BASE_URL}/api/certificates/pa-lookup`,
    payload,
    tagRequest('pa_lookup')
  );

  writeLatency.add(resp.timings.duration);
  dbQueryLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'pa_lookup');
  thinkTime();
}
