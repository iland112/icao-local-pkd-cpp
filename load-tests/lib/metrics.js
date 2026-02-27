/**
 * k6 Load Test - Custom Metrics
 *
 * Beyond k6 built-in metrics, these track application-specific data points.
 */

import { Counter, Trend, Rate } from 'k6/metrics';

// --- Latency by operation type ---
export const readLatency = new Trend('read_latency', true);
export const writeLatency = new Trend('write_latency', true);

// --- Per-category latency ---
export const dbQueryLatency = new Trend('db_query_latency', true);    // DB-bound endpoints
export const ldapQueryLatency = new Trend('ldap_query_latency', true); // LDAP-bound endpoints
export const cpuHeavyLatency = new Trend('cpu_heavy_latency', true);   // CPU-intensive endpoints

// --- Error tracking ---
export const rateLimitHits = new Counter('rate_limit_429');
export const serverErrors = new Counter('server_errors_5xx');
export const connectionErrors = new Counter('connection_errors');
export const timeoutErrors = new Counter('timeout_errors');

// --- Success rate ---
export const successRate = new Rate('success_rate');

// --- Response size ---
export const responseSize = new Trend('response_size', false);
