import { useMemo } from 'react';
import {
  CheckCircle,
  XCircle,
  AlertTriangle,
  Shield,
  ShieldCheck,
  FileCheck,
  FileX
} from 'lucide-react';

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

interface ValidationSummaryPanelProps {
  data: ValidationSummaryData;
  title?: string;
  isProcessing?: boolean;
}

export function ValidationSummaryPanel({
  data,
  title = '검증 결과 요약',
  isProcessing = false,
}: ValidationSummaryPanelProps) {
  const expiredValidCount = data.expiredValidCount ?? 0;

  // Validation rate
  const validationRate = useMemo(() => {
    const total = data.processedCount ?? (data.validCount + data.invalidCount + data.pendingCount + expiredValidCount);
    if (total === 0) return 0;
    return Math.round((data.validCount / total) * 100);
  }, [data.validCount, data.invalidCount, data.pendingCount, data.processedCount, expiredValidCount]);

  // Trust chain success rate
  const trustChainSuccessRate = useMemo(() => {
    const total = data.trustChainValidCount + data.trustChainInvalidCount;
    if (total === 0) return 0;
    return Math.round((data.trustChainValidCount / total) * 100);
  }, [data.trustChainValidCount, data.trustChainInvalidCount]);

  // ICAO compliance rate
  const icaoComplianceRate = useMemo(() => {
    const total = (data.icaoCompliantCount ?? 0) + (data.icaoNonCompliantCount ?? 0) + (data.icaoWarningCount ?? 0);
    if (total === 0) return 0;
    return Math.round(((data.icaoCompliantCount ?? 0) / total) * 100);
  }, [data.icaoCompliantCount, data.icaoNonCompliantCount, data.icaoWarningCount]);

  // Section visibility
  const hasTrustChainData = data.trustChainValidCount > 0 || data.trustChainInvalidCount > 0 || data.cscaNotFoundCount > 0;
  const hasIcaoData = (data.icaoCompliantCount ?? 0) > 0 || (data.icaoNonCompliantCount ?? 0) > 0 || (data.icaoWarningCount ?? 0) > 0;
  const hasExpirationData = data.expiredCount > 0 || (data.validPeriodCount ?? 0) > 0;
  const hasRevokedData = (data.revokedCount ?? 0) > 0;
  const hasCertTypeData = data.certificateTypes && Object.keys(data.certificateTypes).length > 0;
  const hasSigAlgoData = data.signatureAlgorithms && Object.keys(data.signatureAlgorithms).length > 0;
  const hasKeySizeData = data.keySizes && Object.keys(data.keySizes).length > 0;

  // Dynamic summary cards
  type CardInfo = { key: string; label: string; value: number; subtitle: string; color: string; icon: 'check' | 'alert' | 'clock' | 'x' };
  const activeCards: CardInfo[] = [];

  if (data.validCount > 0) {
    activeCards.push({ key: 'valid', label: '유효', value: data.validCount, subtitle: `${validationRate}%`, color: 'green', icon: 'check' });
  }
  if (expiredValidCount > 0) {
    activeCards.push({ key: 'expired-valid', label: '만료-유효', value: expiredValidCount, subtitle: '서명유효/기간만료', color: 'amber', icon: 'alert' });
  }
  if (data.pendingCount > 0) {
    activeCards.push({ key: 'pending', label: '보류', value: data.pendingCount, subtitle: 'CSCA 미등록', color: 'yellow', icon: 'clock' });
  }
  if (data.invalidCount > 0) {
    activeCards.push({ key: 'invalid', label: '무효', value: data.invalidCount, subtitle: '신뢰체인 실패', color: 'red', icon: 'x' });
  }
  if ((data.errorCount ?? 0) > 0) {
    activeCards.push({ key: 'error', label: '오류', value: data.errorCount!, subtitle: '처리 오류', color: 'gray', icon: 'x' });
  }

  const cardColorMap: Record<string, { bg: string; border: string; text: string; bold: string }> = {
    green: { bg: 'bg-green-50 dark:bg-green-900/20', border: 'border-green-200 dark:border-green-800', text: 'text-green-600 dark:text-green-400', bold: 'text-green-700 dark:text-green-300' },
    amber: { bg: 'bg-amber-50 dark:bg-amber-900/20', border: 'border-amber-200 dark:border-amber-800', text: 'text-amber-600 dark:text-amber-400', bold: 'text-amber-700 dark:text-amber-300' },
    yellow: { bg: 'bg-yellow-50 dark:bg-yellow-900/20', border: 'border-yellow-200 dark:border-yellow-800', text: 'text-yellow-600 dark:text-yellow-400', bold: 'text-yellow-700 dark:text-yellow-300' },
    red: { bg: 'bg-red-50 dark:bg-red-900/20', border: 'border-red-200 dark:border-red-800', text: 'text-red-600 dark:text-red-400', bold: 'text-red-700 dark:text-red-300' },
    gray: { bg: 'bg-gray-100 dark:bg-gray-700/50', border: 'border-gray-300 dark:border-gray-600', text: 'text-gray-600 dark:text-gray-400', bold: 'text-gray-600 dark:text-gray-400' },
    blue: { bg: 'bg-blue-50 dark:bg-blue-900/20', border: 'border-blue-200 dark:border-blue-800', text: 'text-blue-600 dark:text-blue-400', bold: 'text-blue-700 dark:text-blue-300' },
  };

  // Grid columns: processedCount card (if available) + active cards
  const hasProcessedCard = data.processedCount != null && data.totalCertificates != null;
  const totalCards = (hasProcessedCard ? 1 : 0) + activeCards.length;
  const gridColsClass = totalCards <= 2 ? 'md:grid-cols-2' : totalCards <= 3 ? 'md:grid-cols-3' : totalCards <= 4 ? 'md:grid-cols-4' : 'md:grid-cols-5';

  // Compliance violation label map
  const violationLabelMap: Record<string, string> = {
    keyUsage: 'Key Usage',
    algorithm: '서명 알고리즘',
    keySize: '키 크기',
    validityPeriod: '유효 기간',
    dnFormat: 'DN 형식',
    extensions: '확장 필드',
  };

  return (
    <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4 space-y-4">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <ShieldCheck className="w-4 h-4 text-blue-600 dark:text-blue-400" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">{title}</h3>
        </div>
        {isProcessing && (
          <div className="flex items-center gap-2 text-sm text-blue-600 dark:text-blue-400">
            <div className="animate-spin rounded-full h-4 w-4 border-b-2 border-blue-600" />
            <span>처리 중...</span>
          </div>
        )}
      </div>

      {/* Summary Cards */}
      {totalCards > 0 && (
        <div className={`grid grid-cols-2 ${gridColsClass} gap-2`}>
          {/* Processed count card (SSE mode) */}
          {hasProcessedCard && (
            <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg p-2.5 text-center">
              <p className="text-xl font-bold text-blue-700 dark:text-blue-300">
                {data.processedCount!.toLocaleString()}
              </p>
              <span className="text-xs text-blue-600 dark:text-blue-400">
                처리됨 / {data.totalCertificates!.toLocaleString()}
              </span>
            </div>
          )}

          {activeCards.map((card) => {
            const colors = cardColorMap[card.color];
            return (
              <div key={card.key} className={`${colors.bg} border ${colors.border} rounded-lg p-2.5 text-center`}>
                <p className={`text-xl font-bold ${colors.bold}`}>
                  {card.value.toLocaleString()}
                </p>
                <span className={`text-xs ${colors.text}`}>{card.label}</span>
              </div>
            );
          })}
        </div>
      )}

      {/* Trust Chain & ICAO Compliance */}
      {(hasTrustChainData || hasIcaoData) && (
        <div className={`grid grid-cols-1 ${hasTrustChainData && hasIcaoData ? 'md:grid-cols-2' : ''} gap-3`}>
          {/* Trust Chain */}
          {hasTrustChainData && (
            <div className="bg-gray-50 dark:bg-gray-700/30 rounded-lg p-3 space-y-1.5">
              <div className="flex items-center gap-2 mb-1">
                <Shield className="w-3.5 h-3.5 text-gray-600 dark:text-gray-400" />
                <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wide">
                  신뢰 체인 검증
                </h4>
              </div>
              <div className="flex justify-between items-center text-xs">
                <span className="text-green-600 dark:text-green-400 flex items-center gap-1">
                  <CheckCircle className="w-3 h-3" /> 성공
                </span>
                <span className="font-semibold text-green-700 dark:text-green-300">
                  {data.trustChainValidCount.toLocaleString()} ({trustChainSuccessRate}%)
                </span>
              </div>
              <div className="flex justify-between items-center text-xs">
                <span className="text-red-600 dark:text-red-400 flex items-center gap-1">
                  <XCircle className="w-3 h-3" /> 실패
                </span>
                <span className="font-semibold text-red-700 dark:text-red-300">
                  {data.trustChainInvalidCount.toLocaleString()}
                </span>
              </div>
              <div className="flex justify-between items-center text-xs">
                <span className="text-yellow-600 dark:text-yellow-400 flex items-center gap-1">
                  <AlertTriangle className="w-3 h-3" /> CSCA 미발견
                </span>
                <span className="font-semibold text-yellow-700 dark:text-yellow-300">
                  {data.cscaNotFoundCount.toLocaleString()}
                </span>
              </div>
            </div>
          )}

          {/* ICAO 9303 Compliance */}
          {hasIcaoData && (
            <div className="bg-gray-50 dark:bg-gray-700/30 rounded-lg p-3 space-y-1.5">
              <div className="flex items-center gap-2 mb-1">
                <ShieldCheck className="w-3.5 h-3.5 text-gray-600 dark:text-gray-400" />
                <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wide">
                  ICAO 9303 준수
                </h4>
              </div>
              <div className="flex justify-between items-center text-xs">
                <span className="text-green-600 dark:text-green-400 flex items-center gap-1">
                  <ShieldCheck className="w-3 h-3" /> 준수
                </span>
                <span className="font-semibold text-green-700 dark:text-green-300">
                  {(data.icaoCompliantCount ?? 0).toLocaleString()} ({icaoComplianceRate}%)
                </span>
              </div>
              {(data.icaoNonCompliantCount ?? 0) > 0 && (
                <div className="flex justify-between items-center text-xs">
                  <span className="text-red-600 dark:text-red-400 flex items-center gap-1">
                    <XCircle className="w-3 h-3" /> 미준수
                  </span>
                  <span className="font-semibold text-red-700 dark:text-red-300">
                    {(data.icaoNonCompliantCount ?? 0).toLocaleString()}
                  </span>
                </div>
              )}

              {/* Per-category violation breakdown */}
              {data.complianceViolations && Object.keys(data.complianceViolations).length > 0 && (
                <div className="mt-2 pt-2 border-t border-gray-200 dark:border-gray-600">
                  <div className="text-xs font-medium text-gray-500 dark:text-gray-400 mb-1.5">
                    미준수 항목별 상세
                  </div>
                  <div className="grid grid-cols-2 gap-1.5">
                    {Object.entries(data.complianceViolations)
                      .sort(([, a], [, b]) => b - a)
                      .map(([category, count]) => (
                        <div key={category} className="flex items-center justify-between text-xs px-2 py-1 bg-red-50 dark:bg-red-900/10 rounded">
                          <span className="text-gray-600 dark:text-gray-400">
                            {violationLabelMap[category] ?? category}
                          </span>
                          <span className="font-semibold text-red-600 dark:text-red-400">
                            {count.toLocaleString()}
                          </span>
                        </div>
                      ))}
                  </div>
                </div>
              )}
            </div>
          )}
        </div>
      )}

      {/* Certificate Status: Expiration + Revoked */}
      {(hasExpirationData || hasRevokedData) && (
        <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-3">
          <div className="text-xs font-medium text-gray-700 dark:text-gray-300 mb-2">
            인증서 유효 기간
          </div>
          <div className={`grid ${hasRevokedData ? 'grid-cols-3' : 'grid-cols-2'} gap-2 text-sm`}>
            {(data.validPeriodCount ?? 0) > 0 && (
              <div className="flex items-center justify-between px-3 py-1.5 bg-green-50 dark:bg-green-900/20 rounded">
                <span className="text-green-600 dark:text-green-400 flex items-center gap-1">
                  <FileCheck className="w-3 h-3" /> 유효
                </span>
                <span className="font-semibold text-green-700 dark:text-green-300">
                  {(data.validPeriodCount ?? 0).toLocaleString()}
                </span>
              </div>
            )}
            {data.expiredCount > 0 && (
              <div className="flex items-center justify-between px-3 py-1.5 bg-red-50 dark:bg-red-900/20 rounded">
                <span className="text-red-600 dark:text-red-400 flex items-center gap-1">
                  <FileX className="w-3 h-3" /> 만료
                </span>
                <span className="font-semibold text-red-700 dark:text-red-300">
                  {data.expiredCount.toLocaleString()}
                </span>
              </div>
            )}
            {hasRevokedData && (
              <div className="flex items-center justify-between px-3 py-1.5 bg-orange-50 dark:bg-orange-900/20 rounded">
                <span className="text-orange-600 dark:text-orange-400 flex items-center gap-1">
                  <XCircle className="w-3 h-3" /> 해지
                </span>
                <span className="font-semibold text-orange-700 dark:text-orange-300">
                  {(data.revokedCount ?? 0).toLocaleString()}
                </span>
              </div>
            )}
          </div>
        </div>
      )}

      {/* Certificate Types Distribution */}
      {hasCertTypeData && (
        <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-3">
          <div className="text-xs font-medium text-gray-700 dark:text-gray-300 mb-2">
            인증서 유형별 분포
          </div>
          <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-5 gap-2">
            {Object.entries(data.certificateTypes!)
              .sort(([, a], [, b]) => b - a)
              .map(([type, count]) => (
                <div key={type} className="bg-gray-50 dark:bg-gray-700/50 rounded px-2 py-1.5 text-center">
                  <div className="text-xs text-gray-600 dark:text-gray-400">{type}</div>
                  <div className="text-base font-bold text-gray-900 dark:text-gray-100">
                    {count.toLocaleString()}
                  </div>
                </div>
              ))}
          </div>
        </div>
      )}

      {/* Signature Algorithms Distribution */}
      {hasSigAlgoData && (
        <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-3">
          <div className="text-xs font-medium text-gray-700 dark:text-gray-300 mb-2">
            서명 알고리즘 분포
          </div>
          <div className="space-y-1.5">
            {(() => {
              const sigAlgoTotal = Object.values(data.signatureAlgorithms!).reduce((sum, c) => sum + c, 0);
              return Object.entries(data.signatureAlgorithms!)
              .sort(([, a], [, b]) => b - a)
              .slice(0, 5)
              .map(([algorithm, count]) => {
                const percentage = sigAlgoTotal > 0 ? Math.round((count / sigAlgoTotal) * 100) : 0;
                return (
                  <div key={algorithm} className="space-y-0.5">
                    <div className="flex items-center justify-between text-xs">
                      <span className="text-gray-600 dark:text-gray-400">{algorithm}</span>
                      <span className="font-medium text-gray-900 dark:text-gray-100">
                        {count.toLocaleString()} ({percentage}%)
                      </span>
                    </div>
                    <div className="w-full bg-gray-200 dark:bg-gray-700 rounded-full h-1.5">
                      <div
                        className="bg-blue-600 dark:bg-blue-500 h-1.5 rounded-full transition-all duration-300"
                        style={{ width: `${percentage}%` }}
                      />
                    </div>
                  </div>
                );
              });
            })()}
          </div>
        </div>
      )}

      {/* Key Sizes Distribution */}
      {hasKeySizeData && (
        <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-3">
          <div className="text-xs font-medium text-gray-700 dark:text-gray-300 mb-2">
            키 크기 분포
          </div>
          <div className="grid grid-cols-3 sm:grid-cols-4 md:grid-cols-6 gap-2">
            {Object.entries(data.keySizes!)
              .sort(([a], [b]) => parseInt(b) - parseInt(a))
              .map(([size, count]) => (
                <div key={size} className="bg-gray-50 dark:bg-gray-700/50 rounded px-2 py-1.5 text-center">
                  <div className="text-xs font-medium text-gray-900 dark:text-gray-100">{size} bits</div>
                  <div className="text-sm font-bold text-blue-600 dark:text-blue-400">
                    {count.toLocaleString()}
                  </div>
                </div>
              ))}
          </div>
        </div>
      )}

      {/* Validation Success Rate Bar */}
      {(() => {
        const total = data.validCount + data.invalidCount + data.pendingCount + expiredValidCount;
        if (total === 0) return null;
        const validPct = Math.round(((data.validCount + expiredValidCount) / total) * 100);
        return (
          <div>
            <div className="flex justify-between text-xs mb-1">
              <span className="text-gray-600 dark:text-gray-400">검증 성공률</span>
              <span className="font-semibold text-gray-900 dark:text-gray-100">{validPct}%</span>
            </div>
            <div className="w-full bg-gray-200 dark:bg-gray-700 rounded-full h-2">
              <div
                className="bg-green-500 h-2 rounded-full transition-all"
                style={{ width: `${validPct}%` }}
              />
            </div>
          </div>
        );
      })()}
    </div>
  );
}
