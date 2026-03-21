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
} from 'lucide-react';
import { syncServiceApi, type SyncConfigResponse, type RevalidationHistoryItem, type RevalidationResult } from '@/services/api';
import type { SyncStatusResponse, SyncHistoryItem, SyncStatusType } from '@/types';
import { cn } from '@/utils/cn';
import { formatDateTime } from '@/utils/dateFormat';
import { ReconciliationHistory } from '@/components/sync/ReconciliationHistory';
import { RevalidationHistoryTable } from '@/components/sync/RevalidationHistoryTable';
import { RevalidationResultDialog } from '@/components/sync/RevalidationResultDialog';
import { SyncCheckResultDialog } from '@/components/sync/SyncCheckResultDialog';
import { DailySyncResultDialog } from '@/components/sync/DailySyncResultDialog';
import { ConfigSaveResultDialog } from '@/components/sync/ConfigSaveResultDialog';
import { SyncConfigDialog } from '@/components/sync/SyncConfigDialog';
import { useSortableTable } from '@/hooks/useSortableTable';
import { SortableHeader } from '@/components/common/SortableHeader';
import { GlossaryTerm } from '@/components/common';
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
  const [revalidationResult, setRevalidationResult] = useState<RevalidationResult | null>(null);
  const [syncCheckResult, setSyncCheckResult] = useState<SyncStatusResponse | null>(null);
  const [configSaveResult, setConfigSaveResult] = useState<SyncConfigResponse | null>(null);
  const [triggeringDailySync, setTriggeringDailySync] = useState(false);
  const [dailySyncResult, setDailySyncResult] = useState<{ success: boolean; message: string } | null>(null);

  const { sortedData: sortedHistory, sortConfig: historySortConfig, requestSort: requestHistorySort } = useSortableTable(history);

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
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4">
          <div className="flex items-center justify-between mb-4">
            <div className="flex items-center gap-2">
              <Settings className="w-5 h-5 text-gray-500" />
              <h3 className="text-sm font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                {t('dashboard.serviceConfig')}
              </h3>
            </div>
            <button
              onClick={() => setShowConfigDialog(true)}
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
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 h-full">
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
                <div className="mt-2 text-xs text-gray-500 dark:text-gray-400">
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
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 md:col-span-2 h-full">
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
                    <th className="text-center py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap">
                      {t('dashboard.certType')}
                    </th>
                    <th className="text-center py-2.5 px-3 font-semibold text-blue-600 dark:text-blue-400 whitespace-nowrap">
                      <div className="flex items-center justify-end gap-1">
                        <Database className="w-3.5 h-3.5" />
                        DB
                      </div>
                    </th>
                    <th className="text-center py-2.5 px-3 font-semibold text-green-600 dark:text-green-400 whitespace-nowrap">
                      <div className="flex items-center justify-end gap-1">
                        <Server className="w-3.5 h-3.5" />
                        LDAP
                      </div>
                    </th>
                    <th className="text-center py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap">
                      {t('dashboard.difference')}
                    </th>
                  </tr>
                </thead>
                <tbody>
                  <tr className="border-b border-gray-100 dark:border-gray-700/50">
                    <td className="py-2 px-3 text-gray-700 dark:text-gray-300"><GlossaryTerm term="CSCA" /></td>
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
                    <td className="py-2 px-3 text-gray-700 dark:text-gray-300"><GlossaryTerm term="MLSC" /></td>
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
                    <td className="py-2 px-3 text-gray-700 dark:text-gray-300"><GlossaryTerm term="DSC" /></td>
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
                    <td className="py-2 px-3 text-gray-700 dark:text-gray-300"><GlossaryTerm term="DSC_NC" /></td>
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
                    <td className="py-2 px-3 text-gray-700 dark:text-gray-300"><GlossaryTerm term="CRL" /></td>
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
              <div className="text-xs text-gray-500 dark:text-gray-400"><GlossaryTerm term="CSCA" /></div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancies.mlsc}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400"><GlossaryTerm term="MLSC" /></div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancies.dsc}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400"><GlossaryTerm term="DSC" /></div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancies.dscNc}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400"><GlossaryTerm term="DSC_NC" /></div>
            </div>
            <div className="bg-white dark:bg-gray-800 rounded-lg p-3 text-center">
              <div className="text-xl font-semibold text-gray-700 dark:text-gray-300">
                {status.discrepancies.crl}
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400"><GlossaryTerm term="CRL" /></div>
            </div>
          </div>
          <p className="mt-4 text-sm text-yellow-700 dark:text-yellow-300">
            {t('dashboard.positiveNegativeExplanation')}
          </p>
        </div>
      )}

      {/* Sync History */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4">
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
                    <td className="py-2 px-3 text-gray-900 dark:text-white">
                      {formatDateTime(item.checkedAt)}
                    </td>
                    <td className="py-2 px-3 text-center">
                      <span
                        className={cn(
                          'inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium',
                          getStatusColor(item.status)
                        )}
                      >
                        {getStatusLabel(item.status)}
                      </span>
                    </td>
                    <td className="py-2 px-3 text-right font-mono text-gray-700 dark:text-gray-300">
                      {item.dbTotal?.toLocaleString()}
                    </td>
                    <td className="py-2 px-3 text-right font-mono text-gray-700 dark:text-gray-300">
                      {item.ldapTotal?.toLocaleString()}
                    </td>
                    <td className="py-2 px-3 text-right">
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
                    <td className="py-2 px-3 text-right text-gray-500 dark:text-gray-400">
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
      <RevalidationHistoryTable items={revalidationHistory} />

      {/* Reconciliation History */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4">
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

      {/* Revalidation Result Dialog */}
      {revalidationResult && (
        <RevalidationResultDialog
          result={revalidationResult}
          onClose={() => setRevalidationResult(null)}
        />
      )}

      {/* Sync Check Result Dialog */}
      {syncCheckResult && (
        <SyncCheckResultDialog
          result={syncCheckResult}
          onClose={() => setSyncCheckResult(null)}
        />
      )}

      {/* Daily Sync Trigger Result Dialog */}
      {dailySyncResult && (
        <DailySyncResultDialog
          result={dailySyncResult}
          config={config}
          onClose={() => setDailySyncResult(null)}
        />
      )}

      {/* Config Save Result Dialog */}
      {configSaveResult && (
        <ConfigSaveResultDialog
          result={configSaveResult}
          onClose={() => setConfigSaveResult(null)}
        />
      )}

      {/* Config Edit Dialog */}
      {showConfigDialog && config && (
        <SyncConfigDialog
          config={config}
          onClose={() => setShowConfigDialog(false)}
          onSaved={(savedConfig) => setConfigSaveResult(savedConfig)}
          onError={(message) => setError(message)}
          onRefresh={fetchData}
        />
      )}
    </div>
  );
}

export default SyncDashboard;
