import { useTranslation } from 'react-i18next';
import { useState, useEffect } from 'react';
import { RefreshCw, AlertCircle, CheckCircle, Download, Globe, Loader2, Clock, FileText } from 'lucide-react';
import { cn } from '@/utils/cn';
import { formatDateTime } from '@/utils/dateFormat';
import { icaoApi } from '@/services/pkdApi';
import { Dialog } from '@/components/common/Dialog';

interface IcaoVersion {
  id: number;
  collection_type: string;
  file_name: string;
  file_version: number;
  status: string;
  status_description: string;
  detected_at: string;
  downloaded_at?: string;
  imported_at?: string;
  notification_sent: boolean;
  notification_sent_at?: string;
  import_upload_id?: string;
  certificate_count?: number;
  error_message?: string;
}

interface VersionStatus {
  collection_type: string;
  detected_version: number;
  uploaded_version: number;
  upload_timestamp: string;
  version_diff: number;
  needs_update: boolean;
  status: string;
  status_message: string;
}

interface ApiResponse {
  success: boolean;
  count?: number;
  limit?: number;
  versions: IcaoVersion[];
  error?: string;
  message?: string;
}

interface StatusApiResponse {
  success: boolean;
  count: number;
  status: VersionStatus[];
  any_needs_update?: boolean;
  last_checked_at?: string | null;
}

interface CheckUpdateResult {
  success: boolean;
  message: string;
  new_version_count: number;
  new_versions: IcaoVersion[];
}

function getCollectionFullName(t: (key: string) => string, collectionType: string): string {
  if (collectionType === 'DSC_CRL') return t('icao:statusPage.collectionFull.DSC_CRL');
  if (collectionType === 'DSC_NC') return t('icao:statusPage.collectionFull.DSC_NC');
  return t('icao:statusPage.collectionFull.MASTER_LIST');
}

function getCollectionShortName(t: (key: string) => string, collectionType: string): string {
  if (collectionType === 'DSC_CRL') return t('icao:statusPage.collectionShort.DSC_CRL');
  if (collectionType === 'DSC_NC') return t('icao:statusPage.collectionShort.DSC_NC');
  return t('icao:statusPage.collectionShort.MASTER_LIST');
}

