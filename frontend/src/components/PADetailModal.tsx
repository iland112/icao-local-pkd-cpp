import { useTranslation } from 'react-i18next';
import { useState, useEffect } from 'react';
import {
  CheckCircle,
  XCircle,
  AlertCircle,
  Search,
  Loader2,
  X,
  Info,
  User,
  AlertTriangle,
  ShieldCheck,
} from 'lucide-react';
import type { PAHistoryItem, PAStatus } from '@/types';
import { cn } from '@/utils/cn';
import { getFlagSvgPath } from '@/utils/countryCode';
import { getCountryName } from '@/utils/countryNames';
import { formatDateTime } from '@/utils/dateFormat';
import { paApi } from '@/services/paApi';

// DG1 MRZ Data interface
interface DG1Data {
  surname?: string;
  givenNames?: string;
  documentNumber?: string;
  nationality?: string;
  sex?: string;
  dateOfBirth?: string;
  expirationDate?: string;
}

// DG2 Face Image interface
interface DG2Data {
  faceImages?: Array<{
    imageDataUrl?: string;
    imageFormat?: string;
  }>;
}

// DG Data response interface
interface DGDataResponse {
  hasDg1: boolean;
  hasDg2: boolean;
  dg1?: DG1Data;
  dg2?: DG2Data;
}

interface PADetailModalProps {
  open: boolean;
  record: PAHistoryItem | null;
  onClose: () => void;
}

function getStatusBadge(status: PAStatus, t: (key: string) => string) {
  const styles: Record<PAStatus, { bg: string; text: string; icon: React.ReactNode }> = {
    VALID: {
      bg: 'bg-green-100 dark:bg-green-900/30',
      text: 'text-green-600 dark:text-green-400',
      icon: <CheckCircle className="w-3 h-3" />,
    },
    EXPIRED_VALID: {
      bg: 'bg-amber-100 dark:bg-amber-900/30',
      text: 'text-amber-600 dark:text-amber-400',
      icon: <AlertTriangle className="w-3 h-3" />,
    },
    INVALID: {
      bg: 'bg-red-100 dark:bg-red-900/30',
      text: 'text-red-600 dark:text-red-400',
      icon: <XCircle className="w-3 h-3" />,
    },
    ERROR: {
      bg: 'bg-yellow-100 dark:bg-yellow-900/30',
      text: 'text-yellow-600 dark:text-yellow-400',
      icon: <AlertCircle className="w-3 h-3" />,
    },
  };

  const style = styles[status];
  if (!style) {
    return <span className="inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium bg-gray-100 text-gray-600">{status}</span>;
  }
  const label: Record<PAStatus, string> = {
    VALID: t('common:status.valid'),
    EXPIRED_VALID: t('common:status.expiredValid'),
    INVALID: t('common:status.invalid'),
    ERROR: t('pa:history.errorVerifications'),
  };

  return (
    <span className={cn('inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium', style.bg, style.text)}>
      {style.icon}
      {label[status]}
    </span>
  );
}

