import { useState, useEffect, useCallback } from 'react';
import {
  RefreshCw,
  CheckCircle,
  AlertTriangle,
  XCircle,
  Database,
  Server,
  ArrowRightLeft,
  Clock,
  Activity,
  Loader2,
  Play,
  History,
  Settings,
  CalendarClock,
  ShieldCheck,
  RotateCcw,
} from 'lucide-react';
import { syncServiceApi, type SyncConfigResponse, type RevalidationHistoryItem } from '@/services/api';
import type { SyncStatusResponse, SyncHistoryItem, SyncStatusType } from '@/types';
import { cn } from '@/utils/cn';

export function SyncDashboard() {
  const [status, setStatus] = useState<SyncStatusResponse | null>(null);
  const [history, setHistory] = useState<SyncHistoryItem[]>([]);
  const [config, setConfig] = useState<SyncConfigResponse | null>(null);
  const [revalidationHistory, setRevalidationHistory] = useState<RevalidationHistoryItem[]>([]);
  const [loading, setLoading] = useState(true);
  const [checking, setChecking] = useState(false);
  const [revalidating, setRevalidating] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const fetchData = useCallback(async () => {
    try {
      setError(null);
      const [statusRes, historyRes, configRes, revalHistoryRes] = await Promise.all([
        syncServiceApi.getStatus(),
        syncServiceApi.getHistory(10),
        syncServiceApi.getConfig(),
        syncServiceApi.getRevalidationHistory(5),
      ]);
      setStatus(statusRes.data);
      setHistory(historyRes.data);
      setConfig(configRes.data);
      setRevalidationHistory(revalHistoryRes.data);
    } catch (err) {
      console.error('Failed to fetch sync data:', err);
      setError('동기화 서비스에 연결할 수 없습니다.');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchData();
    // Auto-refresh every 30 seconds
    const interval = setInterval(fetchData, 30000);
    return () => clearInterval(interval);
  }, [fetchData]);

  const handleManualCheck = async () => {
    setChecking(true);
    try {
      await syncServiceApi.triggerCheck();
      await fetchData();
    } catch (err) {
      console.error('Manual check failed:', err);
      setError('수동 검사 실패');
    } finally {
      setChecking(false);
    }
  };

  const handleRevalidation = async () => {
    setRevalidating(true);
    try {
      await syncServiceApi.triggerRevalidation();
      await fetchData();
    } catch (err) {
      console.error('Revalidation failed:', err);
      setError('인증서 재검증 실패');
    } finally {
      setRevalidating(false);
    }
  };

  const getStatusIcon = (syncStatus: SyncStatusType) => {
    switch (syncStatus) {
      case 'SYNCED':
        return <CheckCircle className="w-6 h-6 text-green-500" />;
      case 'DISCREPANCY':
        return <AlertTriangle className="w-6 h-6 text-yellow-500" />;
      case 'ERROR':
        return <XCircle className="w-6 h-6 text-red-500" />;
      default:
        return <Clock className="w-6 h-6 text-gray-400" />;
    }
  };

  const getStatusColor = (syncStatus: SyncStatusType) => {
    switch (syncStatus) {
      case 'SYNCED':
        return 'bg-green-100 text-green-800 dark:bg-green-900/30 dark:text-green-300';
      case 'DISCREPANCY':
        return 'bg-yellow-100 text-yellow-800 dark:bg-yellow-900/30 dark:text-yellow-300';
      case 'ERROR':
        return 'bg-red-100 text-red-800 dark:bg-red-900/30 dark:text-red-300';
      default:
        return 'bg-gray-100 text-gray-800 dark:bg-gray-700 dark:text-gray-300';
    }
  };

  const getStatusLabel = (syncStatus: SyncStatusType) => {
    switch (syncStatus) {
      case 'SYNCED':
        return '동기화됨';
      case 'DISCREPANCY':
        return '불일치 감지';
      case 'ERROR':
        return '오류';
      case 'NO_DATA':
        return '데이터 없음';
      default:
        return '대기 중';
    }
  };

  const formatTime = (timestamp: string | undefined) => {
    if (!timestamp) return '-';
    try {
      // PostgreSQL TIMESTAMP WITH TIME ZONE 형식 처리
      // 예: "2026-01-07 08:30:00+00" -> ISO 8601 형식으로 변환
      let isoTimestamp = timestamp;

      // PostgreSQL 형식을 ISO 8601로 변환 (공백을 T로, +00을 +00:00으로)
      if (timestamp.includes(' ') && !timestamp.includes('T')) {
        isoTimestamp = timestamp.replace(' ', 'T');
      }
      // +00 또는 +09 형식을 +00:00 또는 +09:00으로 변환
      if (/[+-]\d{2}$/.test(isoTimestamp)) {
        isoTimestamp = isoTimestamp + ':00';
      }

      const date = new Date(isoTimestamp);
      if (isNaN(date.getTime())) {
        return timestamp;
      }

      return date.toLocaleString('ko-KR', {
        timeZone: 'Asia/Seoul',
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
      });
    } catch {
      return timestamp;
    }
  };

  if (loading) {
    return (
      <div className="flex items-center justify-center h-64">
        <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
        <span className="ml-2 text-gray-600 dark:text-gray-400">로딩 중...</span>
      </div>
    );
  }

  if (error && !status) {
    return (
      <div className="p-6">
        <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl p-6 text-center">
          <XCircle className="w-12 h-12 text-red-500 mx-auto mb-4" />
          <h3 className="text-lg font-semibold text-red-700 dark:text-red-400 mb-2">
            동기화 서비스 연결 실패
          </h3>
          <p className="text-red-600 dark:text-red-300 mb-4">{error}</p>
          <button
            onClick={fetchData}
            className="px-4 py-2 bg-red-100 dark:bg-red-800 text-red-700 dark:text-red-200 rounded-lg hover:bg-red-200 dark:hover:bg-red-700 transition-colors"
          >
            다시 시도
          </button>
        </div>
      </div>
    );
  }

  return (
    <div className="p-6 space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-gray-900 dark:text-white flex items-center gap-3">
            <ArrowRightLeft className="w-7 h-7 text-blue-500" />
            DB-LDAP 동기화 상태
          </h1>
          <p className="text-gray-600 dark:text-gray-400 mt-1">
            PostgreSQL과 LDAP 간의 데이터 일관성을 모니터링합니다.
          </p>
        </div>
        <div className="flex items-center gap-3">
          <button
            onClick={handleRevalidation}
            disabled={revalidating}
            className={cn(
              'flex items-center gap-2 px-4 py-2 rounded-lg font-medium transition-colors',
              revalidating
                ? 'bg-gray-300 dark:bg-gray-600 cursor-not-allowed'
                : 'bg-purple-500 hover:bg-purple-600 text-white'
            )}
          >
            {revalidating ? (
              <Loader2 className="w-4 h-4 animate-spin" />
            ) : (
              <ShieldCheck className="w-4 h-4" />
            )}
            {revalidating ? '재검증 중...' : '인증서 재검증'}
          </button>
          <button
            onClick={handleManualCheck}
            disabled={checking}
            className={cn(
              'flex items-center gap-2 px-4 py-2 rounded-lg font-medium transition-colors',
              checking
                ? 'bg-gray-300 dark:bg-gray-600 cursor-not-allowed'
                : 'bg-blue-500 hover:bg-blue-600 text-white'
            )}
          >
            {checking ? (
              <Loader2 className="w-4 h-4 animate-spin" />
            ) : (
              <Play className="w-4 h-4" />
            )}
            {checking ? '검사 중...' : '수동 검사'}
          </button>
        </div>
      </div>

      {/* Config Card */}
      {config && (
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-lg p-6">
          <div className="flex items-center gap-2 mb-4">
            <Settings className="w-5 h-5 text-gray-500" />
            <h3 className="text-sm font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
              서비스 설정
            </h3>
          </div>
          <div className="grid grid-cols-2 md:grid-cols-3 gap-4">
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3">
              <div className="flex items-center gap-2 mb-1">
                <CalendarClock className="w-4 h-4 text-purple-500" />
                <span className="text-xs text-gray-500 dark:text-gray-400">일일 동기화 시간</span>
              </div>
              <p className="text-lg font-semibold text-gray-900 dark:text-white">
                {config.dailySyncEnabled ? `매일 ${config.dailySyncTime}` : '비활성화'}
              </p>
            </div>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3">
              <div className="flex items-center gap-2 mb-1">
                <ShieldCheck className="w-4 h-4 text-green-500" />
                <span className="text-xs text-gray-500 dark:text-gray-400">인증서 자동 재검증</span>
              </div>
              <p className="text-lg font-semibold text-gray-900 dark:text-white">
                {config.revalidateCertsOnSync ? '활성화' : '비활성화'}
              </p>
            </div>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3">
              <div className="flex items-center gap-2 mb-1">
                <RotateCcw className="w-4 h-4 text-orange-500" />
                <span className="text-xs text-gray-500 dark:text-gray-400">불일치 자동 조정</span>
              </div>
              <p className="text-lg font-semibold text-gray-900 dark:text-white">
                {config.autoReconcile ? '활성화' : '비활성화'}
              </p>
            </div>
          </div>
        </div>
      )}

      {/* Status Overview */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
        {/* Current Status Card */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-lg p-6">
          <div className="flex items-center justify-between mb-4">
            <h3 className="text-sm font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
              현재 상태
            </h3>
            {status && getStatusIcon(status.status)}
          </div>
          <div className="space-y-3">
            <div
              className={cn(
                'inline-flex items-center px-3 py-1 rounded-full text-sm font-medium',
                status ? getStatusColor(status.status) : 'bg-gray-100 text-gray-600'
              )}
            >
              {status ? getStatusLabel(status.status) : '알 수 없음'}
            </div>
            <div className="text-xs text-gray-500 dark:text-gray-400">
              마지막 검사: {formatTime(status?.checkedAt)}
            </div>
            {status?.checkDurationMs && (
              <div className="text-xs text-gray-500 dark:text-gray-400">
                검사 소요 시간: {status.checkDurationMs}ms
              </div>
            )}
          </div>
        </div>

        {/* DB Stats Card */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-lg p-6">
          <div className="flex items-center gap-2 mb-4">
            <Database className="w-5 h-5 text-blue-500" />
            <h3 className="text-sm font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
              PostgreSQL
            </h3>
          </div>
          {status?.dbStats ? (
            <div className="grid grid-cols-2 gap-3 text-sm">
              <div>
                <span className="text-gray-500 dark:text-gray-400">CSCA:</span>
                <span className="ml-2 font-semibold text-gray-900 dark:text-white">
                  {status.dbStats.csca?.toLocaleString()}
                </span>
              </div>
              <div>
                <span className="text-gray-500 dark:text-gray-400">DSC:</span>
                <span className="ml-2 font-semibold text-gray-900 dark:text-white">
                  {status.dbStats.dsc?.toLocaleString()}
                </span>
              </div>
              <div>
                <span className="text-gray-500 dark:text-gray-400">DSC_NC:</span>
                <span className="ml-2 font-semibold text-gray-900 dark:text-white">
                  {status.dbStats.dscNc?.toLocaleString()}
                </span>
              </div>
              <div>
                <span className="text-gray-500 dark:text-gray-400">CRL:</span>
                <span className="ml-2 font-semibold text-gray-900 dark:text-white">
                  {status.dbStats.crl?.toLocaleString()}
                </span>
              </div>
            </div>
          ) : (
            <div className="text-gray-400">데이터 없음</div>
          )}
        </div>

        {/* LDAP Stats Card */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-lg p-6">
          <div className="flex items-center gap-2 mb-4">
            <Server className="w-5 h-5 text-green-500" />
            <h3 className="text-sm font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
              LDAP (OpenLDAP)
            </h3>
          </div>
          {status?.ldapStats ? (
            <div className="grid grid-cols-2 gap-3 text-sm">
              <div>
                <span className="text-gray-500 dark:text-gray-400">CSCA:</span>
                <span className="ml-2 font-semibold text-gray-900 dark:text-white">
                  {status.ldapStats.csca?.toLocaleString()}
                </span>
              </div>
              <div>
                <span className="text-gray-500 dark:text-gray-400">DSC:</span>
                <span className="ml-2 font-semibold text-gray-900 dark:text-white">
                  {status.ldapStats.dsc?.toLocaleString()}
                </span>
              </div>
              <div>
                <span className="text-gray-500 dark:text-gray-400">DSC_NC:</span>
                <span className="ml-2 font-semibold text-gray-900 dark:text-white">
                  {status.ldapStats.dscNc?.toLocaleString()}
                </span>
              </div>
              <div>
                <span className="text-gray-500 dark:text-gray-400">CRL:</span>
                <span className="ml-2 font-semibold text-gray-900 dark:text-white">
                  {status.ldapStats.crl?.toLocaleString()}
                </span>
              </div>
            </div>
          ) : (
            <div className="text-gray-400">데이터 없음</div>
          )}
        </div>
      </div>

      {/* Discrepancy Details */}
      {status?.discrepancy && status.discrepancy.total > 0 && (
        <div className="bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800 rounded-xl p-6">
          <div className="flex items-center gap-2 mb-4">
            <AlertTriangle className="w-5 h-5 text-yellow-500" />
            <h3 className="text-lg font-semibold text-yellow-800 dark:text-yellow-300">
              불일치 상세
            </h3>
          </div>
          <div className="grid grid-cols-2 md:grid-cols-5 gap-4">
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-2xl font-bold text-yellow-600 dark:text-yellow-400">
                {status.discrepancy.total}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400">총 불일치</div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancy.csca}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400">CSCA</div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancy.dsc}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400">DSC</div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancy.dscNc}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400">DSC_NC</div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancy.crl}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400">CRL</div>
            </div>
          </div>
          <p className="mt-4 text-sm text-yellow-700 dark:text-yellow-300">
            양수 값: DB에 있고 LDAP에 없음 | 음수 값: LDAP에 있고 DB에 없음
          </p>
        </div>
      )}

      {/* Sync History */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-lg p-6">
        <div className="flex items-center gap-2 mb-4">
          <History className="w-5 h-5 text-purple-500" />
          <h3 className="text-lg font-semibold text-gray-900 dark:text-white">
            동기화 검사 이력
          </h3>
          <button
            onClick={fetchData}
            className="ml-auto p-2 text-gray-400 hover:text-gray-600 dark:hover:text-gray-300 transition-colors"
          >
            <RefreshCw className="w-4 h-4" />
          </button>
        </div>

        {history.length > 0 ? (
          <div className="overflow-x-auto">
            <table className="w-full text-sm">
              <thead>
                <tr className="border-b border-gray-200 dark:border-gray-700">
                  <th className="text-left py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    검사 시간
                  </th>
                  <th className="text-center py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    상태
                  </th>
                  <th className="text-right py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    DB 총계
                  </th>
                  <th className="text-right py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    LDAP 총계
                  </th>
                  <th className="text-right py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    불일치
                  </th>
                  <th className="text-right py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    소요 시간
                  </th>
                </tr>
              </thead>
              <tbody>
                {history.map((item) => (
                  <tr
                    key={item.id}
                    className="border-b border-gray-100 dark:border-gray-700/50 hover:bg-gray-50 dark:hover:bg-gray-700/30"
                  >
                    <td className="py-3 px-4 text-gray-900 dark:text-white">
                      {formatTime(item.checkedAt)}
                    </td>
                    <td className="py-3 px-4 text-center">
                      <span
                        className={cn(
                          'inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium',
                          getStatusColor(item.status)
                        )}
                      >
                        {getStatusLabel(item.status)}
                      </span>
                    </td>
                    <td className="py-3 px-4 text-right font-mono text-gray-700 dark:text-gray-300">
                      {item.dbTotal?.toLocaleString()}
                    </td>
                    <td className="py-3 px-4 text-right font-mono text-gray-700 dark:text-gray-300">
                      {item.ldapTotal?.toLocaleString()}
                    </td>
                    <td className="py-3 px-4 text-right">
                      <span
                        className={cn(
                          'font-mono font-semibold',
                          item.totalDiscrepancy === 0
                            ? 'text-green-600 dark:text-green-400'
                            : 'text-yellow-600 dark:text-yellow-400'
                        )}
                      >
                        {item.totalDiscrepancy}
                      </span>
                    </td>
                    <td className="py-3 px-4 text-right text-gray-500 dark:text-gray-400">
                      {item.checkDurationMs}ms
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        ) : (
          <div className="text-center py-8 text-gray-500 dark:text-gray-400">
            <Activity className="w-12 h-12 mx-auto mb-3 opacity-50" />
            <p>동기화 검사 이력이 없습니다.</p>
            <p className="text-sm mt-1">수동 검사를 실행하거나 자동 검사를 기다려주세요.</p>
          </div>
        )}
      </div>

      {/* Revalidation History */}
      {revalidationHistory.length > 0 && (
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-lg p-6">
          <div className="flex items-center gap-2 mb-4">
            <ShieldCheck className="w-5 h-5 text-green-500" />
            <h3 className="text-lg font-semibold text-gray-900 dark:text-white">
              인증서 재검증 이력
            </h3>
          </div>

          <div className="overflow-x-auto">
            <table className="w-full text-sm">
              <thead>
                <tr className="border-b border-gray-200 dark:border-gray-700">
                  <th className="text-left py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    실행 시간
                  </th>
                  <th className="text-right py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    처리된 인증서
                  </th>
                  <th className="text-right py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    새로 만료
                  </th>
                  <th className="text-right py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    새로 유효
                  </th>
                  <th className="text-right py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    변경 없음
                  </th>
                  <th className="text-right py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    오류
                  </th>
                  <th className="text-right py-3 px-4 font-medium text-gray-500 dark:text-gray-400">
                    소요 시간
                  </th>
                </tr>
              </thead>
              <tbody>
                {revalidationHistory.map((item) => (
                  <tr
                    key={item.id}
                    className="border-b border-gray-100 dark:border-gray-700/50 hover:bg-gray-50 dark:hover:bg-gray-700/30"
                  >
                    <td className="py-3 px-4 text-gray-900 dark:text-white">
                      {formatTime(item.executedAt)}
                    </td>
                    <td className="py-3 px-4 text-right font-mono text-gray-700 dark:text-gray-300">
                      {item.totalProcessed.toLocaleString()}
                    </td>
                    <td className="py-3 px-4 text-right">
                      <span
                        className={cn(
                          'font-mono font-semibold',
                          item.newlyExpired > 0
                            ? 'text-orange-600 dark:text-orange-400'
                            : 'text-gray-500 dark:text-gray-400'
                        )}
                      >
                        {item.newlyExpired}
                      </span>
                    </td>
                    <td className="py-3 px-4 text-right">
                      <span
                        className={cn(
                          'font-mono font-semibold',
                          item.newlyValid > 0
                            ? 'text-green-600 dark:text-green-400'
                            : 'text-gray-500 dark:text-gray-400'
                        )}
                      >
                        {item.newlyValid}
                      </span>
                    </td>
                    <td className="py-3 px-4 text-right font-mono text-gray-500 dark:text-gray-400">
                      {item.unchanged.toLocaleString()}
                    </td>
                    <td className="py-3 px-4 text-right">
                      <span
                        className={cn(
                          'font-mono font-semibold',
                          item.errors > 0
                            ? 'text-red-600 dark:text-red-400'
                            : 'text-gray-500 dark:text-gray-400'
                        )}
                      >
                        {item.errors}
                      </span>
                    </td>
                    <td className="py-3 px-4 text-right text-gray-500 dark:text-gray-400">
                      {item.durationMs}ms
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}

      {/* Info */}
      <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-xl p-4">
        <div className="flex items-start gap-3">
          <Activity className="w-5 h-5 text-blue-500 mt-0.5" />
          <div className="text-sm text-blue-700 dark:text-blue-300">
            <p className="font-medium mb-1">일일 동기화 및 인증서 재검증</p>
            {config?.dailySyncEnabled ? (
              <div>
                <p className="mb-1">
                  Sync Service가 매일 {config.dailySyncTime}에 자동으로 동기화를 실행합니다.
                </p>
                <ul className="list-disc list-inside space-y-0.5 ml-1">
                  <li>PostgreSQL과 LDAP의 데이터 일관성을 검사합니다.</li>
                  {config.revalidateCertsOnSync && (
                    <li>인증서 만료 상태를 자동으로 재검증합니다.</li>
                  )}
                </ul>
              </div>
            ) : (
              <p>
                일일 동기화가 비활성화되어 있습니다. 수동 검사 버튼을 사용하여 동기화 상태를 확인하세요.
              </p>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

export default SyncDashboard;
