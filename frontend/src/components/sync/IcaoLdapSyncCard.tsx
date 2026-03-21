import { useState, useEffect, useCallback } from 'react';
import { Globe, RefreshCw, CheckCircle, XCircle, Clock, Wifi, WifiOff } from 'lucide-react';
import { syncApi, type IcaoLdapSyncStatus, type IcaoLdapSyncHistoryItem } from '@/services/relayApi';

export default function IcaoLdapSyncCard() {
  const [status, setStatus] = useState<IcaoLdapSyncStatus | null>(null);
  const [history, setHistory] = useState<IcaoLdapSyncHistoryItem[]>([]);
  const [syncing, setSyncing] = useState(false);
  const [error, setError] = useState('');

  const fetchStatus = useCallback(async () => {
    try {
      const res = await syncApi.getIcaoLdapSyncStatus();
      setStatus(res.data);
    } catch {
      /* non-critical */
    }
  }, []);

  const fetchHistory = useCallback(async () => {
    try {
      const res = await syncApi.getIcaoLdapSyncHistory(5);
      setHistory(Array.isArray(res.data) ? res.data : []);
    } catch {
      /* non-critical */
    }
  }, []);

  useEffect(() => {
    fetchStatus();
    fetchHistory();
    const interval = setInterval(() => {
      fetchStatus();
      fetchHistory();
    }, 10000);
    return () => clearInterval(interval);
  }, [fetchStatus, fetchHistory]);

  const handleSync = async () => {
    setSyncing(true);
    setError('');
    try {
      await syncApi.triggerIcaoLdapSync();
      // Poll for completion
      const poll = setInterval(async () => {
        const res = await syncApi.getIcaoLdapSyncStatus();
        setStatus(res.data);
        if (!res.data.running) {
          clearInterval(poll);
          setSyncing(false);
          fetchHistory();
        }
      }, 3000);
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : '동기화 트리거 실패';
      setError(msg);
      setSyncing(false);
    }
  };

  const statusIcon = (s: string) => {
    switch (s) {
      case 'COMPLETED': return <CheckCircle className="w-4 h-4 text-green-500" />;
      case 'FAILED': return <XCircle className="w-4 h-4 text-red-500" />;
      default: return <Clock className="w-4 h-4 text-yellow-500" />;
    }
  };

  if (!status) return null;

  return (
    <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-lg p-4">
      <div className="flex items-center justify-between mb-3">
        <div className="flex items-center gap-2">
          <Globe className="w-5 h-5 text-blue-600 dark:text-blue-400" />
          <h3 className="font-semibold text-sm">ICAO PKD LDAP 동기화</h3>
          {status.enabled ? (
            <span className="flex items-center gap-1 text-xs text-green-600 dark:text-green-400">
              <Wifi className="w-3 h-3" /> 활성
            </span>
          ) : (
            <span className="flex items-center gap-1 text-xs text-gray-400">
              <WifiOff className="w-3 h-3" /> 비활성
            </span>
          )}
        </div>
        <button
          onClick={handleSync}
          disabled={syncing || status.running}
          className="flex items-center gap-1 px-3 py-1.5 text-xs font-medium rounded-lg
                     bg-blue-600 text-white hover:bg-blue-700 disabled:opacity-50 disabled:cursor-not-allowed
                     transition-colors"
        >
          <RefreshCw className={`w-3.5 h-3.5 ${syncing || status.running ? 'animate-spin' : ''}`} />
          {syncing || status.running ? '동기화 중...' : '수동 동기화'}
        </button>
      </div>

      {error && (
        <div className="mb-2 p-2 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded text-xs text-red-600 dark:text-red-400">
          {error}
        </div>
      )}

      {/* Connection Info */}
      <div className="grid grid-cols-3 gap-2 mb-3">
        <div className="text-xs">
          <span className="text-gray-500 dark:text-gray-400">호스트</span>
          <div className="font-mono text-xs mt-0.5">{status.host}:{status.port}</div>
        </div>
        <div className="text-xs">
          <span className="text-gray-500 dark:text-gray-400">주기</span>
          <div className="mt-0.5">{status.syncIntervalMinutes}분</div>
        </div>
        <div className="text-xs">
          <span className="text-gray-500 dark:text-gray-400">상태</span>
          <div className="mt-0.5">{status.running ? '동기화 중' : '대기'}</div>
        </div>
      </div>

      {/* Last Sync Result */}
      {status.lastSync && (
        <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3 mb-3">
          <div className="flex items-center gap-1.5 mb-2">
            {statusIcon(status.lastSync.status)}
            <span className="text-xs font-medium">
              마지막 동기화 — {status.lastSync.status}
            </span>
            <span className="text-xs text-gray-400 ml-auto">
              {(status.lastSync.durationMs / 1000).toFixed(1)}초
            </span>
          </div>
          <div className="grid grid-cols-4 gap-2 text-xs">
            <div>
              <span className="text-gray-500 dark:text-gray-400">전체</span>
              <div className="font-semibold">{status.lastSync.totalRemoteCount.toLocaleString()}</div>
            </div>
            <div>
              <span className="text-green-600 dark:text-green-400">신규</span>
              <div className="font-semibold text-green-600 dark:text-green-400">{status.lastSync.newCertificates}</div>
            </div>
            <div>
              <span className="text-gray-500 dark:text-gray-400">기존</span>
              <div className="font-semibold">{status.lastSync.existingSkipped.toLocaleString()}</div>
            </div>
            <div>
              <span className="text-red-500">실패</span>
              <div className="font-semibold text-red-500">{status.lastSync.failedCount}</div>
            </div>
          </div>
          {status.lastSync.errorMessage && (
            <div className="mt-2 text-xs text-red-500 truncate">{status.lastSync.errorMessage}</div>
          )}
        </div>
      )}

      {/* History */}
      {history.length > 0 && (
        <div>
          <h4 className="text-xs font-medium text-gray-500 dark:text-gray-400 mb-1.5">동기화 이력</h4>
          <div className="space-y-1">
            {history.map((h, i) => (
              <div key={i} className="flex items-center gap-2 text-xs py-1 border-b border-gray-100 dark:border-gray-700 last:border-0">
                {statusIcon(h.status)}
                <span className="text-gray-500 dark:text-gray-400">{h.triggeredBy}</span>
                <span className="text-green-600 dark:text-green-400">+{h.newCertificates}</span>
                <span className="text-gray-400">/{h.totalRemoteCount.toLocaleString()}</span>
                <span className="ml-auto text-gray-400">{(h.durationMs / 1000).toFixed(1)}s</span>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}
