/**
 * k6 Load Test - Base Configuration
 *
 * Usage:
 *   k6 run --insecure-skip-tls-verify -e BASE_URL=https://pkd.smartcoreinc.com tests/00-smoke.js
 */

// Target URL (override with -e BASE_URL=...)
export const BASE_URL = __ENV.BASE_URL || 'https://pkd.smartcoreinc.com';

// Default HTTP request parameters
export const HTTP_PARAMS = {
  headers: {
    'Content-Type': 'application/json',
    'Accept': 'application/json',
  },
  timeout: '30s',
};

// Think time range (seconds) - simulates real user behavior
export const THINK_TIME_MIN = 1;
export const THINK_TIME_MAX = 5;

// Pagination
export const PAGE_SIZE = 20;
export const MAX_PAGE = 50;

// Certificate types
export const CERT_TYPES = ['CSCA', 'DSC', 'DSC_NC', 'MLSC'];

// Validation statuses
export const VALIDATION_STATUSES = ['VALID', 'INVALID', 'EXPIRED', 'EXPIRED_VALID', 'PENDING'];
