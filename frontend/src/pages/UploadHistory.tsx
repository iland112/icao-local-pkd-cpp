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
  X,
  Database,
  Server,
  FileCheck,
  ShieldCheck,
  HardDrive,
} from 'lucide-react';
import { uploadApi } from '@/services/api';
import type { PageResponse, UploadStatus, FileFormat } from '@/types';
import { cn } from '@/utils/cn';

// API response interface (matches actual backend response)
interface UploadHistoryItem {
  id: string;
  fileName: string;
  fileFormat: FileFormat;
  fileSize: number;
  status: UploadStatus;
  certificateCount: number;
  crlCount: number;
  errorMessage: string;
  createdAt: string;
  updatedAt: string;
}

// Status step definition
const STATUS_STEPS: { key: UploadStatus; label: string; icon: React.ReactNode }[] = [
  { key: 'PENDING', label: '대기', icon: <Clock className="w-4 h-4" /> },
  { key: 'UPLOADING', label: '업로드', icon: <Upload className="w-4 h-4" /> },
  { key: 'PARSING', label: '파싱', icon: <FileCheck className="w-4 h-4" /> },
  { key: 'VALIDATING', label: '검증', icon: <ShieldCheck className="w-4 h-4" /> },
  { key: 'SAVING_DB', label: 'DB 저장', icon: <Database className="w-4 h-4" /> },
  { key: 'SAVING_LDAP', label: 'LDAP 저장', icon: <Server className="w-4 h-4" /> },
  { key: 'COMPLETED', label: '완료', icon: <CheckCircle className="w-4 h-4" /> },
];

