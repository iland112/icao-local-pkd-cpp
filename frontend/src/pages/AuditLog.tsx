import { useState, useEffect } from 'react';
import { Shield, Filter, User, Activity, CheckCircle, XCircle, Clock, ChevronLeft, ChevronRight, Eye } from 'lucide-react';

interface AuditLogEntry {
  id: string;
  userId: string;
  username: string;
  operationType: string;
  operationSubtype?: string;
  ipAddress: string;
  userAgent: string;
  requestMethod?: string;
  requestPath?: string;
  success: boolean;
  statusCode?: number;
  errorMessage?: string;
  durationMs?: number;
  createdAt: string;
}

interface AuditStats {
  totalOperations: number;
  successfulOperations: number;
  failedOperations: number;
  operationsByType: Record<string, number>;
  topUsers: { username: string; operationCount: number }[];
  averageDurationMs: number;
}

export function AuditLog() {
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
  const [limit] = useState(20);

  // Detail Dialog
  const [selectedLog, setSelectedLog] = useState<AuditLogEntry | null>(null);
  const [dialogOpen, setDialogOpen] = useState(false);

  useEffect(() => {
    fetchAuditLogs();
    fetchStats();
  }, [page, username, eventType, successFilter]);

  const fetchAuditLogs = async () => {
    try {
      setLoading(true);
      const token = localStorage.getItem('access_token');
      const offset = (page - 1) * limit;

      const params = new URLSearchParams({
        limit: limit.toString(),
        offset: offset.toString(),
      });

      if (username) params.append('username', username);
      if (eventType) params.append('operationType', eventType);
      if (successFilter) params.append('success', successFilter);

      const response = await fetch(`/api/audit/operations?${params}`, {
        headers: {
          'Authorization': `Bearer ${token}`,
        },
      });

      if (!response.ok) throw new Error('Failed to fetch audit logs');

      const data = await response.json();
      setLogs(data.data || []);
      setTotal(data.total || 0);
    } catch (error) {
      console.error('Error fetching audit logs:', error);
    } finally {
      setLoading(false);
    }
  };

  const fetchStats = async () => {
    try {
      const token = localStorage.getItem('access_token');
      const response = await fetch('/api/audit/operations/stats', {
        headers: {
          'Authorization': `Bearer ${token}`,
        },
      });

      if (!response.ok) throw new Error('Failed to fetch stats');

      const data = await response.json();
      setStats(data.data || null);
    } catch (error) {
      console.error('Error fetching stats:', error);
    }
  };

  const handleReset = () => {
    setUsername('');
    setEventType('');
    setSuccessFilter('');
    setPage(1);
  };

  const totalPages = Math.ceil(total / limit);

  const getEventBadgeColor = (operationType: string) => {
    if (operationType === 'UPLOAD') return 'bg-purple-100 text-purple-700 dark:bg-purple-900/30 dark:text-purple-300';
    if (operationType === 'PA_VERIFY') return 'bg-orange-100 text-orange-700 dark:bg-orange-900/30 dark:text-orange-300';
    if (operationType === 'RECONCILIATION') return 'bg-yellow-100 text-yellow-700 dark:bg-yellow-900/30 dark:text-yellow-300';
    if (operationType === 'CERTIFICATE_SEARCH') return 'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-300';
    if (operationType === 'EXPORT') return 'bg-teal-100 text-teal-700 dark:bg-teal-900/30 dark:text-teal-300';
    return 'bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-300';
  };

  const formatDate = (dateString: string) => {
    const date = new Date(dateString);
    return date.toLocaleString('ko-KR', {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    });
  };

  return (
    <div className="p-6 space-y-6">
      {/* Header */}
      <div className="flex items-center gap-4">
        <div className="w-12 h-12 bg-gradient-to-br from-purple-500 to-pink-500 rounded-xl flex items-center justify-center shadow-lg">
          <Shield className="w-6 h-6 text-white" />
        </div>
        <div>
          <h1 className="text-2xl font-bold text-gray-900 dark:text-white">
            로그인 이력
          </h1>
          <p className="text-gray-600 dark:text-gray-400">
            사용자 인증 및 활동 로그
          </p>
        </div>
      </div>

      {/* Statistics Cards */}
      {stats && (
        <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700 p-4">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 bg-blue-100 dark:bg-blue-900/30 rounded-lg flex items-center justify-center">
                <Activity className="w-5 h-5 text-blue-600 dark:text-blue-400" />
              </div>
              <div>
                <p className="text-2xl font-bold text-gray-900 dark:text-white">
                  {(stats.totalOperations ?? 0).toLocaleString()}
                </p>
                <p className="text-sm text-gray-600 dark:text-gray-400">전체 이벤트</p>
              </div>
            </div>
          </div>

          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700 p-4">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 bg-green-100 dark:bg-green-900/30 rounded-lg flex items-center justify-center">
                <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400" />
              </div>
              <div>
                <p className="text-2xl font-bold text-gray-900 dark:text-white">
                  {(stats.successfulOperations ?? 0).toLocaleString()}
                </p>
                <p className="text-sm text-gray-600 dark:text-gray-400">성공한 작업</p>
              </div>
            </div>
          </div>

          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700 p-4">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 bg-red-100 dark:bg-red-900/30 rounded-lg flex items-center justify-center">
                <XCircle className="w-5 h-5 text-red-600 dark:text-red-400" />
              </div>
              <div>
                <p className="text-2xl font-bold text-gray-900 dark:text-white">
                  {(stats.failedOperations ?? 0).toLocaleString()}
                </p>
                <p className="text-sm text-gray-600 dark:text-gray-400">실패한 작업</p>
              </div>
            </div>
          </div>

          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700 p-4">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 bg-purple-100 dark:bg-purple-900/30 rounded-lg flex items-center justify-center">
                <Clock className="w-5 h-5 text-purple-600 dark:text-purple-400" />
              </div>
              <div>
                <p className="text-2xl font-bold text-gray-900 dark:text-white">
                  {Math.round(stats.averageDurationMs ?? 0).toLocaleString()}ms
                </p>
                <p className="text-sm text-gray-600 dark:text-gray-400">평균 처리 시간</p>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Filter Card */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700 p-6">
        <div className="flex items-center gap-2 mb-4">
          <Filter className="w-5 h-5 text-gray-400" />
          <h2 className="text-lg font-semibold text-gray-900 dark:text-white">필터</h2>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
          <div>
            <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              사용자명
            </label>
            <input
              type="text"
              value={username}
              onChange={(e) => setUsername(e.target.value)}
              placeholder="사용자명 입력..."
              className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              이벤트 타입
            </label>
            <select
              value={eventType}
              onChange={(e) => setEventType(e.target.value)}
              className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="">전체</option>
              <option value="UPLOAD">파일 업로드</option>
              <option value="CERTIFICATE_SEARCH">인증서 검색</option>
              <option value="PA_VERIFY">PA 검증</option>
              <option value="ICAO_SYNC">ICAO 동기화</option>
              <option value="RECONCILIATION">조정</option>
              <option value="EXPORT">내보내기</option>
            </select>
          </div>

          <div>
            <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              성공 여부
            </label>
            <select
              value={successFilter}
              onChange={(e) => setSuccessFilter(e.target.value)}
              className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="">전체</option>
              <option value="true">성공</option>
              <option value="false">실패</option>
            </select>
          </div>

          <div className="flex items-end">
            <button
              onClick={handleReset}
              className="w-full px-4 py-2 bg-gray-100 dark:bg-gray-700 text-gray-700 dark:text-gray-300 rounded-lg hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors"
            >
              초기화
            </button>
          </div>
        </div>
      </div>

      {/* Audit Log Table */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700 overflow-hidden">
        <div className="overflow-x-auto">
          <table className="w-full">
            <thead className="bg-gray-50 dark:bg-gray-900/50 border-b border-gray-200 dark:border-gray-700">
              <tr>
                <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                  시간
                </th>
                <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                  사용자
                </th>
                <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                  이벤트
                </th>
                <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                  IP 주소
                </th>
                <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                  상태
                </th>
                <th className="px-6 py-3 text-center text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                  상세
                </th>
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
              {loading ? (
                <tr>
                  <td colSpan={6} className="px-6 py-12 text-center text-gray-500 dark:text-gray-400">
                    로딩 중...
                  </td>
                </tr>
              ) : logs.length === 0 ? (
                <tr>
                  <td colSpan={6} className="px-6 py-12 text-center text-gray-500 dark:text-gray-400">
                    로그가 없습니다.
                  </td>
                </tr>
              ) : (
                logs.map((log) => (
                  <tr key={log.id} className="hover:bg-gray-50 dark:hover:bg-gray-900/30">
                    <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                      {formatDate(log.createdAt)}
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap">
                      <div className="flex items-center gap-2">
                        <User className="w-4 h-4 text-gray-400" />
                        <span className="text-sm font-medium text-gray-900 dark:text-white">
                          {log.username || '시스템'}
                        </span>
                      </div>
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap">
                      <span className={`px-2 py-1 text-xs font-medium rounded-full ${getEventBadgeColor(log.operationType)}`}>
                        {log.operationType}{log.operationSubtype ? ` / ${log.operationSubtype}` : ''}
                      </span>
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-600 dark:text-gray-400 font-mono">
                      {log.ipAddress}
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap">
                      {log.success ? (
                        <span className="flex items-center gap-1 text-sm text-green-600 dark:text-green-400">
                          <CheckCircle className="w-4 h-4" />
                          성공
                        </span>
                      ) : (
                        <span className="flex items-center gap-1 text-sm text-red-600 dark:text-red-400">
                          <XCircle className="w-4 h-4" />
                          실패
                        </span>
                      )}
                    </td>
                    <td className="px-6 py-4 text-center">
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
              >
                <ChevronRight className="w-4 h-4" />
              </button>
            </div>
          </div>
        )}
      </div>

      {/* Detail Dialog */}
      {dialogOpen && selectedLog && (
        <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-2xl max-w-3xl w-full max-h-[90vh] overflow-y-auto">
            {/* Dialog Header */}
            <div className="sticky top-0 bg-gradient-to-r from-purple-500 to-pink-500 px-6 py-4 rounded-t-xl">
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
                    <dd className="text-sm text-gray-900 dark:text-gray-100 mt-1">{formatDate(selectedLog.createdAt)}</dd>
                  </div>
                  <div>
                    <dt className="text-xs text-gray-500 dark:text-gray-400">사용자 ID</dt>
                    <dd className="text-sm text-gray-900 dark:text-gray-100 font-mono mt-1">{selectedLog.userId || '-'}</dd>
                  </div>
                  <div>
                    <dt className="text-xs text-gray-500 dark:text-gray-400">사용자 이름</dt>
                    <dd className="text-sm text-gray-900 dark:text-gray-100 mt-1">{selectedLog.username || '시스템'}</dd>
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
                      <span className={`px-2 py-1 text-xs font-medium rounded-full ${getEventBadgeColor(selectedLog.operationType)}`}>
                        {selectedLog.operationType}
                      </span>
                    </dd>
                  </div>
                  <div>
                    <dt className="text-xs text-gray-500 dark:text-gray-400">하위 유형</dt>
                    <dd className="text-sm text-gray-900 dark:text-gray-100 mt-1">{selectedLog.operationSubtype || '-'}</dd>
                  </div>
                </dl>
              </div>

              {/* Request Information */}
              <div>
                <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">요청 정보</h4>
                <dl className="grid grid-cols-2 gap-4">
                  <div>
                    <dt className="text-xs text-gray-500 dark:text-gray-400">IP 주소</dt>
                    <dd className="text-sm text-gray-900 dark:text-gray-100 font-mono mt-1">{selectedLog.ipAddress}</dd>
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
                  {selectedLog.durationMs !== undefined && (
                    <div>
                      <dt className="text-xs text-gray-500 dark:text-gray-400">소요 시간</dt>
                      <dd className="text-sm text-gray-900 dark:text-gray-100 font-mono mt-1">{selectedLog.durationMs}ms</dd>
                    </div>
                  )}
                  {selectedLog.errorMessage && (
                    <div className="col-span-2">
                      <dt className="text-xs text-gray-500 dark:text-gray-400">오류 메시지</dt>
                      <dd className="text-sm text-red-600 dark:text-red-400 mt-1 break-all">{selectedLog.errorMessage}</dd>
                    </div>
                  )}
                </dl>
              </div>
            </div>

            {/* Dialog Footer */}
            <div className="bg-gray-50 dark:bg-gray-700/50 px-6 py-4 rounded-b-xl">
              <button
                onClick={() => setDialogOpen(false)}
                className="w-full px-4 py-2 bg-purple-600 hover:bg-purple-700 text-white rounded-lg font-medium transition-colors"
              >
                닫기
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default AuditLog;
