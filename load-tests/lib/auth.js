/**
 * k6 Load Test - JWT Authentication Helper
 *
 * Caches JWT token per VU with automatic refresh before expiry.
 */

import http from 'k6/http';
import { BASE_URL } from '../config/base.js';
import { ENV } from '../config/prod.js';

let cachedToken = null;
let tokenExpiry = 0;

/**
 * Get a valid JWT token, refreshing if needed.
 * Uses cached token if still valid (expires 100s before actual expiry).
 */
export function getJwtToken() {
  const now = Date.now();
  if (cachedToken && now < tokenExpiry) {
    return cachedToken;
  }

  const resp = http.post(
    `${BASE_URL}/api/auth/login`,
    JSON.stringify({
      username: ENV.username,
      password: ENV.password,
    }),
    {
      headers: { 'Content-Type': 'application/json' },
      tags: { endpoint: 'login_internal' },
      timeout: '10s',
    }
  );

  if (resp.status === 200) {
    try {
      const body = JSON.parse(resp.body);
      cachedToken = body.access_token || (body.data && body.data.token) || body.token;
      // JWT typically expires in 1 hour; refresh 100s before
      tokenExpiry = now + 3500 * 1000;
    } catch (e) {
      // parse error - return null
      cachedToken = null;
    }
  }

  return cachedToken;
}

/**
 * Get authorization headers with JWT Bearer token
 */
export function authHeaders() {
  const token = getJwtToken();
  if (!token) return {};
  return { Authorization: `Bearer ${token}` };
}
