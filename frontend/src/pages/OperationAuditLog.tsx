import { useState, useEffect } from 'react';
import { useSortableTable } from '@/hooks/useSortableTable';
import { SortableHeader } from '@/components/common/SortableHeader';
import {
  ShieldCheck,
  Filter,
  User,
  CheckCircle,
  XCircle,
  X,
  Clock,
  Search,
  ChevronLeft,
  ChevronRight,
  Eye,
  FileText,
  Download,
  Activity,
  Trash2,
  Upload,
  RefreshCw,
  KeyRound,
  Settings,
  UserPlus,
  UserCog,
  UserX,
  Lock,
  Globe,
  Database,
  BookOpen,
} from 'lucide-react';
import {
  getAuditLogs,
  getAuditStatistics,
  type AuditLogEntry,
  type AuditLogQueryParams,
  type OperationType,
} from '@/services/auditApi';
import { cn } from '@/utils/cn';
import { formatDateTime } from '@/utils/dateFormat';

// Operation type labels
const OPERATION_TYPE_LABELS: Record<OperationType, string> = {
  FILE_UPLOAD: '파일 업로드',
  CERT_UPLOAD: '인증서 업로드',
  CERT_EXPORT: '인증서 내보내기',
  UPLOAD_DELETE: '업로드 삭제',
  UPLOAD_RETRY: '업로드 재시도',
  PA_VERIFY: 'PA 검증',
  PA_PARSE_SOD: 'SOD 파싱',
  PA_PARSE_DG1: 'DG1 파싱',
  PA_PARSE_DG2: 'DG2 파싱',
  API_CLIENT_CREATE: 'API 클라이언트 생성',
  API_CLIENT_UPDATE: 'API 클라이언트 수정',
  API_CLIENT_DELETE: 'API 클라이언트 삭제',
  API_CLIENT_KEY_REGEN: 'API 키 재발급',
  CODE_MASTER_CREATE: '코드 마스터 생성',
  CODE_MASTER_UPDATE: '코드 마스터 수정',
  CODE_MASTER_DELETE: '코드 마스터 삭제',
  USER_CREATE: '사용자 생성',
  USER_UPDATE: '사용자 수정',
  USER_DELETE: '사용자 삭제',
  PASSWORD_CHANGE: '비밀번호 변경',
  ICAO_CHECK: 'ICAO 버전 확인',
  SYNC_TRIGGER: '동기화 트리거',
  SYNC_CHECK: '동기화 체크',
  RECONCILE: '재조정',
  REVALIDATE: '재검증',
  TRIGGER_DAILY_SYNC: '일일 동기화',
  CONFIG_UPDATE: '설정 변경',
};