export default function IcaoStatus() {
  const { t } = useTranslation(['icao', 'common']);
  const [versionHistory, setVersionHistory] = useState<IcaoVersion[]>([]);
  const [versionStatus, setVersionStatus] = useState<VersionStatus[]>([]);
  const [lastCheckedAt, setLastCheckedAt] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [checking, setChecking] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [checkResult, setCheckResult] = useState<CheckUpdateResult | null>(null);

  useEffect(() => {
    fetchDashboardData();
  }, []);

  const fetchDashboardData = async () => {
    setLoading(true);
    try {
      await Promise.all([
        fetchVersionStatus(),
        fetchVersionHistory()
      ]);
    } finally {
      setLoading(false);
    }
  };

  const fetchVersionStatus = async () => {
    try {
      const response = await icaoApi.getStatus();
      const data = response.data as StatusApiResponse;

      if (data.success) {
        setVersionStatus(data.status);
        if (data.last_checked_at) {
          setLastCheckedAt(data.last_checked_at);
        }
      }
    } catch (err) {
      if (import.meta.env.DEV) console.error('Failed to fetch version status:', err);
    }
  };

  const fetchVersionHistory = async () => {
    try {
      const response = await icaoApi.getHistory(10);
      const data = response.data as ApiResponse;

      if (data.success) {
        setVersionHistory(data.versions);
      }
    } catch (err) {
      if (import.meta.env.DEV) console.error('Failed to fetch version history:', err);
    }
  };

  const handleCheckUpdates = async () => {
    setChecking(true);
    setError(null);

    try {
      const res = await icaoApi.checkUpdates();
      const result = res.data as CheckUpdateResult;
      setCheckResult(result);
      // Refresh page data with the latest results
      await Promise.all([fetchVersionStatus(), fetchVersionHistory()]);
    } catch (err) {
      setCheckResult({ success: false, message: t('icao:statusPage.networkError'), new_version_count: 0, new_versions: [] });
      if (import.meta.env.DEV) console.error('Check updates error:', err);
    } finally {
      setChecking(false);
    }
  };

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-blue-500 to-cyan-600 shadow-lg">
            <Globe className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">{t('icao:statusPage.pageTitle')}</h1>
            <div className="flex items-center gap-3">
              <p className="text-sm text-gray-500 dark:text-gray-400">
                {t('icao:statusPage.pageSubtitle')}
              </p>
              {lastCheckedAt && (
                <span className="inline-flex items-center gap-1 text-xs text-gray-400 dark:text-gray-500">
                  <Clock className="w-3 h-3" />
                  {t('icao:statusPage.lastChecked', { time: formatDateTime(lastCheckedAt) })}
                </span>
              )}
            </div>
          </div>
          {/* Quick Actions */}
          <div className="flex gap-2">
            <button
              onClick={handleCheckUpdates}
              disabled={checking}
              className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl text-sm font-medium text-white bg-gradient-to-r from-blue-500 to-cyan-500 hover:from-blue-600 hover:to-cyan-600 transition-all duration-200 shadow-md hover:shadow-lg disabled:opacity-50 disabled:cursor-not-allowed"
            >
              <RefreshCw className={cn('w-4 h-4', checking && 'animate-spin')} />
              {checking ? t('icao:checking') : t('icao:checkUpdates')}
            </button>
            <button
              onClick={fetchDashboardData}
              disabled={loading}
              className="inline-flex items-center gap-2 px-3 py-2 rounded-xl text-sm font-medium transition-all duration-200 text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700"
            >
              <RefreshCw className={cn('w-4 h-4', loading && 'animate-spin')} />
            </button>
          </div>
        </div>
      </div>

      {loading ? (
        <div className="flex items-center justify-center py-20">
          <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
        </div>
      ) : (
        <>
          {/* Error Alert */}
          {error && (
            <div className="mb-6 p-4 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl flex items-start gap-3">
              <AlertCircle className="w-5 h-5 text-red-600 dark:text-red-400 flex-shrink-0 mt-0.5" />
              <div>
                <h3 className="font-semibold text-red-900 dark:text-red-300">{t('common:error')}</h3>
                <p className="text-red-700 dark:text-red-400">{error}</p>
              </div>
            </div>
          )}

          {/* Version Status Overview */}
          {versionStatus.length > 0 && (
            <div className="mb-6">
              <h2 className="text-lg font-semibold text-gray-900 dark:text-white mb-4">{t('icao:statusPage.versionOverview')}</h2>
              <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
                {versionStatus.map((status) => (
                  <div
                    key={status.collection_type}
                    className={cn(
                      "bg-white dark:bg-gray-800 rounded-2xl border shadow-lg p-5 h-full flex flex-col transition-all duration-200",
                      status.status === 'DETECTION_STALE'
                        ? 'border-yellow-300 dark:border-yellow-700'
                        : status.needs_update
                        ? 'border-orange-300 dark:border-orange-700'
                        : 'border-green-300 dark:border-green-700'
                    )}
                  >
                    <div className="flex items-start justify-between mb-4">
                      <div>
                        <h3 className="text-base font-semibold text-gray-900 dark:text-white mb-1">
                          {getCollectionFullName(t, status.collection_type)}
                        </h3>
                      </div>
                      {status.status === 'DETECTION_STALE' ? (
                        <Clock className="w-5 h-5 text-yellow-500" />
                      ) : status.needs_update ? (
                        <AlertCircle className="w-5 h-5 text-orange-500" />
                      ) : (
                        <CheckCircle className="w-5 h-5 text-green-500" />
                      )}
                    </div>

                    <div className="space-y-3 flex-1 flex flex-col">
                      {/* Status Badge */}
                      <div>
                        <span
                          className={cn(
                            "inline-flex items-center gap-2 px-2.5 py-1 rounded-full border text-xs font-medium",
                            status.status === 'UPDATE_NEEDED'
                              ? 'bg-orange-100 dark:bg-orange-900/30 text-orange-800 dark:text-orange-300 border-orange-300 dark:border-orange-700'
                              : status.status === 'DETECTION_STALE'
                              ? 'bg-yellow-100 dark:bg-yellow-900/30 text-yellow-800 dark:text-yellow-300 border-yellow-300 dark:border-yellow-700'
                              : status.status === 'UP_TO_DATE'
                              ? 'bg-green-100 dark:bg-green-900/30 text-green-800 dark:text-green-300 border-green-300 dark:border-green-700'
                              : 'bg-gray-100 dark:bg-gray-700 text-gray-800 dark:text-gray-300 border-gray-300 dark:border-gray-600'
                          )}
                        >
                          {status.status === 'UPDATE_NEEDED'
                            ? t('icao:statusPage.updateNeeded')
                            : status.status === 'DETECTION_STALE'
                            ? t('icao:statusPage.detectionStale')
                            : t('icao:upToDate')}
                        </span>
                      </div>

                      {/* Version Comparison */}
                      <div className="space-y-2">
                        <div className="flex items-center justify-between">
                          <span className="text-sm text-gray-600 dark:text-gray-400">{t('icao:statusPage.detectedVersion')}</span>
                          <span className="text-lg font-bold text-blue-600 dark:text-blue-400">
                            {status.detected_version.toString().padStart(6, '0')}
                          </span>
                        </div>
                        <div className="flex items-center justify-between">
                          <span className="text-sm text-gray-600 dark:text-gray-400">{t('icao:statusPage.uploadedVersion')}</span>
                          <span className={cn(
                            "text-lg font-bold",
                            status.uploaded_version === 0
                              ? 'text-gray-400 dark:text-gray-500'
                              : 'text-gray-700 dark:text-gray-300'
                          )}>
                            {status.uploaded_version === 0
                              ? 'N/A'
                              : status.uploaded_version.toString().padStart(6, '0')}
                          </span>
                        </div>
                        {status.version_diff > 0 && (
                          <div className="flex items-center justify-between pt-2 border-t border-gray-200 dark:border-gray-700">
                            <span className="text-sm text-gray-600 dark:text-gray-400">{t('icao:statusPage.versionDiff')}</span>
                            <span className="text-base font-bold text-orange-600 dark:text-orange-400">
                              +{status.version_diff}
                            </span>
                          </div>
                        )}
                      </div>

                      {/* Status Message */}
                      <div className="pt-3 border-t border-gray-200 dark:border-gray-700 mt-auto">
                        <p className="text-sm text-gray-600 dark:text-gray-400">{status.status_message}</p>
                        {status.upload_timestamp !== 'N/A' && (
                          <p className="text-xs text-gray-500 dark:text-gray-500 mt-1">
                            {t('icao:statusPage.lastUpload', { time: formatDateTime(status.upload_timestamp) })}
                          </p>
                        )}
                      </div>

                      {/* Download Link for Updates */}
                      {status.status === 'UPDATE_NEEDED' && (
                        <div className="pt-3 border-t border-gray-200 dark:border-gray-700">
                          <a
                            href="https://pkddownloadsg.icao.int/"
                            target="_blank"
                            rel="noopener noreferrer"
                            className="inline-flex items-center gap-2 text-sm text-blue-600 dark:text-blue-400 hover:text-blue-700 dark:hover:text-blue-300 font-medium"
                          >
                            <Download className="w-4 h-4" />
                            {t('icao:statusPage.downloadFromIcao')}
                          </a>
                        </div>
                      )}
                      {status.status === 'DETECTION_STALE' && (
                        <div className="pt-3 border-t border-gray-200 dark:border-gray-700">
                          <p className="text-xs text-yellow-700 dark:text-yellow-400">
                            {t('icao:statusPage.staleMessage')}
                          </p>
                        </div>
                      )}
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )}

          {/* Version History Table */}
          <div className="mb-6">
            <h2 className="text-lg font-semibold text-gray-900 dark:text-white mb-4">{t('icao:statusPage.detectionHistory')}</h2>
            <div className="bg-white dark:bg-gray-800 rounded-2xl border border-gray-200 dark:border-gray-700 shadow-lg overflow-hidden">
              <div className="overflow-x-auto">
                <table className="w-full">
                  <thead className="bg-slate-100 dark:bg-gray-700">
                    <tr>
                      <th className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                        {t('icao:statusPage.table.collection')}
                      </th>
                      <th className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                        {t('icao:statusPage.table.fileName')}
                      </th>
                      <th className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                        {t('icao:statusPage.table.version')}
                      </th>
                      <th className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                        {t('icao:statusPage.table.status')}
                      </th>
                      <th className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                        {t('icao:statusPage.table.detectedAt')}
                      </th>
                    </tr>
                  </thead>
                  <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
                    {versionHistory.length === 0 ? (
                      <tr>
                        <td colSpan={5} className="px-3 py-8 text-center text-xs text-gray-500 dark:text-gray-400">
                          {t('icao:statusPage.noHistory')}
                        </td>
                      </tr>
                    ) : (
                      versionHistory.map((version) => (
                        <tr key={version.id} className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors">
                          <td className="px-3 py-2.5 whitespace-nowrap">
                            <span className="text-xs font-medium text-gray-900 dark:text-white">
                              {getCollectionShortName(t, version.collection_type)}
                            </span>
                          </td>
                          <td className="px-3 py-2.5 whitespace-nowrap">
                            <span className="text-xs text-gray-700 dark:text-gray-300 font-mono">
                              {version.file_name}
                            </span>
                          </td>
                          <td className="px-3 py-2.5 whitespace-nowrap">
                            <span className="text-xs font-semibold text-blue-600 dark:text-blue-400">
                              {version.file_version.toString().padStart(6, '0')}
                            </span>
                          </td>
                          <td className="px-3 py-2.5 whitespace-nowrap">
                            <span
                              className={cn(
                                "inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full border text-xs font-medium",
                                version.status === 'DETECTED'
                                  ? 'bg-yellow-100 dark:bg-yellow-900/30 text-yellow-800 dark:text-yellow-300 border-yellow-300 dark:border-yellow-700'
                                  : version.status === 'NOTIFIED'
                                  ? 'bg-blue-100 dark:bg-blue-900/30 text-blue-800 dark:text-blue-300 border-blue-300 dark:border-blue-700'
                                  : version.status === 'DOWNLOADED'
                                  ? 'bg-indigo-100 dark:bg-indigo-900/30 text-indigo-800 dark:text-indigo-300 border-indigo-300 dark:border-indigo-700'
                                  : version.status === 'IMPORTED'
                                  ? 'bg-green-100 dark:bg-green-900/30 text-green-800 dark:text-green-300 border-green-300 dark:border-green-700'
                                  : version.status === 'FAILED'
                                  ? 'bg-red-100 dark:bg-red-900/30 text-red-800 dark:text-red-300 border-red-300 dark:border-red-700'
                                  : 'bg-gray-100 dark:bg-gray-700 text-gray-800 dark:text-gray-300 border-gray-300 dark:border-gray-600'
                              )}
                            >
                              {version.status}
                            </span>
                          </td>
                          <td className="px-3 py-2.5 whitespace-nowrap text-xs text-gray-500 dark:text-gray-400">
                            {formatDateTime(version.detected_at)}
                          </td>
                        </tr>
                      ))
                    )}
                  </tbody>
                </table>
              </div>
            </div>
          </div>

          {/* Info Section */}
          <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-2xl p-6">
            <h3 className="text-base font-semibold text-blue-900 dark:text-blue-300 mb-3">{t('icao:statusPage.autoSyncInfo')}</h3>
            <div className="text-sm text-blue-800 dark:text-blue-300 space-y-3">
              <p>
                {t('icao:statusPage.autoSyncDesc')}
                {' '}
                {t('icao:statusPage.autoSyncManual')}
              </p>
              <div>
                <h4 className="font-semibold mb-2">{t('icao:statusPage.lifecycleTitle')}</h4>
                <ul className="list-disc list-inside space-y-1 ml-2">
                  <li><strong>DETECTED</strong>: {t('icao:statusPage.lifecycleDetected')}</li>
                  <li><strong>NOTIFIED</strong>: {t('icao:statusPage.lifecycleNotified')}</li>
                  <li><strong>DOWNLOADED</strong>: {t('icao:statusPage.lifecycleDownloaded')}</li>
                  <li><strong>IMPORTED</strong>: {t('icao:statusPage.lifecycleImported')}</li>
                  <li><strong>FAILED</strong>: {t('icao:statusPage.lifecycleFailed')}</li>
                </ul>
              </div>
              <div className="pt-3 border-t border-blue-300 dark:border-blue-700">
                <p className="text-xs">
                  <strong>{t('icao:statusPage.noteLabel')}</strong>: {t('icao:statusPage.tier1Note')}
                </p>
              </div>
            </div>
          </div>
        </>
      )}

      {/* Check Update Result Dialog */}
      {checkResult && (
        <Dialog isOpen={true} onClose={() => setCheckResult(null)} title={t('icao:statusPage.checkResultTitle')} size="lg">
          <div className="space-y-4">
            {/* Status Banner */}
            {(() => {
              const isNewDetected = checkResult.new_version_count > 0;
              const hasPending = checkResult.new_version_count < 0;
              const isUpToDate = checkResult.success && checkResult.new_version_count === 0;
              const pendingCount = Math.abs(checkResult.new_version_count);

              const bannerColor = !checkResult.success
                ? "bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800"
                : isNewDetected
                ? "bg-orange-50 dark:bg-orange-900/20 border-orange-200 dark:border-orange-800"
                : hasPending
                ? "bg-yellow-50 dark:bg-yellow-900/20 border-yellow-200 dark:border-yellow-800"
                : "bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800";

              const iconBg = !checkResult.success
                ? "bg-red-100 dark:bg-red-900/40"
                : isNewDetected
                ? "bg-orange-100 dark:bg-orange-900/40"
                : hasPending
                ? "bg-yellow-100 dark:bg-yellow-900/40"
                : "bg-green-100 dark:bg-green-900/40";

              const textColor = !checkResult.success
                ? "text-red-900 dark:text-red-300"
                : isNewDetected
                ? "text-orange-900 dark:text-orange-300"
                : hasPending
                ? "text-yellow-900 dark:text-yellow-300"
                : "text-green-900 dark:text-green-300";

              const subTextColor = !checkResult.success
                ? "text-red-700 dark:text-red-400"
                : isNewDetected
                ? "text-orange-700 dark:text-orange-400"
                : hasPending
                ? "text-yellow-700 dark:text-yellow-400"
                : "text-green-700 dark:text-green-400";

              const title = !checkResult.success
                ? t('icao:statusPage.checkFailed')
                : isNewDetected
                ? t('icao:statusPage.newVersionsDetectedTitle', { num: checkResult.new_version_count })
                : hasPending
                ? t('icao:statusPage.pendingVersionsTitle', { num: pendingCount })
                : t('icao:statusPage.systemUpToDate');

              return (
                <div className={cn("flex items-center gap-3 p-4 rounded-xl border", bannerColor)}>
                  <div className={cn("flex items-center justify-center w-10 h-10 rounded-full flex-shrink-0", iconBg)}>
                    {!checkResult.success ? (
                      <AlertCircle className="w-5 h-5 text-red-600 dark:text-red-400" />
                    ) : isUpToDate ? (
                      <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400" />
                    ) : (
                      <AlertCircle className={cn("w-5 h-5", isNewDetected ? "text-orange-600 dark:text-orange-400" : "text-yellow-600 dark:text-yellow-400")} />
                    )}
                  </div>
                  <div className="flex-1 min-w-0">
                    <p className={cn("text-sm font-semibold", textColor)}>{title}</p>
                    <p className={cn("text-xs mt-0.5", subTextColor)}>{checkResult.message}</p>
                  </div>
                  <span className="text-xs text-gray-400 dark:text-gray-500 whitespace-nowrap">
                    {formatDateTime(new Date().toISOString())}
                  </span>
                </div>
              );
            })()}

            {/* New Versions Detail */}
            {checkResult.new_version_count > 0 && (
              <div className="space-y-3">
                <h4 className="text-sm font-semibold text-gray-900 dark:text-white">{t('icao:statusPage.detectedNewVersions')}</h4>
                <div className="space-y-2">
                  {checkResult.new_versions.map((v, i) => (
                    <div key={v.id ?? i} className="flex items-center gap-3 p-3 bg-white dark:bg-gray-800 rounded-xl border border-gray-200 dark:border-gray-700">
                      <div className="flex items-center justify-center w-8 h-8 rounded-lg bg-blue-100 dark:bg-blue-900/40 flex-shrink-0">
                        <FileText className="w-4 h-4 text-blue-600 dark:text-blue-400" />
                      </div>
                      <div className="flex-1 min-w-0">
                        <div className="flex items-center gap-2">
                          <span className="text-sm font-semibold text-gray-900 dark:text-white">
                            {getCollectionShortName(t, v.collection_type)}
                          </span>
                          <span className="inline-flex items-center px-2 py-0.5 rounded-full text-[10px] font-medium bg-yellow-100 dark:bg-yellow-900/30 text-yellow-800 dark:text-yellow-300 border border-yellow-300 dark:border-yellow-700">
                            {v.status}
                          </span>
                        </div>
                        <p className="text-xs text-gray-500 dark:text-gray-400 font-mono truncate mt-0.5">
                          {v.file_name}
                        </p>
                      </div>
                      <div className="text-right flex-shrink-0">
                        <span className="text-lg font-bold text-blue-600 dark:text-blue-400">
                          {v.file_version.toString().padStart(6, '0')}
                        </span>
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            )}

            {/* Up-to-date detail */}
            {checkResult.success && checkResult.new_version_count === 0 && (
              <div className="text-center py-4">
                <Globe className="w-10 h-10 text-green-400 dark:text-green-500 mx-auto mb-2" />
                <p className="text-sm text-gray-600 dark:text-gray-400">
                  {t('icao:statusPage.versionsInSync')}
                </p>
              </div>
            )}

            {/* Pending uploads detail */}
            {checkResult.success && checkResult.new_version_count < 0 && (
              <div className="text-center py-4">
                <Download className="w-10 h-10 text-yellow-400 dark:text-yellow-500 mx-auto mb-2" />
                <p className="text-sm text-gray-600 dark:text-gray-400">
                  {t('icao:statusPage.pendingUploadsDesc')}
                </p>
                <a
                  href="https://pkddownloadsg.icao.int/"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="inline-flex items-center gap-2 mt-3 text-sm text-blue-600 dark:text-blue-400 hover:text-blue-700 dark:hover:text-blue-300 font-medium"
                >
                  <Download className="w-4 h-4" />
                  {t('icao:statusPage.downloadFromIcao')}
                </a>
              </div>
            )}

            {/* Close Button */}
            <div className="flex justify-end pt-2 border-t border-gray-200 dark:border-gray-700">
              <button
                onClick={() => setCheckResult(null)}
                className="px-5 py-2 text-sm font-medium text-white bg-gradient-to-r from-blue-500 to-cyan-500 hover:from-blue-600 hover:to-cyan-600 rounded-xl transition-all duration-200 shadow-sm"
              >
                {t('common:confirm.title')}
              </button>
            </div>
          </div>
        </Dialog>
      )}
    </div>
  );
}
