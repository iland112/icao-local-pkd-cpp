/**
 * k6 Load Test - Reusable Check Functions
 */

import { check } from 'k6';
import { rateLimitHits, serverErrors, successRate, connectionErrors, timeoutErrors } from './metrics.js';

/**
 * Standard response checks for JSON API endpoints
 */
export function checkJsonResponse(resp, endpointName) {
  // Track rate limits and server errors
  if (resp.status === 429) rateLimitHits.add(1);
  if (resp.status >= 500) serverErrors.add(1);
  if (resp.status === 0) connectionErrors.add(1);
  if (resp.error && resp.error.includes('timeout')) timeoutErrors.add(1);

  const success = resp.status === 200;
  successRate.add(success);

  return check(resp, {
    [`${endpointName}: status 200`]: (r) => r.status === 200,
    [`${endpointName}: is JSON`]: (r) =>
      r.headers['Content-Type'] && r.headers['Content-Type'].includes('application/json'),
    [`${endpointName}: has body`]: (r) => r.body && r.body.length > 0,
  });
}

/**
 * Check response has data field (standard API response format)
 */
export function checkHasData(resp, endpointName) {
  return check(resp, {
    [`${endpointName}: has data`]: (r) => {
      try {
        const body = JSON.parse(r.body);
        return body.data !== undefined || body.success !== undefined;
      } catch {
        return false;
      }
    },
  });
}

/**
 * Check paginated response
 */
export function checkPaginatedResponse(resp, endpointName) {
  checkJsonResponse(resp, endpointName);
  return check(resp, {
    [`${endpointName}: has pagination`]: (r) => {
      try {
        const body = JSON.parse(r.body);
        return (
          body.data !== undefined &&
          (body.totalCount !== undefined || body.total !== undefined || body.pagination !== undefined)
        );
      } catch {
        return false;
      }
    },
  });
}
