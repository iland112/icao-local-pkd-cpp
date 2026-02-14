import { useState, useEffect, useRef } from 'react';
import {
  AlertTriangle,
  ChevronDown,
  ChevronUp,
  Database,
  FileX,
  Server,
} from 'lucide-react';
import type { ProcessingError } from '@/types';
import { cn } from '@/utils/cn';

interface ProcessingErrorsPanelProps {
  errors: ProcessingError[];
  totalErrorCount: number;
  parseErrorCount: number;
  dbSaveErrorCount: number;
  ldapSaveErrorCount: number;
  isProcessing: boolean;
}

const ERROR_TYPE_CONFIG: Record<string, { label: string; color: string; bgColor: string }> = {
  BASE64_DECODE_FAILED: { label: 'Base64', color: 'text-orange-700 dark:text-orange-300', bgColor: 'bg-orange-100 dark:bg-orange-900/30' },
  CERT_PARSE_FAILED: { label: 'Parse', color: 'text-orange-700 dark:text-orange-300', bgColor: 'bg-orange-100 dark:bg-orange-900/30' },
  CRL_PARSE_FAILED: { label: 'CRL Parse', color: 'text-orange-700 dark:text-orange-300', bgColor: 'bg-orange-100 dark:bg-orange-900/30' },
  ML_PARSE_FAILED: { label: 'ML Parse', color: 'text-orange-700 dark:text-orange-300', bgColor: 'bg-orange-100 dark:bg-orange-900/30' },
  ML_CERT_PARSE_FAILED: { label: 'ML Cert', color: 'text-orange-700 dark:text-orange-300', bgColor: 'bg-orange-100 dark:bg-orange-900/30' },
  DB_SAVE_FAILED: { label: 'DB', color: 'text-red-700 dark:text-red-300', bgColor: 'bg-red-100 dark:bg-red-900/30' },
  ML_CERT_SAVE_FAILED: { label: 'DB', color: 'text-red-700 dark:text-red-300', bgColor: 'bg-red-100 dark:bg-red-900/30' },
  LDAP_SAVE_FAILED: { label: 'LDAP', color: 'text-purple-700 dark:text-purple-300', bgColor: 'bg-purple-100 dark:bg-purple-900/30' },
  ML_LDAP_SAVE_FAILED: { label: 'LDAP', color: 'text-purple-700 dark:text-purple-300', bgColor: 'bg-purple-100 dark:bg-purple-900/30' },
  ENTRY_PROCESSING_EXCEPTION: { label: 'Exception', color: 'text-red-700 dark:text-red-300', bgColor: 'bg-red-100 dark:bg-red-900/30' },
};

function getErrorConfig(errorType: string) {
  return ERROR_TYPE_CONFIG[errorType] ?? { label: errorType, color: 'text-gray-700 dark:text-gray-300', bgColor: 'bg-gray-100 dark:bg-gray-900/30' };
}