export function PADetailModal({ open, record, onClose }: PADetailModalProps) {
  const { t } = useTranslation(['pa', 'common']);
  const [dgData, setDgData] = useState<DGDataResponse | null>(null);
  const [dgLoading, setDgLoading] = useState(false);
  const [dgError, setDgError] = useState<string | null>(null);

  // Fetch DG data when modal opens with a VALID full verification record
  useEffect(() => {
    if (!open || !record) {
      setDgData(null);
      setDgError(null);
      return;
    }

    if (record.status === 'VALID' && record.verificationId && record.verificationType !== 'LOOKUP') {
      setDgLoading(true);
      setDgData(null);
      setDgError(null);
      paApi.getDataGroups(record.verificationId)
        .then((response) => {
          setDgData(response.data as DGDataResponse);
        })
        .catch((error) => {
          if (import.meta.env.DEV) console.error('Failed to load DG data:', error);
          setDgError(t('pa:history.dgLoadFailed'));
        })
        .finally(() => {
          setDgLoading(false);
        });
    }
  }, [open, record, t]);

  if (!open || !record) return null;

  return (
    <div className="fixed inset-0 z-[70] flex items-center justify-center">
      {/* Backdrop */}
      <div
        className="absolute inset-0 bg-black/50 backdrop-blur-sm"
        onClick={onClose}
      />

      {/* Modal Content */}
      <div className="relative bg-white dark:bg-gray-800 rounded-xl shadow-xl w-full max-w-sm sm:max-w-2xl lg:max-w-4xl max-h-[90vh] overflow-y-auto mx-4">
        {/* Modal Header */}
        <div className="sticky top-0 z-10 px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center justify-between bg-white dark:bg-gray-800">
          <div className="flex items-center gap-2.5">
            <div className="p-1.5 rounded-lg bg-gradient-to-br from-blue-500 to-indigo-600">
              <Info className="w-4 h-4 text-white" />
            </div>
            <h2 className="text-base font-bold text-gray-900 dark:text-white">{t('pa:history.verificationDetail')}</h2>
            {record.verificationType === 'LOOKUP' ? (
              <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium bg-emerald-100 dark:bg-emerald-900/30 text-emerald-600 dark:text-emerald-400">
                <Search className="w-3 h-3" />
                {t('pa:history.typeLookup')}
              </span>
            ) : (
              <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium bg-blue-100 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400">
                <ShieldCheck className="w-3 h-3" />
                {t('pa:history.typeFull')}
              </span>
            )}
            {getStatusBadge(record.status, t)}
          </div>
          <button
            onClick={onClose}
            className="p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
          >
            <X className="w-4 h-4 text-gray-500" />
          </button>
        </div>

        {/* Modal Body */}
        <div className="p-4 space-y-3">

          {/* Section 1: Basic Info */}
          <div className="grid grid-cols-5 gap-2">
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
              <p className="text-xs font-semibold text-gray-400 uppercase tracking-wider">{t('pa:history.verificationId')}</p>
              <p className="font-mono text-xs text-gray-900 dark:text-white truncate mt-0.5" title={record.verificationId}>
                {record.verificationId.substring(0, 12)}...
              </p>
            </div>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
              <p className="text-xs font-semibold text-gray-400 uppercase tracking-wider">{t('pa:history.verificationTime')}</p>
              <p className="text-xs text-gray-900 dark:text-white mt-0.5">
                {formatDateTime(record.verificationTimestamp)}
              </p>
            </div>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
              <p className="text-xs font-semibold text-gray-400 uppercase tracking-wider">{t('pa:history.country')}</p>
              <div className="flex items-center gap-1.5 mt-0.5">
                {record.issuingCountry && getFlagSvgPath(record.issuingCountry) && (
                  <img
                    src={getFlagSvgPath(record.issuingCountry)}
                    alt={record.issuingCountry}
                    title={getCountryName(record.issuingCountry)}
                    className="w-5 h-3.5 object-cover rounded shadow-sm border border-gray-200 dark:border-gray-600"
                    onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }}
                  />
                )}
                <span className="text-xs font-medium text-gray-900 dark:text-white">
                  {record.issuingCountry || '-'}
                </span>
              </div>
            </div>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
              <p className="text-xs font-semibold text-gray-400 uppercase tracking-wider">{t('pa:history.documentNumber')}</p>
              <p className="font-mono text-xs font-medium text-blue-600 dark:text-blue-400 mt-0.5">
                {record.documentNumber || '-'}
              </p>
            </div>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
              <p className="text-xs font-semibold text-gray-400 uppercase tracking-wider">{t('pa:history.requestedBy')}</p>
              {record.requestedBy ? (
                <p className="text-xs font-medium text-gray-900 dark:text-white truncate mt-0.5">
                  {record.requestedBy}
                </p>
              ) : (
                <div className="mt-0.5">
                  <p className="text-xs text-gray-400 dark:text-gray-500">{t('pa:history.anonymous')}</p>
                  {record.clientIp && (
                    <p className="text-xs font-mono text-gray-400 dark:text-gray-500 mt-0.5">
                      IP: {record.clientIp}
                    </p>
                  )}
                  {record.userAgent && (
                    <p className="text-xs text-gray-400 dark:text-gray-500 mt-0.5 truncate" title={record.userAgent}>
                      UA: {record.userAgent.length > 40 ? record.userAgent.substring(0, 40) + '...' : record.userAgent}
                    </p>
                  )}
                </div>
              )}
            </div>
          </div>

          {/* Section 2: Verification Results */}
          <div>
            <div className="flex items-center gap-1.5 mb-2">
              <div className="w-1 h-3.5 rounded-full bg-green-500" />
              <h3 className="text-xs font-bold text-gray-800 dark:text-gray-200">{t('pa:history.verificationResult')}</h3>
            </div>

            <div className={cn('grid gap-2', record.verificationType === 'LOOKUP' ? 'grid-cols-1' : 'grid-cols-3')}>
              {/* SOD Signature — hide for LOOKUP */}
              {record.verificationType !== 'LOOKUP' && (
                <div className={cn(
                  'rounded-lg px-3 py-2.5 border-l-3',
                  record.sodSignatureValid
                    ? 'bg-green-50 dark:bg-green-900/20 border-green-500'
                    : 'bg-red-50 dark:bg-red-900/20 border-red-500'
                )}>
                  <p className="text-xs font-semibold text-gray-500 dark:text-gray-400 mb-1">{t('pa:steps.step3')}</p>
                  <div className="flex items-center gap-1.5">
                    {record.sodSignatureValid ? (
                      <CheckCircle className="w-4 h-4 text-green-500" />
                    ) : (
                      <XCircle className="w-4 h-4 text-red-500" />
                    )}
                    <span className={cn(
                      'text-xs font-bold',
                      record.sodSignatureValid
                        ? 'text-green-600 dark:text-green-400'
                        : 'text-red-600 dark:text-red-400'
                    )}>
                      {record.sodSignatureValid ? t('common:status.valid') : t('common:status.invalid')}
                    </span>
                  </div>
                </div>
              )}

              {/* Certificate Chain */}
              <div className={cn(
                'rounded-lg px-3 py-2.5 border-l-3',
                record.trustChainValid
                  ? 'bg-green-50 dark:bg-green-900/20 border-green-500'
                  : 'bg-red-50 dark:bg-red-900/20 border-red-500'
              )}>
                <p className="text-xs font-semibold text-gray-500 dark:text-gray-400 mb-1">{t('pa:history.certChainVerification')}</p>
                <div className="flex items-center gap-1.5">
                  {record.trustChainValid ? (
                    <CheckCircle className="w-4 h-4 text-green-500" />
                  ) : (
                    <XCircle className="w-4 h-4 text-red-500" />
                  )}
                  <span className={cn(
                    'text-xs font-bold',
                    record.trustChainValid
                      ? 'text-green-600 dark:text-green-400'
                      : 'text-red-600 dark:text-red-400'
                  )}>
                    {record.trustChainValid ? t('common:status.valid') : t('common:status.invalid')}
                  </span>
                </div>
                {record.trustChainMessage && (
                  <p className="text-xs text-gray-500 dark:text-gray-400 mt-1 truncate" title={record.trustChainMessage}>
                    {record.trustChainMessage}
                  </p>
                )}
              </div>

              {/* Data Group Hash — hide for LOOKUP */}
              {record.verificationType !== 'LOOKUP' && (
                <div className={cn(
                  'rounded-lg px-3 py-2.5 border-l-3',
                  record.dgHashesValid
                    ? 'bg-green-50 dark:bg-green-900/20 border-green-500'
                    : 'bg-red-50 dark:bg-red-900/20 border-red-500'
                )}>
                  <p className="text-xs font-semibold text-gray-500 dark:text-gray-400 mb-1">{t('pa:steps.step2')}</p>
                  <div className="flex items-center gap-1.5">
                    {record.dgHashesValid ? (
                      <CheckCircle className="w-4 h-4 text-green-500" />
                    ) : (
                      <XCircle className="w-4 h-4 text-red-500" />
                    )}
                    <span className={cn(
                      'text-xs font-bold',
                      record.dgHashesValid
                        ? 'text-green-600 dark:text-green-400'
                        : 'text-red-600 dark:text-red-400'
                    )}>
                      {record.dgHashesValid ? t('common:status.valid') : t('common:status.invalid')}
                    </span>
                  </div>
                </div>
              )}
            </div>

            {/* CRL Status - Show when not VALID */}
            {record.crlStatus && record.crlStatus !== 'VALID' && (
              <div className={cn(
                'rounded-lg px-3 py-2 mt-2 flex items-center gap-2',
                record.crlStatus === 'REVOKED' || record.crlStatus === 'CRL_INVALID'
                  ? 'bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
                  : 'bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800'
              )}>
                {record.crlStatus === 'REVOKED' || record.crlStatus === 'CRL_INVALID' ? (
                  <AlertTriangle className="w-4 h-4 text-red-500 flex-shrink-0" />
                ) : (
                  <AlertCircle className="w-4 h-4 text-yellow-500 flex-shrink-0" />
                )}
                <span className={cn(
                  'text-xs font-bold',
                  record.crlStatus === 'REVOKED' || record.crlStatus === 'CRL_INVALID'
                    ? 'text-red-700 dark:text-red-400'
                    : 'text-yellow-700 dark:text-yellow-400'
                )}>
                  CRL: {record.crlStatus?.replace(/_/g, ' ')}
                </span>
              </div>
            )}
          </div>

          {/* DSC Non-Conformant Warning Banner */}
          {record.dscNonConformant && (
            <div className="bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-lg px-3 py-2.5">
              <div className="flex items-center gap-2">
                <AlertTriangle className="w-4 h-4 text-amber-500 flex-shrink-0" />
                <div>
                  <span className="text-xs font-bold text-amber-700 dark:text-amber-400">
                    {t('pa:history.nonConformantDsc')}
                  </span>
                  {record.pkdConformanceCode && (
                    <span className="ml-2 px-1.5 py-0.5 text-xs rounded bg-amber-100 dark:bg-amber-900/40 text-amber-600 dark:text-amber-300 font-mono">
                      {record.pkdConformanceCode}
                    </span>
                  )}
                  {record.pkdConformanceText ? (
                    <p className="text-xs text-amber-600 dark:text-amber-400 mt-0.5">
                      {record.pkdConformanceText}
                    </p>
                  ) : (
                    <p className="text-xs text-amber-600 dark:text-amber-400 mt-0.5">
                      {t('pa:history.ncDataDefaultWarning')}
                    </p>
                  )}
                </div>
              </div>
            </div>
          )}

          {/* Section 3: Data Groups (DG1 + DG2) — hide for LOOKUP */}
          {record.status === 'VALID' && record.verificationType !== 'LOOKUP' && (
            <div>
              <div className="flex items-center gap-1.5 mb-2">
                <div className="w-1 h-3.5 rounded-full bg-purple-500" />
                <h3 className="text-xs font-bold text-gray-800 dark:text-gray-200">{t('pa:history.dataGroups')}</h3>
              </div>

              {dgLoading ? (
                <div className="flex items-center justify-center py-6 bg-gray-50 dark:bg-gray-700/30 rounded-lg">
                  <Loader2 className="w-4 h-4 animate-spin text-blue-500" />
                  <span className="ml-2 text-xs text-gray-500">{t('common:button.loading')}</span>
                </div>
              ) : dgError ? (
                <div className="bg-yellow-50 dark:bg-yellow-900/20 rounded-lg px-3 py-2 flex items-center gap-2">
                  <AlertCircle className="w-4 h-4 text-yellow-500" />
                  <span className="text-xs text-yellow-700 dark:text-yellow-400">{dgError}</span>
                </div>
              ) : (
                <div className="flex gap-2">
                  {/* DG1 - MRZ */}
                  <div className="flex-1 bg-blue-50 dark:bg-blue-900/20 rounded-lg p-3 border border-blue-100 dark:border-blue-800">
                    <div className="flex items-center gap-1.5 mb-2">
                      <div className="w-1.5 h-1.5 rounded-full bg-blue-500" />
                      <p className="text-xs font-bold text-blue-700 dark:text-blue-300">{t('pa:history.dg1MrzData')}</p>
                    </div>
                    {dgData?.hasDg1 && dgData.dg1 ? (
                      <div className="grid grid-cols-4 gap-1.5">
                        <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5">
                          <p className="text-xs text-gray-400 leading-none">{t('pa:history.surname')}</p>
                          <p className="font-mono text-xs font-medium text-gray-900 dark:text-white truncate mt-0.5">{dgData.dg1.surname || '-'}</p>
                        </div>
                        <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5 col-span-2">
                          <p className="text-xs text-gray-400 leading-none">{t('pa:history.givenNames')}</p>
                          <p className="font-mono text-xs font-medium text-gray-900 dark:text-white truncate mt-0.5">{dgData.dg1.givenNames || '-'}</p>
                        </div>
                        <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5">
                          <p className="text-xs text-gray-400 leading-none">{t('pa:history.gender')}</p>
                          <p className="font-mono text-xs font-medium text-gray-900 dark:text-white mt-0.5">
                            {dgData.dg1.sex === 'M' ? t('pa:result.male') : dgData.dg1.sex === 'F' ? t('pa:result.female') : dgData.dg1.sex || '-'}
                          </p>
                        </div>
                        <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5 col-span-2">
                          <p className="text-xs text-gray-400 leading-none">{t('pa:history.documentNumber')}</p>
                          <p className="font-mono text-xs font-bold text-blue-600 dark:text-blue-400 mt-0.5">{dgData.dg1.documentNumber || '-'}</p>
                        </div>
                        <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5">
                          <p className="text-xs text-gray-400 leading-none">{t('pa:history.nationality')}</p>
                          <p className="font-mono text-xs font-medium text-gray-900 dark:text-white mt-0.5">{dgData.dg1.nationality || '-'}</p>
                        </div>
                        <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5">
                          <p className="text-xs text-gray-400 leading-none">{t('pa:history.dateOfBirth')}</p>
                          <p className="font-mono text-xs font-medium text-gray-900 dark:text-white mt-0.5">{dgData.dg1.dateOfBirth || '-'}</p>
                        </div>
                      </div>
                    ) : (
                      <div className="text-xs text-gray-500 dark:text-gray-400 text-center py-3">
                        {t('pa:history.noDg1Data')}
                      </div>
                    )}
                  </div>

                  {/* DG2 - Face */}
                  <div className="w-40 flex-shrink-0 bg-purple-50 dark:bg-purple-900/20 rounded-lg p-3 border border-purple-100 dark:border-purple-800">
                    <div className="flex items-center gap-1.5 mb-2">
                      <div className="w-1.5 h-1.5 rounded-full bg-purple-500" />
                      <p className="text-xs font-bold text-purple-700 dark:text-purple-300">{t('pa:history.dg2Face')}</p>
                    </div>
                    {dgData?.hasDg2 && dgData.dg2?.faceImages?.[0]?.imageDataUrl ? (
                      <div className="flex flex-col items-center">
                        <img
                          src={dgData.dg2.faceImages[0].imageDataUrl}
                          alt={t('pa:history.dg2Face')}
                          className="w-28 aspect-[3/4] object-cover rounded-lg shadow-md border-2 border-purple-200 dark:border-purple-700"
                        />
                        <span className="mt-1.5 px-1.5 py-0.5 text-xs rounded-full bg-purple-100 dark:bg-purple-900/50 text-purple-600 dark:text-purple-400 font-medium">
                          {dgData.dg2.faceImages[0].imageFormat || 'N/A'}
                        </span>
                      </div>
                    ) : (
                      <div className="flex flex-col items-center justify-center h-28 text-xs text-gray-500 dark:text-gray-400">
                        <User className="w-6 h-6 text-gray-300 dark:text-gray-600 mb-1" />
                        {t('pa:history.noFaceImage')}
                      </div>
                    )}
                  </div>
                </div>
              )}
            </div>
          )}
        </div>

        {/* Modal Footer */}
        <div className="sticky bottom-0 px-5 py-3 border-t border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800 flex justify-end">
          <button
            onClick={onClose}
            className="inline-flex items-center gap-1.5 px-4 py-1.5 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors border border-gray-200 dark:border-gray-600"
          >
            {t('common:button.close')}
          </button>
        </div>
      </div>
    </div>
  );
}

export default PADetailModal;
