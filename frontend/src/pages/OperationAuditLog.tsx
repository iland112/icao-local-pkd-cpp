import { useState, useEffect } from 'react';
import {
  ShieldCheck,
  Filter,
  User,
  CheckCircle,
  XCircle,
  Clock,
  Search,
  ChevronLeft,
  ChevronRight,
  Eye,
  FileText,
  Download,
  Activity,
  Trash2,
} from 'lucide-react';
import {
  getAuditLogs,
  getAuditStatistics,
  type AuditLogEntry,
  type AuditLogQueryParams,
  type OperationType,
} from '@/services/auditApi';
import { cn } from '@/utils/cn';

// Operation type labels
const OPERATION_TYPE_LABELS: Record<OperationType, string> = {
  FILE_UPLOAD: '파일 업로드',
  CERT_EXPORT: '인증서 내보내기',
  UPLOAD_DELETE: '업로드 삭제',
  PA_VERIFY: 'PA 검증',
  SYNC_TRIGGER: '동기화 트리거',
};

// Operation type icons
const OPERATION_TYPE_ICONS: Record<OperationType, React.ReactNode> = {
  FILE_UPLOAD: <FileText className="w-4 h-4" />,
  CERT_EXPORT: <Download className="w-4 h-4" />,
  UPLOAD_DELETE: <Trash2 className="w-4 h-4" />,
  PA_VERIFY: <ShieldCheck className="w-4 h-4" />,
  SYNC_TRIGGER: <Activity className="w-4 h-4" />,
};

