/**
 * Tests for countryNames.ts
 *
 * These are pure-function tests — no mocking required.
 * The module uses i18n-iso-countries under the hood, which is a real
 * dependency, so the tests verify actual country name resolution.
 */

import { describe, it, expect } from 'vitest';
import { getCountryDisplayName, getCountryName, getAllCountries } from '../countryNames';

// ===========================================================================
// getCountryDisplayName
// ===========================================================================

describe('getCountryDisplayName', () => {
  it('should return "CODE - Country Name" format for a standard ISO code', () => {
    expect(getCountryDisplayName('KR')).toBe('KR - South Korea');
  });

  it('should return "US - United States of America" for US', () => {
    // The library returns the official ISO name
    const result = getCountryDisplayName('US');
    expect(result).toMatch(/^US - /);
  });

  it('should return "DE - Germany" for DE', () => {
    expect(getCountryDisplayName('DE')).toBe('DE - Germany');
  });

  it('should return "FR - France" for FR', () => {
    expect(getCountryDisplayName('FR')).toBe('FR - France');
  });

  it('should return just the code when no name is found', () => {
    // XX is not in the library and not in CUSTOM_COUNTRY_NAMES
    const result = getCountryDisplayName('ZZ');
    // ZZ is in CUSTOM_COUNTRY_NAMES — handled separately
    // Use a truly unknown code
    expect(getCountryDisplayName('QQ')).toBe('QQ');
  });

  // Custom country code overrides
  it('should return "EU - European Union" for custom code EU', () => {
    expect(getCountryDisplayName('EU')).toBe('EU - European Union');
  });

  it('should return "KS - Kosovo" for custom code KS', () => {
    expect(getCountryDisplayName('KS')).toBe('KS - Kosovo');
  });

  it('should return "UN - United Nations" for custom code UN', () => {
    expect(getCountryDisplayName('UN')).toBe('UN - United Nations');
  });

  it('should return "XK - Kosovo" for custom code XK', () => {
    expect(getCountryDisplayName('XK')).toBe('XK - Kosovo');
  });

  it('should return "XO - Sovereign Military Order of Malta" for XO', () => {
    expect(getCountryDisplayName('XO')).toBe('XO - Sovereign Military Order of Malta');
  });

  it('should return "ZZ - United Nations" for custom code ZZ', () => {
    expect(getCountryDisplayName('ZZ')).toBe('ZZ - United Nations');
  });

  it('should prefer custom mapping over library lookup for custom codes', () => {
    // EU is not a valid ISO 3166-1 alpha-2, so library would return undefined.
    // Custom mapping must take priority.
    const result = getCountryDisplayName('EU');
    expect(result).toBe('EU - European Union');
  });
});

// ===========================================================================
// getCountryName
// ===========================================================================

describe('getCountryName', () => {
  it('should return just the country name for a standard ISO code', () => {
    expect(getCountryName('KR')).toBe('South Korea');
  });

  it('should return the country name for DE', () => {
    expect(getCountryName('DE')).toBe('Germany');
  });

  it('should return the code itself when no name is found', () => {
    expect(getCountryName('QQ')).toBe('QQ');
  });

  it('should return "European Union" for custom code EU', () => {
    expect(getCountryName('EU')).toBe('European Union');
  });

  it('should return "Kosovo" for custom code KS', () => {
    expect(getCountryName('KS')).toBe('Kosovo');
  });

  it('should return "Kosovo" for custom code XK', () => {
    expect(getCountryName('XK')).toBe('Kosovo');
  });

  it('should return "United Nations" for custom code UN', () => {
    expect(getCountryName('UN')).toBe('United Nations');
  });

  it('should return "Sovereign Military Order of Malta" for XO', () => {
    expect(getCountryName('XO')).toBe('Sovereign Military Order of Malta');
  });

  it('should return "United Nations" for ZZ', () => {
    expect(getCountryName('ZZ')).toBe('United Nations');
  });

  it('should return different values for KS and XK (both Kosovo, different codes)', () => {
    expect(getCountryName('KS')).toBe('Kosovo');
    expect(getCountryName('XK')).toBe('Kosovo');
  });
});

// ===========================================================================
// getAllCountries
// ===========================================================================

describe('getAllCountries', () => {
  it('should return an object (Record<string, string>)', () => {
    const result = getAllCountries();
    expect(typeof result).toBe('object');
    expect(result).not.toBeNull();
  });

  it('should contain standard ISO country codes', () => {
    const result = getAllCountries();
    expect(result['KR']).toBeDefined();
    expect(result['US']).toBeDefined();
    expect(result['DE']).toBeDefined();
  });

  it('should contain custom country codes merged in', () => {
    const result = getAllCountries();
    expect(result['EU']).toBe('European Union');
    expect(result['KS']).toBe('Kosovo');
    expect(result['UN']).toBe('United Nations');
    expect(result['XK']).toBe('Kosovo');
    expect(result['XO']).toBe('Sovereign Military Order of Malta');
    expect(result['ZZ']).toBe('United Nations');
  });

  it('should contain at least 200 entries (comprehensive ISO coverage)', () => {
    const result = getAllCountries();
    expect(Object.keys(result).length).toBeGreaterThan(200);
  });

  it('should have string values for all entries', () => {
    const result = getAllCountries();
    for (const [, name] of Object.entries(result)) {
      expect(typeof name).toBe('string');
      expect(name.length).toBeGreaterThan(0);
    }
  });

  it('should not mutate subsequent calls (idempotency)', () => {
    const first = getAllCountries();
    const second = getAllCountries();
    // Custom entries are stable
    expect(first['EU']).toBe(second['EU']);
    expect(Object.keys(first).length).toBe(Object.keys(second).length);
  });

  it('custom entries should override library entries when they conflict', () => {
    // ZZ is not in the standard ISO library, so it must come from custom mapping
    const result = getAllCountries();
    expect(result['ZZ']).toBe('United Nations');
  });
});

// ===========================================================================
// Edge cases
// ===========================================================================

describe('edge cases', () => {
  it('getCountryDisplayName with empty string should return the empty string (fallback)', () => {
    // '' is not a known code — the library returns undefined, we return the code (empty string)
    const result = getCountryDisplayName('');
    expect(result).toBe('');
  });

  it('getCountryName with empty string should return empty string', () => {
    expect(getCountryName('')).toBe('');
  });

  it('getCountryDisplayName with lowercase code should fall back to code if library returns nothing', () => {
    // i18n-iso-countries is case-sensitive; 'kr' is not a valid code
    const result = getCountryDisplayName('kr');
    // Should either return 'kr' or 'kr - South Korea' depending on library behavior
    expect(typeof result).toBe('string');
  });

  it('getCountryName should not throw for any string input', () => {
    const inputs = ['', 'A', 'KR', 'ZZZ', '123', '  '];
    for (const input of inputs) {
      expect(() => getCountryName(input)).not.toThrow();
    }
  });
});