export function UploadHistory() {
  const [uploads, setUploads] = useState<UploadHistoryItem[]>([]);
  const [loading, setLoading] = useState(true);
  const [page, setPage] = useState(0);
  const [totalPages, setTotalPages] = useState(0);
  const [totalElements, setTotalElements] = useState(0);
  const [searchTerm, setSearchTerm] = useState('');
  const [statusFilter, setStatusFilter] = useState<UploadStatus | ''>('');

  // Detail dialog state
  const [selectedUpload, setSelectedUpload] = useState<UploadHistoryItem | null>(null);
  const [dialogOpen, setDialogOpen] = useState(false);

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
        sort: 'createdAt',
        direction: 'DESC',
      });
      // Cast the response to our interface that matches actual API response
      const data = response.data as unknown as PageResponse<UploadHistoryItem>;
      setUploads(data.content);
      setTotalPages(data.totalPages);
      setTotalElements(data.totalElements);
    } catch (error) {
      console.error('Failed to fetch upload history:', error);
    } finally {
      setLoading(false);
    }
  };

  // Parse PostgreSQL timestamp format: "2025-12-31 09:04:28.432487+09"
  const formatDate = (dateString: string): string => {
    if (!dateString) return '-';
    try {
      // PostgreSQL format: "2025-12-31 09:04:28.432487+09"
      // Convert to ISO format for JavaScript Date parsing
      const isoString = dateString
        .replace(' ', 'T')
        .replace(/\+(\d{2})$/, '+$1:00'); // "+09" -> "+09:00"

      const date = new Date(isoString);
      if (isNaN(date.getTime())) {
        return dateString; // Return original if parsing fails
      }
      return date.toLocaleString('ko-KR', {
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
      });
    } catch {
      return dateString;
    }
  };

  // Get current step index for status progress
  const getStatusStepIndex = (status: UploadStatus): number => {
    if (status === 'FAILED') return -1;
    return STATUS_STEPS.findIndex(step => step.key === status);
  };

  // Render status progress bar
  const renderStatusProgress = (status: UploadStatus) => {
    if (status === 'FAILED') {
      return (
        <div className="flex items-center gap-2">
          <XCircle className="w-5 h-5 text-red-500" />
          <span className="text-sm font-medium text-red-600 dark:text-red-400">실패</span>
        </div>
      );
    }

    const currentIndex = getStatusStepIndex(status);
    const isCompleted = status === 'COMPLETED';

    return (
      <div className="flex items-center gap-1">
        {STATUS_STEPS.map((step, index) => {
          const isPassed = index < currentIndex || isCompleted;
          const isCurrent = index === currentIndex && !isCompleted;

          return (
            <div key={step.key} className="flex items-center">
              <div
                className={cn(
                  'flex items-center justify-center w-6 h-6 rounded-full transition-all',
                  isPassed && 'bg-green-500 text-white',
                  isCurrent && 'bg-blue-500 text-white animate-pulse',
                  !isPassed && !isCurrent && 'bg-gray-200 dark:bg-gray-600 text-gray-400 dark:text-gray-500'
                )}
                title={step.label}
              >
                {isPassed ? (
                  <CheckCircle className="w-4 h-4" />
                ) : isCurrent ? (
                  <Loader2 className="w-4 h-4 animate-spin" />
                ) : (
                  <span className="text-xs">{index + 1}</span>
                )}
              </div>
              {index < STATUS_STEPS.length - 1 && (
                <div
                  className={cn(
                    'w-4 h-0.5 mx-0.5',
                    isPassed ? 'bg-green-500' : 'bg-gray-200 dark:bg-gray-600'
                  )}
                />
              )}
            </div>
          );
        })}
      </div>
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
        {format === 'MASTER_LIST' ? 'ML' : format}
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

  const handleViewDetail = (upload: UploadHistoryItem) => {
    setSelectedUpload(upload);
    setDialogOpen(true);
  };

  const closeDialog = () => {
    setDialogOpen(false);
    setSelectedUpload(null);
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
                      진행 상태
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      인증서
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
                      <td className="px-6 py-4">{renderStatusProgress(upload.status)}</td>
                      <td className="px-6 py-4">
                        <div className="flex gap-2 text-xs">
                          <span className="px-2 py-0.5 rounded bg-blue-100 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400">
                            <HardDrive className="w-3 h-3 inline mr-1" />
                            {upload.certificateCount}
                          </span>
                          {upload.crlCount > 0 && (
                            <span className="px-2 py-0.5 rounded bg-amber-100 dark:bg-amber-900/30 text-amber-600 dark:text-amber-400">
                              CRL: {upload.crlCount}
                            </span>
                          )}
                        </div>
                      </td>
                      <td className="px-6 py-4 text-sm text-gray-500 dark:text-gray-400">
                        {formatDate(upload.createdAt)}
                      </td>
                      <td className="px-6 py-4 text-right">
                        <button
                          onClick={() => handleViewDetail(upload)}
                          className="inline-flex items-center gap-1 px-3 py-1.5 rounded-lg text-sm font-medium text-blue-600 dark:text-blue-400 hover:bg-blue-50 dark:hover:bg-blue-900/20 transition-colors"
                        >
                          <Eye className="w-4 h-4" />
                          상세
                        </button>
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

      {/* Detail Dialog */}
      {dialogOpen && selectedUpload && (
        <div className="fixed inset-0 z-50 flex items-center justify-center">
          {/* Backdrop */}
          <div
            className="absolute inset-0 bg-black/50 backdrop-blur-sm"
            onClick={closeDialog}
          />

          {/* Dialog Content */}
          <div className="relative bg-white dark:bg-gray-800 rounded-2xl shadow-2xl w-full max-w-2xl mx-4 max-h-[90vh] overflow-y-auto">
            {/* Header */}
            <div className="flex items-center justify-between p-6 border-b border-gray-200 dark:border-gray-700">
              <div className="flex items-center gap-3">
                <div className="p-2 rounded-lg bg-gradient-to-br from-blue-500 to-indigo-600">
                  <FileText className="w-5 h-5 text-white" />
                </div>
                <div>
                  <h2 className="text-lg font-semibold text-gray-900 dark:text-white">
                    업로드 상세 정보
                  </h2>
                  <p className="text-sm text-gray-500 dark:text-gray-400">
                    {selectedUpload.fileName}
                  </p>
                </div>
              </div>
              <button
                onClick={closeDialog}
                className="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              >
                <X className="w-5 h-5 text-gray-500" />
              </button>
            </div>

            {/* Body */}
            <div className="p-6 space-y-6">
              {/* Status Progress */}
              <div>
                <h3 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-3">진행 상태</h3>
                <div className="flex items-center justify-between bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                  {STATUS_STEPS.map((step, index) => {
                    const currentIndex = getStatusStepIndex(selectedUpload.status);
                    const isPassed = index < currentIndex || selectedUpload.status === 'COMPLETED';
                    const isCurrent = index === currentIndex && selectedUpload.status !== 'COMPLETED';
                    const isFailed = selectedUpload.status === 'FAILED' && index === 0;

                    return (
                      <div key={step.key} className="flex flex-col items-center">
                        <div
                          className={cn(
                            'flex items-center justify-center w-10 h-10 rounded-full mb-2 transition-all',
                            isPassed && 'bg-green-500 text-white',
                            isCurrent && 'bg-blue-500 text-white animate-pulse',
                            isFailed && 'bg-red-500 text-white',
                            !isPassed && !isCurrent && !isFailed && 'bg-gray-200 dark:bg-gray-600 text-gray-400'
                          )}
                        >
                          {isPassed ? (
                            <CheckCircle className="w-5 h-5" />
                          ) : isCurrent ? (
                            <Loader2 className="w-5 h-5 animate-spin" />
                          ) : isFailed ? (
                            <XCircle className="w-5 h-5" />
                          ) : (
                            step.icon
                          )}
                        </div>
                        <span className={cn(
                          'text-xs font-medium',
                          isPassed && 'text-green-600 dark:text-green-400',
                          isCurrent && 'text-blue-600 dark:text-blue-400',
                          isFailed && 'text-red-600 dark:text-red-400',
                          !isPassed && !isCurrent && !isFailed && 'text-gray-400'
                        )}>
                          {step.label}
                        </span>
                      </div>
                    );
                  })}
                </div>
              </div>

              {/* Error Message */}
              {selectedUpload.errorMessage && (
                <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl p-4">
                  <div className="flex items-start gap-3">
                    <XCircle className="w-5 h-5 text-red-500 mt-0.5" />
                    <div>
                      <h4 className="text-sm font-medium text-red-800 dark:text-red-300">오류 발생</h4>
                      <p className="text-sm text-red-600 dark:text-red-400 mt-1">
                        {selectedUpload.errorMessage}
                      </p>
                    </div>
                  </div>
                </div>
              )}

              {/* Details Grid */}
              <div className="grid grid-cols-2 gap-4">
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                  <span className="text-xs text-gray-500 dark:text-gray-400">파일 형식</span>
                  <div className="mt-1">{getFormatBadge(selectedUpload.fileFormat)}</div>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                  <span className="text-xs text-gray-500 dark:text-gray-400">파일 크기</span>
                  <p className="text-sm font-medium text-gray-900 dark:text-white mt-1">
                    {formatFileSize(selectedUpload.fileSize)}
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                  <span className="text-xs text-gray-500 dark:text-gray-400">인증서 수</span>
                  <p className="text-sm font-medium text-gray-900 dark:text-white mt-1">
                    {selectedUpload.certificateCount}개
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                  <span className="text-xs text-gray-500 dark:text-gray-400">CRL 수</span>
                  <p className="text-sm font-medium text-gray-900 dark:text-white mt-1">
                    {selectedUpload.crlCount}개
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                  <span className="text-xs text-gray-500 dark:text-gray-400">업로드 일시</span>
                  <p className="text-sm font-medium text-gray-900 dark:text-white mt-1">
                    {formatDate(selectedUpload.createdAt)}
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                  <span className="text-xs text-gray-500 dark:text-gray-400">완료 일시</span>
                  <p className="text-sm font-medium text-gray-900 dark:text-white mt-1">
                    {formatDate(selectedUpload.updatedAt)}
                  </p>
                </div>
              </div>

              {/* Upload ID */}
              <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                <span className="text-xs text-gray-500 dark:text-gray-400">업로드 ID</span>
                <p className="text-sm font-mono text-gray-900 dark:text-white mt-1 break-all">
                  {selectedUpload.id}
                </p>
              </div>
            </div>

            {/* Footer */}
            <div className="flex justify-end gap-3 p-6 border-t border-gray-200 dark:border-gray-700">
              <button
                onClick={closeDialog}
                className="px-4 py-2 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
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

export default UploadHistory;
