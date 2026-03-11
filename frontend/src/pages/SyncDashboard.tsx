import { useTranslation } from 'react-i18next';
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
  Edit,
  Save,
  X as CloseIcon,
} from 'lucide-react';
import { syncServiceApi, type SyncConfigResponse, type RevalidationHistoryItem, type RevalidationResult, type UpdateSyncConfigRequest } from '@/services/api';
import type { SyncStatusResponse, SyncHistoryItem, SyncStatusType } from '@/types';
import { cn } from '@/utils/cn';
import { formatDateTime } from '@/utils/dateFormat';
import { Dialog } from '@/components/common/Dialog';
import { ReconciliationHistory } from '@/components/sync/ReconciliationHistory';
import { useSortableTable } from '@/hooks/useSortableTable';
import { SortableHeader } from '@/components/common/SortableHeader';

export function SyncDashboard() {
  const { t } = useTranslation(['sync', 'common']);
  const [status, setStatus] = useState<SyncStatusResponse | null>(null);
  const [history, setHistory] = useState<SyncHistoryItem[]>([]);
  const [config, setConfig] = useState<SyncConfigResponse | null>(null);
  const [revalidationHistory, setRevalidationHistory] = useState<RevalidationHistoryItem[]>([]);
  const [loading, setLoading] = useState(true);
  const [checking, setChecking] = useState(false);
  const [revalidating, setRevalidating] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [showConfigDialog, setShowConfigDialog] = useState(false);
  const [editedConfig, setEditedConfig] = useState<UpdateSyncConfigRequest>({});
  const [saving, setSaving] = useState(false);
  const [revalidationResult, setRevalidationResult] = useState<RevalidationResult | null>(null);
  const [syncCheckResult, setSyncCheckResult] = useState<SyncStatusResponse | null>(null);
  const [configSaveResult, setConfigSaveResult] = useState<SyncConfigResponse | null>(null);
  const [triggeringDailySync, setTriggeringDailySync] = useState(false);
  const [dailySyncResult, setDailySyncResult] = useState<{ success: boolean; message: string } | null>(null);

  const { sortedData: sortedHistory, sortConfig: historySortConfig, requestSort: requestHistorySort } = useSortableTable(history);
  const { sortedData: sortedRevalidation, sortConfig: revalSortConfig, requestSort: requestRevalSort } = useSortableTable(revalidationHistory);

  const fetchData = useCallback(async () => {
    try {
      setError(null);
      const [statusRes, historyRes, configRes, revalHistoryRes] = await Promise.all([
        syncServiceApi.getStatus(),
        syncServiceApi.getHistory(10),
        syncServiceApi.getConfig(),
        syncServiceApi.getRevalidationHistory(5),
      ]);

      // Debug logging
      if (import.meta.env.DEV) console.log('API Responses:', {
        status: statusRes.data,
        history: historyRes.data,
        config: configRes.data,
        revalidation: revalHistoryRes.data
      });

      setStatus(statusRes.data?.data ?? null);  // Extract nested data field
      setHistory(historyRes.data?.data ?? []);  // Extract data array from paginated response
      setConfig(configRes.data ?? null);
      setRevalidationHistory(Array.isArray(revalHistoryRes.data) ? revalHistoryRes.data : []);
    } catch (err) {
      if (import.meta.env.DEV) console.error('Failed to fetch sync data:', err);
      setError(t('dashboard.connectionError'));
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
    setError(null);
    try {
      const checkResult = await syncServiceApi.triggerCheck();

      if (checkResult.data && checkResult.data.success) {
        const resultData = checkResult.data?.data ?? null;
        setStatus(resultData);
        setSyncCheckResult(resultData);
      }

      await fetchData();
    } catch (err) {
      if (import.meta.env.DEV) console.error('Manual check failed:', err);
      setError(t('dashboard.manualCheckFailed'));
    } finally {
      setChecking(false);
    }
  };

  const handleRevalidation = async () => {
    setRevalidating(true);
    try {
      const res = await syncServiceApi.triggerRevalidation();
      const result = res.data as RevalidationResult;
      setRevalidationResult(result);
      await fetchData();
    } catch (err) {
      if (import.meta.env.DEV) console.error('Revalidation failed:', err);
      setError(t('dashboard.revalidationFailed'));
    } finally {
      setRevalidating(false);
    }
  };

  const handleOpenConfigDialog = () => {
    if (config) {
      setEditedConfig({
        dailySyncEnabled: config.dailySyncEnabled,
        dailySyncHour: config.dailySyncHour,
        dailySyncMinute: config.dailySyncMinute,
        autoReconcile: config.autoReconcile,
        revalidateCertsOnSync: config.revalidateCertsOnSync,
        maxReconcileBatchSize: config.maxReconcileBatchSize,
      });
    }
    setShowConfigDialog(true);
  };

  const handleSaveConfig = async () => {
    setSaving(true);
    try {
      const res = await syncServiceApi.updateConfig(editedConfig);
      await fetchData();
      setShowConfigDialog(false);
      setError(null);
      if (res.data?.config) {
        setConfigSaveResult(res.data.config);
      }
    } catch (err) {
      if (import.meta.env.DEV) console.error('Failed to update config:', err);
      setError(t('dashboard.configUpdateFailed'));
    } finally {
      setSaving(false);
    }
  };

  const handleTriggerDailySync = async () => {
    setTriggeringDailySync(true);
    setError(null);
    try {
      const res = await syncServiceApi.triggerDailySync();
      setDailySyncResult(res.data);
    } catch (err) {
      if (import.meta.env.DEV) console.error('Daily sync trigger failed:', err);
      setDailySyncResult({ success: false, message: t('dashboard.manualSyncFailed') });
    } finally {
      setTriggeringDailySync(false);
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
        return t('dashboard.synced');
      case 'DISCREPANCY':
        return t('dashboard.discrepancyDetected');
      case 'ERROR':
        return t('dashboard.error');
      case 'NO_DATA':
        return t('dashboard.noData');
      default:
        return t('dashboard.waiting');
    }
  };

  if (loading) {
    return (
      <div className="flex items-center justify-center h-64">
        <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
        <span className="ml-2 text-gray-600 dark:text-gray-400">{t('common:button.loading')}</span>
      </div>
    );
  }

  if (error && !status) {
    return (
      <div className="p-6">
        <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl p-6 text-center">
          <XCircle className="w-12 h-12 text-red-500 mx-auto mb-4" />
          <h3 className="text-lg font-semibold text-red-700 dark:text-red-400 mb-2">
            {t('dashboard.connectionFailed')}
          </h3>
          <p className="text-red-600 dark:text-red-300 mb-4">{error}</p>
          <button
            onClick={fetchData}
            className="px-4 py-2 bg-red-100 dark:bg-red-800 text-red-700 dark:text-red-200 rounded-lg hover:bg-red-200 dark:hover:bg-red-700 transition-colors"
          >
            {t('common:button.retry')}
          </button>
        </div>
      </div>
    );
  }

  return (
    <div className="w-full px-4 lg:px-6 py-4 space-y-6">
      {/* Header */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
        <div>
          <h1 className="text-2xl font-bold text-gray-900 dark:text-white flex items-center gap-3">
            <ArrowRightLeft className="w-7 h-7 text-blue-500" />
            {t('dashboard.title')}
          </h1>
          <p className="text-gray-600 dark:text-gray-400 mt-1">
            {t('dashboard.subtitle')}
          </p>
        </div>
        <div className="flex flex-wrap items-center gap-2 sm:gap-3">
          <button
            onClick={handleTriggerDailySync}
            disabled={triggeringDailySync}
            className={cn(
              'flex items-center gap-2 px-4 py-2 rounded-lg font-medium transition-colors',
              triggeringDailySync
                ? 'bg-gray-300 dark:bg-gray-600 cursor-not-allowed'
                : 'bg-green-500 hover:bg-green-600 text-white'
            )}
          >
            {triggeringDailySync ? (
              <Loader2 className="w-4 h-4 animate-spin" />
            ) : (
              <CalendarClock className="w-4 h-4" />
            )}
            {triggeringDailySync ? t('dashboard.running') : t('dashboard.manualSync')}
          </button>
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
            {revalidating ? t('dashboard.revalidatingText') : t('dashboard.certRevalidation')}
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
            {checking ? t('dashboard.checkingText') : t('dashboard.manualCheck')}
          </button>
        </div>
      </div>

      {/* Config Card */}
      {config && (
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
          <div className="flex items-center justify-between mb-4">
            <div className="flex items-center gap-2">
              <Settings className="w-5 h-5 text-gray-500" />
              <h3 className="text-sm font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                {t('dashboard.serviceConfig')}
              </h3>
            </div>
            <button
              onClick={handleOpenConfigDialog}
              className="flex items-center gap-2 px-3 py-1.5 text-sm bg-blue-500 hover:bg-blue-600 text-white rounded-lg transition-colors"
            >
              <Edit className="w-4 h-4" />
              {t('dashboard.editConfig')}
            </button>
          </div>
          <div className="grid grid-cols-2 md:grid-cols-3 gap-4">
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3">
              <div className="flex items-center gap-2 mb-1">
                <CalendarClock className="w-4 h-4 text-purple-500" />
                <span className="text-xs text-gray-500 dark:text-gray-400">{t('dashboard.dailySyncTime')}</span>
              </div>
              <p className="text-lg font-semibold text-gray-900 dark:text-white">
                {config.dailySyncEnabled ? t('dashboard.daily', { time: config.dailySyncTime }) : t('dashboard.disabled')}
              </p>
            </div>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3">
              <div className="flex items-center gap-2 mb-1">
                <ShieldCheck className="w-4 h-4 text-green-500" />
                <span className="text-xs text-gray-500 dark:text-gray-400">{t('dashboard.autoRevalidation')}</span>
              </div>
              <p className="text-lg font-semibold text-gray-900 dark:text-white">
                {config.revalidateCertsOnSync ? t('dashboard.enabled') : t('dashboard.disabled')}
              </p>
            </div>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3">
              <div className="flex items-center gap-2 mb-1">
                <RotateCcw className="w-4 h-4 text-orange-500" />
                <span className="text-xs text-gray-500 dark:text-gray-400">{t('dashboard.autoReconcile')}</span>
              </div>
              <p className="text-lg font-semibold text-gray-900 dark:text-white">
                {config.autoReconcile ? t('dashboard.enabled') : t('dashboard.disabled')}
              </p>
            </div>
          </div>
        </div>
      )}

      {/* Status Overview */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
        {/* Current Status Card */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6 h-full">
          <div className="flex items-center justify-between mb-4">
            <h3 className="text-sm font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
              {t('dashboard.currentStatus')}
            </h3>
            {status && getStatusIcon(status.status || 'PENDING')}
          </div>
          <div className="space-y-3">
            <div
              className={cn(
                'inline-flex items-center px-3 py-1 rounded-full text-sm font-medium',
                status ? getStatusColor(status.status || 'PENDING') : 'bg-gray-100 text-gray-600'
              )}
            >
              {status ? getStatusLabel(status.status || 'PENDING') : t('common:status.unknown')}
            </div>

            {/* Discrepancy Details in Current Status Card */}
            {status?.status === 'DISCREPANCY' && status.discrepancies && status.discrepancies.total > 0 && (
              <div className="mt-3 pt-3 border-t border-yellow-200 dark:border-yellow-800/50">
                <div className="text-xs font-medium text-yellow-700 dark:text-yellow-400 mb-2">
                  {t('dashboard.discrepancyDetail')}:
                </div>
                <div className="grid grid-cols-2 gap-1.5 text-xs">
                  {status.discrepancies.csca !== 0 && (
                    <div className="flex justify-between">
                      <span className="text-gray-500 dark:text-gray-400">CSCA:</span>
                      <span className={cn(
                        'font-semibold',
                        status.discrepancies.csca > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400'
                      )}>
                        {status.discrepancies.csca > 0 ? '+' : ''}{status.discrepancies.csca}
                      </span>
                    </div>
                  )}
                  {status.discrepancies.mlsc !== 0 && (
                    <div className="flex justify-between">
                      <span className="text-gray-500 dark:text-gray-400">MLSC:</span>
                      <span className={cn(
                        'font-semibold',
                        status.discrepancies.mlsc > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400'
                      )}>
                        {status.discrepancies.mlsc > 0 ? '+' : ''}{status.discrepancies.mlsc}
                      </span>
                    </div>
                  )}
                  {status.discrepancies.dsc !== 0 && (
                    <div className="flex justify-between">
                      <span className="text-gray-500 dark:text-gray-400">DSC:</span>
                      <span className={cn(
                        'font-semibold',
                        status.discrepancies.dsc > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400'
                      )}>
                        {status.discrepancies.dsc > 0 ? '+' : ''}{status.discrepancies.dsc}
                      </span>
                    </div>
                  )}
                  {status.discrepancies.dscNc !== 0 && (
                    <div className="flex justify-between">
                      <span className="text-gray-500 dark:text-gray-400">DSC_NC:</span>
                      <span className={cn(
                        'font-semibold',
                        status.discrepancies.dscNc > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400'
                      )}>
                        {status.discrepancies.dscNc > 0 ? '+' : ''}{status.discrepancies.dscNc}
                      </span>
                    </div>
                  )}
                  {status.discrepancies.crl !== 0 && (
                    <div className="flex justify-between">
                      <span className="text-gray-500 dark:text-gray-400">CRL:</span>
                      <span className={cn(
                        'font-semibold',
                        status.discrepancies.crl > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400'
                      )}>
                        {status.discrepancies.crl > 0 ? '+' : ''}{status.discrepancies.crl}
                      </span>
                    </div>
                  )}
                </div>
                <div className="mt-2 text-[10px] text-gray-500 dark:text-gray-400">
                  {t('dashboard.dbOnlyLdapOnly')}
                </div>
              </div>
            )}

            <div className="text-xs text-gray-500 dark:text-gray-400">
              {t('dashboard.lastCheck')}: {formatDateTime(status?.checkedAt)}
            </div>
            {status?.checkDurationMs && (
              <div className="text-xs text-gray-500 dark:text-gray-400">
                {t('dashboard.checkDuration')}: {status.checkDurationMs}ms
              </div>
            )}
          </div>
        </div>

        {/* DB vs LDAP Comparison Table */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6 md:col-span-2 h-full">
          <div className="flex items-center gap-2 mb-4">
            <ArrowRightLeft className="w-5 h-5 text-purple-500" />
            <h3 className="text-sm font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
              {t('dashboard.dbLdapComparison')}
            </h3>
          </div>
          {status?.dbCounts && status?.ldapCounts ? (
            <div className="overflow-x-auto">
              <table className="w-full text-xs">
                <thead className="bg-slate-100 dark:bg-gray-700">
                  <tr>
                    <th className="text-left py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap">
                      {t('dashboard.certType')}
                    </th>
                    <th className="text-right py-2.5 px-3 font-semibold text-blue-600 dark:text-blue-400 whitespace-nowrap">
                      <div className="flex items-center justify-end gap-1">
                        <Database className="w-3.5 h-3.5" />
                        DB
                      </div>
                    </th>
                    <th className="text-right py-2.5 px-3 font-semibold text-green-600 dark:text-green-400 whitespace-nowrap">
                      <div className="flex items-center justify-end gap-1">
                        <Server className="w-3.5 h-3.5" />
                        LDAP
                      </div>
                    </th>
                    <th className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap">
                      {t('dashboard.difference')}
                    </th>
                  </tr>
                </thead>
                <tbody>
                  <tr className="border-b border-gray-100 dark:border-gray-700/50">
                    <td className="py-2 px-3 text-gray-700 dark:text-gray-300">CSCA</td>
                    <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                      {status.dbCounts.csca?.toLocaleString()}
                    </td>
                    <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                      {status.ldapCounts.csca?.toLocaleString()}
                    </td>
                    <td className="py-2 px-3 text-right">
                      {status.discrepancies && status.discrepancies.csca !== 0 ? (
                        <span className={cn(
                          'font-mono font-semibold',
                          status.discrepancies.csca > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400'
                        )}>
                          {status.discrepancies.csca > 0 ? '+' : ''}{status.discrepancies.csca}
                        </span>
                      ) : (
                        <span className="text-green-600 dark:text-green-400">✓</span>
                      )}
                    </td>
                  </tr>
                  <tr className="border-b border-gray-100 dark:border-gray-700/50">
                    <td className="py-2 px-3 text-gray-700 dark:text-gray-300">MLSC</td>
                    <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                      {status.dbCounts.mlsc?.toLocaleString()}
                    </td>
                    <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                      {status.ldapCounts.mlsc?.toLocaleString()}
                    </td>
                    <td className="py-2 px-3 text-right">
                      {status.discrepancies && status.discrepancies.mlsc !== 0 ? (
                        <span className={cn(
                          'font-mono font-semibold',
                          status.discrepancies.mlsc > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400'
                        )}>
                          {status.discrepancies.mlsc > 0 ? '+' : ''}{status.discrepancies.mlsc}
                        </span>
                      ) : (
                        <span className="text-green-600 dark:text-green-400">✓</span>
                      )}
                    </td>
                  </tr>
                  <tr className="border-b border-gray-100 dark:border-gray-700/50">
                    <td className="py-2 px-3 text-gray-700 dark:text-gray-300">DSC</td>
                    <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                      {status.dbCounts.dsc?.toLocaleString()}
                    </td>
                    <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                      {status.ldapCounts.dsc?.toLocaleString()}
                    </td>
                    <td className="py-2 px-3 text-right">
                      {status.discrepancies && status.discrepancies.dsc !== 0 ? (
                        <span className={cn(
                          'font-mono font-semibold',
                          status.discrepancies.dsc > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400'
                        )}>
                          {status.discrepancies.dsc > 0 ? '+' : ''}{status.discrepancies.dsc}
                        </span>
                      ) : (
                        <span className="text-green-600 dark:text-green-400">✓</span>
                      )}
                    </td>
                  </tr>
                  <tr className="border-b border-gray-100 dark:border-gray-700/50">
                    <td className="py-2 px-3 text-gray-700 dark:text-gray-300">DSC_NC</td>
                    <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                      {status.dbCounts.dscNc?.toLocaleString()}
                    </td>
                    <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                      {status.ldapCounts.dscNc?.toLocaleString()}
                    </td>
                    <td className="py-2 px-3 text-right">
                      {status.discrepancies && status.discrepancies.dscNc !== 0 ? (
                        <span className={cn(
                          'font-mono font-semibold',
                          status.discrepancies.dscNc > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400'
                        )}>
                          {status.discrepancies.dscNc > 0 ? '+' : ''}{status.discrepancies.dscNc}
                        </span>
                      ) : (
                        <span className="text-green-600 dark:text-green-400">✓</span>
                      )}
                    </td>
                  </tr>
                  <tr>
                    <td className="py-2 px-3 text-gray-700 dark:text-gray-300">CRL</td>
                    <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                      {status.dbCounts.crl?.toLocaleString()}
                    </td>
                    <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                      {status.ldapCounts.crl?.toLocaleString()}
                    </td>
                    <td className="py-2 px-3 text-right">
                      {status.discrepancies && status.discrepancies.crl !== 0 ? (
                        <span className={cn(
                          'font-mono font-semibold',
                          status.discrepancies.crl > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400'
                        )}>
                          {status.discrepancies.crl > 0 ? '+' : ''}{status.discrepancies.crl}
                        </span>
                      ) : (
                        <span className="text-green-600 dark:text-green-400">✓</span>
                      )}
                    </td>
                  </tr>
                </tbody>
              </table>
              <div className="mt-3 pt-3 border-t border-gray-200 dark:border-gray-700">
                <div className="text-xs text-gray-500 dark:text-gray-400 flex items-center justify-between">
                  <span>{t('dashboard.dbMoreLdapMore')}</span>
                  <span className="font-medium">✓ {t('dashboard.consistent')}</span>
                </div>
              </div>
            </div>
          ) : (
            <div className="text-gray-400 text-center py-4">{t('dashboard.noData')}</div>
          )}
        </div>
      </div>

      {/* Discrepancy Details */}
      {status?.discrepancies && status.discrepancies.total > 0 && (
        <div className="bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800 rounded-xl p-6">
          <div className="flex items-center gap-2 mb-4">
            <AlertTriangle className="w-5 h-5 text-yellow-500" />
            <h3 className="text-lg font-semibold text-yellow-800 dark:text-yellow-300">
              {t('dashboard.discrepancyDetail')}
            </h3>
          </div>
          <div className="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-6 gap-4">
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-2xl font-bold text-yellow-600 dark:text-yellow-400">
                {status.discrepancies.total}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400">{t('dashboard.totalDiscrepancy')}</div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancies.csca}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400">CSCA</div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancies.mlsc}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400">MLSC</div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancies.dsc}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400">DSC</div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancies.dscNc}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400">DSC_NC</div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancies.crl}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400">CRL</div>
            </div>
          </div>
          <p className="mt-4 text-sm text-yellow-700 dark:text-yellow-300">
            {t('dashboard.positiveNegativeExplanation')}
          </p>
        </div>
      )}

      {/* Sync History */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
        <div className="flex items-center gap-2 mb-4">
          <History className="w-5 h-5 text-purple-500" />
          <h3 className="text-lg font-semibold text-gray-900 dark:text-white">
            {t('dashboard.syncHistory')}
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
            <table className="w-full text-xs">
              <thead className="bg-slate-100 dark:bg-gray-700">
                <tr>
                  <SortableHeader label={t('dashboard.checkTime')} sortKey="checkedAt" sortConfig={historySortConfig} onSort={requestHistorySort}
                    className="text-left py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                  <SortableHeader label={t('common:label.status')} sortKey="status" sortConfig={historySortConfig} onSort={requestHistorySort}
                    className="text-center py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                  <SortableHeader label={t('dashboard.dbTotal')} sortKey="dbTotal" sortConfig={historySortConfig} onSort={requestHistorySort}
                    className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                  <SortableHeader label={t('dashboard.ldapTotal')} sortKey="ldapTotal" sortConfig={historySortConfig} onSort={requestHistorySort}
                    className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                  <SortableHeader label={t('dashboard.discrepancy')} sortKey="totalDiscrepancy" sortConfig={historySortConfig} onSort={requestHistorySort}
                    className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                  <SortableHeader label={t('dashboard.duration')} sortKey="checkDurationMs" sortConfig={historySortConfig} onSort={requestHistorySort}
                    className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                </tr>
              </thead>
              <tbody>
                {sortedHistory.map((item) => (
                  <tr
                    key={item.id}
                    className="border-b border-gray-100 dark:border-gray-700/50 hover:bg-gray-50 dark:hover:bg-gray-700/30"
                  >
                    <td className="py-2.5 px-3 text-gray-900 dark:text-white">
                      {formatDateTime(item.checkedAt)}
                    </td>
                    <td className="py-2.5 px-3 text-center">
                      <span
                        className={cn(
                          'inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium',
                          getStatusColor(item.status)
                        )}
                      >
                        {getStatusLabel(item.status)}
                      </span>
                    </td>
                    <td className="py-2.5 px-3 text-right font-mono text-gray-700 dark:text-gray-300">
                      {item.dbTotal?.toLocaleString()}
                    </td>
                    <td className="py-2.5 px-3 text-right font-mono text-gray-700 dark:text-gray-300">
                      {item.ldapTotal?.toLocaleString()}
                    </td>
                    <td className="py-2.5 px-3 text-right">
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
                    <td className="py-2.5 px-3 text-right text-gray-500 dark:text-gray-400">
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
            <p>{t('dashboard.noSyncHistory')}</p>
            <p className="text-sm mt-1">{t('dashboard.waitForCheck')}</p>
          </div>
        )}
      </div>

      {/* Revalidation History */}
      {revalidationHistory.length > 0 && (
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
          <div className="flex items-center gap-2 mb-4">
            <ShieldCheck className="w-5 h-5 text-green-500" />
            <h3 className="text-lg font-semibold text-gray-900 dark:text-white">
              {t('dashboard.revalidationHistory')}
            </h3>
          </div>

          <div className="overflow-x-auto">
            <table className="w-full text-xs">
              <thead className="bg-slate-100 dark:bg-gray-700">
                <tr>
                  <SortableHeader label={t('dashboard.executionTime')} sortKey="executedAt" sortConfig={revalSortConfig} onSort={requestRevalSort}
                    className="text-left py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                  <SortableHeader label={t('dashboard.processedCerts')} sortKey="totalProcessed" sortConfig={revalSortConfig} onSort={requestRevalSort}
                    className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                  <SortableHeader label={t('sync:dashboard.newlyExpired')} sortKey="newlyExpired" sortConfig={revalSortConfig} onSort={requestRevalSort}
                    className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                  <SortableHeader label={t('sync:dashboard.newlyValid')} sortKey="newlyValid" sortConfig={revalSortConfig} onSort={requestRevalSort}
                    className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                  <SortableHeader label={t('sync:dashboard.noChange')} sortKey="unchanged" sortConfig={revalSortConfig} onSort={requestRevalSort}
                    className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                  <SortableHeader label={t('sync:dashboard.error')} sortKey="errors" sortConfig={revalSortConfig} onSort={requestRevalSort}
                    className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                  <SortableHeader label={t('sync:dashboard.tcTarget')} sortKey="tcProcessed" sortConfig={revalSortConfig} onSort={requestRevalSort}
                    className="text-right py-2.5 px-3 font-semibold text-blue-700 dark:text-blue-300 whitespace-nowrap" />
                  <SortableHeader label="TC VALID" sortKey="tcNewlyValid" sortConfig={revalSortConfig} onSort={requestRevalSort}
                    className="text-right py-2.5 px-3 font-semibold text-blue-700 dark:text-blue-300 whitespace-nowrap" />
                  <SortableHeader label={t('sync.dashboard.crlCheck')} sortKey="crlChecked" sortConfig={revalSortConfig} onSort={requestRevalSort}
                    className="text-right py-2.5 px-3 font-semibold text-purple-700 dark:text-purple-300 whitespace-nowrap" />
                  <SortableHeader label={t('sync.dashboard.crlRevoked')} sortKey="crlRevoked" sortConfig={revalSortConfig} onSort={requestRevalSort}
                    className="text-right py-2.5 px-3 font-semibold text-purple-700 dark:text-purple-300 whitespace-nowrap" />
                  <SortableHeader label={t('sync:reconciliation.duration')} sortKey="durationMs" sortConfig={revalSortConfig} onSort={requestRevalSort}
                    className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
                </tr>
              </thead>
              <tbody>
                {sortedRevalidation.map((item) => (
                  <tr
                    key={item.id}
                    className="border-b border-gray-100 dark:border-gray-700/50 hover:bg-gray-50 dark:hover:bg-gray-700/30"
                  >
                    <td className="py-2.5 px-3 text-gray-900 dark:text-white">
                      {formatDateTime(item.executedAt)}
                    </td>
                    <td className="py-2.5 px-3 text-right font-mono text-gray-700 dark:text-gray-300">
                      {item.totalProcessed.toLocaleString()}
                    </td>
                    <td className="py-2.5 px-3 text-right">
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
                    <td className="py-2.5 px-3 text-right">
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
                    <td className="py-2.5 px-3 text-right font-mono text-gray-500 dark:text-gray-400">
                      {item.unchanged.toLocaleString()}
                    </td>
                    <td className="py-2.5 px-3 text-right">
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
                    <td className="py-2.5 px-3 text-right font-mono text-blue-600 dark:text-blue-400">
                      {(item.tcProcessed ?? 0).toLocaleString()}
                    </td>
                    <td className="py-2.5 px-3 text-right">
                      <span className={cn('font-mono font-semibold', (item.tcNewlyValid ?? 0) > 0 ? 'text-green-600 dark:text-green-400' : 'text-gray-500 dark:text-gray-400')}>
                        {(item.tcNewlyValid ?? 0).toLocaleString()}
                      </span>
                    </td>
                    <td className="py-2.5 px-3 text-right font-mono text-purple-600 dark:text-purple-400">
                      {(item.crlChecked ?? 0).toLocaleString()}
                    </td>
                    <td className="py-2.5 px-3 text-right">
                      <span className={cn('font-mono font-semibold', (item.crlRevoked ?? 0) > 0 ? 'text-red-600 dark:text-red-400' : 'text-gray-500 dark:text-gray-400')}>
                        {item.crlRevoked ?? 0}
                      </span>
                    </td>
                    <td className="py-2.5 px-3 text-right text-gray-500 dark:text-gray-400">
                      {item.durationMs}ms
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}

      {/* Reconciliation History */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
        <ReconciliationHistory />
      </div>

      {/* Info */}
      <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-xl p-4">
        <div className="flex items-start gap-3">
          <Activity className="w-5 h-5 text-blue-500 mt-0.5" />
          <div className="text-sm text-blue-700 dark:text-blue-300">
            <p className="font-medium mb-1">{t('sync:dashboard.dailySyncTitle')}</p>
            {config?.dailySyncEnabled ? (
              <div>
                <p className="mb-1">
                  {t('sync:dashboard.dailySyncDesc', { time: config.dailySyncTime })}
                </p>
                <ul className="list-disc list-inside space-y-0.5 ml-1">
                  <li>{t('sync:dashboard.dailySyncStep1')}</li>
                  {config.revalidateCertsOnSync && (
                    <li>{t('sync:dashboard.dailySyncStep2')}</li>
                  )}
                </ul>
              </div>
            ) : (
              <p>
                {t('sync:dashboard.dailySyncDisabledMsg')}
              </p>
            )}
          </div>
        </div>
      </div>

      {/* Config Edit Dialog */}
      {/* Revalidation Result Dialog */}
      {revalidationResult && (
        <Dialog
          isOpen={!!revalidationResult}
          onClose={() => setRevalidationResult(null)}
          title={t('sync:dashboard.revalidationComplete')}
          size="lg"
        >
          <div className="space-y-4">
            {/* Status Banner */}
            <div className={cn(
              'flex items-center gap-3 p-3 rounded-lg',
              revalidationResult.success
                ? 'bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800'
                : 'bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
            )}>
              {revalidationResult.success ? (
                <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400 flex-shrink-0" />
              ) : (
                <XCircle className="w-5 h-5 text-red-600 dark:text-red-400 flex-shrink-0" />
              )}
              <span className={cn(
                'text-sm font-medium',
                revalidationResult.success
                  ? 'text-green-800 dark:text-green-300'
                  : 'text-red-800 dark:text-red-300'
              )}>
                {revalidationResult.success
                  ? t('sync:dashboard.certsValidated', { count: revalidationResult.totalProcessed })
                  : t('sync:dashboard.revalidationError')}
              </span>
              <span className="ml-auto text-xs text-gray-500 dark:text-gray-400">
                {revalidationResult.durationMs < 1000
                  ? `${revalidationResult.durationMs}ms`
                  : `${(revalidationResult.durationMs / 1000).toFixed(1)}${t('common:time.seconds')}`}
              </span>
            </div>

            {/* Step 1: Expiration Check */}
            <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
              <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">
                {t('sync:dashboard.step1ExpiryCheck')}
              </h4>
              <div className="grid grid-cols-3 gap-2 text-center">
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{ t('common:label.processing') }</p>
                  <p className="text-base font-bold text-gray-900 dark:text-white">{revalidationResult.totalProcessed.toLocaleString()}</p>
                </div>
                <div className={cn('rounded-lg p-2', revalidationResult.newlyExpired > 0 ? 'bg-orange-50 dark:bg-orange-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{t('sync:dashboard.newlyExpired')}</p>
                  <p className={cn('text-base font-bold', revalidationResult.newlyExpired > 0 ? 'text-orange-600 dark:text-orange-400' : 'text-gray-900 dark:text-white')}>{revalidationResult.newlyExpired}</p>
                </div>
                <div className={cn('rounded-lg p-2', revalidationResult.newlyValid > 0 ? 'bg-green-50 dark:bg-green-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{t('sync:dashboard.newlyValid')}</p>
                  <p className={cn('text-base font-bold', revalidationResult.newlyValid > 0 ? 'text-green-600 dark:text-green-400' : 'text-gray-900 dark:text-white')}>{revalidationResult.newlyValid}</p>
                </div>
              </div>
              <div className="grid grid-cols-2 gap-2 mt-2 text-center">
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{t('sync:dashboard.noChange')}</p>
                  <p className="text-base font-bold text-gray-900 dark:text-white">{revalidationResult.unchanged.toLocaleString()}</p>
                </div>
                <div className={cn('rounded-lg p-2', revalidationResult.errors > 0 ? 'bg-red-50 dark:bg-red-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{ t('sync:dashboard.error') }</p>
                  <p className={cn('text-base font-bold', revalidationResult.errors > 0 ? 'text-red-600 dark:text-red-400' : 'text-gray-900 dark:text-white')}>{revalidationResult.errors}</p>
                </div>
              </div>
            </div>

            {/* Step 2: Trust Chain Re-validation */}
            <div className="border border-blue-200 dark:border-blue-800 rounded-lg p-4">
              <h4 className="text-sm font-semibold text-blue-700 dark:text-blue-300 mb-3">
                {t('sync:dashboard.step2TrustChain')}
              </h4>
              <div className="grid grid-cols-4 gap-2 text-center">
                <div className="bg-blue-50 dark:bg-blue-900/20 rounded-lg p-2">
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{t('sync:dashboard.target')}</p>
                  <p className="text-base font-bold text-blue-700 dark:text-blue-300">{(revalidationResult.tcProcessed ?? 0).toLocaleString()}</p>
                </div>
                <div className={cn('rounded-lg p-2', (revalidationResult.tcNewlyValid ?? 0) > 0 ? 'bg-green-50 dark:bg-green-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{t('sync:dashboard.validTransition')}</p>
                  <p className={cn('text-base font-bold', (revalidationResult.tcNewlyValid ?? 0) > 0 ? 'text-green-600 dark:text-green-400' : 'text-gray-900 dark:text-white')}>{(revalidationResult.tcNewlyValid ?? 0).toLocaleString()}</p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{t('sync:dashboard.stillPending')}</p>
                  <p className="text-base font-bold text-gray-900 dark:text-white">{(revalidationResult.tcStillPending ?? 0).toLocaleString()}</p>
                </div>
                <div className={cn('rounded-lg p-2', (revalidationResult.tcErrors ?? 0) > 0 ? 'bg-red-50 dark:bg-red-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{ t('sync:dashboard.error') }</p>
                  <p className={cn('text-base font-bold', (revalidationResult.tcErrors ?? 0) > 0 ? 'text-red-600 dark:text-red-400' : 'text-gray-900 dark:text-white')}>{revalidationResult.tcErrors ?? 0}</p>
                </div>
              </div>
            </div>

            {/* Step 3: CRL Re-check */}
            <div className="border border-purple-200 dark:border-purple-800 rounded-lg p-4">
              <h4 className="text-sm font-semibold text-purple-700 dark:text-purple-300 mb-3">
                {t('sync:dashboard.step3CrlCheck')}
              </h4>
              <div className="grid grid-cols-5 gap-2 text-center">
                <div className="bg-purple-50 dark:bg-purple-900/20 rounded-lg p-2">
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{t('sync:dashboard.checked')}</p>
                  <p className="text-base font-bold text-purple-700 dark:text-purple-300">{(revalidationResult.crlChecked ?? 0).toLocaleString()}</p>
                </div>
                <div className={cn('rounded-lg p-2', (revalidationResult.crlRevoked ?? 0) > 0 ? 'bg-red-50 dark:bg-red-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{t('sync:dashboard.revoked')}</p>
                  <p className={cn('text-base font-bold', (revalidationResult.crlRevoked ?? 0) > 0 ? 'text-red-600 dark:text-red-400' : 'text-gray-900 dark:text-white')}>{revalidationResult.crlRevoked ?? 0}</p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{t('sync:dashboard.crlUnavailable')}</p>
                  <p className="text-base font-bold text-gray-900 dark:text-white">{(revalidationResult.crlUnavailable ?? 0).toLocaleString()}</p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{t('sync:dashboard.crlExpired')}</p>
                  <p className="text-base font-bold text-gray-900 dark:text-white">{revalidationResult.crlExpired ?? 0}</p>
                </div>
                <div className={cn('rounded-lg p-2', (revalidationResult.crlErrors ?? 0) > 0 ? 'bg-red-50 dark:bg-red-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
                  <p className="text-[10px] text-gray-500 dark:text-gray-400">{ t('sync:dashboard.error') }</p>
                  <p className={cn('text-base font-bold', (revalidationResult.crlErrors ?? 0) > 0 ? 'text-red-600 dark:text-red-400' : 'text-gray-900 dark:text-white')}>{revalidationResult.crlErrors ?? 0}</p>
                </div>
              </div>
            </div>

            {/* Close Button */}
            <div className="flex justify-end pt-2">
              <button
                onClick={() => setRevalidationResult(null)}
                className="px-4 py-2 text-sm font-medium text-white bg-blue-600 rounded-lg hover:bg-blue-700 transition-colors"
              >
                {t('common.confirm.title')}
              </button>
            </div>
          </div>
        </Dialog>
      )}

      {/* Sync Check Result Dialog */}
      {syncCheckResult && (
        <Dialog
          isOpen={!!syncCheckResult}
          onClose={() => setSyncCheckResult(null)}
          title={t('sync:dashboard.syncCheckComplete')}
          size="lg"
        >
          <div className="space-y-4">
            {/* Status Banner */}
            <div className={cn(
              'flex items-center gap-3 p-3 rounded-lg',
              syncCheckResult.status === 'SYNCED'
                ? 'bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800'
                : syncCheckResult.status === 'DISCREPANCY'
                  ? 'bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800'
                  : 'bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
            )}>
              {syncCheckResult.status === 'SYNCED' ? (
                <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400 flex-shrink-0" />
              ) : syncCheckResult.status === 'DISCREPANCY' ? (
                <AlertTriangle className="w-5 h-5 text-yellow-600 dark:text-yellow-400 flex-shrink-0" />
              ) : (
                <XCircle className="w-5 h-5 text-red-600 dark:text-red-400 flex-shrink-0" />
              )}
              <span className={cn(
                'text-sm font-medium',
                syncCheckResult.status === 'SYNCED'
                  ? 'text-green-800 dark:text-green-300'
                  : syncCheckResult.status === 'DISCREPANCY'
                    ? 'text-yellow-800 dark:text-yellow-300'
                    : 'text-red-800 dark:text-red-300'
              )}>
                {syncCheckResult.status === 'SYNCED'
                  ? t('sync.dashboard.dbLdapSynced')
                  : syncCheckResult.status === 'DISCREPANCY'
                    ? t('sync:dashboard.discrepanciesDetected', { count: syncCheckResult.discrepancies?.total ?? 0 })
                    : t('sync:dashboard.syncCheckError')}
              </span>
              {syncCheckResult.checkDurationMs && (
                <span className="ml-auto text-xs text-gray-500 dark:text-gray-400">
                  {syncCheckResult.checkDurationMs < 1000
                    ? `${syncCheckResult.checkDurationMs}ms`
                    : `${(syncCheckResult.checkDurationMs / 1000).toFixed(1)}${t('common:time.seconds')}`}
                </span>
              )}
            </div>

            {/* DB vs LDAP Counts */}
            {syncCheckResult.dbCounts && syncCheckResult.ldapCounts && (
              <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
                <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 flex items-center gap-2">
                  <ArrowRightLeft className="w-4 h-4 text-purple-500" />
                  {t('sync:dashboard.dbLdapCertComparison')}
                </h4>
                <div className="overflow-x-auto">
                  <table className="w-full text-xs">
                    <thead className="bg-slate-100 dark:bg-gray-700">
                      <tr>
                        <th className="text-left py-2 px-3 font-semibold text-slate-700 dark:text-gray-200">{ t('ai:dashboard.filterType') }</th>
                        <th className="text-right py-2 px-3 font-semibold text-blue-600 dark:text-blue-400">DB</th>
                        <th className="text-right py-2 px-3 font-semibold text-green-600 dark:text-green-400">LDAP</th>
                        <th className="text-right py-2 px-3 font-semibold text-slate-700 dark:text-gray-200">{ t('sync:dashboard.difference') }</th>
                      </tr>
                    </thead>
                    <tbody>
                      {([
                        { key: 'csca' as const, label: 'CSCA' },
                        { key: 'mlsc' as const, label: 'MLSC' },
                        { key: 'dsc' as const, label: 'DSC' },
                        { key: 'dscNc' as const, label: 'DSC_NC' },
                        { key: 'crl' as const, label: 'CRL' },
                      ]).map(({ key, label }) => {
                        const dbCount = syncCheckResult.dbCounts?.[key] ?? 0;
                        const ldapCount = syncCheckResult.ldapCounts?.[key] ?? 0;
                        const diff = syncCheckResult.discrepancies?.[key] ?? 0;
                        return (
                          <tr key={key} className="border-b border-gray-100 dark:border-gray-700/50">
                            <td className="py-2 px-3 text-gray-700 dark:text-gray-300 font-medium">{label}</td>
                            <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                              {dbCount.toLocaleString()}
                            </td>
                            <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                              {ldapCount.toLocaleString()}
                            </td>
                            <td className="py-2 px-3 text-right">
                              {diff !== 0 ? (
                                <span className={cn('font-mono font-semibold', diff > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400')}>
                                  {diff > 0 ? '+' : ''}{diff}
                                </span>
                              ) : (
                                <span className="text-green-600 dark:text-green-400 font-semibold">✓</span>
                              )}
                            </td>
                          </tr>
                        );
                      })}
                    </tbody>
                  </table>
                </div>
              </div>
            )}

            {/* Close Button */}
            <div className="flex justify-end pt-2">
              <button
                onClick={() => setSyncCheckResult(null)}
                className="px-4 py-2 text-sm font-medium text-white bg-blue-600 rounded-lg hover:bg-blue-700 transition-colors"
              >
                {t('common.confirm.title')}
              </button>
            </div>
          </div>
        </Dialog>
      )}

      {/* Daily Sync Trigger Result Dialog */}
      {dailySyncResult && (
        <Dialog
          isOpen={!!dailySyncResult}
          onClose={() => setDailySyncResult(null)}
          title={t('sync:dashboard.manualSync')}
          size="md"
        >
          <div className="space-y-4">
            <div className={cn(
              'flex items-center gap-3 p-3 rounded-lg',
              dailySyncResult.success
                ? 'bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800'
                : 'bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
            )}>
              {dailySyncResult.success ? (
                <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400 flex-shrink-0" />
              ) : (
                <XCircle className="w-5 h-5 text-red-600 dark:text-red-400 flex-shrink-0" />
              )}
              <span className={cn(
                'text-sm font-medium',
                dailySyncResult.success
                  ? 'text-green-800 dark:text-green-300'
                  : 'text-red-800 dark:text-red-300'
              )}>
                {dailySyncResult.success
                  ? t('sync:dashboard.dailySyncTriggered')
                  : dailySyncResult.message}
              </span>
            </div>

            {dailySyncResult.success && (
              <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
                <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 flex items-center gap-2">
                  <Activity className="w-4 h-4 text-blue-500" />
                  {t('sync:dashboard.executedTasks')}
                </h4>
                <div className="space-y-2">
                  {[
                    { icon: <Play className="w-3.5 h-3.5 text-blue-500" />, label: t('sync:dashboard.taskSyncCheck') },
                    { icon: <ShieldCheck className="w-3.5 h-3.5 text-purple-500" />, label: t('sync:dashboard.taskRevalidation'), note: config?.revalidateCertsOnSync ? t('dashboard.enabled') : t('dashboard.disabled') },
                    { icon: <RotateCcw className="w-3.5 h-3.5 text-orange-500" />, label: t('sync:dashboard.taskReconciliation'), note: config?.autoReconcile ? t('dashboard.enabled') : t('dashboard.disabled') },
                  ].map((step, i) => (
                    <div key={i} className="flex items-center gap-3 p-2 bg-gray-50 dark:bg-gray-700/50 rounded-lg">
                      <div className="flex items-center justify-center w-6 h-6 bg-white dark:bg-gray-600 rounded-full text-xs font-bold text-gray-600 dark:text-gray-300 shadow-sm">
                        {i + 1}
                      </div>
                      {step.icon}
                      <span className="text-sm text-gray-700 dark:text-gray-300 flex-1">{step.label}</span>
                      {step.note && (
                        <span className={cn(
                          'text-[10px] px-1.5 py-0.5 rounded font-medium',
                          step.note === t('dashboard.enabled')
                            ? 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400'
                            : 'bg-gray-200 dark:bg-gray-600 text-gray-500 dark:text-gray-400'
                        )}>
                          {step.note}
                        </span>
                      )}
                    </div>
                  ))}
                </div>
                <p className="mt-3 text-xs text-gray-500 dark:text-gray-400">
                  {t('sync:dashboard.backgroundRunning')}
                </p>
              </div>
            )}

            <div className="flex justify-end pt-2">
              <button
                onClick={() => setDailySyncResult(null)}
                className="px-4 py-2 text-sm font-medium text-white bg-blue-600 rounded-lg hover:bg-blue-700 transition-colors"
              >
                {t('common.confirm.title')}
              </button>
            </div>
          </div>
        </Dialog>
      )}

      {/* Config Save Result Dialog */}
      {configSaveResult && (
        <Dialog
          isOpen={!!configSaveResult}
          onClose={() => setConfigSaveResult(null)}
          title={t('sync:dashboard.configSaved')}
          size="md"
        >
          <div className="space-y-4">
            <div className="flex items-center gap-3 p-3 rounded-lg bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800">
              <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400 flex-shrink-0" />
              <span className="text-sm font-medium text-green-800 dark:text-green-300">
                {t('sync:dashboard.configSavedMsg')}
              </span>
            </div>

            <div className="border border-gray-200 dark:border-gray-700 rounded-lg divide-y divide-gray-200 dark:divide-gray-700">
              <div className="flex items-center justify-between p-3">
                <div className="flex items-center gap-2">
                  <CalendarClock className="w-4 h-4 text-purple-500" />
                  <span className="text-sm text-gray-700 dark:text-gray-300">{ t('admin:operationAudit.dailySync') }</span>
                </div>
                <span className={cn('text-sm font-semibold', configSaveResult.dailySyncEnabled ? 'text-blue-600 dark:text-blue-400' : 'text-gray-400 dark:text-gray-500')}>
                  {configSaveResult.dailySyncEnabled ? t('dashboard.daily', { time: configSaveResult.dailySyncTime }) : t('dashboard.disabled')}
                </span>
              </div>
              <div className="flex items-center justify-between p-3">
                <div className="flex items-center gap-2">
                  <ShieldCheck className="w-4 h-4 text-green-500" />
                  <span className="text-sm text-gray-700 dark:text-gray-300">{t('dashboard.autoRevalidation')}</span>
                </div>
                <span className={cn('text-sm font-semibold', configSaveResult.revalidateCertsOnSync ? 'text-green-600 dark:text-green-400' : 'text-gray-400 dark:text-gray-500')}>
                  {configSaveResult.revalidateCertsOnSync ? t('dashboard.enabled') : t('dashboard.disabled')}
                </span>
              </div>
              <div className="flex items-center justify-between p-3">
                <div className="flex items-center gap-2">
                  <RotateCcw className="w-4 h-4 text-orange-500" />
                  <span className="text-sm text-gray-700 dark:text-gray-300">{t('dashboard.autoReconcile')}</span>
                </div>
                <span className={cn('text-sm font-semibold', configSaveResult.autoReconcile ? 'text-orange-600 dark:text-orange-400' : 'text-gray-400 dark:text-gray-500')}>
                  {configSaveResult.autoReconcile ? t('dashboard.enabled') : t('dashboard.disabled')}
                </span>
              </div>
            </div>

            <div className="flex justify-end pt-2">
              <button
                onClick={() => setConfigSaveResult(null)}
                className="px-4 py-2 text-sm font-medium text-white bg-blue-600 rounded-lg hover:bg-blue-700 transition-colors"
              >
                {t('common.confirm.title')}
              </button>
            </div>
          </div>
        </Dialog>
      )}

      {showConfigDialog && config && (
        <Dialog
          isOpen={showConfigDialog}
          onClose={() => setShowConfigDialog(false)}
          title={t('sync:dashboard.editServiceConfig')}
          size="lg"
        >
          <div className="space-y-6 py-4">
            {/* Daily Sync Schedule */}
            <div className="space-y-3">
              <div className="flex items-center justify-between">
                <label className="flex items-center gap-2 text-sm font-medium text-gray-700 dark:text-gray-300">
                  <CalendarClock className="w-5 h-5 text-purple-500" />
                  {t('sync.reconciliation.dailySync')}
                </label>
                <button
                  type="button"
                  onClick={() =>
                    setEditedConfig((prev) => ({
                      ...prev,
                      dailySyncEnabled: !prev.dailySyncEnabled,
                    }))
                  }
                  className={cn(
                    'relative inline-flex h-6 w-11 items-center rounded-full transition-colors',
                    editedConfig.dailySyncEnabled
                      ? 'bg-blue-600'
                      : 'bg-gray-200 dark:bg-gray-700'
                  )}
                >
                  <span
                    className={cn(
                      'inline-block h-4 w-4 transform rounded-full bg-white transition-transform',
                      editedConfig.dailySyncEnabled ? 'translate-x-6' : 'translate-x-1'
                    )}
                  />
                </button>
              </div>

              {editedConfig.dailySyncEnabled && (
                <div className="flex items-center gap-3 pl-7">
                  <div className="flex items-center gap-2">
                    <label className="text-sm text-gray-600 dark:text-gray-400">{t('common:time.hour')}</label>
                    <input
                      type="number"
                      min="0"
                      max="23"
                      value={editedConfig.dailySyncHour ?? 0}
                      onChange={(e) =>
                        setEditedConfig((prev) => ({
                          ...prev,
                          dailySyncHour: parseInt(e.target.value) || 0,
                        }))
                      }
                      className="w-16 px-2 py-1 text-center border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white"
                    />
                  </div>
                  <span className="text-gray-400">:</span>
                  <div className="flex items-center gap-2">
                    <label className="text-sm text-gray-600 dark:text-gray-400">{ t('common:label.minutes') }</label>
                    <input
                      type="number"
                      min="0"
                      max="59"
                      value={editedConfig.dailySyncMinute ?? 0}
                      onChange={(e) =>
                        setEditedConfig((prev) => ({
                          ...prev,
                          dailySyncMinute: parseInt(e.target.value) || 0,
                        }))
                      }
                      className="w-16 px-2 py-1 text-center border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white"
                    />
                  </div>
                </div>
              )}
            </div>

            {/* Auto Re-validation */}
            <div className="flex items-center justify-between">
              <label className="flex items-center gap-2 text-sm font-medium text-gray-700 dark:text-gray-300">
                <ShieldCheck className="w-5 h-5 text-green-500" />
                {t('sync.dashboard.autoRevalidation')}
              </label>
              <button
                type="button"
                onClick={() =>
                  setEditedConfig((prev) => ({
                    ...prev,
                    revalidateCertsOnSync: !prev.revalidateCertsOnSync,
                  }))
                }
                className={cn(
                  'relative inline-flex h-6 w-11 items-center rounded-full transition-colors',
                  editedConfig.revalidateCertsOnSync
                    ? 'bg-green-600'
                    : 'bg-gray-200 dark:bg-gray-700'
                )}
              >
                <span
                  className={cn(
                    'inline-block h-4 w-4 transform rounded-full bg-white transition-transform',
                    editedConfig.revalidateCertsOnSync ? 'translate-x-6' : 'translate-x-1'
                  )}
                />
              </button>
            </div>

            {/* Auto Reconcile */}
            <div className="flex items-center justify-between">
              <label className="flex items-center gap-2 text-sm font-medium text-gray-700 dark:text-gray-300">
                <RotateCcw className="w-5 h-5 text-orange-500" />
                {t('sync.dashboard.autoReconcileLabel')}
              </label>
              <button
                type="button"
                onClick={() =>
                  setEditedConfig((prev) => ({
                    ...prev,
                    autoReconcile: !prev.autoReconcile,
                  }))
                }
                className={cn(
                  'relative inline-flex h-6 w-11 items-center rounded-full transition-colors',
                  editedConfig.autoReconcile
                    ? 'bg-orange-600'
                    : 'bg-gray-200 dark:bg-gray-700'
                )}
              >
                <span
                  className={cn(
                    'inline-block h-4 w-4 transform rounded-full bg-white transition-transform',
                    editedConfig.autoReconcile ? 'translate-x-6' : 'translate-x-1'
                  )}
                />
              </button>
            </div>

            {/* Action Buttons */}
            <div className="flex items-center justify-end gap-3 pt-4 border-t border-gray-200 dark:border-gray-700">
              <button
                onClick={() => setShowConfigDialog(false)}
                disabled={saving}
                className="flex items-center gap-2 px-4 py-2 text-sm font-medium text-gray-700 dark:text-gray-300 bg-gray-100 dark:bg-gray-700 rounded-lg hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors disabled:opacity-50"
              >
                <CloseIcon className="w-4 h-4" />
                {t('common.button.cancel')}
              </button>
              <button
                onClick={handleSaveConfig}
                disabled={saving}
                className="flex items-center gap-2 px-4 py-2 text-sm font-medium text-white bg-blue-600 rounded-lg hover:bg-blue-700 transition-colors disabled:opacity-50"
              >
                {saving ? (
                  <>
                    <Loader2 className="w-4 h-4 animate-spin" />
                    {t('common.label.saving')}
                  </>
                ) : (
                  <>
                    <Save className="w-4 h-4" />
                    {t('common.button.save')}
                  </>
                )}
              </button>
            </div>
          </div>
        </Dialog>
      )}
    </div>
  );
}

export default SyncDashboard;
