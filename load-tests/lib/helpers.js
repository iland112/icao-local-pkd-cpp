/**
 * k6 Load Test - Shared Helper Functions
 */

import { sleep } from 'k6';
import { THINK_TIME_MIN, THINK_TIME_MAX, PAGE_SIZE, MAX_PAGE } from '../config/base.js';

/**
 * Simulate realistic user think time between requests
 */
export function thinkTime() {
  sleep(THINK_TIME_MIN + Math.random() * (THINK_TIME_MAX - THINK_TIME_MIN));
}

/**
 * Short think time for rapid-fire scenarios
 */
export function shortThinkTime() {
  sleep(0.1 + Math.random() * 0.5);
}

/**
 * Random page number for paginated endpoints
 */
export function randomPage(maxPage) {
  return Math.floor(Math.random() * (maxPage || MAX_PAGE)) + 1;
}

/**
 * Random page size
 */
export function randomPageSize() {
  const sizes = [10, 20, 50];
  return sizes[Math.floor(Math.random() * sizes.length)];
}

/**
 * Tag request with endpoint name for per-endpoint metrics
 */
export function tagRequest(endpointName, extraHeaders) {
  return {
    tags: { endpoint: endpointName },
    headers: Object.assign(
      { 'Content-Type': 'application/json', 'Accept': 'application/json' },
      extraHeaders || {}
    ),
    timeout: '30s',
  };
}

/**
 * Pick random element from array
 */
export function randomItem(arr) {
  return arr[Math.floor(Math.random() * arr.length)];
}

/**
 * Weighted random selection
 * @param {Array<{value: any, weight: number}>} items
 */
export function weightedRandom(items) {
  const totalWeight = items.reduce((sum, item) => sum + item.weight, 0);
  let random = Math.random() * totalWeight;
  for (const item of items) {
    random -= item.weight;
    if (random <= 0) return item.value;
  }
  return items[items.length - 1].value;
}