function formatTimestamp(ts: string) {
  try {
    const d = new Date(ts);
    return d.toLocaleTimeString('ko-KR', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  } catch {
    return ts;
  }
}

export function ProcessingErrorsPanel({
  errors,
  totalErrorCount,
  parseErrorCount,
  dbSaveErrorCount,
  ldapSaveErrorCount,
  isProcessing,
}: ProcessingErrorsPanelProps) {
  const [isExpanded, setIsExpanded] = useState(false);
  const listRef = useRef<HTMLDivElement>(null);
  const prevErrorCount = useRef(0);

  // Auto-expand on first error
  useEffect(() => {
    if (totalErrorCount > 0 && prevErrorCount.current === 0) {
      setIsExpanded(true);
    }
    prevErrorCount.current = totalErrorCount;
  }, [totalErrorCount]);

  // Auto-scroll to bottom when new errors arrive
  useEffect(() => {
    if (isExpanded && listRef.current) {
      listRef.current.scrollTop = listRef.current.scrollHeight;
    }
  }, [errors.length, isExpanded]);

  if (totalErrorCount === 0) return null;

  return (
    <div className="bg-white dark:bg-gray-800 rounded-lg border border-red-200 dark:border-red-800 overflow-hidden">
      {/* Header - always visible */}
      <button
        onClick={() => setIsExpanded(!isExpanded)}
        className="w-full flex items-center justify-between px-4 py-3 hover:bg-red-50 dark:hover:bg-red-900/10 transition-colors"
      >
        <div className="flex items-center gap-3">
          <div className="p-1.5 rounded-lg bg-red-100 dark:bg-red-900/30">
            <AlertTriangle className="w-4 h-4 text-red-600 dark:text-red-400" />
          </div>
          <span className="text-sm font-semibold text-gray-900 dark:text-gray-100">
            처리 오류
          </span>
          <span className="inline-flex items-center justify-center px-2 py-0.5 rounded-full text-xs font-bold bg-red-100 text-red-700 dark:bg-red-900/40 dark:text-red-300">
            {totalErrorCount}
          </span>

          {/* Category badges */}
          <div className="flex items-center gap-1.5 ml-2">
            {parseErrorCount > 0 && (
              <span className="inline-flex items-center gap-1 px-1.5 py-0.5 rounded text-xs bg-orange-50 text-orange-600 dark:bg-orange-900/20 dark:text-orange-400">
                <FileX className="w-3 h-3" />
                {parseErrorCount}
              </span>
            )}
            {dbSaveErrorCount > 0 && (
              <span className="inline-flex items-center gap-1 px-1.5 py-0.5 rounded text-xs bg-red-50 text-red-600 dark:bg-red-900/20 dark:text-red-400">
                <Database className="w-3 h-3" />
                {dbSaveErrorCount}
              </span>
            )}
            {ldapSaveErrorCount > 0 && (
              <span className="inline-flex items-center gap-1 px-1.5 py-0.5 rounded text-xs bg-purple-50 text-purple-600 dark:bg-purple-900/20 dark:text-purple-400">
                <Server className="w-3 h-3" />
                {ldapSaveErrorCount}
              </span>
            )}
          </div>
        </div>

        <div className="flex items-center gap-2">
          {isProcessing && (
            <div className="animate-spin rounded-full h-3 w-3 border-b-2 border-red-500" />
          )}
          {isExpanded ? (
            <ChevronUp className="w-4 h-4 text-gray-500" />
          ) : (
            <ChevronDown className="w-4 h-4 text-gray-500" />
          )}
        </div>
      </button>

      {/* Error list - collapsible */}
      {isExpanded && (
        <div className="border-t border-red-200 dark:border-red-800">
          <div
            ref={listRef}
            className="max-h-64 overflow-y-auto divide-y divide-gray-100 dark:divide-gray-700/50"
          >
            {errors.map((error, idx) => {
              const config = getErrorConfig(error.errorType);
              return (
                <div key={idx} className="px-4 py-2.5 hover:bg-gray-50 dark:hover:bg-gray-700/30 transition-colors">
                  <div className="flex items-start gap-2">
                    <span className="text-xs text-gray-400 dark:text-gray-500 font-mono whitespace-nowrap mt-0.5">
                      {formatTimestamp(error.timestamp)}
                    </span>
                    <span className={cn(
                      'inline-flex items-center px-1.5 py-0.5 rounded text-xs font-medium whitespace-nowrap',
                      config.bgColor, config.color
                    )}>
                      {config.label}
                    </span>
                    {error.certificateType && (
                      <span className="inline-flex items-center px-1.5 py-0.5 rounded text-xs font-medium bg-gray-100 text-gray-600 dark:bg-gray-700 dark:text-gray-400 whitespace-nowrap">
                        {error.certificateType}
                      </span>
                    )}
                    {error.countryCode && (
                      <span className="inline-flex items-center px-1.5 py-0.5 rounded text-xs font-medium bg-blue-50 text-blue-600 dark:bg-blue-900/20 dark:text-blue-400 whitespace-nowrap">
                        {error.countryCode}
                      </span>
                    )}
                    <span className="text-xs text-gray-700 dark:text-gray-300 break-all">
                      {error.message}
                    </span>
                  </div>
                </div>
              );
            })}
          </div>

          {/* Overflow indicator */}
          {totalErrorCount > errors.length && (
            <div className="px-4 py-2 bg-gray-50 dark:bg-gray-700/30 border-t border-gray-100 dark:border-gray-700/50">
              <span className="text-xs text-gray-500 dark:text-gray-400">
                {totalErrorCount - errors.length}건의 추가 오류가 표시되지 않았습니다 (최근 {errors.length}건만 표시)
              </span>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
