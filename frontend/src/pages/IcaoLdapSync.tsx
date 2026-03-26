import { useState, useEffect, useCallback, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import {
  Globe, RefreshCw, CheckCircle, XCircle, Clock, Wifi, WifiOff,
  Zap, Shield, ShieldCheck, Settings, Plug, Loader2, ArrowRight,
  Server, Database, Activity, History, AlertTriangle, Info, X, ChevronLeft, ChevronRight, Eye
} from 'lucide-react';
import {
  syncApi,
  type IcaoLdapSyncStatus,
  type IcaoLdapSyncHistoryItem,
  type IcaoLdapConnectionTestResult,
  type IcaoLdapSyncProgress,
} from '@/services/relayApi';
import { uploadApi } from '@/services/api';
import { IcaoViolationDetailDialog } from '@/components/IcaoViolationDetailDialog';
import { DuplicateCertificatesTree } from '@/components/DuplicateCertificatesTree';
import { uploadHistoryApi } from '@/services/pkdApi';

const CERT_TYPES = ['CSCA', 'CRL', 'DSC', 'DSC_NC'] as const;

export default function IcaoLdapSync() {
  const { t } = useTranslation(['sync', 'common']);
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
  const [selectedHistory, setSelectedHistory] = useState<IcaoLdapSyncHistoryItem | null>(null);
  const [showSyncResult, setShowSyncResult] = useState(false);
  const [detailTab, setDetailTab] = useState<'summary' | 'doc9303' | 'duplicates'>('summary');
  const [ncDialogOpen, setNcDialogOpen] = useState(false);
  const [ncInitialCategory, setNcInitialCategory] = useState<string | undefined>();
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const [duplicates, setDuplicates] = useState<any[]>([]);
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const [certStats, setCertStats] = useState<any>(null);
  const [historyPage, setHistoryPage] = useState(0);
  const [historyTotal, setHistoryTotal] = useState(0);
  const [historyStatusFilter, setHistoryStatusFilter] = useState('');
  const historyPageSize = 10;
  const eventSourceRef = useRef<EventSource | null>(null);

  const fetchStatus = useCallback(async () => {
    try {
      const res = await syncApi.getIcaoLdapSyncStatus();
      setStatus(res.data);
      if (res.data.running && !syncing) setSyncing(true);
      if (!res.data.running && syncing) {
        setSyncing(false);
        // Keep progress visible if FAILED (user needs to see error + retry button)
        if (progress && progress.phase !== 'FAILED') {
          setProgress(null);
        }
      }
    } catch { /* non-critical */ }
  }, [syncing, progress]);

  const fetchHistory = useCallback(async () => {
    try {
      const res = await syncApi.getIcaoLdapSyncHistory({
        limit: historyPageSize,
        offset: historyPage * historyPageSize,
        ...(historyStatusFilter ? { status: historyStatusFilter } : {}),
      });
      setHistory(res.data.data ?? []);
      setHistoryTotal(res.data.total ?? 0);
    } catch { /* non-critical */ }
  }, [historyPage, historyStatusFilter]);

  // Refs for stable SSE callback access (avoids stale closure)
  const fetchStatusRef = useRef(fetchStatus);
  const fetchHistoryRef = useRef(fetchHistory);
  fetchStatusRef.current = fetchStatus;
  fetchHistoryRef.current = fetchHistory;

  // SSE listener for real-time sync progress
  useEffect(() => {
    // Connect via API Gateway (same origin as the page)
    const sseUrl = `${window.location.origin}/api/sync/notifications/stream`;
    const es = new EventSource(sseUrl);
    eventSourceRef.current = es;

    // Server sends SSE with event type "notification"
    es.addEventListener('notification', (event) => {
      try {
        const data = JSON.parse(event.data);
        if (data.type === 'ICAO_LDAP_SYNC_PROGRESS') {
          const p = data.data as IcaoLdapSyncProgress;
          setProgress(p);
          if (p.phase === 'COMPLETED') {
            setSyncing(false);
            setProgress(null);
            setShowSyncResult(true);
            fetchStatusRef.current();
            fetchHistoryRef.current();
            // Fetch certificate statistics for result detail
            uploadApi.getStatistics().then(r => setCertStats(r.data)).catch(() => {});
          } else if (p.phase === 'FAILED') {
            // Keep progress visible with error message, allow retry
            setSyncing(false);
            fetchStatusRef.current();
            fetchHistoryRef.current();
          }
        }
        if (data.type === 'ICAO_LDAP_SYNC_STARTED') {
          setSyncing(true);
        }
        if (data.type === 'ICAO_LDAP_SYNC_COMPLETED' || data.type === 'ICAO_LDAP_SYNC_FAILED') {
          setSyncing(false);
          // Don't clear progress here — let ICAO_LDAP_SYNC_PROGRESS (phase=COMPLETED/FAILED) handle it
          fetchStatusRef.current();
          fetchHistoryRef.current();
        }
      } catch { /* ignore parse errors */ }
    });

    es.onerror = () => {
      // SSE reconnect handled by browser automatically
    };

    return () => {
      es.close();
      eventSourceRef.current = null;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);  // Mount/unmount only — no deps to prevent SSE reconnection loop

  useEffect(() => {
    fetchStatus();
    fetchHistory();
    const interval = setInterval(() => { fetchStatus(); fetchHistory(); }, 15000);
    return () => clearInterval(interval);
  }, [fetchStatus, fetchHistory]);

  useEffect(() => {
    if (status) {
      setSettingsEnabled(status.enabled);
      setSettingsInterval(status.syncIntervalMinutes);
    }
  }, [status]);

  const handleSync = async () => {
    setSyncing(true);
    setError('');
    setShowSyncResult(false);
    setProgress(null);
    try {
      await syncApi.triggerIcaoLdapSync();
      // Polling fallback: if SSE doesn't deliver progress, poll status
      const pollInterval = setInterval(async () => {
        try {
          const res = await syncApi.getIcaoLdapSyncStatus();
          setStatus(res.data);
          if (!res.data.running) {
            clearInterval(pollInterval);
            setSyncing(false);
            // Don't clear progress — SSE handler manages it
            fetchHistory();
          }
        } catch { /* ignore */ }
      }, 3000);
      // Auto-clear after 10 min max
      setTimeout(() => clearInterval(pollInterval), 600000);
    } catch (err: unknown) {
      const detail = (err && typeof err === 'object' && 'response' in err)
        ? (err as { response?: { data?: { error?: string; message?: string } } }).response?.data?.error
          || (err as { response?: { data?: { message?: string } } }).response?.data?.message
        : undefined;
      const msg = detail || (err instanceof Error ? err.message : t('sync:icaoLdap.syncFailed', '동기화 트리거 실패'));
      setError(msg);
      setSyncing(false);
    }
  };

  const handleTestConnection = async () => {
    setTesting(true);
    setTestResult(null);
    try {
      const res = await syncApi.testIcaoLdapConnection();
      setTestResult(res.data);
    } catch (err: unknown) {
      const detail = (err && typeof err === 'object' && 'response' in err)
        ? (err as { response?: { data?: { error?: string; message?: string; data?: { errorMessage?: string } } } }).response?.data?.data?.errorMessage
          || (err as { response?: { data?: { error?: string; message?: string } } }).response?.data?.error
          || (err as { response?: { data?: { message?: string } } }).response?.data?.message
        : undefined;
      const msg = detail || (err instanceof Error ? err.message : t('sync:icaoLdap.connectionFailed'));
      setTestResult({ success: false, latencyMs: 0, entryCount: 0, serverInfo: '', tlsMode: '', errorMessage: msg });
    }
    setTesting(false);
  };

  const handleSaveSettings = async () => {
    try {
      await syncApi.updateIcaoLdapSyncConfig({ enabled: settingsEnabled, syncIntervalMinutes: settingsInterval });
      setShowSettings(false);
      fetchStatus();
    } catch { setError(t('sync:icaoLdap.saveFailed', '설정 저장 실패')); }
  };

  const statusLabel = (s: string) => {
    switch (s) {
      case 'COMPLETED': return t('sync:icaoLdap.statusCompleted');
      case 'FAILED': return t('sync:icaoLdap.statusFailed');
      case 'RUNNING': return t('sync:icaoLdap.statusRunning');
      default: return s;
    }
  };

  const triggerLabel = (s: string) => {
    switch (s) {
      case 'MANUAL': return t('sync:icaoLdap.triggerManual');
      case 'SCHEDULED': return t('sync:icaoLdap.triggerScheduled');
      default: return s;
    }
  };

  const statusIcon = (s: string) => {
    switch (s) {
      case 'COMPLETED': return <CheckCircle className="w-4 h-4 text-green-500" />;
      case 'FAILED': return <XCircle className="w-4 h-4 text-red-500" />;
      case 'RUNNING': return <Loader2 className="w-4 h-4 text-blue-500 animate-spin" />;
      default: return <Clock className="w-4 h-4 text-yellow-500" />;
    }
  };

  if (!status) {
    return (
      <div className="px-4 lg:px-6 py-4 space-y-4">
        <div className="flex items-center gap-2">
          <Globe className="w-5 h-5 text-blue-500" />
          <h1 className="text-xl font-bold">{t('sync:icaoLdap.pageTitle')}</h1>
        </div>
        <div className="bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800 rounded-xl p-4 text-sm text-yellow-700 dark:text-yellow-300">
          <AlertTriangle className="w-4 h-4 inline mr-1" />
          {t('sync:icaoLdap.disabledMessage')} <code className="text-xs">ICAO_LDAP_SYNC_ENABLED=true</code>로 설정하세요.
        </div>
      </div>
    );
  }

  const progressPercent = progress && progress.currentTypeTotal > 0
    ? Math.round((progress.currentTypeProcessed / progress.currentTypeTotal) * 100) : 0;
  const overallPercent = progress
    ? Math.round(((progress.completedTypes + (progress.currentTypeTotal > 0 ? progress.currentTypeProcessed / progress.currentTypeTotal : 0)) / progress.totalTypes) * 100) : 0;

  return (
    <div className="px-4 lg:px-6 py-4 space-y-4">
      {/* Header — matches project design system (gradient icon badge) */}
      <div className="flex items-center justify-between flex-wrap gap-3">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-blue-500 to-cyan-600 shadow-lg">
            <Globe className="w-6 h-6 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">{t('sync:icaoLdap.pageTitle')}</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400 mt-1">
              {t('sync:icaoLdap.pageSubtitle')}
            </p>
          </div>
        </div>
        <div className="flex items-center gap-2">
          <button onClick={() => setShowSettings(!showSettings)}
            className="inline-flex items-center gap-1.5 px-3 py-2 text-sm font-medium rounded-lg border border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-800 transition-colors text-gray-700 dark:text-gray-300">
            <Settings className="w-4 h-4" /> {t('sync:icaoLdap.settings')}
          </button>
          <button onClick={handleTestConnection} disabled={testing}
            className="inline-flex items-center gap-1.5 px-3 py-2 text-sm font-medium rounded-lg border border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-800 transition-colors text-gray-700 dark:text-gray-300">
            <Plug className={`w-4 h-4 ${testing ? 'animate-pulse' : ''}`} />
            {testing ? t('sync:icaoLdap.testing') : t('sync:icaoLdap.connectionTest')}
          </button>
          <button onClick={handleSync} disabled={syncing || status.running}
            className="inline-flex items-center gap-1.5 px-4 py-2 text-sm font-medium rounded-lg bg-gradient-to-r from-blue-500 to-cyan-600 hover:from-blue-600 hover:to-cyan-700 text-white shadow-md disabled:opacity-50 disabled:cursor-not-allowed transition-all">
            <RefreshCw className={`w-4 h-4 ${syncing ? 'animate-spin' : ''}`} />
            {syncing ? t('sync:icaoLdap.syncing') : t('sync:icaoLdap.manualSync')}
          </button>
        </div>
      </div>

      {error && (
        <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl text-xs text-red-600 dark:text-red-400">
          {error}
        </div>
      )}

      {/* Settings Panel */}
      {showSettings && (
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4">
          <h3 className="text-sm font-semibold mb-3 flex items-center gap-2"><Settings className="w-4 h-4 text-blue-500" /> {t('sync:icaoLdap.syncSettings')}</h3>
          <div className="grid grid-cols-1 sm:grid-cols-3 gap-4 text-sm">
            <label className="flex items-center gap-2">
              <input type="checkbox" checked={settingsEnabled} onChange={(e) => setSettingsEnabled(e.target.checked)}
                className="rounded border-gray-300" />
              <span>{t('sync:icaoLdap.autoSync')} 활성화</span>
            </label>
            <label className="flex items-center gap-2">
              <span className="text-gray-500"> {t('sync:icaoLdap.syncInterval')}:</span>
              <input type="number" value={settingsInterval} onChange={(e) => setSettingsInterval(Number(e.target.value))}
                min={5} max={1440} className="w-20 px-2 py-1.5 border border-gray-300 dark:border-gray-600 rounded-lg text-sm bg-white dark:bg-gray-900" />
              <span className="text-gray-400">{t('sync:icaoLdap.minuteUnit')}</span>
            </label>
            <div className="flex items-center gap-2">
              <button onClick={handleSaveSettings}
                className="px-4 py-1.5 bg-blue-600 text-white rounded-lg text-sm hover:bg-blue-700 transition-colors">{t('sync:icaoLdap.save')}</button>
              <button onClick={() => setShowSettings(false)}
                className="px-4 py-1.5 border border-gray-300 dark:border-gray-600 rounded-lg text-sm hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors">{t('sync:icaoLdap.cancel')}</button>
            </div>
          </div>
        </div>
      )}

      {/* KPI Cards — border-l-4 accent (matches project design system) */}
      <div className="grid grid-cols-2 lg:grid-cols-4 gap-3">
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-blue-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-blue-50 dark:bg-blue-900/30">
              <Server className="w-5 h-5 text-blue-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium"> {t('sync:icaoLdap.connectionStatus')}</p>
              {status.enabled ? (
                <p className="text-xl font-bold text-green-600 dark:text-green-400 flex items-center gap-1"><Wifi className="w-4 h-4" /> {t('sync:icaoLdap.active')}</p>
              ) : (
                <p className="text-xl font-bold text-gray-400 flex items-center gap-1"><WifiOff className="w-4 h-4" /> {t('sync:icaoLdap.inactive')}</p>
              )}
              <p className="text-xs text-gray-400 font-mono">{status.host}:{status.port}</p>
            </div>
          </div>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-purple-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-purple-50 dark:bg-purple-900/30">
              <Clock className="w-5 h-5 text-purple-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium"> {t('sync:icaoLdap.syncCycle')}</p>
              <p className="text-xl font-bold text-purple-600 dark:text-purple-400">{status.syncIntervalMinutes}{t('sync:icaoLdap.minuteSuffix')}</p>
              <p className="text-xs text-gray-400">{status.running ? t('sync:icaoLdap.running') : t('sync:icaoLdap.waiting')}</p>
            </div>
          </div>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-green-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-green-50 dark:bg-green-900/30">
              <Database className="w-5 h-5 text-green-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium"> {t('sync:icaoLdap.lastSync')}</p>
              {status.lastSync ? (
                <>
                  <p className={`text-xl font-bold flex items-center gap-1 ${status.lastSync.status === 'COMPLETED' ? 'text-green-600 dark:text-green-400' : 'text-red-500'}`}>
                    {statusIcon(status.lastSync.status)} {statusLabel(status.lastSync.status)}
                  </p>
                  <p className="text-xs text-gray-400">{(status.lastSync.durationMs / 1000).toFixed(1)}{t('sync:icaoLdap.secondsSuffix')}</p>
                </>
              ) : (
                <p className="text-xl font-bold text-gray-400">—</p>
              )}
            </div>
          </div>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-amber-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-amber-50 dark:bg-amber-900/30">
              <Activity className="w-5 h-5 text-amber-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium"> {t('sync:icaoLdap.syncStats')}</p>
              {status.lastSync ? (
                <div className="flex items-center gap-3 mt-1">
                  <span className="text-sm font-bold">{status.lastSync.totalRemoteCount.toLocaleString()}</span>
                  <span className="text-sm font-bold text-green-600">+{status.lastSync.newCertificates.toLocaleString()}</span>
                  {status.lastSync.failedCount > 0 && <span className="text-sm font-bold text-red-500">{status.lastSync.failedCount.toLocaleString()} {t('sync:icaoLdap.failSuffix')}</span>}
                </div>
              ) : (
                <p className="text-xl font-bold text-gray-400">—</p>
              )}
            </div>
          </div>
        </div>
      </div>

      {/* Connection Test Result */}
      {testResult && (
        <div className={`border rounded-xl p-4 ${testResult.success
          ? 'bg-green-50 dark:bg-green-900/10 border-green-200 dark:border-green-800'
          : 'bg-red-50 dark:bg-red-900/10 border-red-200 dark:border-red-800'}`}>
          <div className="flex items-center gap-2 mb-3">
            {testResult.success ? <ShieldCheck className="w-5 h-5 text-green-500" /> : <XCircle className="w-5 h-5 text-red-500" />}
            <h3 className="text-sm font-semibold">{testResult.success ? t('sync:icaoLdap.connectionSuccess') : t('sync:icaoLdap.connectionFailed')}</h3>
            <button onClick={() => setTestResult(null)} className="ml-auto text-xs text-gray-400 hover:text-gray-600">{t('sync:icaoLdap.close')}</button>
          </div>
          {testResult.success ? (
            <div className="space-y-3">
              <div className="grid grid-cols-2 sm:grid-cols-4 gap-4 text-sm">
                <div><span className="text-gray-500 text-xs">{t('sync:icaoLdap.server')}</span><div className="font-mono text-sm">{testResult.serverInfo}</div></div>
                <div><span className="text-gray-500 text-xs">{t('sync:icaoLdap.responseTime')}</span><div className="font-semibold">{testResult.latencyMs}ms</div></div>
                <div><span className="text-gray-500 text-xs">{t('sync:icaoLdap.certCount')}</span><div className="font-semibold">{testResult.entryCount >= 0 ? testResult.entryCount.toLocaleString() : <span className="text-gray-400">{t('sync:icaoLdap.ditEmpty')}</span>}</div></div>
                <div><span className="text-gray-500 text-xs">{t('sync:icaoLdap.authMethod')}</span>
                  <div className="font-semibold flex items-center gap-1">
                    {testResult.tlsMode.includes('TLS') ? <Shield className="w-3.5 h-3.5 text-green-500" /> : <Zap className="w-3.5 h-3.5 text-yellow-500" />}
                    {testResult.tlsMode.includes('TLS') ? t('sync:icaoLdap.tlsMutual') : 'Simple Bind'}
                  </div>
                </div>
              </div>
              {testResult.tlsCertInfo && (
                <div className="border-t border-green-200 dark:border-green-800 pt-3">
                  <p className="text-xs font-semibold text-gray-500 dark:text-gray-400 mb-2 flex items-center gap-1">
                    <Shield className="w-3.5 h-3.5" /> {t('sync:icaoLdap.tlsCertTitle')}
                  </p>
                  <div className="grid grid-cols-1 sm:grid-cols-2 gap-3 text-xs">
                    <div className="bg-white/60 dark:bg-gray-800/40 rounded-lg p-2.5 space-y-1">
                      <p className="font-semibold text-gray-700 dark:text-gray-300">{t('sync:icaoLdap.clientCert')}</p>
                      <p><span className="text-gray-400">Subject:</span> <span className="font-mono">{testResult.tlsCertInfo.clientSubject}</span></p>
                      <p><span className="text-gray-400">Issuer:</span> <span className="font-mono">{testResult.tlsCertInfo.clientIssuer}</span></p>
                      <p><span className="text-gray-400">{t('sync:icaoLdap.expiryDate')}:</span> <span className="font-semibold">{testResult.tlsCertInfo.clientExpiry}</span></p>
                    </div>
                    <div className="bg-white/60 dark:bg-gray-800/40 rounded-lg p-2.5 space-y-1">
                      <p className="font-semibold text-gray-700 dark:text-gray-300">{t('sync:icaoLdap.caCert')}</p>
                      <p><span className="text-gray-400">Subject:</span> <span className="font-mono">{testResult.tlsCertInfo.caSubject}</span></p>
                      <p><span className="text-gray-400">Issuer:</span> <span className="font-mono">{testResult.tlsCertInfo.caIssuer}</span></p>
                      <p><span className="text-gray-400">{t('sync:icaoLdap.expiryDate')}:</span> <span className="font-semibold">{testResult.tlsCertInfo.caExpiry}</span></p>
                    </div>
                  </div>
                </div>
              )}
            </div>
          ) : (
            <div className="text-sm text-red-600 dark:text-red-400">{testResult.errorMessage}</div>
          )}
        </div>
      )}

      {/* Real-time Sync Progress */}
      {progress && (
        <div className="bg-blue-50 dark:bg-blue-900/10 border border-blue-200 dark:border-blue-700 rounded-xl p-4">
          <div className="flex items-center justify-between mb-3">
            <div className="flex items-center gap-2">
              <Loader2 className="w-5 h-5 text-blue-500 animate-spin" />
              <h3 className="text-sm font-semibold text-blue-700 dark:text-blue-300"> {t('sync:icaoLdap.syncInProgress')}</h3>
            </div>
            <span className="text-xs text-blue-500 font-mono">{(progress.elapsedMs / 1000).toFixed(1)}{t('sync:icaoLdap.secondsElapsed')}</span>
          </div>

          {/* Overall progress bar */}
          <div className="mb-4">
            <div className="flex justify-between text-xs mb-1">
              <span className="text-gray-600 dark:text-gray-400"> {t('sync:icaoLdap.overallProgress')}</span>
              <span className="font-semibold">{overallPercent}% — {progress.completedTypes.toLocaleString()}/{progress.totalTypes.toLocaleString()} {t('sync:icaoLdap.typeCompleteOf')}</span>
            </div>
            <div className="h-3 bg-gray-200 dark:bg-gray-600 rounded-full overflow-hidden">
              <div className="h-full bg-blue-500 rounded-full transition-all duration-300 ease-out" style={{ width: `${overallPercent}%` }} />
            </div>
          </div>

          {/* Current type progress */}
          {progress.currentType && progress.phase === 'PROCESSING' && (
            <div className="mb-4">
              <div className="flex justify-between text-xs mb-1">
                <span className="font-medium">{progress.currentType} {t('sync:icaoLdap.processingType')}</span>
                <span className="font-mono">{progress.currentTypeProcessed.toLocaleString()} / {progress.currentTypeTotal.toLocaleString()} ({progressPercent}%)</span>
              </div>
              <div className="h-2 bg-gray-200 dark:bg-gray-600 rounded-full overflow-hidden">
                <div className="h-full bg-green-500 rounded-full transition-all duration-300" style={{ width: `${progressPercent}%` }} />
              </div>
              <div className="flex gap-4 mt-1 text-[11px]">
                <span className="text-green-600 dark:text-green-400"> {t('sync:icaoLdap.newCount')}: +{progress.currentTypeNew.toLocaleString()}</span>
                <span className="text-gray-500"> {t('sync:icaoLdap.existingCount')}:{progress.currentTypeSkipped.toLocaleString()}</span>
              </div>
            </div>
          )}

          {/* Cumulative stats */}
          <div className="grid grid-cols-4 gap-3 mb-3">
            <div className="text-center bg-white/60 dark:bg-gray-800/60 rounded-lg py-2">
              <div className="text-lg font-bold text-blue-600">{progress.totalRemoteCount.toLocaleString()}</div>
              <div className="text-[10px] text-gray-500">{t('sync:icaoLdap.total')}</div>
            </div>
            <div className="text-center bg-white/60 dark:bg-gray-800/60 rounded-lg py-2">
              <div className="text-lg font-bold text-green-600">+{progress.totalNew.toLocaleString()}</div>
              <div className="text-[10px] text-gray-500">{t('sync:icaoLdap.newCount')}</div>
            </div>
            <div className="text-center bg-white/60 dark:bg-gray-800/60 rounded-lg py-2">
              <div className="text-lg font-bold text-gray-500">{progress.totalSkipped.toLocaleString()}</div>
              <div className="text-[10px] text-gray-500">{t('sync:icaoLdap.existingCount')}</div>
            </div>
            <div className="text-center bg-white/60 dark:bg-gray-800/60 rounded-lg py-2">
              <div className="text-lg font-bold text-red-500">{progress.totalFailed.toLocaleString()}</div>
              <div className="text-[10px] text-gray-500">{t('sync:icaoLdap.failedCount')}</div>
            </div>
          </div>

          {/* Type pipeline */}
          <div className="flex items-center justify-center gap-1">
            {CERT_TYPES.map((type, i) => {
              const isCompleted = i < progress.completedTypes;
              const isCurrent = type === progress.currentType;
              return (
                <div key={type} className="flex items-center gap-1">
                  {i > 0 && <ArrowRight className="w-3 h-3 text-gray-300" />}
                  <span className={`px-2 py-1 rounded-md text-[11px] font-semibold transition-all ${
                    isCompleted ? 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400' :
                    isCurrent ? 'bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400 ring-2 ring-blue-300 animate-pulse' :
                    'bg-gray-100 dark:bg-gray-700 text-gray-400'
                  }`}>
                    {isCompleted && <CheckCircle className="w-3 h-3 inline mr-0.5" />}
                    {isCurrent && <Loader2 className="w-3 h-3 inline mr-0.5 animate-spin" />}
                    {type}
                  </span>
                </div>
              );
            })}
          </div>

          {/* Status message */}
          <div className={`mt-3 text-xs rounded-lg p-2 ${
            progress.phase === 'FAILED'
              ? 'text-red-600 dark:text-red-400 bg-red-50/60 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
              : 'text-blue-600 dark:text-blue-400 bg-white/40 dark:bg-gray-800/40'
          }`}>
            {progress.phase === 'FAILED' ? <AlertTriangle className="w-3 h-3 inline mr-1" /> : <Info className="w-3 h-3 inline mr-1" />}
            {progress.message}
          </div>

          {/* Retry button on failure */}
          {progress.phase === 'FAILED' && (
            <div className="mt-2 flex items-center gap-3">
              <button onClick={handleSync}
                className="inline-flex items-center gap-1.5 px-4 py-2 text-xs font-medium rounded-lg bg-blue-500 hover:bg-blue-600 text-white transition-colors">
                <RefreshCw className="w-3.5 h-3.5" /> {t('sync:icaoLdap.syncRetry')}
              </button>
              <span className="text-xs text-gray-500">{t('sync:icaoLdap.syncRetryHint')}</span>
            </div>
          )}
        </div>
      )}

      {/* Sync Result Summary (shown after sync completes) */}
      {showSyncResult && status?.lastSync && (
        <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg border border-teal-200 dark:border-teal-800 overflow-hidden">
          {/* Header */}
          <div className={`px-5 py-3 flex items-center justify-between ${
            status.lastSync.status === 'COMPLETED'
              ? 'bg-gradient-to-r from-teal-500 to-emerald-500'
              : 'bg-gradient-to-r from-red-500 to-orange-500'
          }`}>
            <div className="flex items-center gap-2 text-white">
              {status.lastSync.status === 'COMPLETED'
                ? <CheckCircle className="w-5 h-5" />
                : <XCircle className="w-5 h-5" />}
              <h3 className="text-sm font-bold">
                {status.lastSync.status === 'COMPLETED' ? t('sync:icaoLdap.resultComplete') : t('sync:icaoLdap.resultFail')}
              </h3>
              <span className="text-xs opacity-80">
                {(status.lastSync.durationMs / 1000).toFixed(0)}{t('sync:icaoLdap.secondsSuffix')}
              </span>
            </div>
            <button onClick={() => setShowSyncResult(false)} className="text-white/70 hover:text-white">
              <X className="w-4 h-4" />
            </button>
          </div>

          <div className="p-5 space-y-4">
            {/* Summary stats */}
            <div className="grid grid-cols-2 sm:grid-cols-5 gap-3">
              <div className="text-center p-3 rounded-xl bg-blue-50 dark:bg-blue-900/20">
                <div className="text-xl font-bold text-blue-600">{status.lastSync.totalRemoteCount.toLocaleString()}</div>
                <div className="text-[10px] text-gray-500 font-medium">{t('sync:icaoLdap.icaoPkdTotal')}</div>
              </div>
              <div className="text-center p-3 rounded-xl bg-green-50 dark:bg-green-900/20">
                <div className="text-xl font-bold text-green-600">+{status.lastSync.newCertificates.toLocaleString()}</div>
                <div className="text-[10px] text-gray-500 font-medium">{t('sync:icaoLdap.newSaved')}</div>
              </div>
              <div className="text-center p-3 rounded-xl bg-gray-50 dark:bg-gray-700/30">
                <div className="text-xl font-bold text-gray-500">{status.lastSync.existingSkipped.toLocaleString()}</div>
                <div className="text-[10px] text-gray-500 font-medium">{t('sync:icaoLdap.existingSkip')}</div>
              </div>
              <div className="text-center p-3 rounded-xl bg-red-50 dark:bg-red-900/20">
                <div className="text-xl font-bold text-red-500">{status.lastSync.failedCount.toLocaleString()}</div>
                <div className="text-[10px] text-gray-500 font-medium">{t('sync:icaoLdap.failedCol')}</div>
              </div>
              <div className="text-center p-3 rounded-xl bg-purple-50 dark:bg-purple-900/20">
                <div className="text-xl font-bold text-purple-600">{triggerLabel(status.lastSync.triggeredBy)}</div>
                <div className="text-[10px] text-gray-500 font-medium">{t('sync:icaoLdap.trigger')}</div>
              </div>
            </div>

            {/* Type breakdown from latest history */}
            {history.length > 0 && history[0].typeStats && history[0].typeStats.length > 0 && (
              <div>
                <p className="text-xs font-semibold text-gray-500 mb-2">{t('sync:icaoLdap.typeResult')}</p>
                <div className="overflow-x-auto">
                  <table className="w-full text-xs">
                    <thead>
                      <tr className="bg-gray-50 dark:bg-gray-700">
                        <th className="px-3 py-2 text-left font-semibold">{t('sync:icaoLdap.typeCol')}</th>
                        <th className="px-3 py-2 text-center font-semibold">{t('sync:icaoLdap.totalCol')}</th>
                        <th className="px-3 py-2 text-center font-semibold text-green-600">{t('sync:icaoLdap.newCol')}</th>
                        <th className="px-3 py-2 text-center font-semibold text-gray-500">{t('sync:icaoLdap.existingCol')}</th>
                        <th className="px-3 py-2 text-center font-semibold text-red-500">{t('sync:icaoLdap.failedCol')}</th>
                        <th className="px-3 py-2 text-left font-semibold">{t('sync:icaoLdap.progressCol')}</th>
                      </tr>
                    </thead>
                    <tbody>
                      {history[0].typeStats.map((ts, i) => {
                        const total = ts.total || 1;
                        const successRate = ((ts.new + ts.skipped) / total * 100).toFixed(0);
                        return (
                          <tr key={i} className="border-t border-gray-100 dark:border-gray-700">
                            <td className="px-3 py-2 font-semibold">{ts.type}</td>
                            <td className="px-3 py-2 text-center font-mono">{ts.total.toLocaleString()}</td>
                            <td className="px-3 py-2 text-center font-semibold text-green-600">+{ts.new.toLocaleString()}</td>
                            <td className="px-3 py-2 text-center text-gray-500">{ts.skipped.toLocaleString()}</td>
                            <td className="px-3 py-2 text-center text-red-500">{ts.failed.toLocaleString()}</td>
                            <td className="px-3 py-2">
                              <div className="flex items-center gap-2">
                                <div className="flex-1 h-2 bg-gray-200 dark:bg-gray-600 rounded-full overflow-hidden">
                                  <div className="h-full bg-green-500 rounded-full" style={{ width: `${successRate}%` }} />
                                </div>
                                <span className="text-[10px] text-gray-400 w-8">{successRate}%</span>
                              </div>
                            </td>
                          </tr>
                        );
                      })}
                    </tbody>
                  </table>
                </div>
              </div>
            )}

            {/* Error message */}
            {status.lastSync.errorMessage && (
              <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg p-3">
                <div className="text-xs text-red-600 dark:text-red-400">{status.lastSync.errorMessage}</div>
              </div>
            )}

            {/* Certificate Statistics (from /api/upload/statistics) */}
            {certStats && (
              <div className="space-y-3">
                {/* 인증서 타입 분포 — 배지 형태 */}
                <div className="bg-gray-50 dark:bg-gray-800/50 rounded-xl p-3 border border-gray-200 dark:border-gray-700">
                  <p className="text-xs font-bold text-gray-600 dark:text-gray-300 mb-2">{t('sync:icaoLdap.certTypeDistribution')}</p>
                  <div className="flex flex-wrap gap-1.5">
                    <span className="px-2 py-0.5 rounded-full text-xs font-semibold bg-gray-200 text-gray-700 dark:bg-gray-600 dark:text-gray-200">
                      {certStats.countriesCount}{t('sync:icaoLdap.countrySuffix')}
                    </span>
                    {[
                      { label: 'CSCA (Self-Signed)', count: certStats.cscaBreakdown?.selfSigned, color: 'bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-300' },
                      { label: 'CSCA (Link Cert)', count: certStats.cscaBreakdown?.linkCertificates, color: 'bg-indigo-100 text-indigo-700 dark:bg-indigo-900/30 dark:text-indigo-300' },
                      { label: 'MLSC', count: certStats.mlscCount, color: 'bg-cyan-100 text-cyan-700 dark:bg-cyan-900/30 dark:text-cyan-300' },
                      { label: 'DSC', count: certStats.dscCount, color: 'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-300' },
                      { label: 'CRL', count: certStats.crlCount, color: 'bg-orange-100 text-orange-700 dark:bg-orange-900/30 dark:text-orange-300' },
                      { label: 'DSC_NC', count: certStats.dscNcCount, color: 'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-300' },
                    ].filter(t => t.count > 0).map(t => (
                      <span key={t.label} className={`px-2 py-0.5 rounded-full text-xs font-semibold ${t.color}`}>
                        {t.count?.toLocaleString()} {t.label}
                      </span>
                    ))}
                  </div>
                </div>

                {/* Trust Chain + 유효기간 — 2열 */}
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                  {/* Trust Chain 검증 */}
                  <div className="bg-gray-50 dark:bg-gray-800/50 rounded-xl p-3 border border-gray-200 dark:border-gray-700">
                    <p className="text-xs font-bold text-gray-600 dark:text-gray-300 mb-2 flex items-center gap-1">
                      <ShieldCheck className="w-3.5 h-3.5" /> {t('sync:icaoLdap.trustChainVerification')}
                    </p>
                    {certStats.validation && (certStats.validation.validCount > 0 || certStats.validation.invalidCount > 0 || certStats.validation.pendingCount > 0) ? (
                      <div className="space-y-1.5">
                        {[
                          { label: t('sync:icaoLdap.trustChainValid'), count: certStats.validation.validCount, color: 'text-green-600', bg: 'bg-green-500' },
                          { label: t('sync:icaoLdap.trustChainExpiredValid'), count: certStats.validation.expiredValidCount, color: 'text-amber-600', bg: 'bg-amber-500' },
                          { label: t('sync:icaoLdap.trustChainInvalid'), count: certStats.validation.invalidCount, color: 'text-red-600', bg: 'bg-red-500' },
                          { label: t('sync:icaoLdap.trustChainPending'), count: (certStats.validation.pendingCount || 0) + (certStats.validation.cscaNotFoundCount || 0), color: 'text-yellow-600', bg: 'bg-yellow-400' },
                        ].filter(v => v.count > 0).map(v => {
                          const total = (certStats.validation.validCount || 0) + (certStats.validation.expiredValidCount || 0) +
                                       (certStats.validation.invalidCount || 0) + (certStats.validation.pendingCount || 0) || 1;
                          return (
                            <div key={v.label} className="flex items-center gap-2 text-xs">
                              <span className={`w-28 truncate font-medium ${v.color}`}>{v.label}</span>
                              <div className="flex-1 h-1.5 bg-gray-200 dark:bg-gray-600 rounded-full overflow-hidden">
                                <div className={`h-full ${v.bg} rounded-full`} style={{ width: `${(v.count / total * 100)}%` }} />
                              </div>
                              <span className={`w-10 text-right font-bold ${v.color}`}>{v.count?.toLocaleString()}</span>
                            </div>
                          );
                        })}
                      </div>
                    ) : (
                      <p className="text-xs text-gray-400 italic">{t('sync:icaoLdap.trustChainNoData')}</p>
                    )}
                  </div>

                  {/* 유효기간 상태 — certificate.not_after 기반 */}
                  <div className="bg-gray-50 dark:bg-gray-800/50 rounded-xl p-3 border border-gray-200 dark:border-gray-700">
                    <p className="text-xs font-bold text-gray-600 dark:text-gray-300 mb-2 flex items-center gap-1">
                      <Clock className="w-3.5 h-3.5" /> {t('sync:icaoLdap.expiryStatus')}
                    </p>
                    {(() => {
                      const v = certStats.validation || {};
                      const certValid = v.certValidCount || 0;
                      const certExpired = v.certExpiredCount || 0;
                      const revoked = v.revokedCount || 0;
                      const total = certValid + certExpired || 1;
                      const validPct = (certValid / total * 100).toFixed(0);
                      if (certValid === 0 && certExpired === 0) return <p className="text-xs text-gray-400 italic">{t('sync:icaoLdap.trustChainNoData')}</p>;
                      return (
                        <div className="space-y-1.5">
                          <div className="flex items-center gap-2 text-xs">
                            <span className="w-16 font-medium text-green-600">{t('sync:icaoLdap.expiryValid')}</span>
                            <div className="flex-1 h-1.5 bg-gray-200 dark:bg-gray-600 rounded-full overflow-hidden">
                              <div className="h-full bg-green-500 rounded-full" style={{ width: `${validPct}%` }} />
                            </div>
                            <span className="w-14 text-right font-bold text-green-600">{certValid.toLocaleString()}</span>
                          </div>
                          <div className="flex items-center gap-2 text-xs">
                            <span className="w-16 font-medium text-red-600">{t('sync:icaoLdap.expiryExpired')}</span>
                            <div className="flex-1 h-1.5 bg-gray-200 dark:bg-gray-600 rounded-full overflow-hidden">
                              <div className="h-full bg-red-500 rounded-full" style={{ width: `${(100 - Number(validPct))}%` }} />
                            </div>
                            <span className="w-14 text-right font-bold text-red-600">{certExpired.toLocaleString()}</span>
                          </div>
                          {revoked > 0 && (
                            <div className="flex items-center gap-2 text-xs pt-1 border-t border-gray-200 dark:border-gray-600">
                              <span className="w-16 font-medium text-orange-600">{t('sync:icaoLdap.expiryRevoked')}</span>
                              <div className="flex-1" />
                              <span className="w-14 text-right font-bold text-orange-600">{revoked.toLocaleString()}</span>
                            </div>
                          )}
                          <p className="text-[10px] text-gray-400 text-right">{t('sync:icaoLdap.expiryValidPct')}: {validPct}%</p>
                        </div>
                      );
                    })()}
                  </div>
                </div>

                {/* ICAO Doc 9303 준수 검사 결과 */}
                {certStats.validation?.icao && certStats.validation.icao.total > 0 && (
                  <div className="bg-gray-50 dark:bg-gray-800/50 rounded-xl p-3 border border-gray-200 dark:border-gray-700">
                    <p className="text-xs font-bold text-gray-600 dark:text-gray-300 mb-2 flex items-center gap-1">
                      <ShieldCheck className="w-3.5 h-3.5" /> {t('sync:icaoLdap.doc9303Title')}
                      <span className="ml-auto text-gray-400 font-normal">{certStats.validation.icao.total.toLocaleString()}{t('sync:icaoLdap.doc9303Checked')}</span>
                    </p>
                    <div className="grid grid-cols-3 gap-2 mb-3">
                      <div className="text-center p-2 rounded-lg bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800">
                        <div className="text-lg font-bold text-green-600">{certStats.validation.icao.compliantCount.toLocaleString()}</div>
                        <div className="text-[10px] text-gray-500">{t('sync:icaoLdap.doc9303Compliant')}</div>
                      </div>
                      <div className="text-center p-2 rounded-lg bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800">
                        <div className="text-lg font-bold text-amber-600">{certStats.validation.icao.warningCount.toLocaleString()}</div>
                        <div className="text-[10px] text-gray-500">{t('sync:icaoLdap.doc9303Warning')}</div>
                      </div>
                      <div className="text-center p-2 rounded-lg bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800">
                        <div className="text-lg font-bold text-red-600">{certStats.validation.icao.nonCompliantCount.toLocaleString()}</div>
                        <div className="text-[10px] text-gray-500">{t('sync:icaoLdap.doc9303NonCompliant')}</div>
                      </div>
                    </div>
                    {/* Category breakdown */}
                    {(certStats.validation.icao.nonCompliantCount > 0 || certStats.validation.icao.warningCount > 0) && (
                      <div className="grid grid-cols-2 sm:grid-cols-3 gap-1.5">
                        {[
                          { label: 'Key Usage', count: certStats.validation.icao.keyUsageFail, color: 'text-red-600' },
                          { label: 'Algorithm', count: certStats.validation.icao.algorithmFail, color: 'text-red-600' },
                          { label: 'Key Size', count: certStats.validation.icao.keySizeFail, color: 'text-red-600' },
                          { label: t('sync:icaoLdap.expiryStatus'), count: certStats.validation.icao.validityPeriodFail, color: 'text-amber-600' },
                          { label: t('common:label.extensions', '확장 필드'), count: certStats.validation.icao.extensionsFail, color: 'text-red-600' },
                        ].filter(c => c.count > 0).map(c => (
                          <div key={c.label} className="flex items-center justify-between text-xs bg-white dark:bg-gray-800 rounded px-2 py-1">
                            <span className="text-gray-600 dark:text-gray-400">{c.label}</span>
                            <span className={`font-bold ${c.color}`}>{c.count.toLocaleString()}</span>
                          </div>
                        ))}
                      </div>
                    )}
                  </div>
                )}
              </div>
            )}
          </div>
        </div>
      )}

      {/* Sync History Table */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        <div className="flex items-center justify-between p-4 border-b border-gray-100 dark:border-gray-700">
          <div className="flex items-center gap-2">
            <History className="w-4 h-4 text-gray-500" />
            <h3 className="text-sm font-semibold"> {t('sync:icaoLdap.syncHistory')}</h3>
            <span className="text-xs text-gray-400">{historyTotal}{t('sync:icaoLdap.certCountSuffix')}</span>
          </div>
          <div className="flex items-center gap-2">
            <select value={historyStatusFilter}
              onChange={(e) => { setHistoryStatusFilter(e.target.value); setHistoryPage(0); }}
              className="px-2 py-1 text-xs border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700">
              <option value=""> {t('sync:icaoLdap.allStatus')}</option>
              <option value="COMPLETED">{t('sync:icaoLdap.statusCompleted')}</option>
              <option value="FAILED">{t('sync:icaoLdap.statusFailed')}</option>
              <option value="RUNNING">{t('sync:icaoLdap.statusRunning')}</option>
            </select>
          </div>
        </div>
        {history.length > 0 ? (
          <div className="overflow-x-auto">
            <table className="w-full text-xs">
              <thead>
                <tr className="bg-slate-100 dark:bg-gray-700">
                  <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">{t('sync:icaoLdap.startTime')}</th>
                  <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">{t('sync:icaoLdap.status')}</th>
                  <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">{t('sync:icaoLdap.trigger')}</th>
                  <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">{t('sync:icaoLdap.total')}</th>
                  <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">{t('sync:icaoLdap.newCount')}</th>
                  <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">{t('sync:icaoLdap.existingCount')}</th>
                  <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">{t('sync:icaoLdap.failedCount')}</th>
                  <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">{t('sync:icaoLdap.duration')}</th>
                  <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">{t('sync:icaoLdap.detail')}</th>
                </tr>
              </thead>
              <tbody>
                {history.map((h, i) => (
                  <tr key={i} onClick={() => { setSelectedHistory(h); setDetailTab('summary'); if (!certStats) uploadApi.getStatistics().then(r => setCertStats(r.data)).catch(() => {}); }} className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors cursor-pointer">
                    <td className="px-3 py-2 text-center text-xs text-gray-500 dark:text-gray-400 whitespace-nowrap">{h.createdAt || '—'}</td>
                    <td className="px-3 py-2 text-center">
                      <span className={`inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium ${
                        h.status === 'COMPLETED' ? 'bg-green-100 dark:bg-green-900/30 text-green-600 dark:text-green-400' :
                        h.status === 'FAILED' ? 'bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400' :
                        'bg-yellow-100 dark:bg-yellow-900/30 text-yellow-600 dark:text-yellow-400'
                      }`}>{statusIcon(h.status)} {statusLabel(h.status)}</span>
                    </td>
                    <td className="px-3 py-2 text-center">
                      <span className={`inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium ${
                        h.triggeredBy === 'MANUAL' ? 'bg-blue-100 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400' : 'bg-gray-100 dark:bg-gray-700 text-gray-500'
                      }`}>{triggerLabel(h.triggeredBy)}</span>
                    </td>
                    <td className="px-3 py-2 text-center font-mono">{h.totalRemoteCount.toLocaleString()}</td>
                    <td className="px-3 py-2 text-center font-semibold text-green-600 dark:text-green-400">+{h.newCertificates.toLocaleString()}</td>
                    <td className="px-3 py-2 text-center text-gray-400">{h.existingSkipped.toLocaleString()}</td>
                    <td className="px-3 py-2 text-center text-red-500">{h.failedCount.toLocaleString()}</td>
                    <td className="px-3 py-2 text-center text-gray-400">{(h.durationMs / 1000).toFixed(1)}s</td>
                    <td className="px-3 py-2 text-center">
                      <button onClick={(e) => { e.stopPropagation(); setSelectedHistory(h); setDetailTab('summary'); if (!certStats) uploadApi.getStatistics().then(r => setCertStats(r.data)).catch(() => {}); }}
                        className="inline-flex items-center gap-1 px-2.5 py-1 rounded-md text-xs font-medium text-blue-600 dark:text-blue-400 hover:bg-blue-50 dark:hover:bg-blue-900/20 transition-colors">
                        <Eye className="w-3 h-3" />
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        ) : (
          <div className="p-8 text-center text-sm text-gray-400">
            {t('sync:icaoLdap.noHistoryYet')}
          </div>
        )}
        {/* Pagination */}
        {historyTotal > historyPageSize && (
          <div className="flex items-center justify-between px-4 py-3 border-t border-gray-200 dark:border-gray-700">
            <span className="text-xs text-gray-500 dark:text-gray-400">
              총 {historyTotal}{t('sync:icaoLdap.certCountSuffix')}
            </span>
            <div className="flex items-center gap-2">
              <button onClick={() => setHistoryPage(p => Math.max(0, p - 1))} disabled={historyPage === 0}
                className="p-1 rounded hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-30">
                <ChevronLeft className="w-4 h-4" />
              </button>
              <span className="text-xs text-gray-500">{historyPage + 1} / {Math.ceil(historyTotal / historyPageSize)}</span>
              <button onClick={() => setHistoryPage(p => p + 1)} disabled={(historyPage + 1) * historyPageSize >= historyTotal}
                className="p-1 rounded hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-30">
                <ChevronRight className="w-4 h-4" />
              </button>
            </div>
          </div>
        )}
      </div>

      {/* Info */}
      <div className="bg-blue-50 dark:bg-blue-900/10 border border-blue-200 dark:border-blue-800 rounded-xl p-4">
        <div className="flex items-start gap-3">
          <Info className="w-5 h-5 text-blue-500 mt-0.5 flex-shrink-0" />
          <div className="text-xs text-blue-700 dark:text-blue-300 space-y-1">
            <p className="font-medium">{t('sync:icaoLdap.infoTitle')}</p>
            <p>{t('sync:icaoLdap.infoDesc1')}</p>
            <p>{t('sync:icaoLdap.infoDesc2')}</p>
            <p>{t('sync:icaoLdap.infoDesc3')}</p>
          </div>
        </div>
      </div>
      {/* Sync Detail Dialog */}
      {selectedHistory && (
        <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/50" onClick={() => setSelectedHistory(null)}>
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl w-full max-w-2xl mx-4 max-h-[85vh] flex flex-col" onClick={(e) => e.stopPropagation()}>
            {/* Header */}
            <div className="flex items-center justify-between px-5 py-4 border-b border-gray-200 dark:border-gray-700 flex-shrink-0">
              <div>
                <div className="flex items-center gap-2">
                  {statusIcon(selectedHistory.status)}
                  <h2 className="text-base font-bold">{t('sync:icaoLdap.detailTitle')}</h2>
                </div>
                <p className="text-xs text-gray-400 mt-0.5">{selectedHistory.createdAt} · {triggerLabel(selectedHistory.triggeredBy)} · {(selectedHistory.durationMs / 1000).toFixed(0)}{t('sync:icaoLdap.secondsSuffix')}</p>
              </div>
              <button onClick={() => setSelectedHistory(null)} className="p-1 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700">
                <X className="w-5 h-5 text-gray-400" />
              </button>
            </div>

            {/* Tabs */}
            <div className="flex border-b border-gray-200 dark:border-gray-700 px-5 flex-shrink-0">
              {[
                { key: 'summary' as const, label: '상세 정보' },
                { key: 'doc9303' as const, label: t('sync:icaoLdap.doc9303Title') },
                ...(selectedHistory.existingSkipped > 0 ? [{
                  key: 'duplicates' as const,
                  label: t('sync:icaoLdap.dupTabTitle'),
                  badge: selectedHistory.existingSkipped,
                }] : []),
              ].map(tab => (
                <button key={tab.key}
                  onClick={() => {
                    setDetailTab(tab.key);
                    if (tab.key === 'duplicates' && duplicates.length === 0) {
                      uploadHistoryApi.getIssues('ICAO_PKD_SYNC').then(r => {
                        const d = r.data;
                        setDuplicates(Array.isArray(d) ? d : (d as { duplicates?: unknown[] })?.duplicates || []);
                      }).catch(() => {});
                    }
                  }}
                  className={`px-4 py-2.5 text-sm font-medium border-b-2 transition-colors ${
                    detailTab === tab.key
                      ? 'border-blue-500 text-blue-600 dark:text-blue-400'
                      : 'border-transparent text-gray-500 hover:text-gray-700 dark:hover:text-gray-300'
                  }`}>
                  {tab.label}
                  {'badge' in tab && tab.badge != null && (
                    <span className="ml-1.5 px-1.5 py-0.5 rounded-full text-[10px] font-bold bg-amber-100 text-amber-700 dark:bg-amber-900/30 dark:text-amber-300">
                      {(tab.badge as number).toLocaleString()}
                    </span>
                  )}
                </button>
              ))}
            </div>

                  {/* Tab Content */}
                  <div className="overflow-y-auto flex-1 p-5 space-y-4">
                    {detailTab === 'summary' && (
                      <>
                        {/* Totals */}
                        <div className="grid grid-cols-4 gap-2">
                          <div className="text-center bg-blue-50 dark:bg-blue-900/20 rounded-lg py-2">
                            <div className="text-lg font-bold text-blue-600">{selectedHistory.totalRemoteCount.toLocaleString()}</div>
                            <div className="text-[10px] text-gray-500">{t('sync:icaoLdap.total')}</div>
                          </div>
                          <div className="text-center bg-green-50 dark:bg-green-900/20 rounded-lg py-2">
                            <div className="text-lg font-bold text-green-600">+{selectedHistory.newCertificates.toLocaleString()}</div>
                            <div className="text-[10px] text-gray-500">{t('sync:icaoLdap.newCount')}</div>
                          </div>
                          <div className="text-center bg-gray-50 dark:bg-gray-700/50 rounded-lg py-2">
                            <div className="text-lg font-bold text-gray-500">{selectedHistory.existingSkipped.toLocaleString()}</div>
                            <div className="text-[10px] text-gray-500">{t('sync:icaoLdap.existingCount')}</div>
                          </div>
                          <div className="text-center bg-red-50 dark:bg-red-900/20 rounded-lg py-2">
                            <div className="text-lg font-bold text-red-500">{selectedHistory.failedCount.toLocaleString()}</div>
                            <div className="text-[10px] text-gray-500">{t('sync:icaoLdap.failedCount')}</div>
                          </div>
                        </div>

                        {/* Type Breakdown */}
                        {selectedHistory.typeStats && selectedHistory.typeStats.length > 0 && (
                          <div>
                            <h3 className="text-xs font-bold text-gray-600 dark:text-gray-300 mb-2 flex items-center gap-1.5">
                              <Database className="w-3.5 h-3.5" /> {t('sync:icaoLdap.typeBreakdown')}
                            </h3>
                            <div className="border border-gray-200 dark:border-gray-700 rounded-lg overflow-hidden">
                              <table className="w-full text-xs">
                                <thead>
                                  <tr className="bg-gray-50 dark:bg-gray-700/50 text-gray-500">
                                    <th className="px-3 py-2 text-left font-medium">{t('sync:icaoLdap.certType')}</th>
                                    <th className="px-3 py-2 text-right font-medium">{t('sync:icaoLdap.total')}</th>
                                    <th className="px-3 py-2 text-right font-medium text-green-600">{t('sync:icaoLdap.newCount')}</th>
                                    <th className="px-3 py-2 text-right font-medium">{t('sync:icaoLdap.existingCount')}</th>
                                    <th className="px-3 py-2 text-right font-medium text-red-500">{t('sync:icaoLdap.failedCount')}</th>
                                  </tr>
                                </thead>
                                <tbody>
                                  {selectedHistory.typeStats.map((ts, i) => (
                                    <tr key={i} className="border-t border-gray-100 dark:border-gray-700/50">
                                      <td className="px-3 py-2"><span className={`px-1.5 py-0.5 rounded text-[10px] font-semibold ${
                                        ts.type === 'ML→CSCA' ? 'bg-purple-100 text-purple-700 dark:bg-purple-900/30 dark:text-purple-300' :
                                        ts.type === 'CRL' ? 'bg-amber-100 text-amber-700 dark:bg-amber-900/30 dark:text-amber-300' :
                                        ts.type === 'DSC' ? 'bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-300' :
                                        'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-300'
                                      }`}>{ts.type}</span></td>
                                      <td className="px-3 py-2 text-right font-mono">{ts.total.toLocaleString()}</td>
                                      <td className="px-3 py-2 text-right font-mono text-green-600">+{ts.new.toLocaleString()}</td>
                                      <td className="px-3 py-2 text-right font-mono text-gray-400">{ts.skipped.toLocaleString()}</td>
                                      <td className="px-3 py-2 text-right font-mono text-red-500">{ts.failed.toLocaleString()}</td>
                                    </tr>
                                  ))}
                                </tbody>
                              </table>
                            </div>
                          </div>
                        )}

                        {/* Trust Chain + Expiry (from certStats if available) */}
                        {certStats?.validation && (
                          <div className="grid grid-cols-2 gap-3">
                            <div className="bg-gray-50 dark:bg-gray-800/50 rounded-xl p-3 border border-gray-200 dark:border-gray-700">
                              <p className="text-xs font-bold text-gray-600 dark:text-gray-300 mb-2 flex items-center gap-1">
                                <ShieldCheck className="w-3.5 h-3.5" /> {t('sync:icaoLdap.trustChainVerification')}
                              </p>
                              {(certStats.validation.validCount > 0 || certStats.validation.invalidCount > 0 || certStats.validation.pendingCount > 0) ? (
                                <div className="space-y-1">
                                  {[
                                    { label: t('sync:icaoLdap.trustChainValid'), count: certStats.validation.validCount, color: 'text-green-600' },
                                    { label: t('sync:icaoLdap.trustChainExpiredValid'), count: certStats.validation.expiredValidCount, color: 'text-amber-600' },
                                    { label: t('sync:icaoLdap.trustChainInvalid'), count: certStats.validation.invalidCount, color: 'text-red-600' },
                                    { label: t('sync:icaoLdap.trustChainPending'), count: (certStats.validation.pendingCount || 0), color: 'text-yellow-600' },
                                  ].filter(v => v.count > 0).map(v => (
                                    <div key={v.label} className="flex justify-between text-xs">
                                      <span className={v.color}>{v.label}</span>
                                      <span className={`font-bold ${v.color}`}>{v.count?.toLocaleString()}</span>
                                    </div>
                                  ))}
                                </div>
                              ) : <p className="text-xs text-gray-400 italic">{t('sync:icaoLdap.trustChainNoData')}</p>}
                            </div>
                            <div className="bg-gray-50 dark:bg-gray-800/50 rounded-xl p-3 border border-gray-200 dark:border-gray-700">
                              <p className="text-xs font-bold text-gray-600 dark:text-gray-300 mb-2 flex items-center gap-1">
                                <Clock className="w-3.5 h-3.5" /> {t('sync:icaoLdap.expiryStatus')}
                              </p>
                              {(certStats.validation.certValidCount > 0 || certStats.validation.certExpiredCount > 0) ? (
                                <div className="space-y-1">
                                  <div className="flex justify-between text-xs">
                                    <span className="text-green-600">{t('sync:icaoLdap.expiryValid')}</span>
                                    <span className="font-bold text-green-600">{certStats.validation.certValidCount?.toLocaleString()}</span>
                                  </div>
                                  <div className="flex justify-between text-xs">
                                    <span className="text-red-600">{t('sync:icaoLdap.expiryExpired')}</span>
                                    <span className="font-bold text-red-600">{certStats.validation.certExpiredCount?.toLocaleString()}</span>
                                  </div>
                                </div>
                              ) : <p className="text-xs text-gray-400 italic">{t('sync:icaoLdap.trustChainNoData')}</p>}
                            </div>
                          </div>
                        )}

                        {/* Error */}
                        {selectedHistory.errorMessage && selectedHistory.errorMessage.trim() && (
                          <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg p-3">
                            <div className="text-xs text-red-600 dark:text-red-400">{selectedHistory.errorMessage}</div>
                          </div>
                        )}
                      </>
                    )}

                    {detailTab === 'doc9303' && (
                      <>
                        {certStats?.validation?.icao && certStats.validation.icao.total > 0 ? (
                          <div className="space-y-4">
                            {/* Compliance summary */}
                            <div className="grid grid-cols-3 gap-3">
                              <div className="text-center p-3 rounded-xl bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800">
                                <div className="text-2xl font-bold text-green-600">{certStats.validation.icao.compliantCount.toLocaleString()}</div>
                                <div className="text-xs text-gray-500">{t('sync:icaoLdap.doc9303Compliant')}</div>
                                <div className="text-[10px] text-green-500">
                                  {((certStats.validation.icao.compliantCount / certStats.validation.icao.total) * 100).toFixed(0)}%
                                </div>
                              </div>
                              <div className="text-center p-3 rounded-xl bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800">
                                <div className="text-2xl font-bold text-amber-600">{certStats.validation.icao.warningCount.toLocaleString()}</div>
                                <div className="text-xs text-gray-500">{t('sync:icaoLdap.doc9303Warning')}</div>
                              </div>
                              <div className="text-center p-3 rounded-xl bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800">
                                <div className="text-2xl font-bold text-red-600">{certStats.validation.icao.nonCompliantCount.toLocaleString()}</div>
                                <div className="text-xs text-gray-500">{t('sync:icaoLdap.doc9303NonCompliant')}</div>
                              </div>
                            </div>

                            {/* Category breakdown */}
                            <div className="bg-gray-50 dark:bg-gray-800/50 rounded-xl p-4 border border-gray-200 dark:border-gray-700">
                              <p className="text-xs font-bold text-gray-600 dark:text-gray-300 mb-3">{t('sync:icaoLdap.doc9303CategoryDetail')} <span className="font-normal text-gray-400">({t('sync:icaoLdap.doc9303ClickHint')})</span></p>
                              <div className="space-y-2">
                                {[
                                  { label: 'Key Usage', apiKey: 'keyUsage', count: certStats.validation.icao.keyUsageFail, desc: 'keyCertSign, cRLSign, digitalSignature 등' },
                                  { label: 'Algorithm', apiKey: 'algorithm', count: certStats.validation.icao.algorithmFail, desc: 'RSA/ECDSA + SHA-256/384/512' },
                                  { label: 'Key Size', apiKey: 'keySize', count: certStats.validation.icao.keySizeFail, desc: 'RSA ≥ 2048, ECDSA P-256/384/521' },
                                  { label: t('sync:icaoLdap.expiryStatus'), apiKey: 'validityPeriod', count: certStats.validation.icao.validityPeriodFail, desc: 'CSCA ≤ 15년, DSC ≤ 3년' },
                                  { label: t('common:label.extensions', '확장 필드'), apiKey: 'extensions', count: certStats.validation.icao.extensionsFail, desc: 'Basic Constraints, Key Usage 확장' },
                                ].map(c => (
                                  <button key={c.label} disabled={c.count === 0}
                                    onClick={() => {
                                      if (c.count === 0) return;
                                      setNcInitialCategory(c.apiKey);
                                      setNcDialogOpen(true);
                                    }}
                                    className={`w-full flex items-center gap-3 text-xs rounded-lg px-2 py-1.5 transition-colors ${
                                      c.count > 0 ? 'hover:bg-white dark:hover:bg-gray-700 cursor-pointer' : 'opacity-50 cursor-default'
                                    }`}>
                                    <span className="w-20 text-left font-medium text-gray-700 dark:text-gray-300">{c.label}</span>
                                    <div className="flex-1 h-1.5 bg-gray-200 dark:bg-gray-600 rounded-full overflow-hidden">
                                      <div className={`h-full rounded-full ${c.count > 0 ? 'bg-red-500' : 'bg-green-500'}`}
                                        style={{ width: `${Math.min(100, (c.count / (certStats.validation.icao.total || 1)) * 100 * 10)}%` }} />
                                    </div>
                                    <span className={`w-12 text-right font-bold ${c.count > 0 ? 'text-red-600' : 'text-green-600'}`}>{c.count}{t('sync:icaoLdap.certCountSuffix')}</span>
                                    {c.count > 0 && <ChevronRight className="w-3 h-3 text-gray-400" />}
                                  </button>
                                ))}
                              </div>
                              <p className="text-[10px] text-gray-400 mt-3">ICAO Doc 9303 Part 12 기반 ({certStats.validation.icao.total.toLocaleString()}{t('sync:icaoLdap.doc9303Checked')})</p>
                            </div>
                          </div>
                        ) : (
                          <div className="text-center py-10 text-gray-400">
                            <ShieldCheck className="w-10 h-10 mx-auto mb-2 opacity-30" />
                            <p className="text-sm">{t('sync:icaoLdap.doc9303NoData')}</p>
                            <p className="text-xs mt-1">{t('sync:icaoLdap.doc9303NoDataHint')}</p>
                          </div>
                        )}
                      </>
                    )}

                    {detailTab === 'duplicates' && selectedHistory.existingSkipped > 0 && (
                      <div className="space-y-4">
                        {duplicates.length > 0 ? (
                          <DuplicateCertificatesTree duplicates={duplicates} />
                        ) : (
                          <>
                            <div className="bg-amber-50 dark:bg-amber-900/10 border border-amber-200 dark:border-amber-800 rounded-xl p-4">
                              <div className="flex items-center gap-2 mb-2">
                                <Info className="w-4 h-4 text-amber-600" />
                                <p className="text-xs text-amber-700 dark:text-amber-400 font-medium">
                                  {t('sync:icaoLdap.dupSkipMsg', '').replace('{{count}}', selectedHistory.existingSkipped.toLocaleString())}
                                </p>
                              </div>
                              <p className="text-xs text-amber-600 dark:text-amber-500">
                                {t('sync:icaoLdap.dupLoadingHint')}
                              </p>
                            </div>

                            {/* Per-type skip breakdown */}
                            {selectedHistory.typeStats && selectedHistory.typeStats.length > 0 && (
                              <div className="bg-gray-50 dark:bg-gray-800/50 rounded-xl p-4 border border-gray-200 dark:border-gray-700">
                                <p className="text-xs font-bold text-gray-600 dark:text-gray-300 mb-3">{t('sync:icaoLdap.dupTypeBreakdown')}</p>
                                <div className="space-y-2">
                                  {selectedHistory.typeStats.filter(ts => ts.skipped > 0).map((ts, i) => {
                                    const total = ts.total || 1;
                                    const skipPct = (ts.skipped / total * 100).toFixed(0);
                                    return (
                                      <div key={i} className="flex items-center gap-3 text-xs">
                                        <span className={`w-20 px-1.5 py-0.5 rounded text-center font-semibold ${
                                          ts.type === 'ML→CSCA' ? 'bg-purple-100 text-purple-700 dark:bg-purple-900/30 dark:text-purple-300' :
                                          ts.type === 'CRL' ? 'bg-amber-100 text-amber-700 dark:bg-amber-900/30 dark:text-amber-300' :
                                          ts.type === 'DSC' ? 'bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-300' :
                                          'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-300'
                                        }`}>{ts.type}</span>
                                        <div className="flex-1 h-2 bg-gray-200 dark:bg-gray-600 rounded-full overflow-hidden">
                                          <div className="h-full bg-amber-400 rounded-full" style={{ width: `${skipPct}%` }} />
                                        </div>
                                        <span className="w-24 text-right text-gray-500">
                                          <span className="font-bold text-amber-600">{ts.skipped.toLocaleString()}</span>
                                          <span className="text-gray-400">/{ts.total.toLocaleString()}</span>
                                        </span>
                                        <span className="w-10 text-right text-gray-400">{skipPct}%</span>
                                      </div>
                                    );
                                  })}
                                </div>
                              </div>
                            )}
                          </>
                        )}
                      </div>
                    )}
                  </div>
            <div className="flex justify-end p-4 border-t border-gray-200 dark:border-gray-700 flex-shrink-0">
              <button onClick={() => setSelectedHistory(null)}
                className="px-4 py-2 text-sm font-medium rounded-lg border border-gray-300 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors">
                {t('sync:icaoLdap.close')}
              </button>
            </div>
          </div>
        </div>
      )}
      {/* ICAO Non-Compliant Detail Dialog (reuse existing component) */}
      {certStats?.validation?.icao && (
        <IcaoViolationDetailDialog
          open={ncDialogOpen}
          onClose={() => setNcDialogOpen(false)}
          uploadId="ICAO_PKD_SYNC"
          violations={{
            ...(certStats.validation.icao.keyUsageFail > 0 ? { keyUsage: certStats.validation.icao.keyUsageFail } : {}),
            ...(certStats.validation.icao.algorithmFail > 0 ? { algorithm: certStats.validation.icao.algorithmFail } : {}),
            ...(certStats.validation.icao.keySizeFail > 0 ? { keySize: certStats.validation.icao.keySizeFail } : {}),
            ...(certStats.validation.icao.validityPeriodFail > 0 ? { validityPeriod: certStats.validation.icao.validityPeriodFail } : {}),
            ...(certStats.validation.icao.extensionsFail > 0 ? { extensions: certStats.validation.icao.extensionsFail } : {}),
          }}
          totalNonCompliantCount={certStats.validation.icao.nonCompliantCount + certStats.validation.icao.warningCount}
          initialCategory={ncInitialCategory}
        />
      )}
    </div>
  );
}
