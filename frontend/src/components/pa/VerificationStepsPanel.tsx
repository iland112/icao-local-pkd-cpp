import { useTranslation } from 'react-i18next';
import {
  CheckCircle,
  XCircle,
  AlertTriangle,
  Loader2,
  ListChecks,
  ChevronDown,
  ChevronRight,
  Award,
  FileKey,
  FileText,
  Hash,
  ShieldAlert,
} from 'lucide-react';
import { cn } from '@/utils/cn';
import { getFlagSvgPath, getAlpha2Code } from '@/utils/countryCode';
import countries from 'i18n-iso-countries';
import ko from 'i18n-iso-countries/langs/ko.json';

countries.registerLocale(ko);

const getCountryName = (code: string): string => {
  const alpha2 = code.length === 2 ? code : getAlpha2Code(code);
  return countries.getName(alpha2?.toUpperCase() || code, 'ko') || code;
};

const extractCountryFromDn = (dn: string): string | null => {
  const match = dn.match(/\/C=([A-Za-z]{2,3})\b/i) || dn.match(/C=([A-Za-z]{2,3})\b/i);
  return match ? match[1].toUpperCase() : null;
};

const ERROR_MESSAGES: Record<string, { titleKey: string; descriptionKey: string }> = {
  CSCA_NOT_FOUND: {
    titleKey: 'steps.cscaNotRegisteredTitle',
    descriptionKey: 'steps.cscaNotInPkd',
  },
  CSCA_DN_MISMATCH: {
    titleKey: 'steps.cscaDnMismatch',
    descriptionKey: 'steps.cscaDnMismatchDesc',
  },
  CSCA_SELF_SIGNATURE_FAILED: {
    titleKey: 'steps.cscaSelfSignFailed',
    descriptionKey: 'steps.cscaSelfSignFailedDesc',
  },
};

// Verification Step 상태 타입
export type StepStatus = 'pending' | 'running' | 'success' | 'warning' | 'error';

// eslint-disable-next-line @typescript-eslint/no-explicit-any
export type StepDetails = Record<string, any>;

export interface VerificationStep {
  id: number;
  title: string;
  description: string;
  status: StepStatus;
  message?: string;
  details?: StepDetails;
  expanded?: boolean;
}

// DG 해시 상세 타입
interface DGHashDetail {
  valid: boolean;
  expectedHash: string;
  actualHash: string;
}

export function getStepStatusIcon(status: StepStatus) {
  switch (status) {
    case 'success':
      return <CheckCircle className="w-5 h-5 text-green-500" />;
    case 'error':
      return <XCircle className="w-5 h-5 text-red-500" />;
    case 'warning':
      return <AlertTriangle className="w-5 h-5 text-yellow-500" />;
    case 'running':
      return <Loader2 className="w-5 h-5 text-blue-500 animate-spin" />;
    default:
      return <div className="w-5 h-5 rounded-full border-2 border-gray-300 dark:border-gray-600" />;
  }
}

export function getStepBgColor(status: StepStatus) {
  switch (status) {
    case 'success':
      return 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800';
    case 'error':
      return 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800';
    case 'warning':
      return 'bg-yellow-50 dark:bg-yellow-900/20 border-yellow-200 dark:border-yellow-800';
    case 'running':
      return 'bg-blue-50 dark:bg-blue-900/20 border-blue-200 dark:border-blue-800';
    default:
      return 'bg-gray-50 dark:bg-gray-700/50 border-gray-200 dark:border-gray-700';
  }
}

interface VerificationStepsPanelProps {
  steps: VerificationStep[];
  expandedSteps: Set<number>;
  toggleStep: (stepId: number) => void;
}

