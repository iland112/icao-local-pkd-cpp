import { describe, it, expect } from 'vitest';
import { formatNumbersInMessage } from '../numberFormat';

// ---------------------------------------------------------------------------
// formatNumbersInMessage
// ---------------------------------------------------------------------------
// The function replaces every run of 4+ consecutive digits with the same
// number formatted via Number.toLocaleString().  Runs of 1-3 digits are left
// untouched.
// ---------------------------------------------------------------------------

describe('formatNumbersInMessage', () => {
  // --- Happy path ---

  it('should format a 4-digit number in a plain message', () => {
    const result = formatNumbersInMessage('총 1000건');
    // 1000 → "1,000" (en-US locale as minimum expectation; locale chars vary)
    expect(result).toContain('1');
    expect(result).not.toContain('1000'); // raw run of 4 digits must be gone
  });

  it('should format a 5-digit number', () => {
    const result = formatNumbersInMessage('신규: 22820');
    expect(result).not.toContain('22820');
  });

  it('should format multiple numbers in a single message', () => {
    const result = formatNumbersInMessage('DSC 9200/22820 처리 완료 (신규: 9200)');
    expect(result).not.toContain('9200/22820');
  });

  it('should leave 1-digit numbers unchanged', () => {
    const result = formatNumbersInMessage('오류 3건');
    expect(result).toBe('오류 3건');
  });

  it('should leave 2-digit numbers unchanged', () => {
    const result = formatNumbersInMessage('오류 42건');
    expect(result).toBe('오류 42건');
  });

  it('should leave 3-digit numbers unchanged', () => {
    const result = formatNumbersInMessage('오류 999건');
    expect(result).toBe('오류 999건');
  });

  // --- Edge cases: empty / whitespace ---

  it('should return an empty string for an empty input', () => {
    expect(formatNumbersInMessage('')).toBe('');
  });

  it('should return whitespace unchanged', () => {
    expect(formatNumbersInMessage('   ')).toBe('   ');
  });

  // --- Messages with no numbers ---

  it('should return a message with no numbers unchanged', () => {
    expect(formatNumbersInMessage('처리 완료')).toBe('처리 완료');
  });

  it('should return a message containing only short numbers unchanged', () => {
    expect(formatNumbersInMessage('오류 1건, 경고 23건, 정보 456건')).toBe(
      '오류 1건, 경고 23건, 정보 456건'
    );
  });

  // --- Boundary: exactly 4 digits ---

  it('should format exactly 4 digits (boundary)', () => {
    const result = formatNumbersInMessage('1000');
    expect(result).not.toBe('1000');
  });

  it('should NOT format 3 digits (boundary below threshold)', () => {
    const result = formatNumbersInMessage('999');
    expect(result).toBe('999');
  });

  // --- Large numbers ---

  it('should format 6-digit numbers', () => {
    const result = formatNumbersInMessage('인증서 총 123456개');
    expect(result).not.toContain('123456');
  });

  it('should format 7-digit numbers', () => {
    const result = formatNumbersInMessage('총 1234567건');
    expect(result).not.toContain('1234567');
  });

  // --- Text with mixed Korean and numbers ---

  it('should format numbers embedded in Korean text', () => {
    const result = formatNumbersInMessage('DSC 9200/22820 처리 완료 (신규: 9200)');
    // Korean surroundings must be preserved
    expect(result).toContain('처리 완료');
    expect(result).toContain('신규');
  });

  it('should handle consecutive formatted numbers separated by slash', () => {
    const result = formatNumbersInMessage('9200/22820');
    // Both numbers independently formatted; slash and surrounding text remain
    expect(result).toContain('/');
  });

  // --- Special inputs ---

  it('should handle a message that is only digits (4+)', () => {
    const result = formatNumbersInMessage('12345');
    expect(result).not.toBe('12345');
  });

  it('should handle digits adjacent to non-digit non-space characters', () => {
    // e.g. "(9200)" — parens are not digits, number should still be formatted
    const result = formatNumbersInMessage('(9200)');
    expect(result).not.toContain('9200');
    // Parentheses should remain
    expect(result).toMatch(/\(/);
    expect(result).toMatch(/\)/);
  });

  it('should handle multiple separate 4-digit numbers in a single string', () => {
    const result = formatNumbersInMessage('A:1000 B:2000 C:3000');
    // None of the raw 4-digit sequences should remain
    expect(result).not.toContain('1000');
    expect(result).not.toContain('2000');
    expect(result).not.toContain('3000');
  });

  // --- Idempotency ---

  it('should produce the same output when called twice on the same input', () => {
    const input = 'DSC 9200/22820 처리 완료';
    const first = formatNumbersInMessage(input);
    // Note: result of first call contains locale separators (commas / dots /
    // narrow no-break spaces) which are NOT 4+ consecutive digits, so calling
    // the function again on an already-formatted string must not change it.
    const second = formatNumbersInMessage(first);
    expect(second).toBe(first);
  });

  // --- Zero ---

  it('should format the string "0000" (four zeros)', () => {
    const result = formatNumbersInMessage('0000');
    // 0 → "0" regardless of locale; the raw run "0000" is gone
    expect(result).not.toBe('0000');
  });

  // --- Text-only messages (regression: must not crash) ---

  it('should handle an English-only message with no numbers', () => {
    expect(formatNumbersInMessage('Processing complete')).toBe('Processing complete');
  });

  it('should handle a URL-like string with short numeric segments', () => {
    // URL segments like "8081" should be formatted
    const result = formatNumbersInMessage('http://localhost:8081/api');
    expect(result).not.toContain('8081');
  });
});
