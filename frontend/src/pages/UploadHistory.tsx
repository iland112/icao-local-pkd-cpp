import { useState, useEffect } from 'react';
import { Link } from 'react-router-dom';
import {
  Clock,
  Upload,
  CheckCircle,
  XCircle,
  AlertCircle,
  FileText,
  ChevronLeft,
  ChevronRight,
  Search,
  Filter,
  Eye,
  Loader2,
} from 'lucide-react';
import { uploadApi } from '@/services/api';
import type { UploadedFile, PageResponse, UploadStatus, FileFormat } from '@/types';
import { cn } from '@/utils/cn';

export function UploadHistory() {
  const [uploads, setUploads] = useState<UploadedFile[]>([]);
  const [loading, setLoading] = useState(true);
  const [page, setPage] = useState(0);
  const [totalPages, setTotalPages] = useState(0);
  const [totalElements, setTotalElements] = useState(0);
  const [searchTerm, setSearchTerm] = useState('');
  const [statusFilter, setStatusFilter] = useState<UploadStatus | ''>('');

  const pageSize = 10;

  useEffect(() => {
    fetchUploads();
  }, [page]);

  const fetchUploads = async () => {
    setLoading(true);
    try {
      const response = await uploadApi.getHistory({
        page,
        size: pageSize,
        sort: 'uploadedAt',
        direction: 'DESC',
      });
      const data: PageResponse<UploadedFile> = response.data;
      setUploads(data.content);
      setTotalPages(data.totalPages);
      setTotalElements(data.totalElements);
    } catch (error) {
      console.error('Failed to fetch upload history:', error);
    } finally {
      setLoading(false);
    }
  };

  const getStatusBadge = (status: UploadStatus) => {
    const styles: Record<UploadStatus, { bg: string; text: string; icon: React.ReactNode }> = {
      PENDING: { bg: 'bg-gray-100 dark:bg-gray-700', text: 'text-gray-600 dark:text-gray-300', icon: <Clock className="w-3 h-3" /> },
      UPLOADING: { bg: 'bg-blue-100 dark:bg-blue-900/30', text: 'text-blue-600 dark:text-blue-400', icon: <Loader2 className="w-3 h-3 animate-spin" /> },
      PARSING: { bg: 'bg-blue-100 dark:bg-blue-900/30', text: 'text-blue-600 dark:text-blue-400', icon: <Loader2 className="w-3 h-3 animate-spin" /> },
      VALIDATING: { bg: 'bg-yellow-100 dark:bg-yellow-900/30', text: 'text-yellow-600 dark:text-yellow-400', icon: <Loader2 className="w-3 h-3 animate-spin" /> },
      SAVING_DB: { bg: 'bg-indigo-100 dark:bg-indigo-900/30', text: 'text-indigo-600 dark:text-indigo-400', icon: <Loader2 className="w-3 h-3 animate-spin" /> },
      SAVING_LDAP: { bg: 'bg-purple-100 dark:bg-purple-900/30', text: 'text-purple-600 dark:text-purple-400', icon: <Loader2 className="w-3 h-3 animate-spin" /> },
      COMPLETED: { bg: 'bg-green-100 dark:bg-green-900/30', text: 'text-green-600 dark:text-green-400', icon: <CheckCircle className="w-3 h-3" /> },
      FAILED: { bg: 'bg-red-100 dark:bg-red-900/30', text: 'text-red-600 dark:text-red-400', icon: <XCircle className="w-3 h-3" /> },
    };

    const style = styles[status];
    const label = {
      PENDING: '대기',
      UPLOADING: '업로드 중',
      PARSING: '파싱 중',
      VALIDATING: '검증 중',
      SAVING_DB: 'DB 저장 중',
      SAVING_LDAP: 'LDAP 저장 중',
      COMPLETED: '완료',
      FAILED: '실패',
    }[status];

    return (
      <span className={cn('inline-flex items-center gap-1 px-2 py-1 rounded-full text-xs font-medium', style.bg, style.text)}>
        {style.icon}
        {label}
      </span>
    );
  };

  const getFormatBadge = (format: FileFormat) => {
    const isLdif = format === 'LDIF';
    return (
      <span
        className={cn(
          'inline-flex items-center px-2 py-1 rounded text-xs font-medium',
          isLdif
            ? 'bg-orange-100 dark:bg-orange-900/30 text-orange-600 dark:text-orange-400'
            : 'bg-teal-100 dark:bg-teal-900/30 text-teal-600 dark:text-teal-400'
        )}
      >
        {format}
      </span>
    );
  };

  const formatFileSize = (bytes: number): string => {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
  };

  const formatDate = (dateString: string): string => {
    return new Date(dateString).toLocaleString('ko-KR', {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
    });
  };

  const filteredUploads = uploads.filter((upload) => {
    const matchesSearch = upload.fileName.toLowerCase().includes(searchTerm.toLowerCase());
    const matchesStatus = !statusFilter || upload.status === statusFilter;
    return matchesSearch && matchesStatus;
  });

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-8">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-amber-500 to-orange-600 shadow-lg">
            <Clock className="w-7 h-7 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">업로드 이력</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              LDIF 및 Master List 파일 업로드 이력을 확인합니다.
            </p>
          </div>
        </div>
      </div>

      {/* Action Bar */}
      <div className="mb-6 flex flex-col sm:flex-row gap-4 justify-between">
        <div className="flex gap-3">
          {/* Search */}
          <div className="relative">
            <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
            <input
              type="text"
              placeholder="파일명 검색..."
              value={searchTerm}
              onChange={(e) => setSearchTerm(e.target.value)}
              className="pl-10 pr-4 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-800 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          {/* Status Filter */}
          <div className="relative">
            <Filter className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
            <select
              value={statusFilter}
              onChange={(e) => setStatusFilter(e.target.value as UploadStatus | '')}
              className="pl-10 pr-8 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-800 focus:outline-none focus:ring-2 focus:ring-blue-500 appearance-none"
            >
              <option value="">모든 상태</option>
              <option value="COMPLETED">완료</option>
              <option value="FAILED">실패</option>
              <option value="PENDING">대기</option>
            </select>
          </div>
        </div>

        <Link
          to="/upload"
          className="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-white bg-gradient-to-r from-indigo-500 to-purple-500 hover:from-indigo-600 hover:to-purple-600 transition-all shadow-md hover:shadow-lg"
        >
          <Upload className="w-4 h-4" />
          새 업로드
        </Link>
      </div>

      {/* Table */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        {loading ? (
          <div className="flex items-center justify-center py-20">
            <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
          </div>
        ) : filteredUploads.length === 0 ? (
          <div className="flex flex-col items-center justify-center py-20 text-gray-500 dark:text-gray-400">
            <AlertCircle className="w-12 h-12 mb-4 opacity-50" />
            <p className="text-lg font-medium">업로드 이력이 없습니다.</p>
            <p className="text-sm">새 파일을 업로드하여 시작하세요.</p>
          </div>
        ) : (
          <>
            <div className="overflow-x-auto">
              <table className="w-full">
                <thead className="bg-gray-50 dark:bg-gray-700/50">
                  <tr>
                    <th className="px-6 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      파일명
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      형식
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      크기
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      상태
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      통계
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      업로드 일시
                    </th>
                    <th className="px-6 py-3 text-right text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      액션
                    </th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
                  {filteredUploads.map((upload) => (
                    <tr
                      key={upload.id}
                      className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors"
                    >
                      <td className="px-6 py-4">
                        <div className="flex items-center gap-3">
                          <div className="p-2 rounded-lg bg-gray-100 dark:bg-gray-700">
                            <FileText className="w-4 h-4 text-gray-500" />
                          </div>
                          <span className="font-medium text-gray-900 dark:text-white truncate max-w-[200px]">
                            {upload.fileName}
                          </span>
                        </div>
                      </td>
                      <td className="px-6 py-4">{getFormatBadge(upload.fileFormat)}</td>
                      <td className="px-6 py-4 text-sm text-gray-500 dark:text-gray-400">
                        {formatFileSize(upload.fileSize)}
                      </td>
                      <td className="px-6 py-4">{getStatusBadge(upload.status)}</td>
                      <td className="px-6 py-4">
                        {upload.statistics ? (
                          <div className="flex gap-2 text-xs">
                            <span className="px-2 py-0.5 rounded bg-blue-100 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400">
                              CSCA: {upload.statistics.cscaCount}
                            </span>
                            <span className="px-2 py-0.5 rounded bg-green-100 dark:bg-green-900/30 text-green-600 dark:text-green-400">
                              DSC: {upload.statistics.dscCount}
                            </span>
                          </div>
                        ) : (
                          <span className="text-gray-400">-</span>
                        )}
                      </td>
                      <td className="px-6 py-4 text-sm text-gray-500 dark:text-gray-400">
                        {formatDate(upload.uploadedAt)}
                      </td>
                      <td className="px-6 py-4 text-right">
                        <Link
                          to={`/upload/${upload.id}`}
                          className="inline-flex items-center gap-1 px-3 py-1.5 rounded-lg text-sm font-medium text-blue-600 dark:text-blue-400 hover:bg-blue-50 dark:hover:bg-blue-900/20 transition-colors"
                        >
                          <Eye className="w-4 h-4" />
                          상세
                        </Link>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>

            {/* Pagination */}
            <div className="px-6 py-4 border-t border-gray-200 dark:border-gray-700 flex items-center justify-between">
              <p className="text-sm text-gray-500 dark:text-gray-400">
                총 {totalElements}개 중 {page * pageSize + 1}-{Math.min((page + 1) * pageSize, totalElements)}개 표시
              </p>
              <div className="flex items-center gap-2">
                <button
                  onClick={() => setPage((p) => Math.max(0, p - 1))}
                  disabled={page === 0}
                  className="p-2 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
                >
                  <ChevronLeft className="w-4 h-4" />
                </button>
                <span className="text-sm text-gray-600 dark:text-gray-300">
                  {page + 1} / {totalPages}
                </span>
                <button
                  onClick={() => setPage((p) => Math.min(totalPages - 1, p + 1))}
                  disabled={page >= totalPages - 1}
                  className="p-2 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
                >
                  <ChevronRight className="w-4 h-4" />
                </button>
              </div>
            </div>
          </>
        )}
      </div>
    </div>
  );
}

export default UploadHistory;
