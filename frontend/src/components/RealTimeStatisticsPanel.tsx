import { useMemo } from 'react';
import {
  CheckCircle,
  XCircle,
  Clock,
  AlertTriangle,
  Shield,
  ShieldCheck,
  ShieldAlert,
  FileCheck,
  FileClock,
  FileX
} from 'lucide-react';
import type { ValidationStatistics } from '@/types';

interface RealTimeStatisticsPanelProps {
  statistics: ValidationStatistics;
  isProcessing: boolean;
}

export function RealTimeStatisticsPanel({ statistics, isProcessing }: RealTimeStatisticsPanelProps) {
  // Calculate percentages
  const validationRate = useMemo(() => {
    if (statistics.processedCount === 0) return 0;
    return Math.round((statistics.validCount / statistics.processedCount) * 100);
  }, [statistics.processedCount, statistics.validCount]);

  const trustChainSuccessRate = useMemo(() => {
    const total = statistics.trustChainValidCount + statistics.trustChainInvalidCount;
    if (total === 0) return 0;
    return Math.round((statistics.trustChainValidCount / total) * 100);
  }, [statistics.trustChainValidCount, statistics.trustChainInvalidCount]);

  const icaoComplianceRate = useMemo(() => {
    const total = statistics.icaoCompliantCount + statistics.icaoNonCompliantCount + statistics.icaoWarningCount;
    if (total === 0) return 0;
    return Math.round((statistics.icaoCompliantCount / total) * 100);
  }, [statistics.icaoCompliantCount, statistics.icaoNonCompliantCount, statistics.icaoWarningCount]);

  return (
    <div className="bg-white dark:bg-gray-800 rounded-lg border border-gray-200 dark:border-gray-700 p-6 space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <h3 className="text-lg font-semibold text-gray-900 dark:text-gray-100">
          실시간 검증 통계
        </h3>
        {isProcessing && (
          <div className="flex items-center gap-2 text-sm text-blue-600 dark:text-blue-400">
            <div className="animate-spin rounded-full h-4 w-4 border-b-2 border-blue-600" />
            <span>처리 중...</span>
          </div>
        )}
      </div>

      {/* Overall Progress */}
      <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
        {/* Total Processed */}
        <div className="bg-blue-50 dark:bg-blue-900/20 rounded-lg p-4">
          <div className="flex items-center gap-2 text-blue-600 dark:text-blue-400 mb-2">
            <FileCheck className="w-4 h-4" />
            <span className="text-xs font-medium">처리됨</span>
          </div>
          <div className="text-2xl font-bold text-blue-700 dark:text-blue-300">
            {statistics.processedCount.toLocaleString()}
          </div>
          <div className="text-xs text-blue-600 dark:text-blue-400 mt-1">
            / {statistics.totalCertificates.toLocaleString()}
          </div>
        </div>

        {/* Valid */}
        <div className="bg-green-50 dark:bg-green-900/20 rounded-lg p-4">
          <div className="flex items-center gap-2 text-green-600 dark:text-green-400 mb-2">
            <CheckCircle className="w-4 h-4" />
            <span className="text-xs font-medium">유효</span>
          </div>
          <div className="text-2xl font-bold text-green-700 dark:text-green-300">
            {statistics.validCount.toLocaleString()}
          </div>
          <div className="text-xs text-green-600 dark:text-green-400 mt-1">
            {validationRate}%
          </div>
        </div>

        {/* Pending */}
        <div className="bg-yellow-50 dark:bg-yellow-900/20 rounded-lg p-4">
          <div className="flex items-center gap-2 text-yellow-600 dark:text-yellow-400 mb-2">
            <Clock className="w-4 h-4" />
            <span className="text-xs font-medium">대기</span>
          </div>
          <div className="text-2xl font-bold text-yellow-700 dark:text-yellow-300">
            {statistics.pendingCount.toLocaleString()}
          </div>
          <div className="text-xs text-yellow-600 dark:text-yellow-400 mt-1">
            만료/검증대기
          </div>
        </div>

        {/* Invalid */}
        <div className="bg-red-50 dark:bg-red-900/20 rounded-lg p-4">
          <div className="flex items-center gap-2 text-red-600 dark:text-red-400 mb-2">
            <XCircle className="w-4 h-4" />
            <span className="text-xs font-medium">무효</span>
          </div>
          <div className="text-2xl font-bold text-red-700 dark:text-red-300">
            {statistics.invalidCount.toLocaleString()}
          </div>
          <div className="text-xs text-red-600 dark:text-red-400 mt-1">
            신뢰체인 실패
          </div>
        </div>
      </div>

      {/* Trust Chain & ICAO Compliance */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {/* Trust Chain Status */}
        <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
          <div className="flex items-center gap-2 mb-3">
            <Shield className="w-4 h-4 text-gray-600 dark:text-gray-400" />
            <span className="text-sm font-medium text-gray-700 dark:text-gray-300">
              신뢰 체인 검증
            </span>
          </div>
          <div className="space-y-2">
            <div className="flex items-center justify-between text-sm">
              <span className="text-green-600 dark:text-green-400 flex items-center gap-1">
                <CheckCircle className="w-3 h-3" />
                유효
              </span>
              <span className="font-semibold text-green-700 dark:text-green-300">
                {statistics.trustChainValidCount.toLocaleString()} ({trustChainSuccessRate}%)
              </span>
            </div>
            <div className="flex items-center justify-between text-sm">
              <span className="text-red-600 dark:text-red-400 flex items-center gap-1">
                <XCircle className="w-3 h-3" />
                무효
              </span>
              <span className="font-semibold text-red-700 dark:text-red-300">
                {statistics.trustChainInvalidCount.toLocaleString()}
              </span>
            </div>
            <div className="flex items-center justify-between text-sm">
              <span className="text-yellow-600 dark:text-yellow-400 flex items-center gap-1">
                <AlertTriangle className="w-3 h-3" />
                CSCA 미발견
              </span>
              <span className="font-semibold text-yellow-700 dark:text-yellow-300">
                {statistics.cscaNotFoundCount.toLocaleString()}
              </span>
            </div>
          </div>
        </div>

        {/* ICAO 9303 Compliance */}
        <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
          <div className="flex items-center gap-2 mb-3">
            <ShieldCheck className="w-4 h-4 text-gray-600 dark:text-gray-400" />
            <span className="text-sm font-medium text-gray-700 dark:text-gray-300">
              ICAO 9303 준수
            </span>
          </div>
          <div className="space-y-2">
            <div className="flex items-center justify-between text-sm">
              <span className="text-green-600 dark:text-green-400 flex items-center gap-1">
                <ShieldCheck className="w-3 h-3" />
                준수
              </span>
              <span className="font-semibold text-green-700 dark:text-green-300">
                {statistics.icaoCompliantCount.toLocaleString()} ({icaoComplianceRate}%)
              </span>
            </div>
            <div className="flex items-center justify-between text-sm">
              <span className="text-yellow-600 dark:text-yellow-400 flex items-center gap-1">
                <ShieldAlert className="w-3 h-3" />
                경고
              </span>
              <span className="font-semibold text-yellow-700 dark:text-yellow-300">
                {statistics.icaoWarningCount.toLocaleString()}
              </span>
            </div>
            <div className="flex items-center justify-between text-sm">
              <span className="text-red-600 dark:text-red-400 flex items-center gap-1">
                <XCircle className="w-3 h-3" />
                미준수
              </span>
              <span className="font-semibold text-red-700 dark:text-red-300">
                {statistics.icaoNonCompliantCount.toLocaleString()}
              </span>
            </div>
          </div>
        </div>
      </div>

      {/* Certificate Types Distribution */}
      {Object.keys(statistics.certificateTypes).length > 0 && (
        <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
          <div className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-3">
            인증서 유형별 분포
          </div>
          <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-5 gap-3">
            {Object.entries(statistics.certificateTypes)
              .sort(([, a], [, b]) => b - a)
              .map(([type, count]) => (
                <div key={type} className="bg-gray-50 dark:bg-gray-700/50 rounded px-3 py-2">
                  <div className="text-xs text-gray-600 dark:text-gray-400">{type}</div>
                  <div className="text-lg font-bold text-gray-900 dark:text-gray-100">
                    {count.toLocaleString()}
                  </div>
                </div>
              ))}
          </div>
        </div>
      )}

      {/* Signature Algorithms Distribution */}
      {Object.keys(statistics.signatureAlgorithms).length > 0 && (
        <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
          <div className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-3">
            서명 알고리즘 분포
          </div>
          <div className="space-y-2">
            {Object.entries(statistics.signatureAlgorithms)
              .sort(([, a], [, b]) => b - a)
              .slice(0, 5)
              .map(([algorithm, count]) => {
                const percentage = Math.round((count / statistics.processedCount) * 100);
                return (
                  <div key={algorithm} className="space-y-1">
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
              })}
          </div>
        </div>
      )}

      {/* Key Sizes Distribution */}
      {Object.keys(statistics.keySizes).length > 0 && (
        <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
          <div className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-3">
            키 크기 분포
          </div>
          <div className="grid grid-cols-3 sm:grid-cols-4 md:grid-cols-6 gap-2">
            {Object.entries(statistics.keySizes)
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

      {/* Expiration Status */}
      <div className="grid grid-cols-3 gap-3 text-sm">
        <div className="flex items-center justify-between px-3 py-2 bg-red-50 dark:bg-red-900/20 rounded">
          <span className="text-red-600 dark:text-red-400 flex items-center gap-1">
            <FileX className="w-3 h-3" />
            만료됨
          </span>
          <span className="font-semibold text-red-700 dark:text-red-300">
            {statistics.expiredCount.toLocaleString()}
          </span>
        </div>
        <div className="flex items-center justify-between px-3 py-2 bg-green-50 dark:bg-green-900/20 rounded">
          <span className="text-green-600 dark:text-green-400 flex items-center gap-1">
            <FileCheck className="w-3 h-3" />
            유효 기간
          </span>
          <span className="font-semibold text-green-700 dark:text-green-300">
            {statistics.validPeriodCount.toLocaleString()}
          </span>
        </div>
        <div className="flex items-center justify-between px-3 py-2 bg-yellow-50 dark:bg-yellow-900/20 rounded">
          <span className="text-yellow-600 dark:text-yellow-400 flex items-center gap-1">
            <FileClock className="w-3 h-3" />
            미도래
          </span>
          <span className="font-semibold text-yellow-700 dark:text-yellow-300">
            {statistics.notYetValidCount.toLocaleString()}
          </span>
        </div>
      </div>
    </div>
  );
}
