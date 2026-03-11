import { useTranslation } from 'react-i18next';
import { useState, useEffect } from 'react';
import { DEFAULT_PAGE_SIZE } from '@/config/pagination';
import { useSortableTable } from '@/hooks/useSortableTable';
import { SortableHeader } from '@/components/common/SortableHeader';
import { Shield, Filter, User, Activity, CheckCircle, XCircle, X, Clock, ChevronLeft, ChevronRight, Eye } from 'lucide-react';
import { createAuthenticatedClient } from '@/services/authApi';
import { formatDateTime } from '@/utils/dateFormat';

const authClient = createAuthenticatedClient('/api/auth');

interface AuditLogEntry {
  id: string;
  userId: string;
  username: string;
  eventType: string;
  ipAddress: string;
  userAgent: string;
  success: boolean;
  errorMessage?: string;
  createdAt: string;
}

interface AuditStats {
  totalEvents: number;
  byEventType: Record<string, number>;
  topUsers: { username: string; count: number }[];
  failedLogins: number;
  last24hEvents: number;
}

export function AuditLog() {
  const { t } = useTranslation(['admin', 'common']);
  const [logs, setLogs] = useState<AuditLogEntry[]>([]);
  const [stats, setStats] = useState<AuditStats | null>(null);
  const [loading, setLoading] = useState(true);
  const [total, setTotal] = useState(0);

  // Filters
  const [username, setUsername] = useState('');
  const [eventType, setEventType] = useState('');
  const [successFilter, setSuccessFilter] = useState('');

  // Pagination
  const [page, setPage] = useState(1);
  const limit = DEFAULT_PAGE_SIZE;

  // Detail Dialog
  const [selectedLog, setSelectedLog] = useState<AuditLogEntry | null>(null);
  const [dialogOpen, setDialogOpen] = useState(false);

  const { sortedData: sortedLogs, sortConfig: logSortConfig, requestSort: requestLogSort } = useSortableTable<AuditLogEntry>(logs);

  useEffect(() => {
    fetchAuditLogs();
    fetchStats();
  }, [page, username, eventType, successFilter]);

  const fetchAuditLogs = async () => {
    try {
      setLoading(true);
      const offset = (page - 1) * limit;

      const params: Record<string, string> = {
        limit: limit.toString(),
        offset: offset.toString(),
      };

      if (username) params.username = username;
      if (eventType) params.event_type = eventType;
      if (successFilter) params.success = successFilter;

      const { data } = await authClient.get('/audit-log', { params });
      setLogs(data.logs || []);
      setTotal(data.total || 0);
    } catch (error) {
      if (import.meta.env.DEV) console.error('Error fetching audit logs:', error);
    } finally {
      setLoading(false);
    }
  };

  const fetchStats = async () => {
    try {
      const { data } = await authClient.get('/audit-log/stats');
      setStats(data.stats || null);
    } catch (error) {
      if (import.meta.env.DEV) console.error('Error fetching stats:', error);
    }
  };

  const handleReset = () => {
    setUsername('');
    setEventType('');
    setSuccessFilter('');
    setPage(1);
  };

  const totalPages = Math.ceil(total / limit);

  const getEventBadgeColor = (eventType: string) => {
    if (eventType === 'LOGIN') return 'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-300';
    if (eventType === 'LOGIN_FAILED') return 'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-300';
    if (eventType === 'LOGOUT') return 'bg-yellow-100 text-yellow-700 dark:bg-yellow-900/30 dark:text-yellow-300';
    if (eventType === 'TOKEN_REFRESH') return 'bg-purple-100 text-purple-700 dark:bg-purple-900/30 dark:text-purple-300';
    return 'bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-300';
  };

  return (
    <div className="w-full px-4 lg:px-6 py-4 space-y-6">
      {/* Header */}
      <div className="mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-blue-500 to-indigo-600 shadow-lg">
            <Shield className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">
              {t('nav.header.loginHistory')}
            </h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              사용자 인증 및 활동 로그
            </p>
          </div>
        </div>
      </div>

      {/* Statistics Cards */}
      {stats && (
        <div className="grid grid-cols-2 lg:grid-cols-4 gap-4">
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-4">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 bg-blue-100 dark:bg-blue-900/30 rounded-lg flex items-center justify-center">
                <Activity className="w-5 h-5 text-blue-600 dark:text-blue-400" />
              </div>
              <div>
                <p className="text-2xl font-bold text-gray-900 dark:text-white">
                  {(stats.totalEvents ?? 0).toLocaleString()}
                </p>
                <p className="text-sm text-gray-600 dark:text-gray-400">{t('auditLog.allEventTypes')}</p>
              </div>
            </div>
          </div>

          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-4">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 bg-red-100 dark:bg-red-900/30 rounded-lg flex items-center justify-center">
                <XCircle className="w-5 h-5 text-red-600 dark:text-red-400" />
              </div>
              <div>
                <p className="text-2xl font-bold text-gray-900 dark:text-white">
                  {(stats.failedLogins ?? 0).toLocaleString()}
                </p>
                <p className="text-sm text-gray-600 dark:text-gray-400">{ t('admin:auditLog.loginFailed') }</p>
              </div>
            </div>
          </div>

          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-4">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 bg-green-100 dark:bg-green-900/30 rounded-lg flex items-center justify-center">
                <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400" />
              </div>
              <div>
                <p className="text-2xl font-bold text-gray-900 dark:text-white">
                  {(stats.last24hEvents ?? 0).toLocaleString()}
                </p>
                <p className="text-sm text-gray-600 dark:text-gray-400">최근 24시간</p>
              </div>
            </div>
          </div>

          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-4">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 bg-purple-100 dark:bg-purple-900/30 rounded-lg flex items-center justify-center">
                <Clock className="w-5 h-5 text-purple-600 dark:text-purple-400" />
              </div>
              <div>
                <p className="text-2xl font-bold text-gray-900 dark:text-white">
                  {stats.topUsers?.length ?? 0}
                </p>
                <p className="text-sm text-gray-600 dark:text-gray-400">{ t('admin:userManagement.activeUsers') }</p>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Filter Card */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-6">
        <div className="flex items-center gap-2 mb-4">
          <Filter className="w-5 h-5 text-gray-400" />
          <h2 className="text-lg font-semibold text-gray-900 dark:text-white">{ t('common:label.filter') }</h2>
        </div>

        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
          <div>
            <label htmlFor="audit-username" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              {t('common.label.username')}
            </label>
            <input
              id="audit-username"
              name="username"
              type="text"
              value={username}
              onChange={(e) => setUsername(e.target.value)}
              placeholder="사용자명 입력..."
              className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label htmlFor="audit-event-type" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              이벤트 타입
            </label>
            <select
              id="audit-event-type"
              name="eventType"
              value={eventType}
              onChange={(e) => setEventType(e.target.value)}
              className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="">{ t('monitoring:pool.total') }</option>
              <option value="LOGIN">{ t('admin:auditLog.loginSuccess') }</option>
              <option value="LOGIN_FAILED">{ t('admin:auditLog.loginFailed') }</option>
              <option value="LOGOUT">{ t('admin:auditLog.logout') }</option>
              <option value="TOKEN_REFRESH">{ t('admin:auditLog.tokenRefresh') }</option>
            </select>
          </div>

          <div>
            <label htmlFor="audit-success" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              성공 여부
            </label>
            <select
              id="audit-success"
              name="successFilter"
              value={successFilter}
              onChange={(e) => setSuccessFilter(e.target.value)}
              className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="">{ t('monitoring:pool.total') }</option>
              <option value="true">{t('common:toast.success')}</option>
              <option value="false">{t('common:status.failed')}</option>
            </select>
          </div>

          <div className="flex items-end">
            <button
              onClick={handleReset}
              className="w-full px-4 py-2 bg-gray-100 dark:bg-gray-700 text-gray-700 dark:text-gray-300 rounded-lg hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors"
            >
              {t('common.button.reset')}
            </button>
          </div>
        </div>
      </div>

      {/* Audit Log Table */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 overflow-hidden">
        <div className="overflow-x-auto">
          <table className="w-full">
            <thead className="bg-slate-100 dark:bg-gray-700">
              <tr>
                <SortableHeader label={t('admin:operationAudit.timestamp')} sortKey="createdAt" sortConfig={logSortConfig} onSort={requestLogSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                <SortableHeader label={t('common:label.user')} sortKey="username" sortConfig={logSortConfig} onSort={requestLogSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                <SortableHeader label="이벤트" sortKey="eventType" sortConfig={logSortConfig} onSort={requestLogSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                <SortableHeader label={t('admin:operationAudit.ipAddress')} sortKey="ipAddress" sortConfig={logSortConfig} onSort={requestLogSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                <SortableHeader label={t('common:label.status')} sortKey="success" sortConfig={logSortConfig} onSort={requestLogSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                  {t('upload.history.detail')}
                </th>
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
              {loading ? (
                <tr>
                  <td colSpan={6} className="px-3 py-8 text-center text-xs text-gray-500 dark:text-gray-400">
                    {t('common.button.loading')}
                  </td>
                </tr>
              ) : logs.length === 0 ? (
                <tr>
                  <td colSpan={6} className="px-3 py-8 text-center text-xs text-gray-500 dark:text-gray-400">
                    로그가 없습니다.
                  </td>
                </tr>
              ) : (
                sortedLogs.map((log) => (
                  <tr key={log.id} className="hover:bg-gray-50 dark:hover:bg-gray-900/30">
                    <td className="px-3 py-2.5 whitespace-nowrap text-xs text-gray-900 dark:text-white">
                      {formatDateTime(log.createdAt)}
                    </td>
                    <td className="px-3 py-2.5 whitespace-nowrap">
                      <div className="flex items-center gap-1.5">
                        <User className="w-3.5 h-3.5 text-gray-400" />
                        <span className="text-xs font-medium text-gray-900 dark:text-white">
                          {log.username || '시스템'}
                        </span>
                      </div>
                    </td>
                    <td className="px-3 py-2.5 whitespace-nowrap">
                      <span className={`px-2 py-0.5 text-xs font-medium rounded-full ${getEventBadgeColor(log.eventType)}`}>
                        {log.eventType}
                      </span>
                    </td>
                    <td className="px-3 py-2.5 whitespace-nowrap text-xs text-gray-600 dark:text-gray-400 font-mono">
                      {log.ipAddress}
                    </td>
                    <td className="px-3 py-2.5 whitespace-nowrap">
                      {log.success ? (
                        <span className="flex items-center gap-1 text-xs text-green-600 dark:text-green-400">
                          <CheckCircle className="w-3.5 h-3.5" />
                          {t('sync.reconciliation.successCount')}
                        </span>
                      ) : (
                        <span className="flex items-center gap-1 text-xs text-red-600 dark:text-red-400">
                          <XCircle className="w-3.5 h-3.5" />
                          {t('upload.statistics.totalFailed')}
                        </span>
                      )}
                    </td>
                    <td className="px-3 py-2.5 text-center">
                      <button
                        onClick={() => {
                          setSelectedLog(log);
                          setDialogOpen(true);
                        }}
                        className="inline-flex items-center justify-center w-7 h-7 rounded-md hover:bg-gray-100 dark:hover:bg-gray-700 text-gray-600 dark:text-gray-400 hover:text-blue-600 dark:hover:text-blue-400 transition-colors"
                        aria-label={t('common:button.viewDetail')}
                      >
                        <Eye className="w-3.5 h-3.5" />
                      </button>
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>

        {/* Pagination */}
        {totalPages > 1 && (
          <div className="px-6 py-4 border-t border-gray-200 dark:border-gray-700 flex items-center justify-between">
            <div className="text-sm text-gray-600 dark:text-gray-400">
              전체 {total.toLocaleString()}개 중 {((page - 1) * limit) + 1}-{Math.min(page * limit, total)}
            </div>
            <div className="flex items-center gap-2">
              <button
                onClick={() => setPage(page - 1)}
                disabled={page === 1}
                className="px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
                aria-label={t('common:button.prev_page')}
              >
                <ChevronLeft className="w-4 h-4" />
              </button>
              <span className="px-4 py-2 text-sm text-gray-700 dark:text-gray-300">
                {page} / {totalPages}
              </span>
              <button
                onClick={() => setPage(page + 1)}
                disabled={page === totalPages}
                className="px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
                aria-label={t('common:button.next_page')}
              >
                <ChevronRight className="w-4 h-4" />
              </button>
            </div>
          </div>
        )}
      </div>

      {/* Detail Dialog */}
      {dialogOpen && selectedLog && (
        <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-[70] p-4">
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-xl max-w-xl w-full">
            {/* Dialog Header */}
            <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center justify-between">
              <div className="flex items-center gap-2.5">
                <div className="p-1.5 rounded-lg bg-purple-100 dark:bg-purple-900/30">
                  <Shield className="w-4 h-4 text-purple-600 dark:text-purple-400" />
                </div>
                <h3 className="text-base font-bold text-gray-900 dark:text-white">{t('auditLog.title')}</h3>
                <span className={`px-2 py-0.5 text-xs font-medium rounded-full ${getEventBadgeColor(selectedLog.eventType)}`}>
                  {selectedLog.eventType}
                </span>
                {selectedLog.success ? (
                  <span className="inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400">
                    <CheckCircle className="w-3 h-3 mr-1" />성공
                  </span>
                ) : (
                  <span className="inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400">
                    <XCircle className="w-3 h-3 mr-1" />실패
                  </span>
                )}
              </div>
              <button
                onClick={() => setDialogOpen(false)}
                className="p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              >
                <X className="w-4 h-4 text-gray-500" />
              </button>
            </div>

            {/* Dialog Content — dense layout */}
            <div className="px-5 py-4 space-y-3">
              <div className="grid grid-cols-2 lg:grid-cols-4 gap-x-4 gap-y-2">
                <div>
                  <dt className="text-[11px] text-gray-500 dark:text-gray-400">{ t('admin:operationAudit.timestamp') }</dt>
                  <dd className="text-xs text-gray-900 dark:text-gray-100 mt-0.5">{formatDateTime(selectedLog.createdAt)}</dd>
                </div>
                <div>
                  <dt className="text-[11px] text-gray-500 dark:text-gray-400">{ t('common:label.user') }</dt>
                  <dd className="text-xs text-gray-900 dark:text-gray-100 mt-0.5">{selectedLog.username || '시스템'}</dd>
                </div>
                <div>
                  <dt className="text-[11px] text-gray-500 dark:text-gray-400">{t('auditLog.ipAddress')}</dt>
                  <dd className="text-xs text-gray-900 dark:text-gray-100 font-mono mt-0.5">{selectedLog.ipAddress || '-'}</dd>
                </div>
                <div>
                  <dt className="text-[11px] text-gray-500 dark:text-gray-400">{t('common.label.userId')}</dt>
                  <dd className="text-xs text-gray-900 dark:text-gray-100 font-mono mt-0.5 truncate" title={selectedLog.userId || '-'}>{selectedLog.userId || '-'}</dd>
                </div>
              </div>

              <hr className="border-gray-200 dark:border-gray-700" />

              <div>
                <dt className="text-[11px] text-gray-500 dark:text-gray-400">{t('common.label.logId')}</dt>
                <dd className="text-xs text-gray-900 dark:text-gray-100 font-mono mt-0.5 truncate" title={selectedLog.id}>{selectedLog.id}</dd>
              </div>

              {selectedLog.userAgent && (
                <>
                  <hr className="border-gray-200 dark:border-gray-700" />
                  <div>
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">{t('auditLog.userAgent')}</dt>
                    <dd className="text-[11px] text-gray-600 dark:text-gray-400 font-mono mt-0.5 truncate" title={selectedLog.userAgent}>{selectedLog.userAgent}</dd>
                  </div>
                </>
              )}

              {selectedLog.errorMessage && (
                <>
                  <hr className="border-gray-200 dark:border-gray-700" />
                  <div>
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">{t('upload.detail.errorMessage')}</dt>
                    <dd className="text-xs text-red-600 dark:text-red-400 mt-0.5 break-all">{selectedLog.errorMessage}</dd>
                  </div>
                </>
              )}
            </div>

            {/* Dialog Footer */}
            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex justify-end">
              <button
                onClick={() => setDialogOpen(false)}
                className="inline-flex items-center gap-1.5 px-4 py-1.5 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors border border-gray-200 dark:border-gray-600"
              >
                {t('icao.banner.dismiss')}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default AuditLog;
