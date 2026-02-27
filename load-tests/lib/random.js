/**
 * k6 Load Test - Randomization Utilities
 *
 * Loads test data from JSON files and provides random selection functions.
 */

import { CERT_TYPES } from '../config/base.js';
import { SharedArray } from 'k6/data';

// Load test data using SharedArray (memory-efficient, shared across VUs)
const countries = new SharedArray('countries', function () {
  try {
    return JSON.parse(open('../data/countries.json'));
  } catch (e) {
    // Fallback: common ICAO PKD countries
    return [
      'KR', 'US', 'DE', 'FR', 'JP', 'GB', 'AU', 'CA', 'NL', 'SE',
      'NO', 'DK', 'FI', 'BE', 'AT', 'CH', 'ES', 'IT', 'PT', 'IE',
      'NZ', 'SG', 'HK', 'TW', 'TH', 'MY', 'PH', 'IN', 'BR', 'MX',
    ];
  }
});

const fingerprints = new SharedArray('fingerprints', function () {
  try {
    return JSON.parse(open('../data/fingerprints.json'));
  } catch (e) {
    // Empty fallback - doc9303-checklist tests will be skipped
    return [];
  }
});

const subjectDns = new SharedArray('subjectDns', function () {
  try {
    return JSON.parse(open('../data/subject_dns.json'));
  } catch (e) {
    return [];
  }
});

/**
 * Random country code from loaded data
 */
export function randomCountry() {
  return countries[Math.floor(Math.random() * countries.length)];
}

/**
 * Random certificate type
 */
export function randomCertType() {
  return CERT_TYPES[Math.floor(Math.random() * CERT_TYPES.length)];
}

/**
 * Random certificate fingerprint (SHA-256)
 */
export function randomFingerprint() {
  if (fingerprints.length === 0) return null;
  return fingerprints[Math.floor(Math.random() * fingerprints.length)];
}

/**
 * Random DSC subject DN (for pa-lookup)
 */
export function randomSubjectDn() {
  if (subjectDns.length === 0) return null;
  return subjectDns[Math.floor(Math.random() * subjectDns.length)];
}

/**
 * Random integer between min and max (inclusive)
 */
export function randomInt(min, max) {
  return Math.floor(Math.random() * (max - min + 1)) + min;
}
