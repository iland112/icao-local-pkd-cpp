import countries from 'i18n-iso-countries';
import enLocale from 'i18n-iso-countries/langs/en.json' assert { type: 'json' };

// Register English locale
countries.registerLocale(enLocale);

// Custom mappings from the updated countryNames.ts
const CUSTOM_COUNTRY_NAMES = {
  'EU': 'European Union',
  'KS': 'Kosovo',
  'UN': 'United Nations',
  'XK': 'Kosovo',
  'XO': 'Sovereign Military Order of Malta',
  'ZZ': 'United Nations',
};

// Country codes from database
const dbCountryCodes = [
  'AD', 'AE', 'AG', 'AM', 'AO', 'AR', 'AT', 'AU', 'AZ', 'BA', 'BB', 'BD', 'BE', 'BG', 'BH', 'BJ', 'BM', 'BR', 'BS', 'BW', 'BY', 'BZ',
  'CA', 'CH', 'CI', 'CL', 'CM', 'CN', 'CO', 'CR', 'CY', 'CZ',
  'DE', 'DK', 'DM', 'DO', 'DZ',
  'EC', 'EE', 'EG', 'ES', 'ET', 'EU',
  'FI', 'FR',
  'GB', 'GE', 'GH', 'GM', 'GR', 'GU',
  'HR', 'HU',
  'ID', 'IE', 'IL', 'IN', 'IQ', 'IR', 'IS', 'IT',
  'JM', 'JO', 'JP',
  'KE', 'KG', 'KN', 'KP', 'KR', 'KS', 'KW', 'KZ',
  'LB', 'LI', 'LT', 'LU', 'LV',
  'MA', 'MC', 'MD', 'ME', 'MK', 'MN', 'MT', 'MV', 'MX', 'MY',
  'NA', 'NG', 'NO', 'NP', 'NZ',
  'OM',
  'PE', 'PH', 'PK', 'PL', 'PS', 'PT', 'PY',
  'QA',
  'RO', 'RS', 'RU', 'RW',
  'SA', 'SC', 'SE', 'SG', 'SI', 'SK', 'SL', 'SM', 'SN', 'SY',
  'TG', 'TH', 'TJ', 'TL', 'TM', 'TR', 'TW', 'TZ',
  'UA', 'UG', 'UN', 'US', 'UY', 'UZ',
  'VA', 'VC', 'VN',
  'XO',
  'YE',
  'ZW', 'ZZ'
];

const unmatchedCodes = [];

console.log('Checking country codes with updated mappings...\n');

dbCountryCodes.forEach(code => {
  // Check if it's in custom mappings
  if (CUSTOM_COUNTRY_NAMES[code]) {
    console.log(`${code}: ${CUSTOM_COUNTRY_NAMES[code]} (CUSTOM)`);
    return;
  }

  // Check if i18n-iso-countries has it
  const name = countries.getName(code, 'en');
  if (name) {
    console.log(`${code}: ${name}`);
  } else {
    console.log(`${code}: NOT FOUND ❌`);
    unmatchedCodes.push(code);
  }
});

console.log('\n' + '='.repeat(50));
console.log('SUMMARY');
console.log('='.repeat(50));
console.log(`Total codes: ${dbCountryCodes.length}`);
console.log(`Matched codes: ${dbCountryCodes.length - unmatchedCodes.length}`);
console.log(`Unmatched codes: ${unmatchedCodes.length}`);

if (unmatchedCodes.length > 0) {
  console.log('\n❌ Codes that still need custom mapping:');
  unmatchedCodes.forEach(code => {
    console.log(`  - ${code}`);
  });
} else {
  console.log('\n✅ All country codes are now properly mapped!');
}
