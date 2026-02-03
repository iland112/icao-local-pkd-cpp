/**
 * ISO 3166-1 alpha-2 country code to country name mapping
 * Uses i18n-iso-countries library for comprehensive coverage
 */
import countries from 'i18n-iso-countries';
import enLocale from 'i18n-iso-countries/langs/en.json';

// Register English locale
countries.registerLocale(enLocale);

/**
 * Custom country code overrides
 * For non-standard codes or special cases
 */
const CUSTOM_COUNTRY_NAMES: Record<string, string> = {
  'EU': 'European Union',  // EU Laissez-Passer
  'KS': 'Kosovo',  // Kosovo (alternative code)
  'UN': 'United Nations',  // UN Laissez-Passer
  'XK': 'Kosovo',  // Kosovo (non-standard code)
  'XO': 'Sovereign Military Order of Malta',  // SMOM (몰타 기사단)
  'ZZ': 'United Nations',  // United Nations (alternative code)
};

/**
 * Get formatted country name with code
 * @param code ISO 3166-1 alpha-2 country code
 * @returns Formatted string "CODE - Country Name" or just the code if not found
 */
export function getCountryDisplayName(code: string): string {
  // Check custom mappings first
  if (CUSTOM_COUNTRY_NAMES[code]) {
    return `${code} - ${CUSTOM_COUNTRY_NAMES[code]}`;
  }

  // Get name from library
  const name = countries.getName(code, 'en');
  return name ? `${code} - ${name}` : code;
}

/**
 * Get just the country name
 * @param code ISO 3166-1 alpha-2 country code
 * @returns Country name or the code if not found
 */
export function getCountryName(code: string): string {
  // Check custom mappings first
  if (CUSTOM_COUNTRY_NAMES[code]) {
    return CUSTOM_COUNTRY_NAMES[code];
  }

  // Get name from library
  return countries.getName(code, 'en') || code;
}

/**
 * Get all country codes with names
 * @returns Record of country code to name mappings
 */
export function getAllCountries(): Record<string, string> {
  const allCountries = countries.getNames('en');
  return { ...allCountries, ...CUSTOM_COUNTRY_NAMES };
}