export function VerificationStepsPanel({
  steps,
  expandedSteps,
  toggleStep,
}: VerificationStepsPanelProps) {
  const { t } = useTranslation(['pa', 'common']);

  return (
    <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg overflow-hidden">
      <div className="px-5 py-4 bg-gradient-to-r from-indigo-500 to-purple-500">
        <div className="flex items-center gap-3">
          <ListChecks className="w-6 h-6 text-white" />
          <h2 className="text-lg font-bold text-white">{t('pa:steps.panelTitle')}</h2>
        </div>
      </div>
      <div className="p-4 space-y-2">
        {steps.map((step: VerificationStep) => (
          <div
            key={step.id}
            className={cn(
              'rounded-xl border transition-all duration-200',
              getStepBgColor(step.status)
            )}
          >
            {/* Step Header */}
            <div
              className="flex items-center gap-3 p-3 cursor-pointer"
              onClick={() => toggleStep(step.id)}
            >
              {getStepStatusIcon(step.status)}
              <div className="flex-grow">
                <h3 className="font-semibold text-gray-900 dark:text-white text-sm">{step.title}</h3>
                <p className="text-xs text-gray-500 dark:text-gray-400">{step.description}</p>
              </div>
              {step.message && (
                expandedSteps.has(step.id) ? (
                  <ChevronDown className="w-4 h-4 text-gray-400" />
                ) : (
                  <ChevronRight className="w-4 h-4 text-gray-400" />
                )
              )}
            </div>

            {/* Step Details (Expanded) */}
            {expandedSteps.has(step.id) && step.message && (
              <div className="px-4 pb-3 pt-0">
                {step.message ? (
                  <div className={cn(
                    'text-sm mb-2',
                    step.status === 'success' ? 'text-green-700 dark:text-green-400' :
                    step.status === 'error' ? 'text-red-700 dark:text-red-400' :
                    step.status === 'warning' ? 'text-yellow-700 dark:text-yellow-400' :
                    'text-gray-600 dark:text-gray-400'
                  )}>
                    {step.message}
                  </div>
                ) : null}



                {/* Step 1: SOD parsing details */}
                {step.id === 1 && step.details && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-2">
                    <div className="grid grid-cols-2 gap-2">
                      <div>
                        <span className="text-gray-500">{t('pa:steps.hashAlgorithm')}</span>
                        <code className="ml-1 font-mono bg-blue-100 dark:bg-blue-900/30 px-1.5 py-0.5 rounded text-blue-700 dark:text-blue-300">
                          {String(step.details.hashAlgorithm || '')}
                        </code>
                      </div>
                      <div>
                        <span className="text-gray-500">{t('pa:steps.signatureAlgorithm')}</span>
                        <code className="ml-1 font-mono bg-purple-100 dark:bg-purple-900/30 px-1.5 py-0.5 rounded text-purple-700 dark:text-purple-300">
                          {String(step.details.signatureAlgorithm || '')}
                        </code>
                      </div>
                    </div>
                  </div>
                )}

                {/* Step 2: DSC extraction details */}
                {step.id === 2 && step.details && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-1.5">
                    <div className="flex items-start gap-2">
                      <span className="text-gray-500 shrink-0 w-16">{t('pa:steps.subject')}:</span>
                      <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                        {step.details.subject || ''}
                      </code>
                    </div>
                    <div className="flex items-start gap-2">
                      <span className="text-gray-500 shrink-0 w-16">{t('pa:steps.serialNumber')}:</span>
                      <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded">
                        {step.details.serial || ''}
                      </code>
                    </div>
                    <div className="flex items-start gap-2">
                      <span className="text-gray-500 shrink-0 w-16">{t('pa:steps.issuer')}:</span>
                      <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                        {step.details.issuer || ''}
                      </code>
                    </div>
                  </div>
                )}

                {/* Step 3: Trust Chain verification details (success) */}
                {step.id === 3 && step.details && step.status !== 'error' && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs">
                    <div className="font-semibold text-gray-700 dark:text-gray-300 mb-2">{t('pa:steps.trustChainPath')}</div>
                    <div className="flex flex-col items-center gap-1">
                      {/* CSCA (Root) */}
                      <div className="w-full p-2 bg-green-100 dark:bg-green-900/30 rounded border border-green-300 dark:border-green-700">
                        <div className="flex items-center gap-2">
                          <Award className="w-4 h-4 text-green-600 dark:text-green-400" />
                          <span className="font-semibold text-green-700 dark:text-green-300">CSCA (Root)</span>
                        </div>
                        <code className="block mt-1 text-xs font-mono break-all text-gray-600 dark:text-gray-400">
                          {step.details.cscaSubject}
                        </code>
                      </div>
                      {/* Arrow */}
                      <div className="text-gray-400 dark:text-gray-500">&#8595; {t('pa:steps.signature')}</div>
                      {/* DSC (Leaf) */}
                      <div className="w-full p-2 bg-blue-100 dark:bg-blue-900/30 rounded border border-blue-300 dark:border-blue-700">
                        <div className="flex items-center gap-2">
                          <FileKey className="w-4 h-4 text-blue-600 dark:text-blue-400" />
                          <span className="font-semibold text-blue-700 dark:text-blue-300">DSC (Document Signer)</span>
                          {step.details.dscExpired && (
                            <span className="px-1.5 py-0.5 rounded text-[10px] font-medium bg-orange-200 dark:bg-orange-900/50 text-orange-700 dark:text-orange-300">
                              {t('pa:steps.expired')}
                            </span>
                          )}
                        </div>
                        <code className="block mt-1 text-xs font-mono break-all text-gray-600 dark:text-gray-400">
                          {step.details.dscSubject}
                        </code>
                        {step.details.notBefore && step.details.notAfter && (
                          <div className="mt-1 text-xs text-gray-500">
                            {t('pa:steps.validityPeriod')}: {step.details.notBefore} ~ {step.details.notAfter}
                          </div>
                        )}
                      </div>

                      {/* Certificate Expiration Warning/Info */}
                      {step.details.expirationStatus && step.details.expirationStatus !== 'VALID' && (
                        <div className={cn(
                          'w-full mt-2 p-2 rounded text-xs',
                          step.details.expirationStatus === 'EXPIRED'
                            ? 'bg-orange-100 dark:bg-orange-900/30 border border-orange-300 dark:border-orange-700'
                            : 'bg-yellow-100 dark:bg-yellow-900/30 border border-yellow-300 dark:border-yellow-700'
                        )}>
                          <div className="flex items-center gap-2">
                            <AlertTriangle className={cn(
                              'w-4 h-4',
                              step.details.expirationStatus === 'EXPIRED'
                                ? 'text-orange-600 dark:text-orange-400'
                                : 'text-yellow-600 dark:text-yellow-400'
                            )} />
                            <span className={cn(
                              'font-semibold',
                              step.details.expirationStatus === 'EXPIRED'
                                ? 'text-orange-700 dark:text-orange-300'
                                : 'text-yellow-700 dark:text-yellow-300'
                            )}>
                              {step.details.expirationStatus === 'EXPIRED' ? t('pa:steps.certExpired') : t('pa:steps.certExpiringSoon')}
                            </span>
                            {step.details.validAtSigningTime && (
                              <span className="px-1.5 py-0.5 rounded text-[10px] font-medium bg-green-200 dark:bg-green-900/50 text-green-700 dark:text-green-300">
                                &#10003; {t('pa:steps.validAtSigningTime')}
                              </span>
                            )}
                          </div>
                          {step.details.expirationMessage && (
                            <p className="mt-1 text-gray-600 dark:text-gray-400">
                              {step.details.expirationMessage}
                            </p>
                          )}
                        </div>
                      )}
                      {/* DSC Non-Conformant Warning (ICAO PKD nc-data) */}
                      {step.details.dscNonConformant && (
                        <div className="w-full mt-2 p-2 rounded text-xs bg-amber-100 dark:bg-amber-900/30 border border-amber-300 dark:border-amber-700">
                          <div className="flex items-center gap-2">
                            <AlertTriangle className="w-4 h-4 text-amber-600 dark:text-amber-400" />
                            <span className="font-semibold text-amber-700 dark:text-amber-300">{t('pa:steps.nonConformantDsc')}</span>
                          </div>
                          {step.details.pkdConformanceCode && (
                            <p className="mt-1 text-gray-600 dark:text-gray-400">
                              <span className="font-mono font-medium">{step.details.pkdConformanceCode}</span>
                              {step.details.pkdConformanceText && (
                                <span> &mdash; {step.details.pkdConformanceText}</span>
                              )}
                            </p>
                          )}
                        </div>
                      )}
                    </div>
                  </div>
                )}
                {/* Step 3: Trust Chain verification details (failure) */}
                {step.id === 3 && step.status === 'error' && step.details && (
                  <div className="mt-2 space-y-2">
                    {/* Target certificate info */}
                    {(step.details.dscSubject || step.details.cscaSubject) && (
                      <div className="p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-1.5">
                        <div className="font-semibold text-gray-700 dark:text-gray-300 mb-1">{t('pa:steps.targetCertificate')}:</div>
                        {step.details.dscSubject && (
                          <div className="flex items-start gap-2">
                            <span className="text-gray-500 shrink-0 w-12">DSC:</span>
                            <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                              {step.details.dscSubject}
                            </code>
                          </div>
                        )}
                        {step.details.cscaSubject && (
                          <div className="flex items-start gap-2">
                            <span className="text-gray-500 shrink-0 w-12">CSCA:</span>
                            <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                              {step.details.cscaSubject}
                            </code>
                          </div>
                        )}
                        {step.details.notBefore && step.details.notAfter && (
                          <div className="flex items-start gap-2">
                            <span className="text-gray-500 shrink-0 w-12">{t('pa:steps.validityPeriod')}:</span>
                            <span className="text-gray-600 dark:text-gray-400">
                              {step.details.notBefore} ~ {step.details.notAfter}
                            </span>
                          </div>
                        )}
                      </div>
                    )}
                    {/* Failure reason */}
                    {(() => {
                      const errorCode = step.details.errorCode as string | undefined;
                      const errorInfo = errorCode ? ERROR_MESSAGES[errorCode] : null;
                      const issuerDn = (step.details.dscIssuer || '') as string;
                      const cc = extractCountryFromDn(issuerDn || (step.details.dscSubject as string || ''));

                      return errorInfo ? (
                        <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg text-xs space-y-2">
                          <div className="flex items-start gap-2">
                            <ShieldAlert className="w-4 h-4 text-red-500 dark:text-red-400 shrink-0 mt-0.5" />
                            <div>
                              <div className="flex items-center gap-1.5 font-semibold text-red-700 dark:text-red-300">
                                {cc && getFlagSvgPath(cc) && (
                                  <img
                                    src={getFlagSvgPath(cc)}
                                    alt={cc}
                                    className="w-5 h-3.5 object-cover rounded-sm border border-gray-300 dark:border-gray-600"
                                    onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }}
                                  />
                                )}
                                {cc ? `${getCountryName(cc)} (${cc})` : ''} {t(`pa:${errorInfo.titleKey}`)}
                              </div>
                              <p className="text-gray-600 dark:text-gray-400 mt-0.5">{t(`pa:${errorInfo.descriptionKey}`)}</p>
                            </div>
                          </div>
                          {issuerDn && (errorCode === 'CSCA_NOT_FOUND' || errorCode === 'CSCA_DN_MISMATCH') && (
                            <div className="ml-6 p-2 bg-gray-100 dark:bg-gray-700/50 rounded text-xs">
                              <span className="text-gray-500">DSC Issuer DN: </span>
                              <code className="font-mono break-all text-gray-700 dark:text-gray-300">{issuerDn}</code>
                            </div>
                          )}
                        </div>
                      ) : (
                        <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg text-xs">
                          <div className="flex items-start gap-2 text-red-600 dark:text-red-400">
                            <XCircle className="w-4 h-4 shrink-0 mt-0.5" />
                            <div>
                              <span className="font-semibold">{t('common:error.failReason')}</span>
                              {step.details.error
                                ? String(step.details.error)
                                : t('pa:steps.dscNotSignedByCsca')}
                            </div>
                          </div>
                        </div>
                      );
                    })()}
                  </div>
                )}

                {/* Step 4: CSCA lookup details */}
                {step.id === 4 && step.details && step.status === 'success' && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs">
                    <div className="flex items-start gap-2">
                      <span className="text-gray-500 shrink-0">CSCA DN:</span>
                      <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                        {step.details.dn || ''}
                      </code>
                    </div>
                  </div>
                )}
                {step.id === 4 && step.status === 'error' && step.details && (() => {
                  const errorCode = step.details.errorCode as string | undefined;
                  const errorInfo = errorCode ? ERROR_MESSAGES[errorCode] : null;
                  const issuerDn = (step.details.dscIssuer || step.details.dscSubject || '') as string;
                  const countryCode = extractCountryFromDn(issuerDn);

                  return (
                    <div className="mt-2 p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg text-xs space-y-2">
                      {errorInfo ? (
                        <>
                          <div className="flex items-start gap-2">
                            <ShieldAlert className="w-4 h-4 text-red-500 dark:text-red-400 shrink-0 mt-0.5" />
                            <div>
                              <div className="flex items-center gap-1.5 font-semibold text-red-700 dark:text-red-300">
                                {countryCode && getFlagSvgPath(countryCode) && (
                                  <img
                                    src={getFlagSvgPath(countryCode)}
                                    alt={countryCode}
                                    className="w-5 h-3.5 object-cover rounded-sm border border-gray-300 dark:border-gray-600"
                                    onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }}
                                  />
                                )}
                                {countryCode ? `${getCountryName(countryCode)} (${countryCode})` : ''} {t(`pa:${errorInfo.titleKey}`)}
                              </div>
                              <p className="text-gray-600 dark:text-gray-400 mt-0.5">{t(`pa:${errorInfo.descriptionKey}`)}</p>
                            </div>
                          </div>
                          {issuerDn && (errorCode === 'CSCA_NOT_FOUND' || errorCode === 'CSCA_DN_MISMATCH') && (
                            <div className="ml-6 p-2 bg-gray-100 dark:bg-gray-700/50 rounded text-xs">
                              <span className="text-gray-500">DSC Issuer DN: </span>
                              <code className="font-mono break-all text-gray-700 dark:text-gray-300">{issuerDn}</code>
                            </div>
                          )}
                        </>
                      ) : (
                        <>
                          {step.details.dscSubject && (
                            <div className="flex items-start gap-2 text-gray-600 dark:text-gray-400">
                              <span className="text-gray-500 shrink-0">{t('pa:steps.dscIssuer')}:</span>
                              <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                                {step.details.dscSubject}
                              </code>
                            </div>
                          )}
                          {step.details.error && (
                            <div className="text-red-600 dark:text-red-400">
                              <span className="font-semibold">{t('pa:steps.cause')}:</span> {String(step.details.error)}
                            </div>
                          )}
                        </>
                      )}
                    </div>
                  );
                })()}

                {/* Step 5: SOD signature verification details (success) */}
                {step.id === 5 && step.status === 'success' && step.details && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs">
                    <div className="grid grid-cols-2 gap-2">
                      {step.details.signatureAlgorithm && (
                        <div>
                          <span className="text-gray-500">{t('pa:steps.signatureAlgorithm')}</span>
                          <code className="ml-1 font-mono bg-purple-100 dark:bg-purple-900/30 px-1.5 py-0.5 rounded text-purple-700 dark:text-purple-300">
                            {String(step.details.signatureAlgorithm)}
                          </code>
                        </div>
                      )}
                      {step.details.hashAlgorithm && (
                        <div>
                          <span className="text-gray-500">{t('pa:steps.hashAlgorithm')}</span>
                          <code className="ml-1 font-mono bg-blue-100 dark:bg-blue-900/30 px-1.5 py-0.5 rounded text-blue-700 dark:text-blue-300">
                            {String(step.details.hashAlgorithm)}
                          </code>
                        </div>
                      )}
                    </div>
                  </div>
                )}
                {/* Step 5: SOD signature verification details (failure) */}
                {step.id === 5 && step.status === 'error' && (
                  <div className="mt-2 space-y-2">
                    {step.details && (step.details.signatureAlgorithm || step.details.hashAlgorithm) && (
                      <div className="p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs">
                        <div className="grid grid-cols-2 gap-2">
                          {step.details.signatureAlgorithm && (
                            <div>
                              <span className="text-gray-500">{t('pa:steps.signatureAlgorithm')}</span>
                              <code className="ml-1 font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded">
                                {String(step.details.signatureAlgorithm)}
                              </code>
                            </div>
                          )}
                          {step.details.hashAlgorithm && (
                            <div>
                              <span className="text-gray-500">{t('pa:steps.hashAlgorithm')}</span>
                              <code className="ml-1 font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded">
                                {String(step.details.hashAlgorithm)}
                              </code>
                            </div>
                          )}
                        </div>
                      </div>
                    )}
                    <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg text-xs">
                      <div className="flex items-start gap-2 text-red-600 dark:text-red-400">
                        <XCircle className="w-4 h-4 shrink-0 mt-0.5" />
                        <div>
                          <span className="font-semibold">{t('common:error.failReason')}</span>
                          {step.details?.error
                            ? String(step.details.error)
                            : t('pa:steps.sodSignatureNotVerified')}
                        </div>
                      </div>
                    </div>
                  </div>
                )}

                {/* Step 6: DG hash verification details */}
                {step.id === 6 && step.details?.dgDetails ? (() => {
                  const dgDetails = step.details.dgDetails as Record<string, DGHashDetail>;
                  return (
                    <div className="space-y-2 mt-2">
                      {Object.entries(dgDetails).map(([dgName, detail]) => (
                        <div key={dgName} className={cn(
                          'p-3 rounded-lg text-xs',
                          detail.valid
                            ? 'bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800'
                            : 'bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
                        )}>
                          <div className="flex items-center justify-between mb-2">
                            <div className="flex items-center gap-2">
                              <span className="font-semibold text-sm">{dgName}</span>
                              {dgName === 'DG1' ? <span className="text-gray-500">{t('pa:steps.dgDescMrz')}</span> : null}
                              {dgName === 'DG2' ? <span className="text-gray-500">{t('pa:steps.dgDescFace')}</span> : null}
                              {dgName === 'DG3' ? <span className="text-gray-500">{t('pa:steps.dgDescFingerprint')}</span> : null}
                              {dgName === 'DG14' ? <span className="text-gray-500">{t('pa:steps.dgDescSecurityOptions')}</span> : null}
                            </div>
                            <div className={cn(
                              'px-2 py-0.5 rounded-full text-xs font-medium',
                              detail.valid ? 'bg-green-200 text-green-700 dark:bg-green-800 dark:text-green-200' : 'bg-red-200 text-red-700 dark:bg-red-800 dark:text-red-200'
                            )}>
                              {detail.valid ? (
                                <span className="flex items-center gap-1"><CheckCircle className="w-3 h-3" />{t('pa:steps.match')}</span>
                              ) : (
                                <span className="flex items-center gap-1"><XCircle className="w-3 h-3" />{t('pa:steps.mismatch')}</span>
                              )}
                            </div>
                          </div>
                          <div className="space-y-2 mt-2">
                            <div>
                              <div className="flex items-center gap-1 text-gray-500 mb-1">
                                <FileText className="w-3 h-3" />
                                <span>{t('pa:steps.expectedHash')}:</span>
                              </div>
                              <code className="block font-mono bg-gray-200 dark:bg-gray-600 px-2 py-1 rounded break-all text-xs">{detail.expectedHash}</code>
                            </div>
                            <div>
                              <div className="flex items-center gap-1 text-gray-500 mb-1">
                                <Hash className="w-3 h-3" />
                                <span>{t('pa:steps.computedHash')}:</span>
                              </div>
                              <code className="block font-mono bg-gray-200 dark:bg-gray-600 px-2 py-1 rounded break-all text-xs">{detail.actualHash}</code>
                            </div>
                          </div>
                          {detail.valid ? (
                            <div className="mt-2 flex items-center gap-1 text-green-600 dark:text-green-400 font-medium">
                              <CheckCircle className="w-3 h-3" />
                              <span>{t('pa:steps.hashMatchIntegrity')}</span>
                            </div>
                          ) : null}
                        </div>
                      ))}
                    </div>
                  );
                })() : null}

                {/* Step 7: CRL check details */}
                {step.id === 7 && step.details && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-1">
                    {step.details.description && (
                      <div className="text-gray-600 dark:text-gray-400">
                        {step.details.description}
                      </div>
                    )}
                    {step.details.detailedDescription && (
                      <div className="text-gray-500 dark:text-gray-500 text-xs">
                        {step.details.detailedDescription}
                      </div>
                    )}
                  </div>
                )}

              </div>
            )}
          </div>
        ))}
      </div>
    </div>
  );
}

export default VerificationStepsPanel;
