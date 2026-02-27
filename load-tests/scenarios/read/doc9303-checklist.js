import http from 'k6/http';
import { BASE_URL } from '../../config/base.js';
import { tagRequest, thinkTime } from '../../lib/helpers.js';
import { checkJsonResponse } from '../../lib/checks.js';
import { readLatency, cpuHeavyLatency } from '../../lib/metrics.js';
import { randomFingerprint } from '../../lib/random.js';

export function doc9303Checklist() {
  const fp = randomFingerprint();
  if (!fp) {
    // No fingerprints loaded - skip
    thinkTime();
    return;
  }

  const resp = http.get(
    `${BASE_URL}/api/certificates/doc9303-checklist?fingerprint=${fp}`,
    tagRequest('doc9303')
  );
  readLatency.add(resp.timings.duration);
  cpuHeavyLatency.add(resp.timings.duration);
  checkJsonResponse(resp, 'doc9303');
  thinkTime();
}