// Operation type icons
const OPERATION_TYPE_ICONS: Record<OperationType, React.ReactNode> = {
  FILE_UPLOAD: <FileText className="w-3.5 h-3.5" />,
  CERT_UPLOAD: <Upload className="w-3.5 h-3.5" />,
  CERT_EXPORT: <Download className="w-3.5 h-3.5" />,
  UPLOAD_DELETE: <Trash2 className="w-3.5 h-3.5" />,
  UPLOAD_RETRY: <RefreshCw className="w-3.5 h-3.5" />,
  PA_VERIFY: <ShieldCheck className="w-3.5 h-3.5" />,
  PA_PARSE_SOD: <FileText className="w-3.5 h-3.5" />,
  PA_PARSE_DG1: <FileText className="w-3.5 h-3.5" />,
  PA_PARSE_DG2: <FileText className="w-3.5 h-3.5" />,
  API_CLIENT_CREATE: <KeyRound className="w-3.5 h-3.5" />,
  API_CLIENT_UPDATE: <KeyRound className="w-3.5 h-3.5" />,
  API_CLIENT_DELETE: <KeyRound className="w-3.5 h-3.5" />,
  API_CLIENT_KEY_REGEN: <KeyRound className="w-3.5 h-3.5" />,
  CODE_MASTER_CREATE: <BookOpen className="w-3.5 h-3.5" />,
  CODE_MASTER_UPDATE: <BookOpen className="w-3.5 h-3.5" />,
  CODE_MASTER_DELETE: <BookOpen className="w-3.5 h-3.5" />,
  USER_CREATE: <UserPlus className="w-3.5 h-3.5" />,
  USER_UPDATE: <UserCog className="w-3.5 h-3.5" />,
  USER_DELETE: <UserX className="w-3.5 h-3.5" />,
  PASSWORD_CHANGE: <Lock className="w-3.5 h-3.5" />,
  ICAO_CHECK: <Globe className="w-3.5 h-3.5" />,
  SYNC_TRIGGER: <Activity className="w-3.5 h-3.5" />,
  SYNC_CHECK: <Database className="w-3.5 h-3.5" />,
  RECONCILE: <RefreshCw className="w-3.5 h-3.5" />,
  REVALIDATE: <ShieldCheck className="w-3.5 h-3.5" />,
  TRIGGER_DAILY_SYNC: <Activity className="w-3.5 h-3.5" />,
  CONFIG_UPDATE: <Settings className="w-3.5 h-3.5" />,
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

  const { sortedData: sortedAuditLogs, sortConfig: auditSortConfig, requestSort: requestAuditSort } = useSortableTable<AuditLogEntry>(auditLogs);

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
      if (import.meta.env.DEV) console.error('Failed to fetch audit logs:', error);
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
      if (import.meta.env.DEV) console.error('Failed to fetch audit statistics:', error);
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
    <div className="w-full px-4 lg:px-6 py-4">
      <div className="space-y-6">
        {/* Header */}
        <div className="mb-6">
          <div className="flex items-center gap-4">
            <div className="p-3 rounded-xl bg-gradient-to-br from-blue-500 to-indigo-600 shadow-lg">
              <ShieldCheck className="w-7 h-7 text-white" />
            </div>
            <div className="flex-1">
              <h1 className="text-2xl font-bold text-gray-900 dark:text-white">운영 감사 로그</h1>
              <p className="text-sm text-gray-500 dark:text-gray-400">시스템 작업 추적 및 모니터링</p>
            </div>
          </div>
        </div>

        {/* Statistics Cards */}
        <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
          {/* Total Operations */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border border-gray-200 dark:border-gray-700">
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
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border border-gray-200 dark:border-gray-700">
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
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border border-gray-200 dark:border-gray-700">
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

          {/* Top Users */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border border-gray-200 dark:border-gray-700">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-gray-600 dark:text-gray-400">활성 사용자</p>
                <p className="text-2xl font-bold text-purple-600 dark:text-purple-400 mt-1">
                  {statistics.topUsers?.length ?? 0}
                </p>
              </div>
              <User className="w-8 h-8 text-purple-500" />
            </div>
          </div>
        </div>

        {/* Filters */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border border-gray-200 dark:border-gray-700">
          <div className="flex items-center space-x-2 mb-4">
            <Filter className="w-5 h-5 text-gray-600 dark:text-gray-400" />
            <h2 className="text-lg font-semibold text-gray-900 dark:text-white">필터</h2>
          </div>

          <div className="grid grid-cols-1 md:grid-cols-3 lg:grid-cols-5 gap-4">
            {/* Operation Type Filter */}
            <div>
              <label htmlFor="op-audit-type" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                작업 유형
              </label>
              <select
                id="op-audit-type"
                name="operationType"
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
              <label htmlFor="op-audit-user" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                사용자
              </label>
              <div className="relative">
                <Search className="absolute left-3 top-1/2 transform -translate-y-1/2 w-4 h-4 text-gray-400" />
                <input
                  id="op-audit-user"
                  name="username"
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
              <label htmlFor="op-audit-result" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                결과
              </label>
              <select
                id="op-audit-result"
                name="successFilter"
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
              <label htmlFor="op-audit-start" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                시작 날짜
              </label>
              <input
                id="op-audit-start"
                name="startDate"
                type="date"
                value={startDate}
                onChange={(e) => setStartDate(e.target.value)}
                className="w-full px-3 py-2 bg-white dark:bg-gray-700 border border-gray-300 dark:border-gray-600 rounded-lg text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500"
              />
            </div>

            {/* End Date Filter */}
            <div>
              <label htmlFor="op-audit-end" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                종료 날짜
              </label>
              <input
                id="op-audit-end"
                name="endDate"
                type="date"
                value={endDate}
                onChange={(e) => setEndDate(e.target.value)}
                className="w-full px-3 py-2 bg-white dark:bg-gray-700 border border-gray-300 dark:border-gray-600 rounded-lg text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>
        </div>

        {/* Audit Log Table */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 overflow-hidden">
          <div className="overflow-x-auto">
            <table className="w-full">
              <thead className="bg-slate-100 dark:bg-gray-700">
                <tr>
                  <SortableHeader label="시간" sortKey="createdAt" sortConfig={auditSortConfig} onSort={requestAuditSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                  <SortableHeader label="사용자" sortKey="username" sortConfig={auditSortConfig} onSort={requestAuditSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                  <SortableHeader label="작업 유형" sortKey="operationType" sortConfig={auditSortConfig} onSort={requestAuditSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                  <SortableHeader label="리소스" sortKey="resourceType" sortConfig={auditSortConfig} onSort={requestAuditSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                  <SortableHeader label="IP 주소" sortKey="ipAddress" sortConfig={auditSortConfig} onSort={requestAuditSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                  <SortableHeader label="결과" sortKey="success" sortConfig={auditSortConfig} onSort={requestAuditSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                  <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                    상세
                  </th>
                </tr>
              </thead>
              <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
                {loading ? (
                  <tr>
                    <td colSpan={7} className="px-3 py-8 text-center">
                      <div className="flex items-center justify-center space-x-2 text-xs text-gray-500 dark:text-gray-400">
                        <Clock className="w-4 h-4 animate-spin" />
                        <span>로딩 중...</span>
                      </div>
                    </td>
                  </tr>
                ) : auditLogs.length === 0 ? (
                  <tr>
                    <td colSpan={7} className="px-3 py-8 text-center text-xs text-gray-500 dark:text-gray-400">
                      감사 로그가 없습니다.
                    </td>
                  </tr>
                ) : (
                  sortedAuditLogs.map((log) => (
                    <tr key={log.id} className="hover:bg-gray-50 dark:hover:bg-gray-700/50">
                      <td className="px-3 py-2.5 text-xs text-gray-900 dark:text-gray-100">
                        {formatDateTime(log.createdAt)}
                      </td>
                      <td className="px-3 py-2.5 text-xs">
                        <div className="flex items-center space-x-1.5">
                          <User className="w-3.5 h-3.5 text-gray-400" />
                          <span className="text-gray-900 dark:text-gray-100">
                            {log.username || '익명'}
                          </span>
                        </div>
                      </td>
                      <td className="px-3 py-2.5 text-xs">
                        <div className="flex items-center space-x-1.5">
                          {OPERATION_TYPE_ICONS[log.operationType] || <Activity className="w-3.5 h-3.5" />}
                          <span className="text-gray-900 dark:text-gray-100">
                            {OPERATION_TYPE_LABELS[log.operationType] || log.operationType}
                          </span>
                        </div>
                      </td>
                      <td className="px-3 py-2.5 text-xs text-gray-600 dark:text-gray-400">
                        {log.resourceType || '-'}
                      </td>
                      <td className="px-3 py-2.5 text-xs text-gray-600 dark:text-gray-400 font-mono">
                        {log.ipAddress || '-'}
                      </td>
                      <td className="px-3 py-2.5 text-xs">
                        {log.success ? (
                          <span className="inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium bg-green-100 text-green-800 dark:bg-green-900/30 dark:text-green-400">
                            <CheckCircle className="w-3 h-3 mr-1" />
                            성공
                          </span>
                        ) : (
                          <span className="inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium bg-red-100 text-red-800 dark:bg-red-900/30 dark:text-red-400">
                            <XCircle className="w-3 h-3 mr-1" />
                            실패
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
          <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-[70] p-4">
            <div className="bg-white dark:bg-gray-800 rounded-xl shadow-xl max-w-2xl w-full">
              {/* Dialog Header */}
              <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center justify-between">
                <div className="flex items-center gap-2.5">
                  <div className="p-1.5 rounded-lg bg-blue-100 dark:bg-blue-900/30">
                    <ShieldCheck className="w-4 h-4 text-blue-600 dark:text-blue-400" />
                  </div>
                  <h3 className="text-base font-bold text-gray-900 dark:text-white">감사 로그 상세</h3>
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

              {/* Dialog Content — dense 4-column grid */}
              <div className="px-5 py-4 space-y-3">
                {/* Row 1: Basic + Operation */}
                <div className="grid grid-cols-4 gap-x-4 gap-y-2">
                  <div>
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">시간</dt>
                    <dd className="text-xs text-gray-900 dark:text-gray-100 mt-0.5">{formatDateTime(selectedLog.createdAt)}</dd>
                  </div>
                  <div>
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">사용자</dt>
                    <dd className="text-xs text-gray-900 dark:text-gray-100 mt-0.5">{selectedLog.username || '익명'}</dd>
                  </div>
                  <div>
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">작업 유형</dt>
                    <dd className="text-xs text-gray-900 dark:text-gray-100 mt-0.5 flex items-center gap-1">
                      {OPERATION_TYPE_ICONS[selectedLog.operationType]}
                      <span>{OPERATION_TYPE_LABELS[selectedLog.operationType]}</span>
                    </dd>
                  </div>
                  <div>
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">하위 유형</dt>
                    <dd className="text-xs text-gray-900 dark:text-gray-100 mt-0.5">{selectedLog.operationSubtype || '-'}</dd>
                  </div>
                </div>

                <hr className="border-gray-200 dark:border-gray-700" />

                {/* Row 2: IDs */}
                <div className="grid grid-cols-4 gap-x-4 gap-y-2">
                  <div className="col-span-2">
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">로그 ID</dt>
                    <dd className="text-xs text-gray-900 dark:text-gray-100 font-mono mt-0.5 truncate" title={String(selectedLog.id)}>{selectedLog.id}</dd>
                  </div>
                  <div>
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">리소스 유형</dt>
                    <dd className="text-xs text-gray-900 dark:text-gray-100 mt-0.5">{selectedLog.resourceType || '-'}</dd>
                  </div>
                  <div>
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">사용자 ID</dt>
                    <dd className="text-xs text-gray-900 dark:text-gray-100 font-mono mt-0.5 truncate" title={selectedLog.userId || '-'}>{selectedLog.userId || '-'}</dd>
                  </div>
                  {selectedLog.resourceId && (
                    <div className="col-span-4">
                      <dt className="text-[11px] text-gray-500 dark:text-gray-400">리소스 ID</dt>
                      <dd className="text-xs text-gray-900 dark:text-gray-100 font-mono mt-0.5 break-all">{selectedLog.resourceId}</dd>
                    </div>
                  )}
                </div>

                <hr className="border-gray-200 dark:border-gray-700" />

                {/* Row 3: Request */}
                <div className="grid grid-cols-4 gap-x-4 gap-y-2">
                  <div>
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">IP 주소</dt>
                    <dd className="text-xs text-gray-900 dark:text-gray-100 font-mono mt-0.5">{selectedLog.ipAddress || '-'}</dd>
                  </div>
                  <div>
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">요청 방식</dt>
                    <dd className="text-xs text-gray-900 dark:text-gray-100 font-mono mt-0.5">{selectedLog.requestMethod || '-'}</dd>
                  </div>
                  <div>
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">상태 코드</dt>
                    <dd className="text-xs text-gray-900 dark:text-gray-100 font-mono mt-0.5">{selectedLog.statusCode || '-'}</dd>
                  </div>
                  <div>
                    <dt className="text-[11px] text-gray-500 dark:text-gray-400">소요 시간</dt>
                    <dd className="text-xs text-gray-900 dark:text-gray-100 font-mono mt-0.5">{formatDuration(selectedLog.durationMs)}</dd>
                  </div>
                  {selectedLog.requestPath && (
                    <div className="col-span-4">
                      <dt className="text-[11px] text-gray-500 dark:text-gray-400">요청 경로</dt>
                      <dd className="text-xs text-gray-900 dark:text-gray-100 font-mono mt-0.5 break-all">{selectedLog.requestPath}</dd>
                    </div>
                  )}
                  {selectedLog.userAgent && (
                    <div className="col-span-4">
                      <dt className="text-[11px] text-gray-500 dark:text-gray-400">User Agent</dt>
                      <dd className="text-[11px] text-gray-600 dark:text-gray-400 font-mono mt-0.5 truncate" title={selectedLog.userAgent}>{selectedLog.userAgent}</dd>
                    </div>
                  )}
                </div>

                {/* Error Message */}
                {selectedLog.errorMessage && (
                  <>
                    <hr className="border-gray-200 dark:border-gray-700" />
                    <div>
                      <dt className="text-[11px] text-gray-500 dark:text-gray-400">오류 메시지</dt>
                      <dd className="text-xs text-red-600 dark:text-red-400 mt-0.5 break-all">{selectedLog.errorMessage}</dd>
                    </div>
                  </>
                )}

                {/* Metadata */}
                {selectedLog.metadata && Object.keys(selectedLog.metadata).length > 0 && (
                  <>
                    <hr className="border-gray-200 dark:border-gray-700" />
                    <div>
                      <dt className="text-[11px] text-gray-500 dark:text-gray-400 mb-1">메타데이터</dt>
                      <div className="bg-gray-50 dark:bg-gray-900 rounded-md px-3 py-2 overflow-x-auto">
                        <pre className="text-[11px] text-gray-900 dark:text-gray-100 font-mono">
                          {JSON.stringify(selectedLog.metadata, null, 2)}
                        </pre>
                      </div>
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

export default OperationAuditLog;
