import { describe, it, expect } from 'vitest';
import { getAlpha2Code, getFlagSvgPath } from '../countryCode';

// ---------------------------------------------------------------------------
// getAlpha2Code
// ---------------------------------------------------------------------------
describe('getAlpha2Code', () => {
  // --- Happy path: standard ISO 3166-1 alpha-3 → alpha-2 ---
  it('should convert KOR to kr', () => {
    expect(getAlpha2Code('KOR')).toBe('kr');
  });

  it('should convert USA to us', () => {
    expect(getAlpha2Code('USA')).toBe('us');
  });

  it('should convert JPN to jp', () => {
    expect(getAlpha2Code('JPN')).toBe('jp');
  });

  it('should convert DEU to de', () => {
    expect(getAlpha2Code('DEU')).toBe('de');
  });

  it('should convert FRA to fr', () => {
    expect(getAlpha2Code('FRA')).toBe('fr');
  });

  it('should convert GBR to gb', () => {
    expect(getAlpha2Code('GBR')).toBe('gb');
  });

  it('should convert CHN to cn', () => {
    expect(getAlpha2Code('CHN')).toBe('cn');
  });

  it('should convert AUS to au', () => {
    expect(getAlpha2Code('AUS')).toBe('au');
  });

  it('should convert CAN to ca', () => {
    expect(getAlpha2Code('CAN')).toBe('ca');
  });

  it('should convert BRA to br', () => {
    expect(getAlpha2Code('BRA')).toBe('br');
  });

  it('should convert IND to in', () => {
    expect(getAlpha2Code('IND')).toBe('in');
  });

  it('should convert RUS to ru', () => {
    expect(getAlpha2Code('RUS')).toBe('ru');
  });

  it('should convert NLD to nl (Netherlands)', () => {
    expect(getAlpha2Code('NLD')).toBe('nl');
  });

  it('should convert LUX to lu (Luxembourg)', () => {
    expect(getAlpha2Code('LUX')).toBe('lu');
  });

  it('should convert HUN to hu (Hungary)', () => {
    expect(getAlpha2Code('HUN')).toBe('hu');
  });

  // --- Case insensitivity: lowercase and mixed-case inputs ---
  it('should handle lowercase input "kor" → kr', () => {
    expect(getAlpha2Code('kor')).toBe('kr');
  });

  it('should handle mixed-case input "Kor" → kr', () => {
    expect(getAlpha2Code('Kor')).toBe('kr');
  });

  it('should handle lowercase "usa" → us', () => {
    expect(getAlpha2Code('usa')).toBe('us');
  });

  it('should handle "jpn" (all lowercase) → jp', () => {
    expect(getAlpha2Code('jpn')).toBe('jp');
  });

  // --- Whitespace trimming ---
  it('should trim leading/trailing whitespace — "  KOR  " → kr', () => {
    expect(getAlpha2Code('  KOR  ')).toBe('kr');
  });

  it('should trim whitespace from lowercase input "  kor  " → kr', () => {
    expect(getAlpha2Code('  kor  ')).toBe('kr');
  });

  // --- 2-letter alpha-2 inputs ---
  it('should pass through a 2-letter input as lowercase — "KR" → kr', () => {
    expect(getAlpha2Code('KR')).toBe('kr');
  });

  it('should pass through "US" → us', () => {
    expect(getAlpha2Code('US')).toBe('us');
  });

  it('should pass through lowercase "kr" → kr', () => {
    expect(getAlpha2Code('kr')).toBe('kr');
  });

  it('should pass through "DE" → de', () => {
    expect(getAlpha2Code('DE')).toBe('de');
  });

  // --- ICAO/MRTD specific codes ---
  it('should convert ICAO-specific "D" (deprecated Germany) → de', () => {
    expect(getAlpha2Code('D')).toBe('de');
  });

  it('should convert GBD (British Dependent Territories) → gb', () => {
    expect(getAlpha2Code('GBD')).toBe('gb');
  });

  it('should convert GBN (British National Overseas) → gb', () => {
    expect(getAlpha2Code('GBN')).toBe('gb');
  });

  it('should convert GBO (British Overseas Citizen) → gb', () => {
    expect(getAlpha2Code('GBO')).toBe('gb');
  });

  it('should convert GBP (British Protected Person) → gb', () => {
    expect(getAlpha2Code('GBP')).toBe('gb');
  });

  it('should convert GBS (British Subject) → gb', () => {
    expect(getAlpha2Code('GBS')).toBe('gb');
  });

  it('should convert UNA (UN Agency) → un', () => {
    expect(getAlpha2Code('UNA')).toBe('un');
  });

  it('should convert UNK (Kosovo) → xk', () => {
    expect(getAlpha2Code('UNK')).toBe('xk');
  });

  it('should convert UNO (UN Organization) → un', () => {
    expect(getAlpha2Code('UNO')).toBe('un');
  });

  it('should convert XXA (Stateless) → xx', () => {
    expect(getAlpha2Code('XXA')).toBe('xx');
  });

  it('should convert XXB (Refugee) → xx', () => {
    expect(getAlpha2Code('XXB')).toBe('xx');
  });

  it('should convert XXC (Refugee Article 1) → xx', () => {
    expect(getAlpha2Code('XXC')).toBe('xx');
  });

  it('should convert XXX (Unspecified) → xx', () => {
    expect(getAlpha2Code('XXX')).toBe('xx');
  });

  // --- Edge cases: not found ---
  it('should return empty string for an unknown 3-letter code', () => {
    expect(getAlpha2Code('ZZZ')).toBe('');
  });

  it('should return empty string for a 4-letter code', () => {
    expect(getAlpha2Code('KORE')).toBe('');
  });

  it('should return empty string for a numeric string', () => {
    expect(getAlpha2Code('123')).toBe('');
  });

  // --- Edge cases: empty / falsy ---
  it('should return empty string for an empty string', () => {
    expect(getAlpha2Code('')).toBe('');
  });

  it('should return empty string for a whitespace-only string after trim', () => {
    // '   '.trim() === '' → normalized.length === 0, mapping returns '' via ''||''
    // Technically '' is falsy so the first guard returns ''
    // But whitespace-only normalizes to '' which is NOT 2-chars → lookup returns undefined → ''
    expect(getAlpha2Code('   ')).toBe('');
  });

  // --- Idempotency ---
  it('should produce the same result on repeated calls', () => {
    expect(getAlpha2Code('KOR')).toBe(getAlpha2Code('KOR'));
  });

  it('case-normalized calls produce equal results — "KOR" === "kor"', () => {
    expect(getAlpha2Code('KOR')).toBe(getAlpha2Code('kor'));
  });
});

