import type { ValidationStatistics } from '@/types';
import { ValidationSummaryPanel } from './ValidationSummaryPanel';
import type { ValidationSummaryData } from './ValidationSummaryPanel';

interface RealTimeStatisticsPanelProps {
  statistics: ValidationStatistics;
  isProcessing: boolean;
}

/**
 * Thin wrapper that maps SSE ValidationStatistics to the shared ValidationSummaryPanel.
 * Preserves backward compatibility with existing FileUpload.tsx usage.
 */
export function RealTimeStatisticsPanel({ statistics, isProcessing }: RealTimeStatisticsPanelProps) {
  const data: ValidationSummaryData = {
    validCount: statistics.validCount,
    invalidCount: statistics.invalidCount,
    pendingCount: statistics.pendingCount,
    expiredValidCount: statistics.expiredValidCount,
    totalCertificates: statistics.totalCertificates,
    processedCount: statistics.processedCount,
    trustChainValidCount: statistics.trustChainValidCount,
    trustChainInvalidCount: statistics.trustChainInvalidCount,
    cscaNotFoundCount: statistics.cscaNotFoundCount,
    expiredCount: statistics.expiredCount,
    validPeriodCount: statistics.validPeriodCount,
    revokedCount: statistics.revokedCount,
    icaoCompliantCount: statistics.icaoCompliantCount,
    icaoNonCompliantCount: statistics.icaoNonCompliantCount,
    icaoWarningCount: statistics.icaoWarningCount,
    complianceViolations: statistics.complianceViolations,
    certificateTypes: statistics.certificateTypes,
    signatureAlgorithms: statistics.signatureAlgorithms,
    keySizes: statistics.keySizes,
  };

  return (
    <ValidationSummaryPanel
      data={data}
      title="실시간 검증 통계"
      isProcessing={isProcessing}
    />
  );
}
