import { useTranslation } from 'react-i18next';
import {
  Award,
  XCircle,
  AlertTriangle,
  Clock,
  Globe,
  IdCard,
  ExternalLink,
  ShieldAlert,
} from 'lucide-react';
import type { PAVerificationResponse, CertificateChainValidationDto } from '@/types';
import { cn } from '@/utils/cn';
import { formatDateTime } from '@/utils/dateFormat';
import { Link } from 'react-router-dom';
import { getFlagSvgPath, getAlpha2Code } from '@/utils/countryCode';
import countries from 'i18n-iso-countries';
import ko from 'i18n-iso-countries/langs/ko.json';

countries.registerLocale(ko);

const getCountryName = (code: string): string => {
  const alpha2 = code.length === 2 ? code : getAlpha2Code(code);
  return countries.getName(alpha2?.toUpperCase() || code, 'ko') || code;
};

/** Extract country code from issuer DN (/C=XX/...) */
const extractCountryFromDn = (dn: string): string | null => {
  const match = dn.match(/\/C=([A-Za-z]{2,3})\b/i) || dn.match(/C=([A-Za-z]{2,3})\b/i);
  return match ? match[1].toUpperCase() : null;
};

/** Render structured error message for trust chain failures */
function TrustChainErrorDetail({ chainValidation }: { chainValidation: CertificateChainValidationDto }) {
  const { t } = useTranslation(['pa', 'common']);
  const errorCode = chainValidation.errorCode;
  const issuerDn = chainValidation.dscIssuer || '';
  const countryCode = extractCountryFromDn(issuerDn);

  const errorMessages: Record<string, { title: string; description: string }> = {
    CSCA_NOT_FOUND: {
      title: t('pa:steps.cscaNotRegisteredTitle'),
      description: t('pa:steps.cscaNotInPkd'),
    },
    CSCA_DN_MISMATCH: {
      title: t('pa:steps.cscaDnMismatch'),
      description: t('pa:steps.cscaDnMismatchDesc'),
    },
    CSCA_SELF_SIGNATURE_FAILED: {
      title: t('pa:steps.cscaSelfSignFailed'),
      description: t('pa:result.cscaSelfSignFailedDetail'),
    },
  };

  const errorInfo = errorCode ? errorMessages[errorCode] : null;

  if (!errorInfo) {
    // Fallback: raw validationErrors
    return (
      <div className="flex items-center gap-2 text-sm opacity-90">
        <XCircle className="w-3.5 h-3.5 shrink-0" />
        <span>{t('pa:result.trustChainFailed')}{chainValidation.validationErrors ? ` — ${chainValidation.validationErrors}` : ''}</span>
      </div>
    );
  }

  return (
    <div className="space-y-1.5">
      <div className="flex items-start gap-2 text-sm opacity-90">
        <ShieldAlert className="w-4 h-4 shrink-0 mt-0.5" />
        <div>
          <div className="flex items-center gap-1.5 font-semibold">
            {countryCode && getFlagSvgPath(countryCode) && (
              <img
                src={getFlagSvgPath(countryCode)}
                alt={countryCode}
                className="w-5 h-3.5 object-cover rounded-sm border border-white/30"
                onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }}
              />
            )}
            <span>
              {countryCode ? `${getCountryName(countryCode)} (${countryCode})` : ''} {errorInfo.title}
            </span>
          </div>
          <p className="text-xs opacity-80 mt-0.5">{errorInfo.description}</p>
        </div>
      </div>
      {issuerDn && (errorCode === 'CSCA_NOT_FOUND' || errorCode === 'CSCA_DN_MISMATCH') && (
        <div className="ml-6 p-2 bg-black/15 rounded-lg text-xs">
          <span className="opacity-70">DSC Issuer DN: </span>
          <code className="font-mono break-all opacity-90">{issuerDn}</code>
        </div>
      )}
    </div>
  );
}

// DG1 MRZ parse result type
export interface DG1ParseResult {
  success: boolean;
  documentType?: string;
  issuingCountry?: string;
  surname?: string;
  givenNames?: string;
  fullName?: string;
  documentNumber?: string;
  nationality?: string;
  dateOfBirth?: string;
  sex?: string;
  dateOfExpiry?: string;
  mrzLine1?: string;
  mrzLine2?: string;
  mrzFull?: string;
  error?: string;
}