// ---------------------------------------------------------------------------
// getFlagSvgPath
// ---------------------------------------------------------------------------
describe('getFlagSvgPath', () => {
  it('should return the SVG path for a known alpha-3 code', () => {
    expect(getFlagSvgPath('KOR')).toBe('/svg/kr.svg');
  });

  it('should return the SVG path for USA', () => {
    expect(getFlagSvgPath('USA')).toBe('/svg/us.svg');
  });

  it('should return the SVG path for JPN', () => {
    expect(getFlagSvgPath('JPN')).toBe('/svg/jp.svg');
  });

  it('should return the SVG path for DEU', () => {
    expect(getFlagSvgPath('DEU')).toBe('/svg/de.svg');
  });

  it('should return the SVG path for GBR', () => {
    expect(getFlagSvgPath('GBR')).toBe('/svg/gb.svg');
  });

  it('should accept a 2-letter code directly — "KR" → /svg/kr.svg', () => {
    expect(getFlagSvgPath('KR')).toBe('/svg/kr.svg');
  });

  it('should accept a lowercase 2-letter code — "kr" → /svg/kr.svg', () => {
    expect(getFlagSvgPath('kr')).toBe('/svg/kr.svg');
  });

  it('should accept a lowercase 3-letter code — "kor" → /svg/kr.svg', () => {
    expect(getFlagSvgPath('kor')).toBe('/svg/kr.svg');
  });

  it('should accept mixed-case 3-letter code — "Kor" → /svg/kr.svg', () => {
    expect(getFlagSvgPath('Kor')).toBe('/svg/kr.svg');
  });

  it('should return empty string for an unknown country code', () => {
    expect(getFlagSvgPath('ZZZ')).toBe('');
  });

  it('should return empty string for an empty string', () => {
    expect(getFlagSvgPath('')).toBe('');
  });

  it('should return empty string for a 4-letter code not in mapping', () => {
    expect(getFlagSvgPath('KORE')).toBe('');
  });

  it('SVG path should start with "/svg/"', () => {
    const path = getFlagSvgPath('FRA');
    expect(path).toMatch(/^\/svg\//);
  });

  it('SVG path should end with ".svg"', () => {
    const path = getFlagSvgPath('FRA');
    expect(path).toMatch(/\.svg$/);
  });

  it('SVG filename (alpha-2 part) should be lowercase', () => {
    // /svg/fr.svg — the alpha-2 portion "fr" is lowercase
    const path = getFlagSvgPath('FRA');
    const filename = path.replace('/svg/', '').replace('.svg', '');
    expect(filename).toBe(filename.toLowerCase());
  });

  it('should handle ICAO code GBD → /svg/gb.svg', () => {
    expect(getFlagSvgPath('GBD')).toBe('/svg/gb.svg');
  });

  it('should return empty string for whitespace-only input', () => {
    expect(getFlagSvgPath('   ')).toBe('');
  });

  it('should produce the same result on repeated calls (idempotency)', () => {
    expect(getFlagSvgPath('KOR')).toBe(getFlagSvgPath('KOR'));
  });
});
