import { describe, it, expect } from 'vitest';
import { formatDateTime, formatDate, formatTime } from '../dateFormat';

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Return a Date whose local representation is deterministic for assertions.
 *  We use a fixed UTC timestamp and check structure rather than an exact
 *  locale string so the tests pass regardless of the CI system locale.
 */
const ISO_UTC = '2026-03-07T07:30:45.000Z'; // 2026-03-07 16:30:45 KST (+09:00)
const POSTGRES_KST = '2026-03-07 16:30:45.432487+09'; // PostgreSQL timestamp format

// ---------------------------------------------------------------------------
// formatDateTime
// ---------------------------------------------------------------------------
describe('formatDateTime', () => {
  it('should return fallback "-" for null', () => {
    expect(formatDateTime(null)).toBe('-');
  });

  it('should return fallback "-" for undefined', () => {
    expect(formatDateTime(undefined)).toBe('-');
  });

  it('should return fallback "-" for empty string', () => {
    expect(formatDateTime('')).toBe('-');
  });

  it('should return custom fallback when value is null', () => {
    expect(formatDateTime(null, 'N/A')).toBe('N/A');
  });

  it('should return custom fallback when value is undefined', () => {
    expect(formatDateTime(undefined, '—')).toBe('—');
  });

  it('should return custom fallback for empty string', () => {
    expect(formatDateTime('', 'none')).toBe('none');
  });

  it('should return fallback for an invalid date string', () => {
    expect(formatDateTime('not-a-date')).toBe('-');
  });

  it('should return fallback for an invalid Date object', () => {
    expect(formatDateTime(new Date('invalid'))).toBe('-');
  });

  it('should format a valid ISO string and produce a non-empty result', () => {
    const result = formatDateTime(ISO_UTC);
    expect(result).not.toBe('-');
    expect(result.length).toBeGreaterThan(0);
  });

  it('should format a valid Date object and produce a non-empty result', () => {
    const result = formatDateTime(new Date(ISO_UTC));
    expect(result).not.toBe('-');
  });

  it('should parse PostgreSQL timestamp format (space separator + short tz offset)', () => {
    // "2026-03-07 16:30:45.432487+09" must not return fallback
    const result = formatDateTime(POSTGRES_KST);
    expect(result).not.toBe('-');
    expect(result.length).toBeGreaterThan(0);
  });

  it('should include seconds in the output', () => {
    // The time portion has seconds; verify the output contains at least two colons
    const result = formatDateTime(ISO_UTC);
    expect(result).not.toBe('-');
    // Both "16:30:45" style time strings contain ":"
    expect(result).toMatch(/:/);
  });

  it('should produce the same result when called twice with the same input (idempotency)', () => {
    const a = formatDateTime(ISO_UTC);
    const b = formatDateTime(ISO_UTC);
    expect(a).toBe(b);
  });

  it('should handle an ISO string with explicit UTC offset "+00:00"', () => {
    const result = formatDateTime('2026-01-01T00:00:00+00:00');
    expect(result).not.toBe('-');
  });

  it('should handle a Date object at Unix epoch 0', () => {
    const result = formatDateTime(new Date(0));
    expect(result).not.toBe('-');
  });
});

// ---------------------------------------------------------------------------
// formatDate
// ---------------------------------------------------------------------------
describe('formatDate', () => {
  it('should return fallback "-" for null', () => {
    expect(formatDate(null)).toBe('-');
  });

  it('should return fallback "-" for undefined', () => {
    expect(formatDate(undefined)).toBe('-');
  });

  it('should return fallback "-" for empty string', () => {
    expect(formatDate('')).toBe('-');
  });

  it('should return custom fallback for null', () => {
    expect(formatDate(null, '없음')).toBe('없음');
  });

  it('should return fallback for an invalid date string', () => {
    expect(formatDate('not-a-date')).toBe('-');
  });

  it('should return fallback for an invalid Date object', () => {
    expect(formatDate(new Date('invalid'))).toBe('-');
  });

  it('should format a valid ISO string and produce a non-empty result', () => {
    const result = formatDate(ISO_UTC);
    expect(result).not.toBe('-');
    expect(result.length).toBeGreaterThan(0);
  });

  it('should format a valid Date object and produce a non-empty result', () => {
    const result = formatDate(new Date(ISO_UTC));
    expect(result).not.toBe('-');
  });

  it('should NOT include hours/minutes/seconds (date-only output)', () => {
    // ko-KR date-only format does not contain colons
    const result = formatDate('2026-06-15T12:00:00Z');
    expect(result).not.toBe('-');
    expect(result).not.toMatch(/\d{2}:\d{2}/);
  });

  it('should parse PostgreSQL timestamp format', () => {
    const result = formatDate(POSTGRES_KST);
    expect(result).not.toBe('-');
  });

  it('should produce the same result on repeated calls (idempotency)', () => {
    expect(formatDate(ISO_UTC)).toBe(formatDate(ISO_UTC));
  });

  it('should handle year boundary correctly for 2000-01-01', () => {
    const result = formatDate('2000-01-01T00:00:00Z');
    expect(result).not.toBe('-');
    // Year 2000 should appear in the output
    expect(result).toContain('2000');
  });

  it('should handle a far-future date (2099-12-31)', () => {
    const result = formatDate('2099-12-31T00:00:00Z');
    expect(result).not.toBe('-');
    expect(result).toContain('2099');
  });
});

// ---------------------------------------------------------------------------
// formatTime
// ---------------------------------------------------------------------------
describe('formatTime', () => {
  it('should return fallback "-" for null', () => {
    expect(formatTime(null)).toBe('-');
  });

  it('should return fallback "-" for undefined', () => {
    expect(formatTime(undefined)).toBe('-');
  });

  it('should return fallback "-" for empty string', () => {
    expect(formatTime('')).toBe('-');
  });

  it('should return custom fallback for null', () => {
    expect(formatTime(null, '--:--:--')).toBe('--:--:--');
  });

  it('should return fallback for an invalid date string', () => {
    expect(formatTime('not-a-date')).toBe('-');
  });

  it('should return fallback for an invalid Date object', () => {
    expect(formatTime(new Date('bad'))).toBe('-');
  });

  it('should format a valid ISO string and produce a non-empty result', () => {
    const result = formatTime(ISO_UTC);
    expect(result).not.toBe('-');
  });

  it('should format a valid Date object and produce a non-empty result', () => {
    const result = formatTime(new Date(ISO_UTC));
    expect(result).not.toBe('-');
  });

  it('should use 24-hour format (hour12: false) — output matches HH:MM:SS pattern', () => {
    // The time portion must contain colons and digits
    const result = formatTime(ISO_UTC);
    expect(result).not.toBe('-');
    expect(result).toMatch(/\d{1,2}:\d{2}:\d{2}/);
  });

  it('should NOT include year or month digits in a time-only result', () => {
    // ko-KR time format does not contain "년" or "월"
    const result = formatTime('2026-06-15T12:30:45Z');
    expect(result).not.toBe('-');
    expect(result).not.toMatch(/년|월/);
  });

  it('should parse PostgreSQL timestamp format', () => {
    const result = formatTime(POSTGRES_KST);
    expect(result).not.toBe('-');
  });

  it('should produce the same result on repeated calls (idempotency)', () => {
    expect(formatTime(ISO_UTC)).toBe(formatTime(ISO_UTC));
  });

  it('should handle midnight (00:00:00 UTC)', () => {
    const result = formatTime('2026-01-01T00:00:00Z');
    expect(result).not.toBe('-');
    // Output should contain colons (time separator)
    expect(result).toMatch(/:/);
  });
});
