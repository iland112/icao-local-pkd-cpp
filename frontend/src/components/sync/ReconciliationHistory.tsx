import { useState, useEffect } from 'react';
import {
  RefreshCw,
  CheckCircle,
  XCircle,
  Clock,
  AlertTriangle,
  ChevronRight,
  PlayCircle,
  Calendar,
  Zap,
} from 'lucide-react';
import { syncServiceApi, type ReconciliationSummary, type ReconciliationLog } from '@/services/api';
import { cn } from '@/utils/cn';
import { Dialog } from '@/components/common/Dialog';

export function ReconciliationHistory() {
  const [history, setHistory] = useState<ReconciliationSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [selectedItem, setSelectedItem] = useState<ReconciliationSummary | null>(null);
  const [logs, setLogs] = useState<ReconciliationLog[]>([]);
  const [loadingDetails, setLoadingDetails] = useState(false);

  const fetchHistory = async () => {
    try {
      const response = await syncServiceApi.getReconciliationHistory({ limit: 20 });
      setHistory(response.data.history);
    } catch (err) {
      console.error('Failed to fetch reconciliation history:', err);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchHistory();
  }, []);

  const handleViewDetails = async (item: ReconciliationSummary) => {
    setSelectedItem(item);
    setLoadingDetails(true);
    try {
      const response = await syncServiceApi.getReconciliationDetails(item.id);
      setLogs(response.data.logs);
    } catch (err) {
      console.error('Failed to fetch reconciliation details:', err);
    } finally {
      setLoadingDetails(false);
    }
  };

  const getStatusIcon = (status: string) => {
    switch (status) {
      case 'COMPLETED':
        return <CheckCircle className="w-5 h-5 text-green-500" />;
      case 'FAILED':
        return <XCircle className="w-5 h-5 text-red-500" />;
      case 'PARTIAL':
        return <AlertTriangle className="w-5 h-5 text-yellow-500" />;
      case 'IN_PROGRESS':
        return <Clock className="w-5 h-5 text-blue-500 animate-spin" />;
      default:
        return <Clock className="w-5 h-5 text-gray-400" />;
    }
  };

  const getTriggeredByIcon = (triggeredBy: string) => {
    switch (triggeredBy) {
      case 'MANUAL':
        return <PlayCircle className="w-4 h-4 text-blue-500" />;
      case 'AUTO':
        return <Zap className="w-4 h-4 text-purple-500" />;
      case 'DAILY_SYNC':
        return <Calendar className="w-4 h-4 text-green-500" />;
      default:
        return null;
    }
  };

  const getTriggeredByLabel = (triggeredBy: string) => {
    switch (triggeredBy) {
      case 'MANUAL':
        return '수동';
      case 'AUTO':
        return '자동';
      case 'DAILY_SYNC':
        return '일일 동기화';
      default:
        return triggeredBy;
    }
  };

  const formatTimestamp = (timestamp: string) => {
    const date = new Date(timestamp);
    return date.toLocaleString('ko-KR', {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    });
  };

  const formatDuration = (ms: number) => {
    if (ms < 1000) return `${ms}ms`;
    const seconds = Math.floor(ms / 1000);
    if (seconds < 60) return `${seconds}초`;
    const minutes = Math.floor(seconds / 60);
    const remainingSeconds = seconds % 60;
    return `${minutes}분 ${remainingSeconds}초`;
  };

  if (loading) {
    return (
      <div className="flex items-center justify-center py-12">
        <RefreshCw className="w-6 h-6 animate-spin text-blue-500" />
        <span className="ml-2 text-gray-600 dark:text-gray-400">로딩 중...</span>
      </div>
    );
  }

  return (
    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <h2 className="text-xl font-semibold text-gray-900 dark:text-white">
          자동 조정 기록
        </h2>
        <button
          onClick={fetchHistory}
          className="flex items-center gap-2 px-3 py-1.5 text-sm text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white transition-colors"
        >
          <RefreshCw className="w-4 h-4" />
          새로고침
        </button>
      </div>

      {history.length === 0 ? (
        <div className="text-center py-12 text-gray-500 dark:text-gray-400">
          자동 조정 기록이 없습니다.
        </div>
      ) : (
        <div className="bg-white dark:bg-gray-800 rounded-lg shadow overflow-hidden">
          <div className="overflow-x-auto">
            <table className="w-full text-sm">
              <thead className="bg-gray-50 dark:bg-gray-700 text-gray-700 dark:text-gray-300">
                <tr>
                  <th className="px-4 py-3 text-left font-medium">상태</th>
                  <th className="px-4 py-3 text-left font-medium">시작 시간</th>
                  <th className="px-4 py-3 text-left font-medium">트리거</th>
                  <th className="px-4 py-3 text-center font-medium">처리</th>
                  <th className="px-4 py-3 text-center font-medium">성공</th>
                  <th className="px-4 py-3 text-center font-medium">실패</th>
                  <th className="px-4 py-3 text-center font-medium">추가</th>
                  <th className="px-4 py-3 text-center font-medium">소요시간</th>
                  <th className="px-4 py-3 text-center font-medium">상세</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
                {history.map((item) => (
                  <tr
                    key={item.id}
                    className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors"
                  >
                    <td className="px-4 py-3">
                      <div className="flex items-center gap-2">
                        {getStatusIcon(item.status)}
                        <span className="text-xs font-medium">{item.status}</span>
                        {item.dryRun && (
                          <span className="px-2 py-0.5 text-xs bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-400 rounded">
                            DRY-RUN
                          </span>
                        )}
                      </div>
                    </td>
                    <td className="px-4 py-3 text-gray-900 dark:text-white">
                      {formatTimestamp(item.startedAt)}
                    </td>
                    <td className="px-4 py-3">
                      <div className="flex items-center gap-1.5">
                        {getTriggeredByIcon(item.triggeredBy)}
                        <span className="text-gray-600 dark:text-gray-400">
                          {getTriggeredByLabel(item.triggeredBy)}
                        </span>
                      </div>
                    </td>
                    <td className="px-4 py-3 text-center text-gray-900 dark:text-white">
                      {item.totalProcessed}
                    </td>
                    <td className="px-4 py-3 text-center text-green-600 dark:text-green-400 font-medium">
                      {item.successCount}
                    </td>
                    <td className="px-4 py-3 text-center text-red-600 dark:text-red-400 font-medium">
                      {item.failedCount}
                    </td>
                    <td className="px-4 py-3 text-center">
                      <div className="text-xs text-gray-600 dark:text-gray-400">
                        <div>CSCA: {item.cscaAdded}</div>
                        <div>DSC: {item.dscAdded}</div>
                        <div>NC: {item.dscNcAdded}</div>
                      </div>
                    </td>
                    <td className="px-4 py-3 text-center text-gray-600 dark:text-gray-400">
                      {formatDuration(item.durationMs)}
                    </td>
                    <td className="px-4 py-3 text-center">
                      <button
                        onClick={() => handleViewDetails(item)}
                        className="inline-flex items-center gap-1 text-blue-600 dark:text-blue-400 hover:text-blue-700 dark:hover:text-blue-300 transition-colors"
                      >
                        <span className="text-xs font-medium">상세</span>
                        <ChevronRight className="w-4 h-4" />
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}

      {/* Details Dialog */}
      {selectedItem && (
        <Dialog
          open={true}
          onClose={() => {
            setSelectedItem(null);
            setLogs([]);
          }}
          title={`자동 조정 상세 정보 #${selectedItem.id}`}
          maxWidth="4xl"
        >
          <div className="space-y-6">
            {/* Summary */}
            <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
              <div className="p-4 bg-gray-50 dark:bg-gray-700 rounded-lg">
                <div className="text-xs text-gray-500 dark:text-gray-400 mb-1">상태</div>
                <div className="flex items-center gap-2">
                  {getStatusIcon(selectedItem.status)}
                  <span className="font-medium text-gray-900 dark:text-white">
                    {selectedItem.status}
                  </span>
                </div>
              </div>
              <div className="p-4 bg-gray-50 dark:bg-gray-700 rounded-lg">
                <div className="text-xs text-gray-500 dark:text-gray-400 mb-1">트리거</div>
                <div className="flex items-center gap-2">
                  {getTriggeredByIcon(selectedItem.triggeredBy)}
                  <span className="font-medium text-gray-900 dark:text-white">
                    {getTriggeredByLabel(selectedItem.triggeredBy)}
                  </span>
                </div>
              </div>
              <div className="p-4 bg-gray-50 dark:bg-gray-700 rounded-lg">
                <div className="text-xs text-gray-500 dark:text-gray-400 mb-1">처리 건수</div>
                <div className="text-2xl font-bold text-gray-900 dark:text-white">
                  {selectedItem.totalProcessed}
                </div>
              </div>
              <div className="p-4 bg-gray-50 dark:bg-gray-700 rounded-lg">
                <div className="text-xs text-gray-500 dark:text-gray-400 mb-1">소요 시간</div>
                <div className="text-2xl font-bold text-gray-900 dark:text-white">
                  {formatDuration(selectedItem.durationMs)}
                </div>
              </div>
            </div>

            {/* Results Breakdown */}
            <div className="grid grid-cols-3 gap-4">
              <div className="p-4 bg-green-50 dark:bg-green-900/20 rounded-lg border border-green-200 dark:border-green-800">
                <div className="text-sm text-green-700 dark:text-green-300 mb-2">성공</div>
                <div className="text-3xl font-bold text-green-600 dark:text-green-400">
                  {selectedItem.successCount}
                </div>
              </div>
              <div className="p-4 bg-red-50 dark:bg-red-900/20 rounded-lg border border-red-200 dark:border-red-800">
                <div className="text-sm text-red-700 dark:text-red-300 mb-2">실패</div>
                <div className="text-3xl font-bold text-red-600 dark:text-red-400">
                  {selectedItem.failedCount}
                </div>
              </div>
              <div className="p-4 bg-blue-50 dark:bg-blue-900/20 rounded-lg border border-blue-200 dark:border-blue-800">
                <div className="text-sm text-blue-700 dark:text-blue-300 mb-2">추가된 인증서</div>
                <div className="space-y-1">
                  <div className="text-sm text-blue-600 dark:text-blue-400">
                    CSCA: <span className="font-bold">{selectedItem.cscaAdded}</span>
                  </div>
                  <div className="text-sm text-blue-600 dark:text-blue-400">
                    DSC: <span className="font-bold">{selectedItem.dscAdded}</span>
                  </div>
                  <div className="text-sm text-blue-600 dark:text-blue-400">
                    DSC_NC: <span className="font-bold">{selectedItem.dscNcAdded}</span>
                  </div>
                </div>
              </div>
            </div>

            {/* Operation Logs */}
            <div>
              <h3 className="text-lg font-semibold text-gray-900 dark:text-white mb-3">
                작업 로그
              </h3>
              {loadingDetails ? (
                <div className="flex items-center justify-center py-8">
                  <RefreshCw className="w-5 h-5 animate-spin text-blue-500 mr-2" />
                  <span className="text-gray-600 dark:text-gray-400">로딩 중...</span>
                </div>
              ) : logs.length === 0 ? (
                <div className="text-center py-8 text-gray-500 dark:text-gray-400">
                  작업 로그가 없습니다.
                </div>
              ) : (
                <div className="max-h-96 overflow-y-auto border border-gray-200 dark:border-gray-700 rounded-lg">
                  <table className="w-full text-sm">
                    <thead className="bg-gray-50 dark:bg-gray-700 sticky top-0">
                      <tr>
                        <th className="px-3 py-2 text-left text-xs font-medium text-gray-700 dark:text-gray-300">
                          작업
                        </th>
                        <th className="px-3 py-2 text-left text-xs font-medium text-gray-700 dark:text-gray-300">
                          타입
                        </th>
                        <th className="px-3 py-2 text-left text-xs font-medium text-gray-700 dark:text-gray-300">
                          국가
                        </th>
                        <th className="px-3 py-2 text-left text-xs font-medium text-gray-700 dark:text-gray-300">
                          Subject
                        </th>
                        <th className="px-3 py-2 text-center text-xs font-medium text-gray-700 dark:text-gray-300">
                          상태
                        </th>
                        <th className="px-3 py-2 text-center text-xs font-medium text-gray-700 dark:text-gray-300">
                          시간
                        </th>
                      </tr>
                    </thead>
                    <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
                      {logs.map((log) => (
                        <tr
                          key={log.id}
                          className={cn(
                            'hover:bg-gray-50 dark:hover:bg-gray-700/50',
                            log.status === 'FAILED' && 'bg-red-50 dark:bg-red-900/10'
                          )}
                        >
                          <td className="px-3 py-2 text-xs">
                            <span
                              className={cn(
                                'px-2 py-1 rounded font-medium',
                                log.operation === 'ADD' &&
                                  'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-300',
                                log.operation === 'DELETE' &&
                                  'bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-300'
                              )}
                            >
                              {log.operation}
                            </span>
                          </td>
                          <td className="px-3 py-2 text-xs text-gray-600 dark:text-gray-400">
                            {log.certType}
                          </td>
                          <td className="px-3 py-2 text-xs text-gray-600 dark:text-gray-400">
                            {log.countryCode || '-'}
                          </td>
                          <td className="px-3 py-2 text-xs text-gray-900 dark:text-white max-w-md truncate">
                            {log.subject || '-'}
                          </td>
                          <td className="px-3 py-2 text-center">
                            {log.status === 'SUCCESS' ? (
                              <CheckCircle className="w-4 h-4 text-green-500 mx-auto" />
                            ) : (
                              <XCircle className="w-4 h-4 text-red-500 mx-auto" />
                            )}
                          </td>
                          <td className="px-3 py-2 text-center text-xs text-gray-600 dark:text-gray-400">
                            {log.durationMs}ms
                          </td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              )}
            </div>

            {selectedItem.errorMessage && (
              <div className="p-4 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg">
                <div className="text-sm font-medium text-red-700 dark:text-red-300 mb-1">
                  에러 메시지
                </div>
                <div className="text-sm text-red-600 dark:text-red-400 font-mono">
                  {selectedItem.errorMessage}
                </div>
              </div>
            )}
          </div>
        </Dialog>
      )}
    </div>
  );
}
