import { getFlagSvgPath } from '@/utils/countryCode';
import { getCountryName } from '@/utils/countryNames';

interface CountryFlagProps {
  /** ISO 3166-1 alpha-2 or alpha-3 country code */
  code: string | undefined | null;
  /** Show country code text next to the flag (default: true) */
  showCode?: boolean;
  /** Flag image size preset */
  size?: 'xs' | 'sm' | 'md';
  /** Additional className for the wrapper span */
  className?: string;
}

const sizeMap = {
  xs: 'w-3.5 h-2.5',
  sm: 'w-4 h-3',
  md: 'w-5 h-3.5',
};

/**
 * Country flag icon + code with full country name tooltip on hover.
 * Drop-in replacement for inline flag img + country code patterns.
 */
export function CountryFlag({ code, showCode = true, size = 'sm', className }: CountryFlagProps) {
  if (!code) return null;

  const flagPath = getFlagSvgPath(code);
  const countryName = getCountryName(code);
  const tooltip = countryName !== code ? `${code} — ${countryName}` : code;

  return (
    <span className={`inline-flex items-center gap-1 ${className ?? ''}`} title={tooltip}>
      {flagPath && (
        <img
          src={flagPath}
          alt={code}
          className={`${sizeMap[size]} object-cover rounded-sm`}
          onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }}
        />
      )}
      {showCode && <span>{code}</span>}
    </span>
  );
}