// DG2 Face parse result type
export interface DG2ParseResult {
  success: boolean;
  faceCount?: number;
  faceImages?: Array<{
    index: number;
    imageFormat: string;
    imageSize: number;
    width?: number;
    height?: number;
    imageDataUrl?: string;
  }>;
  hasFacContainer?: boolean;
  error?: string;
}

interface VerificationResultCardProps {
  result: PAVerificationResponse;
  dg1ParseResult: DG1ParseResult | null;
  dg2ParseResult: DG2ParseResult | null;
}

export function VerificationResultCard({
  result,
  dg1ParseResult,
  dg2ParseResult,
}: VerificationResultCardProps) {
  const { t } = useTranslation(['pa', 'common']);
  return (
    <div className={cn(
      'rounded-2xl p-4',
      result.status === 'VALID'
        ? 'bg-gradient-to-r from-emerald-600/80 to-teal-600/80 text-white'
        : result.status === 'INVALID'
        ? 'bg-gradient-to-r from-rose-600/80 to-red-700/80 text-white'
        : 'bg-gradient-to-r from-amber-600/80 to-orange-600/80 text-white'
    )}>
      <div className="flex items-center gap-3">
        {result.status === 'VALID' ? (
          <Award className="w-10 h-10" />
        ) : result.status === 'INVALID' ? (
          <XCircle className="w-10 h-10" />
        ) : (
          <AlertTriangle className="w-10 h-10" />
        )}
        <div className="flex-grow">
          <h2 className="text-lg font-bold">
            {result.status === 'VALID'
              ? t('pa:result.passed')
              : result.status === 'INVALID'
              ? t('pa:result.failed')
              : t('pa:result.verificationError')}
          </h2>
          <div className="flex items-center gap-4 mt-0.5 text-sm opacity-90">
            <span className="flex items-center gap-1">
              <Clock className="w-3.5 h-3.5" />
              {result.processingDurationMs}ms
            </span>
            {result.issuingCountry && (
              <span className="flex items-center gap-1">
                {getFlagSvgPath(result.issuingCountry) ? (
                  <img
                    src={getFlagSvgPath(result.issuingCountry)}
                    alt={result.issuingCountry}
                    className="w-5 h-3.5 object-cover rounded-sm border border-white/30"
                    onError={(e) => {
                      const img = e.target as HTMLImageElement;
                      img.style.display = 'none';
                      img.nextElementSibling?.classList.remove('hidden');
                    }}
                  />
                ) : null}
                <Globe className={`w-3.5 h-3.5 ${getFlagSvgPath(result.issuingCountry) ? 'hidden' : ''}`} />
                {getCountryName(result.issuingCountry)} ({result.issuingCountry})
              </span>
            )}
            {result.documentNumber && (
              <span className="flex items-center gap-1">
                <IdCard className="w-3.5 h-3.5" />
                {result.documentNumber}
              </span>
            )}
          </div>
        </div>
        <Link
          to={`/pa/history?id=${result.verificationId}`}
          className="flex items-center gap-1 text-xs opacity-80 hover:opacity-100 underline shrink-0"
        >
          <ExternalLink className="w-3.5 h-3.5" />
          {t('common:button.viewDetail')}
        </Link>
      </div>

      {/* Failure reasons */}
      {result.status === 'INVALID' && (
        <div className="mt-3 pt-3 border-t border-white/20 space-y-1.5">
          <div className="text-xs font-semibold opacity-90">{t('common:error.failReason')}</div>
          {!result.certificateChainValidation?.valid && result.certificateChainValidation && (
            <TrustChainErrorDetail chainValidation={result.certificateChainValidation} />
          )}
          {!result.sodSignatureValidation?.valid && (
            <div className="flex items-center gap-2 text-sm opacity-90">
              <XCircle className="w-3.5 h-3.5 shrink-0" />
              <span>{t('pa:result.sodSignatureFailed')}{result.sodSignatureValidation?.validationErrors ? ` \u2014 ${result.sodSignatureValidation.validationErrors}` : ''}</span>
            </div>
          )}
          {result.dataGroupValidation && result.dataGroupValidation.invalidGroups > 0 && (
            <div className="flex items-center gap-2 text-sm opacity-90">
              <XCircle className="w-3.5 h-3.5 shrink-0" />
              <span>{t('pa:result.dgHashMismatch', { invalid: result.dataGroupValidation.invalidGroups, total: result.dataGroupValidation.totalGroups })}</span>
            </div>
          )}
          {result.certificateChainValidation?.revoked && (
            <div className="flex items-center gap-2 text-sm opacity-90">
              <XCircle className="w-3.5 h-3.5 shrink-0" />
              <span>{t('pa:result.certRevoked')}</span>
            </div>
          )}
        </div>
      )}

      {/* Non-Conformant DSC warning (shown regardless of VALID/INVALID) */}
      {result.certificateChainValidation?.dscNonConformant && (
        <div className="mt-3 pt-3 border-t border-white/20">
          <div className="flex items-center gap-2 text-sm">
            <AlertTriangle className="w-4 h-4 text-amber-300 shrink-0" />
            <span className="font-semibold text-amber-200">Non-Conformant DSC</span>
          </div>
          <p className="mt-1 text-xs opacity-80">
            {result.certificateChainValidation.pkdConformanceCode && (
              <span className="font-mono">{result.certificateChainValidation.pkdConformanceCode}: </span>
            )}
            {result.certificateChainValidation.pkdConformanceText || t('pa:result.icaoNonConformant')}
          </p>
        </div>
      )}

      {/* DG Parsing Results (shown when verification succeeds) */}
      {result.status === 'VALID' && (dg1ParseResult || dg2ParseResult) && (
        <div className="mt-3 pt-3 border-t border-white/20">
          <div className="flex gap-3">
            {/* DG2 Face Image */}
            {dg2ParseResult?.success && dg2ParseResult.faceImages?.[0] && (
              <div className="shrink-0">
                <img
                  src={dg2ParseResult.faceImages[0].imageDataUrl}
                  alt="Passport Face"
                  className="w-20 h-26 object-cover rounded-lg border-2 border-white/40 shadow-md"
                />
              </div>
            )}
            {/* DG1 MRZ Data */}
            {dg1ParseResult?.success && (
              <div className="flex-grow grid grid-cols-3 gap-x-4 gap-y-1 text-xs">
                <div>
                  <span className="opacity-70">{t('common:label.fullName')}</span>
                  <div className="font-semibold">{dg1ParseResult.fullName}</div>
                </div>
                <div>
                  <span className="opacity-70">{t('pa:result.documentNumber')}</span>
                  <div className="font-mono font-semibold">{dg1ParseResult.documentNumber}</div>
                </div>
                <div>
                  <span className="opacity-70">{t('common:label.nationality')}</span>
                  <div className="font-semibold">{dg1ParseResult.nationality}</div>
                </div>
                <div>
                  <span className="opacity-70">{t('common:label.dateOfBirth')}</span>
                  <div className="font-mono font-semibold">{dg1ParseResult.dateOfBirth}</div>
                </div>
                <div>
                  <span className="opacity-70">{t('common:label.expiryDate')}</span>
                  <div className="font-mono font-semibold">{dg1ParseResult.dateOfExpiry}</div>
                </div>
                <div>
                  <span className="opacity-70">{t('common:label.gender')}</span>
                  <div className="font-semibold">
                    {dg1ParseResult.sex === 'M' ? t('pa:result.male') : dg1ParseResult.sex === 'F' ? t('pa:result.female') : dg1ParseResult.sex}
                  </div>
                </div>
              </div>
            )}
          </div>
        </div>
      )}

      {/* Verification ID & Timestamp */}
      <div className="mt-3 pt-2 border-t border-white/20 text-xs flex flex-wrap gap-4 opacity-75">
        <div>
          <span>{t('pa:result.verificationId')}: </span>
          <span className="font-mono">{result.verificationId}</span>
        </div>
        <div>
          <span>{t('pa:result.verificationTimestamp')}: </span>
          <span>{formatDateTime(result.verificationTimestamp)}</span>
        </div>
      </div>
    </div>
  );
}

export default VerificationResultCard;
