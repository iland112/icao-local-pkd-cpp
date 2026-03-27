import { useState, useEffect, useCallback, useRef } from 'react';
import {
  Globe, RefreshCw, CheckCircle, XCircle, Clock, Wifi, WifiOff,
  Zap, Shield, ShieldCheck, Settings, Plug, Loader2, ArrowRight
} from 'lucide-react';
import { formatNumbersInMessage } from '@/utils/numberFormat';
import {
  syncApi,
  type IcaoLdapSyncStatus,
  type IcaoLdapSyncHistoryItem,
  type IcaoLdapConnectionTestResult,
  type IcaoLdapSyncProgress,
} from '@/services/relayApi';

export default function IcaoLdapSyncCard() {
  const [status, setStatus] = useState<IcaoLdapSyncStatus | null>(null);
  const [history, setHistory] = useState<IcaoLdapSyncHistoryItem[]>([]);
  const [syncing, setSyncing] = useState(false);
  const [testing, setTesting] = useState(false);
  const [testResult, setTestResult] = useState<IcaoLdapConnectionTestResult | null>(null);
  const [progress, setProgress] = useState<IcaoLdapSyncProgress | null>(null);
  const [error, setError] = useState('');
  const [showSettings, setShowSettings] = useState(false);
  const [settingsEnabled, setSettingsEnabled] = useState(false);
  const [settingsInterval, setSettingsInterval] = useState(60);
  const eventSourceRef = useRef<EventSource | null>(null);

  const fetchStatus = useCallback(async () => {
    try {
      const res = await syncApi.getIcaoLdapSyncStatus();
      setStatus(res.data);
      if (res.data.running && !syncing) setSyncing(true);
      if (!res.data.running && syncing && !progress) setSyncing(false);
    } catch { /* non-critical */ }
  }, [syncing, progress]);

  const fetchHistory = useCallback(async () => {
    try {
      const res = await syncApi.getIcaoLdapSyncHistory({ limit: 5 });
      setHistory(res.data.data ?? []);
    } catch { /* non-critical */ }
  }, []);

  // SSE listener for real-time sync progress
  useEffect(() => {
    const baseUrl = window.location.origin.replace(/:\d+$/, '') + ':8080';
    const es = new EventSource(`${baseUrl}/api/sync/notifications/stream`);
    eventSourceRef.current = es;

    es.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        if (data.type === 'ICAO_LDAP_SYNC_PROGRESS') {
          const p = data.data as IcaoLdapSyncProgress;
          setProgress(p);
          if (p.phase === 'COMPLETED' || p.phase === 'FAILED') {
            setTimeout(() => {
              setProgress(null);
              setSyncing(false);
              fetchStatus();
              fetchHistory();
            }, 2000);
          }
        }
      } catch { /* ignore parse errors */ }
    };

    return () => { es.close(); };
  }, [fetchStatus, fetchHistory]);

  useEffect(() => {
    fetchStatus();
    fetchHistory();
    const interval = setInterval(() => { fetchStatus(); fetchHistory(); }, 15000);
    return () => clearInterval(interval);
  }, [fetchStatus, fetchHistory]);

  // Sync settings from status
  useEffect(() => {
    if (status) {
      setSettingsEnabled(status.enabled);
      setSettingsInterval(status.syncIntervalMinutes);
    }
  }, [status]);

  const handleSync = async () => {
    setSyncing(true);
    setError('');
    setProgress(null);
    try {
      await syncApi.triggerIcaoLdapSync();
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : '동기화 트리거 실패');
      setSyncing(false);
    }
  };

  const handleTestConnection = async () => {
    setTesting(true);
    setTestResult(null);
    try {
      const res = await syncApi.testIcaoLdapConnection();
      setTestResult(res.data);
    } catch {
      setTestResult({ success: false, latencyMs: 0, entryCount: 0, serverInfo: '', tlsMode: '', errorMessage: '연결 테스트 실패' });
    }
    setTesting(false);
  };

  const handleSaveSettings = async () => {
    try {
      await syncApi.updateIcaoLdapSyncConfig({ enabled: settingsEnabled, syncIntervalMinutes: settingsInterval });
      setShowSettings(false);
      fetchStatus();
    } catch { setError('설정 저장 실패'); }
  };

  const statusIcon = (s: string) => {
    switch (s) {
      case 'COMPLETED': return <CheckCircle className="w-3.5 h-3.5 text-green-500" />;
      case 'FAILED': return <XCircle className="w-3.5 h-3.5 text-red-500" />;
      case 'RUNNING': return <Loader2 className="w-3.5 h-3.5 text-blue-500 animate-spin" />;
      default: return <Clock className="w-3.5 h-3.5 text-yellow-500" />;
    }
  };

  if (!status) return null;

  const progressPercent = progress && progress.currentTypeTotal > 0
    ? Math.round((progress.currentTypeProcessed / progress.currentTypeTotal) * 100)
    : 0;

  const overallPercent = progress
    ? Math.round(((progress.completedTypes + (progress.currentTypeTotal > 0 ? progress.currentTypeProcessed / progress.currentTypeTotal : 0)) / progress.totalTypes) * 100)
    : 0;

  return (
    <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-xl shadow-sm">
      {/* Header */}
      <div className="flex items-center justify-between p-4 border-b border-gray-100 dark:border-gray-700">
        <div className="flex items-center gap-2.5">
          <div className="w-8 h-8 rounded-lg bg-blue-50 dark:bg-blue-900/30 flex items-center justify-center">
            <Globe className="w-4.5 h-4.5 text-blue-600 dark:text-blue-400" />
          </div>
          <div>
            <h3 className="font-semibold text-sm">ICAO PKD LDAP 동기화</h3>
            <div className="flex items-center gap-2 mt-0.5">
              {status.enabled ? (
                <span className="flex items-center gap-1 text-[11px] text-green-600 dark:text-green-400">
                  <Wifi className="w-3 h-3" /> 자동 동기화 활성
                </span>
              ) : (
                <span className="flex items-center gap-1 text-[11px] text-gray-400">
                  <WifiOff className="w-3 h-3" /> 비활성
                </span>
              )}
              <span className="text-[11px] text-gray-400">|</span>
              <span className="text-[11px] text-gray-500 dark:text-gray-400 font-mono">
                {status.host}:{status.port}
              </span>
            </div>
          </div>
        </div>
        <div className="flex items-center gap-1.5">
          <button onClick={() => setShowSettings(!showSettings)}
            className="p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 text-gray-400 hover:text-gray-600 transition-colors"
            title="설정">
            <Settings className="w-4 h-4" />
          </button>
          <button onClick={handleTestConnection} disabled={testing}
            className="p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 text-gray-400 hover:text-gray-600 transition-colors"
            title="연결 테스트">
            <Plug className={`w-4 h-4 ${testing ? 'animate-pulse' : ''}`} />
          </button>
          <button onClick={handleSync} disabled={syncing || status.running}
            className="flex items-center gap-1 px-3 py-1.5 text-xs font-medium rounded-lg
                       bg-blue-600 text-white hover:bg-blue-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors">
            <RefreshCw className={`w-3.5 h-3.5 ${syncing ? 'animate-spin' : ''}`} />
            {syncing ? '동기화 중' : '수동 동기화'}
          </button>
        </div>
      </div>

      {error && (
        <div className="mx-4 mt-3 p-2 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg text-xs text-red-600 dark:text-red-400">
          {error}
        </div>
      )}

      {/* Settings Panel */}
      {showSettings && (
        <div className="mx-4 mt-3 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg border border-gray-200 dark:border-gray-600">
          <h4 className="text-xs font-semibold mb-2">동기화 설정</h4>
          <div className="flex items-center gap-4 text-xs">
            <label className="flex items-center gap-1.5">
              <input type="checkbox" checked={settingsEnabled} onChange={(e) => setSettingsEnabled(e.target.checked)}
                className="rounded border-gray-300" />
              자동 동기화
            </label>
            <label className="flex items-center gap-1.5">
              주기:
              <input type="number" value={settingsInterval} onChange={(e) => setSettingsInterval(Number(e.target.value))}
                min={5} max={1440} className="w-16 px-2 py-1 border border-gray-300 dark:border-gray-600 rounded text-xs bg-white dark:bg-gray-800" />
              분
            </label>
            <button onClick={handleSaveSettings}
              className="px-3 py-1 bg-blue-600 text-white rounded text-xs hover:bg-blue-700">저장</button>
          </div>
        </div>
      )}

      {/* Connection Test Result */}
      {testResult && (
        <div className={`mx-4 mt-3 p-3 rounded-lg border text-xs ${
          testResult.success
            ? 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
            : 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800'
        }`}>
          <div className="flex items-center gap-2 mb-1.5">
            {testResult.success
              ? <ShieldCheck className="w-4 h-4 text-green-500" />
              : <XCircle className="w-4 h-4 text-red-500" />
            }
            <span className="font-semibold">{testResult.success ? '연결 성공' : '연결 실패'}</span>
          </div>
          {testResult.success ? (
            <div className="grid grid-cols-3 gap-2 text-xs">
              <div><span className="text-gray-500">응답 시간</span><div className="font-semibold">{testResult.latencyMs}ms</div></div>
              <div><span className="text-gray-500">인증서 수</span><div className="font-semibold">{testResult.entryCount.toLocaleString()}</div></div>
              <div><span className="text-gray-500">인증 방식</span><div className="font-semibold flex items-center gap-1">
                {testResult.tlsMode.includes('TLS') ? <Shield className="w-3 h-3 text-green-500" /> : <Zap className="w-3 h-3 text-yellow-500" />}
                {testResult.tlsMode.includes('TLS') ? 'TLS' : 'Simple'}
              </div></div>
            </div>
          ) : (
            <div className="text-red-500">{testResult.errorMessage}</div>
          )}
        </div>
      )}

      <div className="p-4 space-y-3">
        {/* Real-time Sync Progress */}
        {progress && (
          <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-700 rounded-lg p-3">
            <div className="flex items-center justify-between mb-2">
              <div className="flex items-center gap-2">
                <Loader2 className="w-4 h-4 text-blue-500 animate-spin" />
                <span className="text-xs font-semibold text-blue-700 dark:text-blue-300">동기화 진행 중</span>
              </div>
              <span className="text-[11px] text-blue-500">{(progress.elapsedMs / 1000).toFixed(1)}초 경과</span>
            </div>

            {/* Overall progress */}
            <div className="mb-2">
              <div className="flex justify-between text-[11px] mb-1">
                <span className="text-gray-600 dark:text-gray-400">전체 진행률</span>
                <span className="font-semibold">{overallPercent}% ({progress.completedTypes}/{progress.totalTypes} 타입)</span>
              </div>
              <div className="h-2 bg-gray-200 dark:bg-gray-600 rounded-full overflow-hidden">
                <div className="h-full bg-blue-500 rounded-full transition-all duration-300" style={{ width: `${overallPercent}%` }} />
              </div>
            </div>

            {/* Current type progress */}
            {progress.currentType && progress.phase === 'PROCESSING' && (
              <div className="mb-2">
                <div className="flex justify-between text-[11px] mb-1">
                  <span className="text-gray-600 dark:text-gray-400">
                    {progress.currentType} 처리 중
                  </span>
                  <span className="font-mono">{progress.currentTypeProcessed.toLocaleString()}/{progress.currentTypeTotal.toLocaleString()} ({progressPercent}%)</span>
                </div>
                <div className="h-1.5 bg-gray-200 dark:bg-gray-600 rounded-full overflow-hidden">
                  <div className="h-full bg-green-500 rounded-full transition-all duration-300" style={{ width: `${progressPercent}%` }} />
                </div>
              </div>
            )}

            {/* Type-level stats */}
            <div className="flex items-center gap-3 text-[11px]">
              <span className="text-green-600 dark:text-green-400 font-semibold">+{progress.totalNew} 신규</span>
              <span className="text-gray-500">{progress.totalSkipped.toLocaleString()} 기존</span>
              {progress.totalFailed > 0 && <span className="text-red-500">{progress.totalFailed} 실패</span>}
              <span className="text-gray-400 ml-auto">합계 {(progress.totalNew + progress.totalSkipped + progress.totalFailed).toLocaleString()}</span>
            </div>

            {/* Phase message */}
            <div className="mt-1.5 text-[11px] text-blue-600 dark:text-blue-400 truncate">
              {formatNumbersInMessage(progress.message)}
            </div>

            {/* Type pipeline visualization */}
            <div className="flex items-center gap-1 mt-2">
              {['CSCA', 'DSC', 'CRL', 'DSC_NC'].map((type, i) => {
                const isCompleted = i < progress.completedTypes;
                const isCurrent = type === progress.currentType;
                return (
                  <div key={type} className="flex items-center gap-1">
                    {i > 0 && <ArrowRight className="w-3 h-3 text-gray-300" />}
                    <span className={`px-1.5 py-0.5 rounded text-[10px] font-medium ${
                      isCompleted ? 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400' :
                      isCurrent ? 'bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400 animate-pulse' :
                      'bg-gray-100 dark:bg-gray-700 text-gray-400'
                    }`}>{type}</span>
                  </div>
                );
              })}
            </div>
          </div>
        )}

        {/* Last Sync Result (when not syncing) */}
        {!progress && status.lastSync && (
          <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3">
            <div className="flex items-center gap-1.5 mb-2">
              {statusIcon(status.lastSync.status)}
              <span className="text-xs font-medium">마지막 동기화</span>
              <span className={`text-xs font-semibold ${
                status.lastSync.status === 'COMPLETED' ? 'text-green-600 dark:text-green-400' : 'text-red-500'
              }`}>{status.lastSync.status}</span>
              <span className="text-[11px] text-gray-400 ml-auto">
                {(status.lastSync.durationMs / 1000).toFixed(1)}초
              </span>
            </div>
            <div className="grid grid-cols-4 gap-2 text-xs">
              <div><span className="text-gray-500 dark:text-gray-400">합계</span>
                <div className="font-semibold">{(status.lastSync.newCertificates + status.lastSync.existingSkipped + status.lastSync.failedCount).toLocaleString()}</div></div>
              <div><span className="text-green-600 dark:text-green-400">신규</span>
                <div className="font-semibold text-green-600 dark:text-green-400">+{status.lastSync.newCertificates}</div></div>
              <div><span className="text-gray-500 dark:text-gray-400">기존</span>
                <div className="font-semibold">{status.lastSync.existingSkipped.toLocaleString()}</div></div>
              <div><span className="text-red-500">실패</span>
                <div className="font-semibold text-red-500">{status.lastSync.failedCount.toLocaleString()}</div></div>
            </div>
            {status.lastSync.errorMessage && (
              <div className="mt-2 text-[11px] text-red-500 truncate">{status.lastSync.errorMessage}</div>
            )}
          </div>
        )}

        {/* Sync History */}
        {history.length > 0 && (
          <div>
            <h4 className="text-[11px] font-medium text-gray-500 dark:text-gray-400 mb-1.5 uppercase tracking-wider">동기화 이력</h4>
            <div className="space-y-0.5">
              {history.map((h, i) => (
                <div key={i} className="flex items-center gap-2 text-xs py-1.5 px-2 rounded hover:bg-gray-50 dark:hover:bg-gray-700/50">
                  {statusIcon(h.status)}
                  <span className="text-gray-500 dark:text-gray-400 w-16">{h.triggeredBy}</span>
                  <span className="text-green-600 dark:text-green-400 font-medium">+{h.newCertificates}</span>
                  <span className="text-gray-300">/</span>
                  <span className="text-gray-400">{(h.newCertificates + h.existingSkipped + h.failedCount).toLocaleString()}</span>
                  {h.failedCount > 0 && <span className="text-red-400 text-[11px]">({h.failedCount} fail)</span>}
                  <span className="ml-auto text-gray-400 text-[11px]">{(h.durationMs / 1000).toFixed(1)}s</span>
                </div>
              ))}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
