import { useTranslation } from 'react-i18next';
import { useState, useEffect } from 'react';
import { useParams, useNavigate, Link } from 'react-router-dom';
import {
  ShieldCheck,
  ArrowLeft,
  CheckCircle,
  XCircle,
  Clock,
  Loader2,
  AlertCircle,
  FileText,
  Calendar,
  Fingerprint,
  Lock,
  Hash,
  FileKey,
  Globe,
  IdCard,
  Award,
  AlertTriangle,
} from 'lucide-react';
import { paApi } from '@/services/paApi';
import type { PAVerificationResponse, PAStatus } from '@/types';
import { cn } from '@/utils/cn';
import { formatDateTime } from '@/utils/dateFormat';

export function PADetail() {
  const { t } = useTranslation(['pa', 'common']);
  const { paId } = useParams<{ paId: string }>();
  const navigate = useNavigate();

  const [result, setResult] = useState<PAVerificationResponse | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (paId) {
      fetchPADetail();
    }
  }, [paId]);

  const fetchPADetail = async () => {
    if (!paId) return;

    setLoading(true);
    setError(null);

    try {
      const response = await paApi.getDetail(paId);
      setResult(response.data);
    } catch (err) {
      setError(t('pa:detail.loadFailed'));
      if (import.meta.env.DEV) console.error('Failed to fetch PA detail:', err);
    } finally {
      setLoading(false);
    }
  };

  const getStatusBadge = (status: PAStatus) => {
    const styles: Record<PAStatus, { bg: string; text: string; icon: React.ReactNode }> = {
      VALID: {
        bg: 'bg-green-100 dark:bg-green-900/30',
        text: 'text-green-600 dark:text-green-400',
        icon: <CheckCircle className="w-5 h-5" />,
      },
      EXPIRED_VALID: {
        bg: 'bg-amber-100 dark:bg-amber-900/30',
        text: 'text-amber-600 dark:text-amber-400',
        icon: <AlertTriangle className="w-5 h-5" />,
      },
      INVALID: {
        bg: 'bg-red-100 dark:bg-red-900/30',
        text: 'text-red-600 dark:text-red-400',
        icon: <XCircle className="w-5 h-5" />,
      },
      ERROR: {
        bg: 'bg-yellow-100 dark:bg-yellow-900/30',
        text: 'text-yellow-600 dark:text-yellow-400',
        icon: <AlertCircle className="w-5 h-5" />,
      },
    };

    const style = styles[status];
    if (!style) {
      return <span className="inline-flex items-center px-4 py-2 rounded-full text-base font-medium bg-gray-100 text-gray-600">{status}</span>;
    }
    const label: Record<PAStatus, string> = {
      VALID: t('pa:detail.statusValid'),
      EXPIRED_VALID: t('pa:detail.statusExpiredValid'),
      INVALID: t('pa:detail.statusInvalid'),
      ERROR: t('pa:detail.statusError'),
    };

    return (
      <span className={cn('inline-flex items-center gap-2 px-4 py-2 rounded-full text-base font-medium', style.bg, style.text)}>
        {style.icon}
        {label[status]}
      </span>
    );
  };

  const getCrlSeverityColor = (severity: string) => {
    switch (severity?.toUpperCase()) {
      case 'SUCCESS':
        return 'text-green-600 bg-green-50 dark:bg-green-900/20';
      case 'WARNING':
        return 'text-yellow-600 bg-yellow-50 dark:bg-yellow-900/20';
      case 'ERROR':
        return 'text-red-600 bg-red-50 dark:bg-red-900/20';
      default:
        return 'text-gray-600 bg-gray-50 dark:bg-gray-700';
    }
  };

  if (loading) {
    return (
      <div className="w-full px-4 lg:px-6 py-4">
        <div className="flex items-center justify-center py-20">
          <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
        </div>
      </div>
    );
  }

  if (error || !result) {
    return (
      <div className="w-full px-4 lg:px-6 py-4">
        <div className="flex flex-col items-center justify-center py-20 text-gray-500 dark:text-gray-400">
          <AlertCircle className="w-12 h-12 mb-4 opacity-50" />
          <p className="text-lg font-medium">{error || t('pa:detail.notFound')}</p>
          <button
            onClick={() => navigate('/pa/history')}
            className="mt-4 px-4 py-2 bg-blue-500 text-white rounded-lg hover:bg-blue-600 transition-colors"
          >
            {t('pa:detail.backToList')}
          </button>
        </div>
      </div>
    );
  }

  const isValid = result.status === 'VALID';

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-8">
        <div className="flex items-center gap-4">
          <button
            onClick={() => navigate(-1)}
            className="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
          >
            <ArrowLeft className="w-5 h-5 text-gray-500" />
          </button>
          <div className="p-3 rounded-xl bg-gradient-to-br from-teal-500 to-cyan-600 shadow-lg">
            <ShieldCheck className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">{t('pa:detail.title')}</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400 font-mono">
              {result.verificationId}
            </p>
          </div>
          {getStatusBadge(result.status)}
        </div>
      </div>

      {/* Overall Status Banner */}
      <div className={cn(
        'rounded-2xl p-5 mb-6',
        isValid
          ? 'bg-gradient-to-r from-green-500 to-emerald-500 text-white'
          : result.status === 'INVALID'
          ? 'bg-gradient-to-r from-red-500 to-rose-500 text-white'
          : 'bg-gradient-to-r from-yellow-500 to-orange-500 text-white'
      )}>
        <div className="flex items-center gap-4">
          {isValid ? (
            <Award className="w-12 h-12" />
          ) : result.status === 'INVALID' ? (
            <XCircle className="w-12 h-12" />
          ) : (
            <AlertTriangle className="w-12 h-12" />
          )}
          <div className="flex-grow">
            <h2 className="text-xl font-bold">
              {isValid
                ? t('pa:detail.paSuccess')
                : result.status === 'INVALID'
                ? t('pa:detail.paFailed')
                : t('pa:detail.paError')}
            </h2>
            <div className="flex items-center gap-4 mt-1 text-sm opacity-90">
              <span className="flex items-center gap-1">
                <Clock className="w-4 h-4" />
                {result.processingDurationMs}ms
              </span>
              {result.issuingCountry && (
                <span className="flex items-center gap-1">
                  <Globe className="w-4 h-4" />
                  {result.issuingCountry}
                </span>
              )}
              {result.documentNumber && (
                <span className="flex items-center gap-1">
                  <IdCard className="w-4 h-4" />
                  {result.documentNumber}
                </span>
              )}
            </div>
          </div>
        </div>
      </div>

      {/* Errors List */}
      {result.errors && result.errors.length > 0 && (
        <div className="rounded-2xl bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 p-4 mb-6">
          <h3 className="font-bold text-red-700 dark:text-red-400 mb-2 flex items-center gap-2">
            <AlertTriangle className="w-5 h-5" />
            {t('pa:detail.verificationErrors')}
          </h3>
          <ul className="space-y-1 text-sm text-red-600 dark:text-red-400">
            {result.errors.map((err, idx) => (
              <li key={idx} className="flex items-start gap-2">
                <span className="font-mono text-xs bg-red-100 dark:bg-red-900/50 px-1 rounded">{err.code}</span>
                <span>{err.message}</span>
              </li>
            ))}
          </ul>
        </div>
      )}

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Left Column: 3 Validation Cards */}
        <div className="lg:col-span-2 space-y-6">
          {/* 1. Certificate Chain Validation */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg overflow-hidden">
            <div className={cn(
              'px-5 py-3 flex items-center gap-2',
              result.certificateChainValidation?.valid
                ? 'bg-green-500 text-white'
                : 'bg-red-500 text-white'
            )}>
              <Lock className="w-5 h-5" />
              <span className="font-bold">{t('common:label.certChainVerification')}</span>
              {result.certificateChainValidation?.valid ? (
                <CheckCircle className="w-5 h-5 ml-auto" />
              ) : (
                <XCircle className="w-5 h-5 ml-auto" />
              )}
            </div>
            <div className="p-5 space-y-4">
              {result.certificateChainValidation && (
                <>
                  <div className="grid grid-cols-2 gap-4">
                    <div>
                      <label className="text-xs text-gray-500 dark:text-gray-400">DSC Subject</label>
                      <p className="font-mono text-xs break-all text-gray-700 dark:text-gray-300">
                        {result.certificateChainValidation.dscSubject || '-'}
                      </p>
                    </div>
                    <div>
                      <label className="text-xs text-gray-500 dark:text-gray-400">DSC Serial</label>
                      <p className="font-mono text-sm text-gray-700 dark:text-gray-300">
                        {result.certificateChainValidation.dscSerialNumber || '-'}
                      </p>
                    </div>
                    <div>
                      <label className="text-xs text-gray-500 dark:text-gray-400">CSCA Subject</label>
                      <p className="font-mono text-xs break-all text-gray-700 dark:text-gray-300">
                        {result.certificateChainValidation.cscaSubject || '-'}
                      </p>
                    </div>
                    <div>
                      <label className="text-xs text-gray-500 dark:text-gray-400">CSCA Serial</label>
                      <p className="font-mono text-sm text-gray-700 dark:text-gray-300">
                        {result.certificateChainValidation.cscaSerialNumber || '-'}
                      </p>
                    </div>
                    <div>
                      <label className="text-xs text-gray-500 dark:text-gray-400">{t('pa:detail.validityStart')}</label>
                      <p className="text-sm text-gray-700 dark:text-gray-300">
                        {result.certificateChainValidation.notBefore || '-'}
                      </p>
                    </div>
                    <div>
                      <label className="text-xs text-gray-500 dark:text-gray-400">{t('pa:detail.validityEnd')}</label>
                      <p className="text-sm text-gray-700 dark:text-gray-300">
                        {result.certificateChainValidation.notAfter || '-'}
                      </p>
                    </div>
                  </div>

                  {/* CRL Status */}
                  {result.certificateChainValidation.crlChecked && (
                    <div className={cn(
                      'p-3 rounded-lg',
                      getCrlSeverityColor(result.certificateChainValidation.crlStatusSeverity)
                    )}>
                      <div className="flex items-center gap-2 mb-1">
                        <FileKey className="w-4 h-4" />
                        <span className="font-semibold text-sm">{ t('pa:steps.step6') }</span>
                      </div>
                      <p className="text-sm">{result.certificateChainValidation.crlStatusDescription}</p>
                      {result.certificateChainValidation.crlStatusDetailedDescription && (
                        <p className="text-sm mt-1 opacity-75">{result.certificateChainValidation.crlStatusDetailedDescription}</p>
                      )}
                    </div>
                  )}

                  {result.certificateChainValidation.validationErrors && (
                    <div className="p-3 rounded-lg bg-red-50 dark:bg-red-900/20 text-red-600 dark:text-red-400 text-sm">
                      {result.certificateChainValidation.validationErrors}
                    </div>
                  )}
                </>
              )}
            </div>
          </div>

          {/* 2. SOD Signature Validation */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg overflow-hidden">
            <div className={cn(
              'px-5 py-3 flex items-center gap-2',
              result.sodSignatureValidation?.valid
                ? 'bg-green-500 text-white'
                : 'bg-red-500 text-white'
            )}>
              <ShieldCheck className="w-5 h-5" />
              <span className="font-bold">{ t('pa:steps.step3') }</span>
              {result.sodSignatureValidation?.valid ? (
                <CheckCircle className="w-5 h-5 ml-auto" />
              ) : (
                <XCircle className="w-5 h-5 ml-auto" />
              )}
            </div>
            <div className="p-5 space-y-4">
              {result.sodSignatureValidation && (
                <>
                  <div className="grid grid-cols-2 gap-4">
                    <div>
                      <label className="text-xs text-gray-500 dark:text-gray-400">{ t('certificate:detail.signatureAlgorithm') }</label>
                      <p className="font-mono text-sm text-gray-700 dark:text-gray-300">
                        {result.sodSignatureValidation.signatureAlgorithm || '-'}
                      </p>
                    </div>
                    <div>
                      <label className="text-xs text-gray-500 dark:text-gray-400">{ t('common:label.hashAlgorithm') }</label>
                      <p className="font-mono text-sm text-gray-700 dark:text-gray-300">
                        {result.sodSignatureValidation.hashAlgorithm || '-'}
                      </p>
                    </div>
                  </div>

                  {result.sodSignatureValidation.validationErrors && (
                    <div className="p-3 rounded-lg bg-red-50 dark:bg-red-900/20 text-red-600 dark:text-red-400 text-sm">
                      {result.sodSignatureValidation.validationErrors}
                    </div>
                  )}
                </>
              )}
            </div>
          </div>

          {/* 3. Data Group Hash Validation */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg overflow-hidden">
            <div className={cn(
              'px-5 py-3 flex items-center gap-2',
              result.dataGroupValidation?.invalidGroups === 0
                ? 'bg-green-500 text-white'
                : 'bg-red-500 text-white'
            )}>
              <Hash className="w-5 h-5" />
              <span className="font-bold">{t('pa:detail.dgHashVerification')}</span>
              {result.dataGroupValidation?.invalidGroups === 0 ? (
                <CheckCircle className="w-5 h-5 ml-auto" />
              ) : (
                <XCircle className="w-5 h-5 ml-auto" />
              )}
            </div>
            <div className="p-5 space-y-4">
              {result.dataGroupValidation && (
                <>
                  <div className="grid grid-cols-3 gap-4 text-center">
                    <div className="p-3 rounded-lg bg-gray-100 dark:bg-gray-700">
                      <p className="text-2xl font-bold text-gray-900 dark:text-white">
                        {result.dataGroupValidation.totalGroups}
                      </p>
                      <p className="text-xs text-gray-500">{ t('monitoring:pool.total') }</p>
                    </div>
                    <div className="p-3 rounded-lg bg-green-100 dark:bg-green-900/30">
                      <p className="text-2xl font-bold text-green-600">
                        {result.dataGroupValidation.validGroups}
                      </p>
                      <p className="text-xs text-gray-500">{t('common:toast.success')}</p>
                    </div>
                    <div className="p-3 rounded-lg bg-red-100 dark:bg-red-900/30">
                      <p className="text-2xl font-bold text-red-600">
                        {result.dataGroupValidation.invalidGroups}
                      </p>
                      <p className="text-xs text-gray-500">{t('common:status.failed')}</p>
                    </div>
                  </div>

                  {/* DG Details */}
                  {result.dataGroupValidation.details && Object.keys(result.dataGroupValidation.details).length > 0 && (
                    <div className="space-y-3">
                      {Object.entries(result.dataGroupValidation.details).map(([dgName, detail]) => (
                        <div key={dgName} className={cn(
                          'p-3 rounded-lg',
                          detail.valid
                            ? 'bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800'
                            : 'bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
                        )}>
                          <div className="flex items-center gap-2 mb-2">
                            {detail.valid ? (
                              <CheckCircle className="w-4 h-4 text-green-500" />
                            ) : (
                              <XCircle className="w-4 h-4 text-red-500" />
                            )}
                            <span className="font-semibold text-sm">{dgName}</span>
                            <span className={cn(
                              'text-xs px-2 py-0.5 rounded',
                              detail.valid
                                ? 'bg-green-100 dark:bg-green-900/50 text-green-600 dark:text-green-400'
                                : 'bg-red-100 dark:bg-red-900/50 text-red-600 dark:text-red-400'
                            )}>
                              {detail.valid ? t('pa:detail.hashMatch') : t('pa:detail.hashMismatch')}
                            </span>
                          </div>
                          <div className="font-mono text-xs space-y-1">
                            <div>
                              <span className="text-gray-500">Expected: </span>
                              <span className="text-gray-700 dark:text-gray-300">{detail.expectedHash}</span>
                            </div>
                            <div>
                              <span className="text-gray-500">Actual: </span>
                              <span className="text-gray-700 dark:text-gray-300">{detail.actualHash}</span>
                            </div>
                          </div>
                        </div>
                      ))}
                    </div>
                  )}
                </>
              )}
            </div>
          </div>
        </div>

        {/* Right Column: Summary & Info */}
        <div className="lg:col-span-1 space-y-6">
          {/* Verification Info */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
            <h2 className="text-lg font-bold text-gray-900 dark:text-white mb-4 flex items-center gap-2">
              <Fingerprint className="w-5 h-5 text-purple-500" />
              {t('pa:detail.verificationInfo')}
            </h2>
            <div className="space-y-4">
              <div className="flex items-center gap-3">
                <div className="p-2 bg-gray-100 dark:bg-gray-700 rounded-lg">
                  <FileText className="w-4 h-4 text-gray-500" />
                </div>
                <div>
                  <p className="text-xs text-gray-500 dark:text-gray-400">{ t('pa:history.verificationId') }</p>
                  <p className="text-xs font-mono text-gray-900 dark:text-white break-all">
                    {result.verificationId}
                  </p>
                </div>
              </div>
              <div className="flex items-center gap-3">
                <div className="p-2 bg-gray-100 dark:bg-gray-700 rounded-lg">
                  <Calendar className="w-4 h-4 text-gray-500" />
                </div>
                <div>
                  <p className="text-xs text-gray-500 dark:text-gray-400">{ t('pa:history.createdAt') }</p>
                  <p className="text-sm font-medium text-gray-900 dark:text-white">
                    {formatDateTime(result.verificationTimestamp)}
                  </p>
                </div>
              </div>
              <div className="flex items-center gap-3">
                <div className="p-2 bg-gray-100 dark:bg-gray-700 rounded-lg">
                  <Clock className="w-4 h-4 text-gray-500" />
                </div>
                <div>
                  <p className="text-xs text-gray-500 dark:text-gray-400">{t('pa:result.processingDuration')}</p>
                  <p className="text-sm font-medium text-gray-900 dark:text-white">
                    {result.processingDurationMs}ms
                  </p>
                </div>
              </div>
              {result.issuingCountry && (
                <div className="flex items-center gap-3">
                  <div className="p-2 bg-gray-100 dark:bg-gray-700 rounded-lg">
                    <Globe className="w-4 h-4 text-gray-500" />
                  </div>
                  <div>
                    <p className="text-xs text-gray-500 dark:text-gray-400">{t('pa:result.country')}</p>
                    <div className="flex items-center gap-2">
                      <img
                        src={`/svg/${result.issuingCountry.toLowerCase()}.svg`}
                        alt={result.issuingCountry}
                        className="w-6 h-4 object-cover rounded shadow-sm border border-gray-200 dark:border-gray-600"
                        onError={(e) => {
                          (e.target as HTMLImageElement).style.display = 'none';
                        }}
                      />
                      <p className="text-sm font-medium text-gray-900 dark:text-white">
                        {result.issuingCountry}
                      </p>
                    </div>
                  </div>
                </div>
              )}
              {result.documentNumber && (
                <div className="flex items-center gap-3">
                  <div className="p-2 bg-gray-100 dark:bg-gray-700 rounded-lg">
                    <IdCard className="w-4 h-4 text-gray-500" />
                  </div>
                  <div>
                    <p className="text-xs text-gray-500 dark:text-gray-400">{t('common:label.passportNumber')}</p>
                    <p className="text-sm font-mono font-medium text-gray-900 dark:text-white">
                      {result.documentNumber}
                    </p>
                  </div>
                </div>
              )}
            </div>
          </div>

          {/* Validation Summary */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
            <h2 className="text-lg font-bold text-gray-900 dark:text-white mb-4">{t('pa:detail.verificationSummary')}</h2>
            <div className="space-y-2">
              <div className="flex items-center justify-between py-2 border-b border-gray-100 dark:border-gray-700">
                <span className="text-sm text-gray-600 dark:text-gray-400">{t('pa:detail.certificateChain')}</span>
                <span className={cn(
                  'text-xs font-medium px-2 py-1 rounded',
                  result.certificateChainValidation?.valid
                    ? 'bg-green-100 dark:bg-green-900/30 text-green-600 dark:text-green-400'
                    : 'bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400'
                )}>
                  {result.certificateChainValidation?.valid ? t('common:toast.success') : t('common:status.failed')}
                </span>
              </div>
              <div className="flex items-center justify-between py-2 border-b border-gray-100 dark:border-gray-700">
                <span className="text-sm text-gray-600 dark:text-gray-400">{t('pa:detail.sodSignature')}</span>
                <span className={cn(
                  'text-xs font-medium px-2 py-1 rounded',
                  result.sodSignatureValidation?.valid
                    ? 'bg-green-100 dark:bg-green-900/30 text-green-600 dark:text-green-400'
                    : 'bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400'
                )}>
                  {result.sodSignatureValidation?.valid ? t('common:toast.success') : t('common:status.failed')}
                </span>
              </div>
              <div className="flex items-center justify-between py-2">
                <span className="text-sm text-gray-600 dark:text-gray-400">{t('pa:detail.dgHash')}</span>
                <span className={cn(
                  'text-xs font-medium px-2 py-1 rounded',
                  result.dataGroupValidation?.invalidGroups === 0
                    ? 'bg-green-100 dark:bg-green-900/30 text-green-600 dark:text-green-400'
                    : 'bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400'
                )}>
                  {result.dataGroupValidation?.validGroups || 0}/{result.dataGroupValidation?.totalGroups || 0}
                </span>
              </div>
            </div>
          </div>

          {/* Quick Actions */}
          <div className="space-y-3">
            <Link
              to="/pa/verify"
              className="w-full inline-flex items-center justify-center gap-2 px-4 py-3 rounded-xl text-sm font-medium text-white bg-gradient-to-r from-teal-500 to-cyan-500 hover:from-teal-600 hover:to-cyan-600 transition-all shadow-md hover:shadow-lg"
            >
              {t('pa:detail.newVerification')}
            </Link>
            <Link
              to="/pa/history"
              className="w-full inline-flex items-center justify-center gap-2 px-4 py-3 rounded-xl text-sm font-medium border border-gray-300 dark:border-gray-600 text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors"
            >
              {t('pa:detail.verifyHistory')}
            </Link>
          </div>
        </div>
      </div>
    </div>
  );
}

export default PADetail;
