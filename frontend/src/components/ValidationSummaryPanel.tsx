import { useTranslation } from 'react-i18next';
import { useMemo, useState } from 'react';
import {
  CheckCircle,
  XCircle,
  AlertTriangle,
  ChevronRight,
  Shield,
  ShieldCheck,
  FileCheck,
  FileX,
  Search,
} from 'lucide-react';
import { IcaoViolationDetailDialog } from './IcaoViolationDetailDialog';
import { getGlossaryTooltip } from '@/components/common';

/**
 * Unified validation summary data interface.
 * Works with both SSE real-time data (ValidationStatistics) and DB stored data (UploadedFile.validation).
 */
export interface ValidationSummaryData {
  // Summary counts (required)
  validCount: number;
  invalidCount: number;
  pendingCount: number;

  // Optional summary counts
  expiredValidCount?: number;
  errorCount?: number;
  totalCertificates?: number;
  processedCount?: number;
  duplicateCount?: number;

  // Trust chain
  trustChainValidCount: number;
  trustChainInvalidCount: number;
  cscaNotFoundCount: number;

  // Expiration
  expiredCount: number;
  validPeriodCount?: number;

  // CRL status
  revokedCount?: number;

  // ICAO compliance (only available from SSE)
  icaoCompliantCount?: number;
  icaoNonCompliantCount?: number;
  icaoWarningCount?: number;
  complianceViolations?: Record<string, number>;

  // Distribution maps (only available from SSE)
  certificateTypes?: Record<string, number>;
  signatureAlgorithms?: Record<string, number>;
  keySizes?: Record<string, number>;
}

/** Display-friendly certificate type label */
const certTypeLabel = (type: string): string => {
  const labels: Record<string, string> = {
    CSCA: 'CSCA (Self-Signed)',
    LINK_CERT: 'CSCA (Link Cert)',
    DSC: 'DSC',
    DSC_NC: 'DSC (Non Conformant)',
    MLSC: 'Master List Signer Cert',
    CRL: 'CRL',
  };
  return labels[type] ?? type;
};

/** Display-friendly key size label with algorithm family hint */
const keySizeLabel = (bits: number): string => {
  // NIST curves: 256 (P-256), 384 (P-384), 521 (P-521)
  // BSI TR-03110 Brainpool curves: 256 (brainpoolP256r1), 384 (brainpoolP384r1), 512 (brainpoolP512r1)
  // Note: EC 256/384 may be NIST or Brainpool — shown as generic "EC" here
  const ecSizes: Record<number, string> = {
    256: 'EC 256bit',
    384: 'EC 384bit',
    521: 'EC P-521',
    320: 'EC 320bit',
    512: 'EC 512bit',
  };
  if (ecSizes[bits]) return ecSizes[bits];
  // RSA sizes: 1024, 1536, 2048, 3072, 4096, 6144, 8192
  if (bits >= 1024) return `RSA ${bits}`;
  return `${bits}bit`;
};

interface ValidationSummaryPanelProps {
  data: ValidationSummaryData;
  title?: string;
  isProcessing?: boolean;
  /** Upload ID for fetching per-certificate violation details */
  uploadId?: string;
}