export function OperationAuditLog() {
  // Data state
  const [auditLogs, setAuditLogs] = useState<AuditLogEntry[]>([]);
  const [loading, setLoading] = useState(true);
  const [total, setTotal] = useState(0);

  // Pagination
  const [page, setPage] = useState(0);
  const limit = 20;

  // Filters
  const [operationTypeFilter, setOperationTypeFilter] = useState<OperationType | ''>('');
  const [usernameFilter, setUsernameFilter] = useState('');
  const [successFilter, setSuccessFilter] = useState<'all' | 'success' | 'failure'>('all');
  const [startDate, setStartDate] = useState('');
  const [endDate, setEndDate] = useState('');

  // Statistics
  const [statistics, setStatistics] = useState({
    totalOperations: 0,
    successfulOperations: 0,
    failedOperations: 0,
    operationsByType: {} as Record<OperationType, number>,
    topUsers: [] as Array<{ username: string; operationCount: number }>,
    averageDurationMs: 0,
  });

  // Detail dialog
  const [selectedLog, setSelectedLog] = useState<AuditLogEntry | null>(null);
  const [dialogOpen, setDialogOpen] = useState(false);

  // Fetch audit logs
  const fetchAuditLogs = async () => {
    setLoading(true);
    try {
      const params: AuditLogQueryParams = {
        limit,
        offset: page * limit,
      };

      if (operationTypeFilter) {
        params.operationType = operationTypeFilter;
      }
      if (usernameFilter) {
        params.username = usernameFilter;
      }
      if (successFilter !== 'all') {
        params.success = successFilter === 'success';
      }
      if (startDate) {
        params.startDate = new Date(startDate).toISOString();
      }
      if (endDate) {
        params.endDate = new Date(endDate).toISOString();
      }

      const response = await getAuditLogs(params);
      setAuditLogs(response.data);
      setTotal(response.total);
    } catch (error) {
      console.error('Failed to fetch audit logs:', error);
    } finally {
      setLoading(false);
    }
  };

  // Fetch statistics
  const fetchStatistics = async () => {
    try {
      const response = await getAuditStatistics();
      setStatistics(response.data);
    } catch (error) {
      console.error('Failed to fetch audit statistics:', error);
    }
  };

  // Initial fetch
  useEffect(() => {
    fetchAuditLogs();
    fetchStatistics();
  }, [page]);

  // Refetch when filters change
  useEffect(() => {
    if (page === 0) {
      fetchAuditLogs();
    } else {
      setPage(0);
    }
  }, [operationTypeFilter, usernameFilter, successFilter, startDate, endDate]);

  // Format timestamp
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

  // Format duration
  const formatDuration = (ms: number | null) => {
    if (ms === null || ms === 0) return '-';
    if (ms < 1000) return `${ms}ms`;
    if (ms < 60000) return `${(ms / 1000).toFixed(2)}s`;
    return `${(ms / 60000).toFixed(2)}m`;
  };

  // Total pages
  const totalPages = Math.ceil(total / limit);

  return (
    <div className="min-h-screen bg-gray-50 dark:bg-gray-900 p-6">
      <div className="max-w-7xl mx-auto space-y-6">
        {/* Header */}
        <div className="bg-gradient-to-r from-blue-500 to-cyan-500 rounded-xl p-6 text-white">
          <div className="flex items-center space-x-3">
            <div className="p-2 bg-white/20 backdrop-blur-sm rounded-lg">
              <ShieldCheck className="w-8 h-8" />
            </div>
            <div>
              <h1 className="text-2xl font-bold">운영 감사 로그</h1>
              <p className="text-sm text-blue-100 mt-1">시스템 작업 추적 및 모니터링</p>
            </div>
          </div>
        </div>

        {/* Statistics Cards */}
        <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
          {/* Total Operations */}
          <div className="bg-white dark:bg-gray-800 rounded-xl p-6 border border-gray-200 dark:border-gray-700">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-gray-600 dark:text-gray-400">총 작업</p>
                <p className="text-2xl font-bold text-gray-900 dark:text-white mt-1">
                  {(statistics.totalOperations ?? 0).toLocaleString()}
                </p>
              </div>
              <Activity className="w-8 h-8 text-blue-500" />
            </div>
          </div>

          {/* Successful Operations */}
          <div className="bg-white dark:bg-gray-800 rounded-xl p-6 border border-gray-200 dark:border-gray-700">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-gray-600 dark:text-gray-400">성공</p>
                <p className="text-2xl font-bold text-green-600 dark:text-green-400 mt-1">
                  {(statistics.successfulOperations ?? 0).toLocaleString()}
                </p>
              </div>
              <CheckCircle className="w-8 h-8 text-green-500" />
            </div>
          </div>

          {/* Failed Operations */}
          <div className="bg-white dark:bg-gray-800 rounded-xl p-6 border border-gray-200 dark:border-gray-700">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-gray-600 dark:text-gray-400">실패</p>
                <p className="text-2xl font-bold text-red-600 dark:text-red-400 mt-1">
                  {(statistics.failedOperations ?? 0).toLocaleString()}
                </p>
              </div>
              <XCircle className="w-8 h-8 text-red-500" />
            </div>
          </div>

          {/* Average Duration */}
          <div className="bg-white dark:bg-gray-800 rounded-xl p-6 border border-gray-200 dark:border-gray-700">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-gray-600 dark:text-gray-400">평균 소요시간</p>
                <p className="text-2xl font-bold text-purple-600 dark:text-purple-400 mt-1">
                  {formatDuration(statistics.averageDurationMs)}
                </p>
              </div>
              <Clock className="w-8 h-8 text-purple-500" />
            </div>
          </div>
        </div>

        {/* Filters */}
        <div className="bg-white dark:bg-gray-800 rounded-xl p-6 border border-gray-200 dark:border-gray-700">
          <div className="flex items-center space-x-2 mb-4">
            <Filter className="w-5 h-5 text-gray-600 dark:text-gray-400" />
            <h2 className="text-lg font-semibold text-gray-900 dark:text-white">필터</h2>
          </div>

          <div className="grid grid-cols-1 md:grid-cols-3 lg:grid-cols-5 gap-4">
            {/* Operation Type Filter */}
            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                작업 유형
              </label>
              <select
                value={operationTypeFilter}
                onChange={(e) => setOperationTypeFilter(e.target.value as OperationType | '')}
                className="w-full px-3 py-2 bg-white dark:bg-gray-700 border border-gray-300 dark:border-gray-600 rounded-lg text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500"
              >
                <option value="">전체</option>
                {Object.entries(OPERATION_TYPE_LABELS).map(([key, label]) => (
                  <option key={key} value={key}>
                    {label}
                  </option>
                ))}
              </select>
            </div>

            {/* Username Filter */}
            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                사용자
              </label>
              <div className="relative">
                <Search className="absolute left-3 top-1/2 transform -translate-y-1/2 w-4 h-4 text-gray-400" />
                <input
                  type="text"
                  value={usernameFilter}
                  onChange={(e) => setUsernameFilter(e.target.value)}
                  placeholder="사용자 이름..."
                  className="w-full pl-10 pr-3 py-2 bg-white dark:bg-gray-700 border border-gray-300 dark:border-gray-600 rounded-lg text-gray-900 dark:text-white placeholder-gray-400 focus:ring-2 focus:ring-blue-500"
                />
              </div>
            </div>

            {/* Success Filter */}
            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                결과
              </label>
              <select
                value={successFilter}
                onChange={(e) => setSuccessFilter(e.target.value as 'all' | 'success' | 'failure')}
                className="w-full px-3 py-2 bg-white dark:bg-gray-700 border border-gray-300 dark:border-gray-600 rounded-lg text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500"
              >
                <option value="all">전체</option>
                <option value="success">성공</option>
                <option value="failure">실패</option>
              </select>
            </div>

            {/* Start Date Filter */}
            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                시작 날짜
              </label>
              <input
                type="date"
                value={startDate}
                onChange={(e) => setStartDate(e.target.value)}
                className="w-full px-3 py-2 bg-white dark:bg-gray-700 border border-gray-300 dark:border-gray-600 rounded-lg text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500"
              />
            </div>

            {/* End Date Filter */}
            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                종료 날짜
              </label>
              <input
                type="date"
                value={endDate}
                onChange={(e) => setEndDate(e.target.value)}
                className="w-full px-3 py-2 bg-white dark:bg-gray-700 border border-gray-300 dark:border-gray-600 rounded-lg text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>
        </div>

        {/* Audit Log Table */}
        <div className="bg-white dark:bg-gray-800 rounded-xl border border-gray-200 dark:border-gray-700 overflow-hidden">
          <div className="overflow-x-auto">
            <table className="w-full">
              <thead className="bg-gray-50 dark:bg-gray-700/50 border-b border-gray-200 dark:border-gray-600">
                <tr>
                  <th className="px-4 py-3 text-left text-xs font-medium text-gray-600 dark:text-gray-300 uppercase tracking-wider">
                    시간
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-gray-600 dark:text-gray-300 uppercase tracking-wider">
                    사용자
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-gray-600 dark:text-gray-300 uppercase tracking-wider">
                    작업 유형
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-gray-600 dark:text-gray-300 uppercase tracking-wider">
                    리소스
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-gray-600 dark:text-gray-300 uppercase tracking-wider">
                    IP 주소
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-gray-600 dark:text-gray-300 uppercase tracking-wider">
                    결과
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-gray-600 dark:text-gray-300 uppercase tracking-wider">
                    소요시간
                  </th>
                  <th className="px-4 py-3 text-center text-xs font-medium text-gray-600 dark:text-gray-300 uppercase tracking-wider">
                    상세
                  </th>
                </tr>
              </thead>
              <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
                {loading ? (
                  <tr>
                    <td colSpan={8} className="px-4 py-8 text-center">
                      <div className="flex items-center justify-center space-x-2 text-gray-500 dark:text-gray-400">
                        <Clock className="w-5 h-5 animate-spin" />
                        <span>로딩 중...</span>
                      </div>
                    </td>
                  </tr>
                ) : auditLogs.length === 0 ? (
                  <tr>
                    <td colSpan={8} className="px-4 py-8 text-center text-gray-500 dark:text-gray-400">
                      감사 로그가 없습니다.
                    </td>
                  </tr>
                ) : (
                  auditLogs.map((log) => (
                    <tr key={log.id} className="hover:bg-gray-50 dark:hover:bg-gray-700/50">
                      <td className="px-4 py-3 text-sm text-gray-900 dark:text-gray-100">
                        {formatTimestamp(log.createdAt)}
                      </td>
                      <td className="px-4 py-3 text-sm">
                        <div className="flex items-center space-x-2">
                          <User className="w-4 h-4 text-gray-400" />
                          <span className="text-gray-900 dark:text-gray-100">
                            {log.username || '익명'}
                          </span>
                        </div>
                      </td>
                      <td className="px-4 py-3 text-sm">
                        <div className="flex items-center space-x-2">
                          {OPERATION_TYPE_ICONS[log.operationType]}
                          <span className="text-gray-900 dark:text-gray-100">
                            {OPERATION_TYPE_LABELS[log.operationType]}
                          </span>
                        </div>
                      </td>
                      <td className="px-4 py-3 text-sm text-gray-600 dark:text-gray-400">
                        {log.resourceType || '-'}
                      </td>
                      <td className="px-4 py-3 text-sm text-gray-600 dark:text-gray-400 font-mono">
                        {log.ipAddress || '-'}
                      </td>
                      <td className="px-4 py-3 text-sm">
                        {log.success ? (
                          <span className="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium bg-green-100 text-green-800 dark:bg-green-900/30 dark:text-green-400">
                            <CheckCircle className="w-3 h-3 mr-1" />
                            성공
                          </span>
                        ) : (
                          <span className="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium bg-red-100 text-red-800 dark:bg-red-900/30 dark:text-red-400">
                            <XCircle className="w-3 h-3 mr-1" />
                            실패
                          </span>
                        )}
                      </td>
                      <td className="px-4 py-3 text-sm text-gray-600 dark:text-gray-400 font-mono">
                        {formatDuration(log.durationMs)}
                      </td>
                      <td className="px-4 py-3 text-center">
                        <button
                          onClick={() => {
                            setSelectedLog(log);
                            setDialogOpen(true);
                          }}
                          className="inline-flex items-center justify-center w-8 h-8 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 text-gray-600 dark:text-gray-400 hover:text-blue-600 dark:hover:text-blue-400 transition-colors"
                        >
                          <Eye className="w-4 h-4" />
                        </button>
                      </td>
                    </tr>
                  ))
                )}
              </tbody>
            </table>
          </div>

          {/* Pagination */}
          {!loading && totalPages > 1 && (
            <div className="bg-gray-50 dark:bg-gray-700/50 px-6 py-4 border-t border-gray-200 dark:border-gray-600">
              <div className="flex items-center justify-between">
                <div className="text-sm text-gray-600 dark:text-gray-400">
                  총 {(total ?? 0).toLocaleString()}개 중 {page * limit + 1}-{Math.min((page + 1) * limit, total ?? 0)} 표시
                </div>
                <div className="flex items-center space-x-2">
                  <button
                    onClick={() => setPage(Math.max(0, page - 1))}
                    disabled={page === 0}
                    className={cn(
                      'px-3 py-2 rounded-lg border transition-colors',
                      page === 0
                        ? 'bg-gray-100 dark:bg-gray-700 border-gray-300 dark:border-gray-600 text-gray-400 dark:text-gray-500 cursor-not-allowed'
                        : 'bg-white dark:bg-gray-800 border-gray-300 dark:border-gray-600 text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700'
                    )}
                  >
                    <ChevronLeft className="w-4 h-4" />
                  </button>
                  <span className="text-sm text-gray-700 dark:text-gray-300">
                    {page + 1} / {totalPages}
                  </span>
                  <button
                    onClick={() => setPage(Math.min(totalPages - 1, page + 1))}
                    disabled={page >= totalPages - 1}
                    className={cn(
                      'px-3 py-2 rounded-lg border transition-colors',
                      page >= totalPages - 1
                        ? 'bg-gray-100 dark:bg-gray-700 border-gray-300 dark:border-gray-600 text-gray-400 dark:text-gray-500 cursor-not-allowed'
                        : 'bg-white dark:bg-gray-800 border-gray-300 dark:border-gray-600 text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700'
                    )}
                  >
                    <ChevronRight className="w-4 h-4" />
                  </button>
                </div>
              </div>
            </div>
          )}
        </div>

        {/* Detail Dialog */}
        {dialogOpen && selectedLog && (
          <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
            <div className="bg-white dark:bg-gray-800 rounded-xl shadow-2xl max-w-3xl w-full max-h-[90vh] overflow-y-auto">
              {/* Dialog Header */}
              <div className="sticky top-0 bg-gradient-to-r from-blue-500 to-cyan-500 px-6 py-4 rounded-t-xl">
                <div className="flex items-center justify-between">
                  <h3 className="text-xl font-bold text-white">감사 로그 상세 정보</h3>
                  <button
                    onClick={() => setDialogOpen(false)}
                    className="text-white/80 hover:text-white transition-colors"
                  >
                    <XCircle className="w-6 h-6" />
                  </button>
                </div>
              </div>

              {/* Dialog Content */}
              <div className="p-6 space-y-6">
                {/* Basic Information */}
                <div>
                  <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">기본 정보</h4>
                  <dl className="grid grid-cols-2 gap-4">
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">로그 ID</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 font-mono mt-1">{selectedLog.id}</dd>
                    </div>
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">생성 시간</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 mt-1">{formatTimestamp(selectedLog.createdAt)}</dd>
                    </div>
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">사용자 ID</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 font-mono mt-1">{selectedLog.userId || '-'}</dd>
                    </div>
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">사용자 이름</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 mt-1">{selectedLog.username || '익명'}</dd>
                    </div>
                  </dl>
                </div>

                {/* Operation Information */}
                <div>
                  <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">작업 정보</h4>
                  <dl className="grid grid-cols-2 gap-4">
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">작업 유형</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 mt-1">
                        <div className="flex items-center space-x-2">
                          {OPERATION_TYPE_ICONS[selectedLog.operationType]}
                          <span>{OPERATION_TYPE_LABELS[selectedLog.operationType]}</span>
                        </div>
                      </dd>
                    </div>
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">하위 유형</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 mt-1">{selectedLog.operationSubtype || '-'}</dd>
                    </div>
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">리소스 ID</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 font-mono mt-1 break-all">{selectedLog.resourceId || '-'}</dd>
                    </div>
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">리소스 유형</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 mt-1">{selectedLog.resourceType || '-'}</dd>
                    </div>
                  </dl>
                </div>

                {/* Request Information */}
                <div>
                  <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">요청 정보</h4>
                  <dl className="grid grid-cols-2 gap-4">
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">IP 주소</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 font-mono mt-1">{selectedLog.ipAddress || '-'}</dd>
                    </div>
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">요청 방식</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 font-mono mt-1">{selectedLog.requestMethod || '-'}</dd>
                    </div>
                    <div className="col-span-2">
                      <dt className="text-xs text-gray-500 dark:text-gray-400">요청 경로</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 font-mono mt-1 break-all">{selectedLog.requestPath || '-'}</dd>
                    </div>
                    <div className="col-span-2">
                      <dt className="text-xs text-gray-500 dark:text-gray-400">User Agent</dt>
                      <dd className="text-xs text-gray-900 dark:text-gray-100 font-mono mt-1 break-all">{selectedLog.userAgent || '-'}</dd>
                    </div>
                  </dl>
                </div>

                {/* Result Information */}
                <div>
                  <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">결과 정보</h4>
                  <dl className="grid grid-cols-2 gap-4">
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">성공 여부</dt>
                      <dd className="text-sm mt-1">
                        {selectedLog.success ? (
                          <span className="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium bg-green-100 text-green-800 dark:bg-green-900/30 dark:text-green-400">
                            <CheckCircle className="w-3 h-3 mr-1" />
                            성공
                          </span>
                        ) : (
                          <span className="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium bg-red-100 text-red-800 dark:bg-red-900/30 dark:text-red-400">
                            <XCircle className="w-3 h-3 mr-1" />
                            실패
                          </span>
                        )}
                      </dd>
                    </div>
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">상태 코드</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 font-mono mt-1">{selectedLog.statusCode || '-'}</dd>
                    </div>
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">소요 시간</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 font-mono mt-1">{formatDuration(selectedLog.durationMs)}</dd>
                    </div>
                    {selectedLog.errorMessage && (
                      <div className="col-span-2">
                        <dt className="text-xs text-gray-500 dark:text-gray-400">오류 메시지</dt>
                        <dd className="text-sm text-red-600 dark:text-red-400 mt-1 break-all">{selectedLog.errorMessage}</dd>
                      </div>
                    )}
                  </dl>
                </div>

                {/* Metadata */}
                {selectedLog.metadata && Object.keys(selectedLog.metadata).length > 0 && (
                  <div>
                    <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">메타데이터</h4>
                    <div className="bg-gray-50 dark:bg-gray-900 rounded-lg p-4 overflow-x-auto">
                      <pre className="text-xs text-gray-900 dark:text-gray-100 font-mono">
                        {JSON.stringify(selectedLog.metadata, null, 2)}
                      </pre>
                    </div>
                  </div>
                )}
              </div>

              {/* Dialog Footer */}
              <div className="bg-gray-50 dark:bg-gray-700/50 px-6 py-4 rounded-b-xl">
                <button
                  onClick={() => setDialogOpen(false)}
                  className="w-full px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg font-medium transition-colors"
                >
                  닫기
                </button>
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
