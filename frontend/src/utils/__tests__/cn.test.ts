import { describe, it, expect } from 'vitest';
import { cn } from '../cn';

describe('cn (classnames utility)', () => {
  it('should join multiple strings', () => {
    expect(cn('foo', 'bar', 'baz')).toBe('foo bar baz');
  });

  it('should filter out falsy values', () => {
    expect(cn('foo', false, 'bar', undefined, null, 'baz')).toBe('foo bar baz');
  });

  it('should handle all falsy inputs', () => {
    expect(cn(false, undefined, null)).toBe('');
  });

  it('should handle empty call', () => {
    expect(cn()).toBe('');
  });

  it('should handle single class', () => {
    expect(cn('solo')).toBe('solo');
  });

  it('should handle conditional classes', () => {
    const isActive = true;
    const isDisabled = false;
    expect(cn('base', isActive && 'active', isDisabled && 'disabled')).toBe('base active');
  });

  it('should handle empty strings', () => {
    expect(cn('foo', '', 'bar')).toBe('foo bar');
  });
});