export function ValidationSummaryPanel({
  data,
  title,
  isProcessing = false,
  uploadId,
}: ValidationSummaryPanelProps) {
  const { t } = useTranslation(['upload', 'common']);
  const [violationDialogOpen, setViolationDialogOpen] = useState(false);
  const [violationDialogCategory, setViolationDialogCategory] = useState<string | undefined>();
  const displayTitle = title ?? t('upload:validationSummary.title');
  const expiredValidCount = data.expiredValidCount ?? 0;

  // Trust chain success rate
  const trustChainSuccessRate = useMemo(() => {
    const total = data.trustChainValidCount + data.trustChainInvalidCount + (data.cscaNotFoundCount ?? 0);
    if (total === 0) return 0;
    return Math.round((data.trustChainValidCount / total) * 100);
  }, [data.trustChainValidCount, data.trustChainInvalidCount, data.cscaNotFoundCount]);

  // Effective non-compliant count: use icaoNonCompliantCount, fallback to complianceViolations sum
  const violationsSum = useMemo(() => {
    if (!data.complianceViolations) return 0;
    return Object.values(data.complianceViolations).reduce((s, v) => s + v, 0);
  }, [data.complianceViolations]);
  const effectiveNonCompliantCount = (data.icaoNonCompliantCount ?? 0) > 0
    ? (data.icaoNonCompliantCount ?? 0)
    : violationsSum;

  // ICAO compliance rate
  const icaoComplianceRate = useMemo(() => {
    const total = (data.icaoCompliantCount ?? 0) + effectiveNonCompliantCount + (data.icaoWarningCount ?? 0);
    if (total === 0) return 0;
    return Math.round(((data.icaoCompliantCount ?? 0) / total) * 100);
  }, [data.icaoCompliantCount, effectiveNonCompliantCount, data.icaoWarningCount]);

  // Section visibility
  const hasTrustChainData = data.trustChainValidCount > 0 || data.trustChainInvalidCount > 0 || data.cscaNotFoundCount > 0;
  const hasIcaoData = (data.icaoCompliantCount ?? 0) > 0 || effectiveNonCompliantCount > 0 || (data.icaoWarningCount ?? 0) > 0;
  const hasExpirationData = data.expiredCount > 0 || (data.validPeriodCount ?? 0) > 0;
  const hasRevokedData = (data.revokedCount ?? 0) > 0;
  const hasCertTypeData = data.certificateTypes && Object.keys(data.certificateTypes).length > 0;
  const hasSigAlgoData = data.signatureAlgorithms && Object.keys(data.signatureAlgorithms).length > 0;
  const hasKeySizeData = data.keySizes && Object.keys(data.keySizes).length > 0;

  // Duplicate flow
  const hasDuplicateFlow = (data.duplicateCount ?? 0) > 0
    && data.totalCertificates != null && data.processedCount != null;
  const dupCount = data.duplicateCount ?? 0;
  const newCount = (data.totalCertificates ?? 0) - (data.duplicateCount ?? 0);
  const fileTotal = data.totalCertificates ?? 0;
  const dupPct = fileTotal > 0 ? Math.round((dupCount / fileTotal) * 100) : 0;
  const hasAnyAnalysisSection = hasTrustChainData || hasIcaoData || hasExpirationData
    || hasCertTypeData || hasSigAlgoData || hasKeySizeData;

  // Dynamic summary cards
  type CardInfo = { key: string; label: string; value: number; color: string };
  const activeCards: CardInfo[] = [];
  if (data.validCount > 0) activeCards.push({ key: 'valid', label: t('common:status.valid'), value: data.validCount, color: 'green' });
  if (expiredValidCount > 0) activeCards.push({ key: 'expired-valid', label: t('upload:fileUpload.expiredValid'), value: expiredValidCount, color: 'amber' });
  if (data.pendingCount > 0) activeCards.push({ key: 'pending', label: t('upload:dashboard.pendingCount'), value: data.pendingCount, color: 'yellow' });
  if (data.invalidCount > 0) activeCards.push({ key: 'invalid', label: t('upload:statistics.invalidCount'), value: data.invalidCount, color: 'red' });
  if ((data.errorCount ?? 0) > 0) activeCards.push({ key: 'error', label: t('sync:dashboard.error'), value: data.errorCount!, color: 'gray' });

  const cc: Record<string, { bg: string; border: string; text: string; bold: string }> = {
    green: { bg: 'bg-green-50 dark:bg-green-900/20', border: 'border-green-200 dark:border-green-800', text: 'text-green-600 dark:text-green-400', bold: 'text-green-700 dark:text-green-300' },
    amber: { bg: 'bg-amber-50 dark:bg-amber-900/20', border: 'border-amber-200 dark:border-amber-800', text: 'text-amber-600 dark:text-amber-400', bold: 'text-amber-700 dark:text-amber-300' },
    yellow: { bg: 'bg-yellow-50 dark:bg-yellow-900/20', border: 'border-yellow-200 dark:border-yellow-800', text: 'text-yellow-600 dark:text-yellow-400', bold: 'text-yellow-700 dark:text-yellow-300' },
    red: { bg: 'bg-red-50 dark:bg-red-900/20', border: 'border-red-200 dark:border-red-800', text: 'text-red-600 dark:text-red-400', bold: 'text-red-700 dark:text-red-300' },
    gray: { bg: 'bg-gray-100 dark:bg-gray-700/50', border: 'border-gray-300 dark:border-gray-600', text: 'text-gray-600 dark:text-gray-400', bold: 'text-gray-600 dark:text-gray-400' },
    blue: { bg: 'bg-blue-50 dark:bg-blue-900/20', border: 'border-blue-200 dark:border-blue-800', text: 'text-blue-600 dark:text-blue-400', bold: 'text-blue-700 dark:text-blue-300' },
  };

  const hasProcessedCard = data.processedCount != null && data.totalCertificates != null;
  const totalCards = (hasProcessedCard ? 1 : 0) + activeCards.length;
  const gridColsClass = totalCards <= 2 ? 'md:grid-cols-2' : totalCards <= 3 ? 'md:grid-cols-3' : totalCards <= 4 ? 'md:grid-cols-4' : 'md:grid-cols-5';

  const violationLabel: Record<string, string> = {
    keyUsage: 'Key Usage', algorithm: t('upload:validationSummary.violationAlgorithm'), keySize: t('upload:validationSummary.violationKeySize'),
    validityPeriod: t('upload:validationSummary.violationValidityPeriod'), dnFormat: t('upload:validationSummary.violationDnFormat'), extensions: t('upload:validationSummary.violationExtensions'),
  };

  return (
    <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-3 space-y-2.5">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-1.5">
          <ShieldCheck className="w-3.5 h-3.5 text-blue-600 dark:text-blue-400" />
          <h3 className="text-xs font-semibold text-gray-700 dark:text-gray-300">{displayTitle}</h3>
        </div>
        {isProcessing && (
          <div className="flex items-center gap-1.5 text-xs text-blue-600 dark:text-blue-400">
            <div className="animate-spin rounded-full h-3 w-3 border-b-2 border-blue-600" />
            <span>{ t('upload:fileUpload.processing') }</span>
          </div>
        )}
      </div>

      {/* ===== Path A: Duplicate flow (3-card funnel + validation cards) ===== */}
      {hasDuplicateFlow && (
        <div className="space-y-2">
          {/* Row 1: Total → Duplicates → New (inline compact) */}
          <div className="flex items-center gap-1">
            <div className="flex-1 bg-slate-50 dark:bg-slate-800/30 border border-slate-200 dark:border-slate-700 rounded px-2 py-1.5 text-center min-w-0">
              <p className="text-base font-bold text-slate-700 dark:text-slate-300 leading-tight">
                {fileTotal.toLocaleString()}
              </p>
              <span className="text-xs text-slate-500 dark:text-slate-400">{t('upload:validationSummary.fileTotal')}</span>
            </div>
            <ChevronRight className="w-3.5 h-3.5 text-gray-300 dark:text-gray-600 shrink-0" />
            <div className="flex-1 bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded px-2 py-1.5 text-center min-w-0">
              <p className="text-base font-bold text-amber-700 dark:text-amber-300 leading-tight">
                {dupCount.toLocaleString()}
              </p>
              <span className="text-xs text-amber-600 dark:text-amber-400">{t('upload:validationSummary.duplicatePercent', { pct: dupPct })}</span>
            </div>
            <ChevronRight className="w-3.5 h-3.5 text-gray-300 dark:text-gray-600 shrink-0" />
            <div className="flex-1 bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded px-2 py-1.5 text-center min-w-0">
              <p className="text-base font-bold text-blue-700 dark:text-blue-300 leading-tight">
                {newCount.toLocaleString()}
              </p>
              <span className="text-xs text-blue-600 dark:text-blue-400">{t('upload:validationSummary.newlyProcessed')}</span>
            </div>
          </div>

          {/* Row 2: Validation result inline chips */}
          {activeCards.length > 0 && (
            <div className="flex items-center gap-1.5 flex-wrap">
              <span className="text-xs text-gray-400 dark:text-gray-500 mr-0.5">{t('upload:validationSummary.validationLabel')}:</span>
              {activeCards.map((card) => {
                const c = cc[card.color];
                return (
                  <div key={card.key} className={`${c.bg} border ${c.border} rounded px-2 py-0.5 flex items-center gap-1`}>
                    <span className={`text-sm font-bold ${c.bold}`}>{card.value.toLocaleString()}</span>
                    <span className={`text-xs ${c.text}`}>{card.label}</span>
                  </div>
                );
              })}
            </div>
          )}
        </div>
      )}

      {/* ===== Path B: Classic layout (no duplicates) ===== */}
      {!hasDuplicateFlow && totalCards > 0 && (
        <div className={`grid grid-cols-2 ${gridColsClass} gap-1.5`}>
          {hasProcessedCard && (
            <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded px-2 py-1.5 text-center">
              <p className="text-base font-bold text-blue-700 dark:text-blue-300 leading-tight">
                {data.processedCount!.toLocaleString()}
              </p>
              <span className="text-xs text-blue-600 dark:text-blue-400">
                {t('upload:validationSummary.processedOf', { num: data.totalCertificates!.toLocaleString() })}
              </span>
            </div>
          )}
          {activeCards.map((card) => {
            const c = cc[card.color];
            return (
              <div key={card.key} className={`${c.bg} border ${c.border} rounded px-2 py-1.5 text-center`}>
                <p className={`text-base font-bold ${c.bold} leading-tight`}>
                  {card.value.toLocaleString()}
                </p>
                <span className={`text-xs ${c.text}`}>{card.label}</span>
              </div>
            );
          })}
        </div>
      )}

      {/* Scope divider */}
      {hasDuplicateFlow && hasAnyAnalysisSection && (
        <div className="flex items-center gap-2">
          <div className="flex-1 border-t border-gray-200 dark:border-gray-700" />
          <span className="text-xs text-gray-400 dark:text-gray-500 whitespace-nowrap">
            {t('upload:validationSummary.fullFileAnalysis', { num: fileTotal.toLocaleString() })}
          </span>
          <div className="flex-1 border-t border-gray-200 dark:border-gray-700" />
        </div>
      )}

      {/* Trust Chain + ICAO + Expiration — packed 2-col grid */}
      {(hasTrustChainData || hasIcaoData || hasExpirationData || hasRevokedData) && (
        <div className={`grid grid-cols-1 ${(hasTrustChainData && hasIcaoData) || (hasTrustChainData && hasExpirationData) ? 'md:grid-cols-2' : ''} gap-2`}>
          {/* Trust Chain */}
          {hasTrustChainData && (
            <div className="bg-gray-50 dark:bg-gray-700/30 rounded p-2 space-y-1">
              <div className="flex items-center gap-1.5">
                <Shield className="w-3 h-3 text-gray-500 dark:text-gray-400" />
                <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wide">{t('upload:validationSummary.trustChain')}</h4>
              </div>
              <div className="flex justify-between items-center text-xs">
                <span className="text-green-600 dark:text-green-400 flex items-center gap-1">
                  <CheckCircle className="w-2.5 h-2.5" /> {t('sync:reconciliation.successCount')}
                </span>
                <span className="font-semibold text-green-700 dark:text-green-300">
                  {data.trustChainValidCount.toLocaleString()} ({trustChainSuccessRate}%)
                </span>
              </div>
              {data.trustChainInvalidCount > 0 && (
                <div className="flex justify-between items-center text-xs">
                  <span className="text-red-600 dark:text-red-400 flex items-center gap-1">
                    <XCircle className="w-2.5 h-2.5" /> {t('upload:statistics.totalFailed')}
                  </span>
                  <span className="font-semibold text-red-700 dark:text-red-300">{data.trustChainInvalidCount.toLocaleString()}</span>
                </div>
              )}
              {data.cscaNotFoundCount > 0 && (
                <div className="flex justify-between items-center text-xs">
                  <span className="text-yellow-600 dark:text-yellow-400 flex items-center gap-1">
                    <AlertTriangle className="w-2.5 h-2.5" /> {t('upload:validationSummary.cscaNotFound')}
                  </span>
                  <span className="font-semibold text-yellow-700 dark:text-yellow-300">{data.cscaNotFoundCount.toLocaleString()}</span>
                </div>
              )}
            </div>
          )}

          {/* ICAO 9303 Compliance */}
          {hasIcaoData && (
            <div className="bg-gray-50 dark:bg-gray-700/30 rounded p-2 space-y-1">
              <div className="flex items-center gap-1.5">
                <ShieldCheck className="w-3 h-3 text-gray-500 dark:text-gray-400" />
                <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wide">{t('upload:validationSummary.icaoCompliance')}</h4>
              </div>
              <div className="flex justify-between items-center text-xs">
                <span className="text-green-600 dark:text-green-400 flex items-center gap-1">
                  <CheckCircle className="w-2.5 h-2.5" /> {t('certificate:doc9303.compliant')}
                </span>
                <span className="font-semibold text-green-700 dark:text-green-300">
                  {(data.icaoCompliantCount ?? 0).toLocaleString()} ({icaoComplianceRate}%)
                </span>
              </div>
              {effectiveNonCompliantCount > 0 && (
                <button
                  onClick={() => { setViolationDialogCategory(undefined); setViolationDialogOpen(true); }}
                  className="w-full flex justify-between items-center text-xs hover:bg-red-50 dark:hover:bg-red-900/10 rounded px-1 -mx-1 py-0.5 transition-colors group cursor-pointer"
                >
                  <span className="text-red-600 dark:text-red-400 flex items-center gap-1">
                    <XCircle className="w-2.5 h-2.5" /> {t('certificate:doc9303.nonCompliant')}
                    <Search className="w-2.5 h-2.5 opacity-0 group-hover:opacity-100 transition-opacity" />
                  </span>
                  <span className="font-semibold text-red-700 dark:text-red-300">
                    {effectiveNonCompliantCount.toLocaleString()}
                  </span>
                </button>
              )}
              {/* Violation breakdown — inline row (clickable for detail) */}
              {data.complianceViolations && Object.keys(data.complianceViolations).length > 0 && (
                <div className="pt-1 mt-1 border-t border-gray-200 dark:border-gray-600">
                  <div className="grid grid-cols-2 gap-1">
                    {Object.entries(data.complianceViolations)
                      .sort(([, a], [, b]) => b - a)
                      .map(([cat, count]) => (
                        <button
                          key={cat}
                          onClick={() => { setViolationDialogCategory(cat); setViolationDialogOpen(true); }}
                          className="flex items-center justify-between text-xs px-1.5 py-0.5 bg-red-50 dark:bg-red-900/10 rounded hover:bg-red-100 dark:hover:bg-red-900/20 transition-colors cursor-pointer group"
                        >
                          <span className="text-gray-500 dark:text-gray-400 group-hover:text-gray-700 dark:group-hover:text-gray-300 flex items-center gap-0.5">
                            {violationLabel[cat] ?? cat}
                            <Search className="w-2.5 h-2.5 opacity-0 group-hover:opacity-100 transition-opacity" />
                          </span>
                          <span className="font-semibold text-red-600 dark:text-red-400 ml-1">{count.toLocaleString()}</span>
                        </button>
                      ))}
                  </div>
                </div>
              )}
            </div>
          )}

          {/* Expiration + Revoked */}
          {(hasExpirationData || hasRevokedData) && (
            <div className="bg-gray-50 dark:bg-gray-700/30 rounded p-2 space-y-1">
              <div className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wide">{ t('common:label.validPeriod') }</div>
              <div className="flex gap-2">
                {(data.validPeriodCount ?? 0) > 0 && (
                  <div className="flex items-center gap-1 text-xs">
                    <FileCheck className="w-2.5 h-2.5 text-green-500" />
                    <span className="text-green-600 dark:text-green-400">{t('common:status.valid')}</span>
                    <span className="font-semibold text-green-700 dark:text-green-300">{(data.validPeriodCount ?? 0).toLocaleString()}</span>
                  </div>
                )}
                {data.expiredCount > 0 && (
                  <div className="flex items-center gap-1 text-xs">
                    <FileX className="w-2.5 h-2.5 text-red-500" />
                    <span className="text-red-600 dark:text-red-400">{t('common:status.expired')}</span>
                    <span className="font-semibold text-red-700 dark:text-red-300">{data.expiredCount.toLocaleString()}</span>
                  </div>
                )}
                {hasRevokedData && (
                  <div className="flex items-center gap-1 text-xs">
                    <XCircle className="w-2.5 h-2.5 text-orange-500" />
                    <span className="text-orange-600 dark:text-orange-400">{t('upload:validationSummary.revoked')}</span>
                    <span className="font-semibold text-orange-700 dark:text-orange-300">{(data.revokedCount ?? 0).toLocaleString()}</span>
                  </div>
                )}
              </div>
            </div>
          )}
        </div>
      )}

      {/* Distributions: CertType + SigAlgo + KeySize — 2-col grid */}
      {(hasCertTypeData || hasSigAlgoData || hasKeySizeData) && (
        <div className="grid grid-cols-1 md:grid-cols-2 gap-2">
          {/* Certificate Types */}
          {hasCertTypeData && (
            <div className="bg-gray-50 dark:bg-gray-700/30 rounded p-2">
              <div className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wide mb-1">{ t('certificate:detail.certificateType') }</div>
              <div className="flex flex-wrap gap-1.5">
                {Object.entries(data.certificateTypes!)
                  .sort(([, a], [, b]) => b - a)
                  .map(([type, count]) => (
                    <div key={type} className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-600 rounded px-2 py-0.5 text-center" title={getGlossaryTooltip(type === 'LINK_CERT' ? 'Link Certificate' : type)}>
                      <span className="text-xs font-bold text-gray-900 dark:text-gray-100">{count.toLocaleString()}</span>
                      <span className="text-xs text-gray-500 dark:text-gray-400 ml-1">{certTypeLabel(type)}</span>
                    </div>
                  ))}
              </div>
            </div>
          )}

          {/* Key Sizes */}
          {hasKeySizeData && (
            <div className="bg-gray-50 dark:bg-gray-700/30 rounded p-2">
              <div className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wide mb-1">{ t('ai:forensic.categories.keySize') }</div>
              <div className="flex flex-wrap gap-1.5">
                {Object.entries(data.keySizes!)
                  .sort(([a], [b]) => parseInt(b) - parseInt(a))
                  .map(([size, count]) => (
                    <div key={size} className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-600 rounded px-2 py-0.5 text-center">
                      <span className="text-xs font-bold text-blue-600 dark:text-blue-400">{count.toLocaleString()}</span>
                      <span className="text-xs text-gray-500 dark:text-gray-400 ml-1">{keySizeLabel(parseInt(size))}</span>
                    </div>
                  ))}
              </div>
            </div>
          )}

          {/* Signature Algorithms — full width */}
          {hasSigAlgoData && (
            <div className="bg-gray-50 dark:bg-gray-700/30 rounded p-2 md:col-span-2">
              <div className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wide mb-1">{ t('certificate:detail.signatureAlgorithm') }</div>
              <div className="space-y-1">
                {(() => {
                  const total = Object.values(data.signatureAlgorithms!).reduce((s, c) => s + c, 0);
                  return Object.entries(data.signatureAlgorithms!)
                    .sort(([, a], [, b]) => b - a)
                    .slice(0, 5)
                    .map(([algo, count]) => {
                      const pct = total > 0 ? Math.round((count / total) * 100) : 0;
                      return (
                        <div key={algo} className="space-y-0.5">
                          <div className="flex items-center justify-between text-xs">
                            <span className="text-gray-600 dark:text-gray-400 truncate mr-2">{algo}</span>
                            <span className="font-medium text-gray-900 dark:text-gray-100 whitespace-nowrap">
                              {count.toLocaleString()} ({pct}%)
                            </span>
                          </div>
                          <div className="w-full bg-gray-200 dark:bg-gray-700 rounded-full h-1">
                            <div className="bg-blue-500 h-1 rounded-full transition-all" style={{ width: `${pct}%` }} />
                          </div>
                        </div>
                      );
                    });
                })()}
              </div>
            </div>
          )}
        </div>
      )}

      {/* Validation Success Rate Bar */}
      {(() => {
        const total = data.validCount + data.invalidCount + data.pendingCount + expiredValidCount;
        if (total === 0) return null;
        const pct = Math.round(((data.validCount + expiredValidCount) / total) * 100);
        return (
          <div>
            <div className="flex justify-between text-xs mb-0.5">
              <span className="text-gray-500 dark:text-gray-400">{t('dashboard:successRate')}</span>
              <span className="font-semibold text-gray-900 dark:text-gray-100">{pct}%</span>
            </div>
            <div className="w-full bg-gray-200 dark:bg-gray-700 rounded-full h-1.5">
              <div className="bg-green-500 h-1.5 rounded-full transition-all" style={{ width: `${pct}%` }} />
            </div>
          </div>
        );
      })()}

      {/* ICAO Violation Detail Dialog */}
      {effectiveNonCompliantCount > 0 && uploadId && (
        <IcaoViolationDetailDialog
          open={violationDialogOpen}
          onClose={() => setViolationDialogOpen(false)}
          uploadId={uploadId}
          violations={data.complianceViolations ?? {}}
          totalNonCompliantCount={effectiveNonCompliantCount}
          initialCategory={violationDialogCategory}
        />
      )}
    </div>
  );
}
