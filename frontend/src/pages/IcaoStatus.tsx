import { useState, useEffect } from 'react';
import { RefreshCw, AlertCircle, CheckCircle, Download, Globe, Loader2, Clock } from 'lucide-react';
import { cn } from '@/utils/cn';
import { icaoApi } from '@/services/pkdApi';

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

export default function IcaoStatus() {
  const [versionHistory, setVersionHistory] = useState<IcaoVersion[]>([]);
  const [versionStatus, setVersionStatus] = useState<VersionStatus[]>([]);
  const [lastCheckedAt, setLastCheckedAt] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [checking, setChecking] = useState(false);
  const [error, setError] = useState<string | null>(null);

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
      await icaoApi.checkUpdates();
      // Wait a moment for async processing
      setTimeout(() => {
        fetchVersionStatus();
        fetchVersionHistory();
        setChecking(false);
      }, 2000);
    } catch (err) {
      setError('Network error: Unable to check for updates');
      if (import.meta.env.DEV) console.error('Check updates error:', err);
      setChecking(false);
    }
  };

  const formatTimestamp = (timestamp: string) => {
    return new Date(timestamp).toLocaleString('ko-KR', {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    });
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
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">ICAO PKD Auto Sync</h1>
            <div className="flex items-center gap-3">
              <p className="text-sm text-gray-500 dark:text-gray-400">
                ICAO PKD 포털의 버전 업데이트를 모니터링합니다.
              </p>
              {lastCheckedAt && (
                <span className="inline-flex items-center gap-1 text-xs text-gray-400 dark:text-gray-500">
                  <Clock className="w-3 h-3" />
                  마지막 확인: {formatTimestamp(lastCheckedAt)}
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
              {checking ? '확인 중...' : '업데이트 확인'}
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
                <h3 className="font-semibold text-red-900 dark:text-red-300">오류</h3>
                <p className="text-red-700 dark:text-red-400">{error}</p>
              </div>
            </div>
          )}

          {/* Version Status Overview */}
          {versionStatus.length > 0 && (
            <div className="mb-6">
              <h2 className="text-lg font-semibold text-gray-900 dark:text-white mb-4">버전 상태 개요</h2>
              <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
                {versionStatus.map((status) => (
                  <div
                    key={status.collection_type}
                    className={cn(
                      "bg-white dark:bg-gray-800 rounded-2xl border shadow-lg p-5 transition-all duration-200",
                      status.needs_update
                        ? 'border-orange-300 dark:border-orange-700'
                        : 'border-green-300 dark:border-green-700'
                    )}
                  >
                    <div className="flex items-start justify-between mb-4">
                      <div>
                        <h3 className="text-base font-semibold text-gray-900 dark:text-white mb-1">
                          {status.collection_type === 'DSC_CRL'
                            ? 'DSC/CRL Collection'
                            : status.collection_type === 'DSC_NC'
                            ? 'DSC_NC (Non-Conformant)'
                            : 'CSCA Master List'}
                        </h3>
                      </div>
                      {status.needs_update ? (
                        <AlertCircle className="w-5 h-5 text-orange-500" />
                      ) : (
                        <CheckCircle className="w-5 h-5 text-green-500" />
                      )}
                    </div>

                    <div className="space-y-3">
                      {/* Status Badge */}
                      <div>
                        <span
                          className={cn(
                            "inline-flex items-center gap-2 px-2.5 py-1 rounded-full border text-xs font-medium",
                            status.status === 'UPDATE_NEEDED'
                              ? 'bg-orange-100 dark:bg-orange-900/30 text-orange-800 dark:text-orange-300 border-orange-300 dark:border-orange-700'
                              : status.status === 'UP_TO_DATE'
                              ? 'bg-green-100 dark:bg-green-900/30 text-green-800 dark:text-green-300 border-green-300 dark:border-green-700'
                              : 'bg-gray-100 dark:bg-gray-700 text-gray-800 dark:text-gray-300 border-gray-300 dark:border-gray-600'
                          )}
                        >
                          {status.status === 'UPDATE_NEEDED' ? '업데이트 필요' : '최신 상태'}
                        </span>
                      </div>

                      {/* Version Comparison */}
                      <div className="space-y-2">
                        <div className="flex items-center justify-between">
                          <span className="text-sm text-gray-600 dark:text-gray-400">감지된 버전</span>
                          <span className="text-lg font-bold text-blue-600 dark:text-blue-400">
                            {status.detected_version.toString().padStart(6, '0')}
                          </span>
                        </div>
                        <div className="flex items-center justify-between">
                          <span className="text-sm text-gray-600 dark:text-gray-400">업로드된 버전</span>
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
                            <span className="text-sm text-gray-600 dark:text-gray-400">버전 차이</span>
                            <span className="text-base font-bold text-orange-600 dark:text-orange-400">
                              +{status.version_diff}
                            </span>
                          </div>
                        )}
                      </div>

                      {/* Status Message */}
                      <div className="pt-3 border-t border-gray-200 dark:border-gray-700">
                        <p className="text-sm text-gray-600 dark:text-gray-400">{status.status_message}</p>
                        {status.upload_timestamp !== 'N/A' && (
                          <p className="text-xs text-gray-500 dark:text-gray-500 mt-1">
                            마지막 업로드: {formatTimestamp(status.upload_timestamp)}
                          </p>
                        )}
                      </div>

                      {/* Download Link for Updates */}
                      {status.needs_update && (
                        <div className="pt-3 border-t border-gray-200 dark:border-gray-700">
                          <a
                            href="https://pkddownloadsg.icao.int/"
                            target="_blank"
                            rel="noopener noreferrer"
                            className="inline-flex items-center gap-2 text-sm text-blue-600 dark:text-blue-400 hover:text-blue-700 dark:hover:text-blue-300 font-medium"
                          >
                            <Download className="w-4 h-4" />
                            ICAO 포털에서 다운로드
                          </a>
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
            <h2 className="text-lg font-semibold text-gray-900 dark:text-white mb-4">버전 감지 이력</h2>
            <div className="bg-white dark:bg-gray-800 rounded-2xl border border-gray-200 dark:border-gray-700 shadow-lg overflow-hidden">
              <div className="overflow-x-auto">
                <table className="min-w-full divide-y divide-gray-200 dark:divide-gray-700">
                  <thead className="bg-gray-50 dark:bg-gray-800">
                    <tr>
                      <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                        Collection
                      </th>
                      <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                        File Name
                      </th>
                      <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                        Version
                      </th>
                      <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                        Status
                      </th>
                      <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                        Detected At
                      </th>
                    </tr>
                  </thead>
                  <tbody className="bg-white dark:bg-gray-800 divide-y divide-gray-200 dark:divide-gray-700">
                    {versionHistory.length === 0 ? (
                      <tr>
                        <td colSpan={5} className="px-6 py-8 text-center text-gray-500 dark:text-gray-400">
                          버전 이력이 없습니다
                        </td>
                      </tr>
                    ) : (
                      versionHistory.map((version) => (
                        <tr key={version.id} className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors">
                          <td className="px-6 py-4 whitespace-nowrap">
                            <span className="text-sm font-medium text-gray-900 dark:text-white">
                              {version.collection_type === 'DSC_CRL'
                                ? 'DSC/CRL'
                                : version.collection_type === 'DSC_NC'
                                ? 'DSC_NC'
                                : 'Master List'}
                            </span>
                          </td>
                          <td className="px-6 py-4 whitespace-nowrap">
                            <span className="text-sm text-gray-700 dark:text-gray-300 font-mono">
                              {version.file_name}
                            </span>
                          </td>
                          <td className="px-6 py-4 whitespace-nowrap">
                            <span className="text-sm font-semibold text-blue-600 dark:text-blue-400">
                              {version.file_version.toString().padStart(6, '0')}
                            </span>
                          </td>
                          <td className="px-6 py-4 whitespace-nowrap">
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
                          <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-500 dark:text-gray-400">
                            {formatTimestamp(version.detected_at)}
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
            <h3 className="text-base font-semibold text-blue-900 dark:text-blue-300 mb-3">ICAO Auto Sync 정보</h3>
            <div className="text-sm text-blue-800 dark:text-blue-300 space-y-3">
              <p>
                이 시스템은 ICAO PKD 포털을 자동으로 모니터링하여 새로운 인증서 버전을 감지합니다.
                새 버전이 감지되면 관리자에게 알림을 보내며, 수동으로 다운로드 및 가져오기를 수행해야 합니다.
              </p>
              <div>
                <h4 className="font-semibold mb-2">상태 수명주기:</h4>
                <ul className="list-disc list-inside space-y-1 ml-2">
                  <li><strong>DETECTED</strong>: ICAO 포털에서 새 버전 발견</li>
                  <li><strong>NOTIFIED</strong>: 관리자에게 알림 전송 완료</li>
                  <li><strong>DOWNLOADED</strong>: 포털에서 파일 다운로드 완료</li>
                  <li><strong>IMPORTED</strong>: Local PKD로 가져오기 성공</li>
                  <li><strong>FAILED</strong>: 가져오기 실패 (오류 메시지 확인)</li>
                </ul>
              </div>
              <div className="pt-3 border-t border-blue-300 dark:border-blue-700">
                <p className="text-xs">
                  <strong>참고</strong>: 현재 구현은 Tier 1 (수동 다운로드)입니다.
                  ICAO 이용 약관 준수를 위해 자동 다운로드 기능은 활성화되지 않았습니다.
                </p>
              </div>
            </div>
          </div>
        </>
      )}
    </div>
  );
}
