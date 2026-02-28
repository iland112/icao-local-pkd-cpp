import { useState, useEffect, useMemo } from 'react';
import { Link } from 'react-router-dom';
import {
  Clock,
  Upload,
  CheckCircle,
  XCircle,
  AlertCircle,
  AlertTriangle,
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
  RefreshCw,
  Trash2,
  Download,
} from 'lucide-react';
import { uploadApi, uploadHistoryApi } from '@/services/api';
import type { UploadedFile, UploadStatus, FileFormat, UploadIssues, UploadDuplicate } from '@/types';
import { cn } from '@/utils/cn';
import { MasterListStructure } from '@/components/MasterListStructure';
import { LdifStructure } from '@/components/LdifStructure';
import { DuplicateCertificatesTree } from '@/components/DuplicateCertificatesTree';
import { DuplicateCertificateDialog } from '@/components/DuplicateCertificateDialog';
import { ValidationSummaryPanel } from '@/components/ValidationSummaryPanel';
import { exportDuplicatesToCsv, exportDuplicateStatisticsToCsv } from '@/utils/csvExport';


// Full status step definition (for dialog detail view)
const STATUS_STEPS: { key: UploadStatus; label: string; icon: React.ReactNode }[] = [
  { key: 'PENDING', label: '대기', icon: <Clock className="w-4 h-4" /> },
  { key: 'UPLOADING', label: '업로드', icon: <Upload className="w-4 h-4" /> },
  { key: 'PARSING', label: '파싱', icon: <FileCheck className="w-4 h-4" /> },
  { key: 'PROCESSING', label: '처리', icon: <Loader2 className="w-4 h-4" /> },
  { key: 'VALIDATING', label: '검증', icon: <ShieldCheck className="w-4 h-4" /> },
  { key: 'SAVING_DB', label: 'DB 저장', icon: <Database className="w-4 h-4" /> },
  { key: 'SAVING_LDAP', label: 'LDAP 저장', icon: <Server className="w-4 h-4" /> },
  { key: 'COMPLETED', label: '완료', icon: <CheckCircle className="w-4 h-4" /> },
];

// Statuses considered "in progress"
const IN_PROGRESS_STATUSES: UploadStatus[] = ['PENDING', 'UPLOADING', 'PARSING', 'PROCESSING', 'VALIDATING', 'SAVING_DB', 'SAVING_LDAP'];

export function UploadHistory() {
  const [uploads, setUploads] = useState<UploadedFile[]>([]);
  const [loading, setLoading] = useState(true);
  const [page, setPage] = useState(0);
  const [totalPages, setTotalPages] = useState(0);
  const [totalElements, setTotalElements] = useState(0);

  // Filters
  const [searchTerm, setSearchTerm] = useState('');
  const [statusFilter, setStatusFilter] = useState<UploadStatus | ''>('');
  const [formatFilter, setFormatFilter] = useState<FileFormat | ''>('');
  const [dateFrom, setDateFrom] = useState('');
  const [dateTo, setDateTo] = useState('');

  // Detail dialog state
  const [selectedUpload, setSelectedUpload] = useState<UploadedFile | null>(null);
  const [dialogOpen, setDialogOpen] = useState(false);
  const [uploadIssues, setUploadIssues] = useState<UploadIssues | null>(null);
  const [loadingIssues, setLoadingIssues] = useState(false);

  // Duplicate certificate detail dialog state
  const [selectedDuplicate, setSelectedDuplicate] = useState<UploadDuplicate | null>(null);
  const [duplicateDialogOpen, setDuplicateDialogOpen] = useState(false);

  // Delete confirmation dialog state
  const [deleteDialogOpen, setDeleteDialogOpen] = useState(false);
  const [uploadToDelete, setUploadToDelete] = useState<UploadedFile | null>(null);
  const [deleting, setDeleting] = useState(false);
  const [retryingId, setRetryingId] = useState<string | null>(null);

  // Tab state for detail dialog
  const [activeTab, setActiveTab] = useState<'details' | 'structure' | 'duplicates'>('details');

  const pageSize = 10;

  useEffect(() => {
    fetchUploads();
  }, [page]);

  // Auto-refresh when any upload is in progress (5s interval)
  useEffect(() => {
    const hasInProgress = uploads.some(u => IN_PROGRESS_STATUSES.includes(u.status));
    if (!hasInProgress) return;

    const interval = setInterval(() => {
      fetchUploads();
    }, 5000);

    return () => clearInterval(interval);
  }, [uploads]);

  // Fetch upload issues when detail dialog is opened (v2.1.2.2)
  useEffect(() => {
    const fetchUploadIssues = async () => {
      if (!selectedUpload || !dialogOpen) {
        setUploadIssues(null);
        return;
      }

      setLoadingIssues(true);
      try {
        const response = await uploadHistoryApi.getIssues(selectedUpload.id);
        setUploadIssues(response.data);
      } catch (error) {
        if (import.meta.env.DEV) console.error('Failed to fetch upload issues:', error);
        setUploadIssues(null);
      } finally {
        setLoadingIssues(false);
      }
    };

    fetchUploadIssues();
  }, [selectedUpload, dialogOpen]);

  const fetchUploads = async () => {
    setLoading(true);
    try {
      const response = await uploadApi.getHistory({
        page,
        size: pageSize,
        sort: 'createdAt',
        direction: 'DESC',
      });
      const data = response.data;
      setUploads(data.content);
      setTotalPages(data.totalPages);
      setTotalElements(data.totalElements);
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to fetch upload history:', error);
    } finally {
      setLoading(false);
    }
  };

  // Calculate statistics from current page data
  const stats = useMemo(() => {
    const completed = uploads.filter((u) => u.status === 'COMPLETED').length;
    const failed = uploads.filter((u) => u.status === 'FAILED').length;
    const inProgress = uploads.filter((u) =>
      IN_PROGRESS_STATUSES.includes(u.status)
    ).length;
    const total = uploads.length;

    return {
      total: totalElements,
      completed,
      failed,
      inProgress,
      completedPercent: total > 0 ? Math.round((completed / total) * 100) : 0,
      failedPercent: total > 0 ? Math.round((failed / total) * 100) : 0,
      inProgressPercent: total > 0 ? Math.round((inProgress / total) * 100) : 0,
    };
  }, [uploads, totalElements]);

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

  // Get current step index for status progress (used in detail dialog)
  const getStatusStepIndex = (status: UploadStatus): number => {
    if (status === 'FAILED') return -1;
    return STATUS_STEPS.findIndex(step => step.key === status);
  };

  // Render status badge with progress info for in-progress uploads
  const renderStatusProgress = (upload: UploadedFile) => {
    const { status } = upload;

    // 실패
    if (status === 'FAILED') {
      return (
        <div className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400">
          <XCircle className="w-4 h-4" />
          <span className="text-sm font-medium">실패</span>
        </div>
      );
    }

    // 완료
    if (status === 'COMPLETED') {
      return (
        <div className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400">
          <CheckCircle className="w-4 h-4" />
          <span className="text-sm font-medium">완료</span>
        </div>
      );
    }

    // 진행 중 (PENDING, UPLOADING, PARSING, PROCESSING, VALIDATING, SAVING_DB, SAVING_LDAP)
    const hasProgress = upload.processedEntries !== undefined && upload.totalEntries && upload.totalEntries > 0;
    const progressPct = hasProgress ? Math.round((upload.processedEntries! / upload.totalEntries!) * 100) : 0;

    return (
      <div className="space-y-1">
        <div className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400">
          <Loader2 className="w-4 h-4 animate-spin" />
          <span className="text-sm font-medium">
            {status === 'PROCESSING' ? '처리 중' : '진행 중'}
          </span>
        </div>
        {hasProgress && (
          <div className="px-1">
            <div className="w-full bg-gray-200 dark:bg-gray-600 rounded-full h-1.5">
              <div
                className="bg-blue-500 h-1.5 rounded-full transition-all duration-500"
                style={{ width: `${progressPct}%` }}
              />
            </div>
            <p className="text-xs text-gray-500 dark:text-gray-400 mt-0.5">
              {upload.processedEntries!.toLocaleString()}/{upload.totalEntries!.toLocaleString()} ({progressPct}%)
            </p>
          </div>
        )}
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

  const handleViewDetail = async (upload: UploadedFile) => {
    try {
      // Fetch full upload details including ldapUploadedCount
      const response = await uploadHistoryApi.getDetail(upload.id);
      const fullUploadData = response.data.data; // ApiResponse<UploadedFile> structure
      setSelectedUpload(fullUploadData ?? null);
      setActiveTab('details'); // Reset to details tab
      setDialogOpen(true);
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to fetch upload details:', error);
      // Fallback to basic upload data from history list
      setSelectedUpload(upload);
      setActiveTab('details'); // Reset to details tab
      setDialogOpen(true);
    }
  };

  const closeDialog = () => {
    setDialogOpen(false);
    setSelectedUpload(null);
  };

  const handleDeleteClick = (upload: UploadedFile) => {
    setUploadToDelete(upload);
    setDeleteDialogOpen(true);
  };

  const closeDeleteDialog = () => {
    setDeleteDialogOpen(false);
    setUploadToDelete(null);
  };

  const handleDeleteConfirm = async () => {
    if (!uploadToDelete) return;

    setDeleting(true);
    try {
      await uploadApi.deleteUpload(uploadToDelete.id);
      // Refresh the list
      await fetchUploads();
      closeDeleteDialog();
      // Show success message (optional - can add toast notification here)
      if (import.meta.env.DEV) console.log('Upload deleted successfully');
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to delete upload:', error);
      alert('업로드 삭제에 실패했습니다.');
    } finally {
      setDeleting(false);
    }
  };

  const handleRetry = async (upload: UploadedFile) => {
    setRetryingId(upload.id);
    try {
      await uploadApi.retryUpload(upload.id);
      // Refresh the list to show updated status
      await fetchUploads();
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to retry upload:', error);
      alert('재시도에 실패했습니다.');
    } finally {
      setRetryingId(null);
    }
  };

  const filteredUploads = uploads.filter((upload) => {
    const matchesSearch = upload.fileName.toLowerCase().includes(searchTerm.toLowerCase());
    const matchesStatus = !statusFilter || upload.status === statusFilter;
    const matchesFormat = !formatFilter || upload.fileFormat === formatFilter;

    // Date range filter
    let matchesDateFrom = true;
    let matchesDateTo = true;
    if (dateFrom && upload.createdAt) {
      const uploadDate = new Date(upload.createdAt.replace(' ', 'T').replace(/\+(\d{2})$/, '+$1:00'));
      matchesDateFrom = uploadDate >= new Date(dateFrom);
    }
    if (dateTo && upload.createdAt) {
      const uploadDate = new Date(upload.createdAt.replace(' ', 'T').replace(/\+(\d{2})$/, '+$1:00'));
      matchesDateTo = uploadDate <= new Date(dateTo + 'T23:59:59');
    }

    return matchesSearch && matchesStatus && matchesFormat && matchesDateFrom && matchesDateTo;
  });

  const clearFilters = () => {
    setSearchTerm('');
    setStatusFilter('');
    setFormatFilter('');
    setDateFrom('');
    setDateTo('');
  };

  const hasActiveFilters = searchTerm || statusFilter || formatFilter || dateFrom || dateTo;

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-amber-500 to-orange-600 shadow-lg">
            <Clock className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">업로드 이력</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              LDIF 및 Master List 파일 업로드 이력을 확인합니다.
            </p>
          </div>
          <div className="flex gap-2">
            <button
              onClick={fetchUploads}
              disabled={loading}
              className="inline-flex items-center gap-2 px-3 py-2 rounded-lg text-sm font-medium text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            >
              <RefreshCw className={cn('w-4 h-4', loading && 'animate-spin')} />
            </button>
            <Link
              to="/upload"
              className="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-white bg-gradient-to-r from-indigo-500 to-purple-500 hover:from-indigo-600 hover:to-purple-600 transition-all shadow-md hover:shadow-lg"
            >
              <Upload className="w-4 h-4" />
              새 업로드
            </Link>
          </div>
        </div>
      </div>

      {/* Filters Card */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md mb-4 p-4">
        <div className="flex items-center gap-2 mb-3">
          <Filter className="w-4 h-4 text-blue-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">검색 필터</h3>
        </div>

        <div className="grid grid-cols-2 md:grid-cols-5 gap-3">
          {/* File Format Filter */}
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              파일 형식
            </label>
            <select
              value={formatFilter}
              onChange={(e) => setFormatFilter(e.target.value as FileFormat | '')}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="">전체</option>
              <option value="LDIF">LDIF</option>
              <option value="ML">Master List</option>
            </select>
          </div>

          {/* Status Filter */}
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              업로드 상태
            </label>
            <select
              value={statusFilter}
              onChange={(e) => setStatusFilter(e.target.value as UploadStatus | '')}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="">전체</option>
              <option value="COMPLETED">완료</option>
              <option value="FAILED">실패</option>
              <option value="PENDING">대기</option>
              <option value="UPLOADING">업로드 중</option>
              <option value="PARSING">파싱 중</option>
              <option value="PROCESSING">처리 중</option>
              <option value="VALIDATING">검증 중</option>
              <option value="SAVING_DB">DB 저장 중</option>
              <option value="SAVING_LDAP">LDAP 저장 중</option>
            </select>
          </div>

          {/* Date From */}
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              시작 날짜
            </label>
            <input
              type="date"
              value={dateFrom}
              onChange={(e) => setDateFrom(e.target.value)}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          {/* Date To */}
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              종료 날짜
            </label>
            <input
              type="date"
              value={dateTo}
              onChange={(e) => setDateTo(e.target.value)}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          {/* Search & Actions */}
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              검색
            </label>
            <div className="flex gap-2">
              <div className="relative flex-1">
                <Search className="absolute left-2.5 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                <input
                  type="text"
                  placeholder="파일명..."
                  value={searchTerm}
                  onChange={(e) => setSearchTerm(e.target.value)}
                  className="w-full pl-8 pr-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
                />
              </div>
              {hasActiveFilters && (
                <button
                  onClick={clearFilters}
                  className="px-3 py-2 text-xs font-medium text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200 border border-gray-200 dark:border-gray-600 rounded-lg hover:bg-gray-50 dark:hover:bg-gray-700"
                >
                  초기화
                </button>
              )}
            </div>
          </div>
        </div>
      </div>

      {/* Statistics Cards - Hierarchical Layout */}
      <div className="mb-4">
        {/* Main Total Card */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-5 border-l-4 border-blue-500">
          <div className="flex items-center gap-3 mb-4">
            <div className="p-2 rounded-lg bg-blue-50 dark:bg-blue-900/30">
              <FileText className="w-6 h-6 text-blue-500" />
            </div>
            <div className="flex-1">
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">총 업로드</p>
              <p className="text-2xl font-bold text-blue-600 dark:text-blue-400">{stats.total.toLocaleString()}</p>
            </div>
          </div>

          {/* Breakdown Cards - Nested */}
          <div className="grid grid-cols-3 gap-3 pt-4 border-t border-gray-200 dark:border-gray-700">
            {/* Completed */}
            <div className="bg-green-50 dark:bg-green-900/20 rounded-lg p-3 border border-green-200 dark:border-green-700">
              <div className="flex items-center gap-2 mb-1">
                <CheckCircle className="w-4 h-4 text-green-500" />
                <p className="text-xs text-gray-600 dark:text-gray-400 font-medium">완료</p>
              </div>
              <p className="text-lg font-bold text-green-600 dark:text-green-400">{stats.completed.toLocaleString()}</p>
              <p className="text-xs text-gray-500 dark:text-gray-400">{stats.completedPercent}%</p>
            </div>

            {/* Failed */}
            <div className="bg-red-50 dark:bg-red-900/20 rounded-lg p-3 border border-red-200 dark:border-red-700">
              <div className="flex items-center gap-2 mb-1">
                <XCircle className="w-4 h-4 text-red-500" />
                <p className="text-xs text-gray-600 dark:text-gray-400 font-medium">실패</p>
              </div>
              <p className="text-lg font-bold text-red-600 dark:text-red-400">{stats.failed.toLocaleString()}</p>
              <p className="text-xs text-gray-500 dark:text-gray-400">{stats.failedPercent}%</p>
            </div>

            {/* In Progress */}
            <div className="bg-yellow-50 dark:bg-yellow-900/20 rounded-lg p-3 border border-yellow-200 dark:border-yellow-700">
              <div className="flex items-center gap-2 mb-1">
                <Loader2 className="w-4 h-4 text-yellow-500" />
                <p className="text-xs text-gray-600 dark:text-gray-400 font-medium">진행 중</p>
              </div>
              <p className="text-lg font-bold text-yellow-600 dark:text-yellow-400">{stats.inProgress.toLocaleString()}</p>
              <p className="text-xs text-gray-500 dark:text-gray-400">{stats.inProgressPercent}%</p>
            </div>
          </div>
        </div>
      </div>

      {/* Table */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center gap-2">
          <FileText className="w-4 h-4 text-orange-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">업로드 이력</h3>
          <span className="px-2 py-0.5 text-xs rounded-full bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300">
            {filteredUploads.length}건
          </span>
        </div>

        {loading ? (
          <div className="flex items-center justify-center py-20">
            <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
          </div>
        ) : filteredUploads.length === 0 ? (
          <div className="flex flex-col items-center justify-center py-20 text-gray-500 dark:text-gray-400">
            <AlertCircle className="w-12 h-12 mb-4 opacity-50" />
            <p className="text-lg font-medium">업로드 이력이 없습니다.</p>
            <p className="text-sm">새 파일을 업로드하거나 필터를 조정하세요.</p>
            <Link
              to="/upload"
              className="mt-4 inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-white bg-gradient-to-r from-indigo-500 to-purple-500"
            >
              <Upload className="w-4 h-4" />
              파일 업로드하기
            </Link>
          </div>
        ) : (
          <>
            <div className="overflow-x-auto">
              <table className="w-full">
                <thead className="bg-slate-100 dark:bg-gray-700">
                  <tr>
                    <th className="px-6 py-3 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">
                      파일명
                    </th>
                    <th className="px-6 py-3 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">
                      형식
                    </th>
                    <th className="px-6 py-3 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">
                      크기
                    </th>
                    <th className="px-6 py-3 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">
                      진행 상태
                    </th>
                    <th className="px-6 py-3 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">
                      인증서
                    </th>
                    <th className="px-6 py-3 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">
                      업로드 일시
                    </th>
                    <th className="px-6 py-3 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">
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
                      <td className="px-6 py-4">{renderStatusProgress(upload)}</td>
                      <td className="px-6 py-4">
                        <div className="flex flex-wrap gap-1 text-xs">
                          {((upload.cscaCount ?? 0) > 0 || (!upload.cscaCount && !upload.dscCount && !upload.dscNcCount && (upload.certificateCount ?? 0) > 0)) && (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded bg-purple-100 dark:bg-purple-900/30 text-purple-600 dark:text-purple-400">
                              <ShieldCheck className="w-3 h-3" />
                              CSCA {upload.cscaCount || upload.certificateCount}
                            </span>
                          )}
                          {upload.mlscCount && upload.mlscCount > 0 && (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded bg-indigo-100 dark:bg-indigo-900/30 text-indigo-600 dark:text-indigo-400">
                              <FileText className="w-3 h-3" />
                              MLSC {upload.mlscCount}
                            </span>
                          )}
                          {(upload.dscCount ?? 0) > 0 && (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded bg-blue-100 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400">
                              <HardDrive className="w-3 h-3" />
                              DSC {upload.dscCount}
                            </span>
                          )}
                          {(upload.dscNcCount ?? 0) > 0 && (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded bg-orange-100 dark:bg-orange-900/30 text-orange-600 dark:text-orange-400">
                              <HardDrive className="w-3 h-3" />
                              DSC_NC {upload.dscNcCount}
                            </span>
                          )}
                          {(upload.crlCount ?? 0) > 0 && (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded bg-amber-100 dark:bg-amber-900/30 text-amber-600 dark:text-amber-400">
                              <AlertCircle className="w-3 h-3" />
                              CRL {upload.crlCount}
                            </span>
                          )}
                          {(upload.mlCount ?? 0) > 0 && (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded bg-teal-100 dark:bg-teal-900/30 text-teal-600 dark:text-teal-400">
                              <FileText className="w-3 h-3" />
                              ML {upload.mlCount}
                            </span>
                          )}
                          {upload.certificateCount === 0 && upload.crlCount === 0 && !upload.mlCount && (
                            <span className="text-gray-400 dark:text-gray-500">-</span>
                          )}
                        </div>
                      </td>
                      <td className="px-6 py-4 text-sm text-gray-500 dark:text-gray-400">
                        {formatDate(upload.createdAt ?? '')}
                      </td>
                      <td className="px-6 py-4 text-right">
                        <div className="flex items-center justify-end gap-2">
                          <button
                            onClick={() => handleViewDetail(upload)}
                            className="inline-flex items-center gap-1 px-3 py-1.5 rounded-lg text-sm font-medium text-blue-600 dark:text-blue-400 hover:bg-blue-50 dark:hover:bg-blue-900/20 transition-colors"
                          >
                            <Eye className="w-4 h-4" />
                            상세
                          </button>
                          {upload.status === 'FAILED' && (
                            <button
                              onClick={() => handleRetry(upload)}
                              disabled={retryingId === upload.id}
                              className="inline-flex items-center gap-1 px-3 py-1.5 rounded-lg text-sm font-medium text-amber-600 dark:text-amber-400 hover:bg-amber-50 dark:hover:bg-amber-900/20 transition-colors disabled:opacity-50"
                              title="실패한 업로드 재시도"
                            >
                              <RefreshCw className={cn("w-4 h-4", retryingId === upload.id && "animate-spin")} />
                              재시도
                            </button>
                          )}
                          {(upload.status === 'FAILED' || upload.status === 'PENDING') && (
                            <button
                              onClick={() => handleDeleteClick(upload)}
                              className="inline-flex items-center gap-1 px-3 py-1.5 rounded-lg text-sm font-medium text-red-600 dark:text-red-400 hover:bg-red-50 dark:hover:bg-red-900/20 transition-colors"
                              title="업로드 삭제"
                            >
                              <Trash2 className="w-4 h-4" />
                              삭제
                            </button>
                          )}
                        </div>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>

            {/* Pagination */}
            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex items-center justify-between">
              <p className="text-xs text-gray-500 dark:text-gray-400">
                총 {totalElements}개 중 {page * pageSize + 1}-{Math.min((page + 1) * pageSize, totalElements)}개 표시
              </p>
              <div className="flex items-center gap-1">
                <button
                  onClick={() => setPage((p) => Math.max(0, p - 1))}
                  disabled={page === 0}
                  className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
                >
                  <ChevronLeft className="w-4 h-4" />
                </button>
                <span className="px-3 text-sm text-gray-600 dark:text-gray-300">
                  {page + 1} / {totalPages || 1}
                </span>
                <button
                  onClick={() => setPage((p) => Math.min(totalPages - 1, p + 1))}
                  disabled={page >= totalPages - 1}
                  className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
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

          {/* Dialog Content - Wide layout without vertical scroll */}
          <div className="relative bg-white dark:bg-gray-800 rounded-2xl shadow-2xl w-full max-w-6xl mx-4 max-h-[85vh] flex flex-col">
            {/* Header */}
            <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700">
              <div className="flex items-center justify-between mb-3">
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

              {/* Tabs - Show if Master List/LDIF file or has duplicates */}
              {((selectedUpload.fileFormat === 'ML' || selectedUpload.fileFormat === 'MASTER_LIST' || selectedUpload.fileFormat === 'LDIF') || (uploadIssues && uploadIssues.totalDuplicates > 0)) && (
                <div className="flex gap-2">
                  <button
                    onClick={() => setActiveTab('details')}
                    className={cn(
                      'px-4 py-2 text-sm font-medium rounded-lg transition-colors',
                      activeTab === 'details'
                        ? 'bg-blue-100 dark:bg-blue-900/50 text-blue-700 dark:text-blue-300'
                        : 'text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700'
                    )}
                  >
                    상세 정보
                  </button>
                  {(selectedUpload.fileFormat === 'ML' ||
                    selectedUpload.fileFormat === 'MASTER_LIST' ||
                    selectedUpload.fileFormat === 'LDIF') && (
                    <button
                      onClick={() => setActiveTab('structure')}
                      className={cn(
                        'px-4 py-2 text-sm font-medium rounded-lg transition-colors',
                        activeTab === 'structure'
                          ? 'bg-blue-100 dark:bg-blue-900/50 text-blue-700 dark:text-blue-300'
                          : 'text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700'
                      )}
                    >
                      {selectedUpload.fileFormat === 'LDIF' ? 'LDIF 구조' : 'Master List 구조'}
                    </button>
                  )}
                  {uploadIssues && uploadIssues.totalDuplicates > 0 && (
                    <button
                      onClick={() => setActiveTab('duplicates')}
                      className={cn(
                        'px-4 py-2 text-sm font-medium rounded-lg transition-colors flex items-center gap-2',
                        activeTab === 'duplicates'
                          ? 'bg-yellow-100 dark:bg-yellow-900/50 text-yellow-700 dark:text-yellow-300'
                          : 'text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700'
                      )}
                    >
                      중복 인증서
                      <span className="px-1.5 py-0.5 text-xs font-medium rounded bg-yellow-200 dark:bg-yellow-900/70 text-yellow-800 dark:text-yellow-200">
                        {uploadIssues.totalDuplicates}
                      </span>
                    </button>
                  )}
                </div>
              )}
            </div>

            {/* Body */}
            <div className="p-5 flex-1 min-h-0 overflow-y-auto">
              {/* Details Tab */}
              {activeTab === 'details' && (
              <div className="space-y-4">
                  {/* Status Progress - Compact horizontal */}
                  <div>
                    <h3 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">진행 상태</h3>
                    <div className="flex items-center justify-between bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3">
                      {STATUS_STEPS.map((step, index) => {
                        const currentIndex = getStatusStepIndex(selectedUpload.status);
                        const isPassed = index < currentIndex || selectedUpload.status === 'COMPLETED';
                        const isCurrent = index === currentIndex && selectedUpload.status !== 'COMPLETED';
                        const isFailed = selectedUpload.status === 'FAILED' && index === 0;

                        return (
                          <div key={step.key} className="flex flex-col items-center">
                            <div
                              className={cn(
                                'flex items-center justify-center w-8 h-8 rounded-full mb-1 transition-all',
                                isPassed && 'bg-green-500 text-white',
                                isCurrent && 'bg-blue-500 text-white animate-pulse',
                                isFailed && 'bg-red-500 text-white',
                                !isPassed && !isCurrent && !isFailed && 'bg-gray-200 dark:bg-gray-600 text-gray-400'
                              )}
                            >
                              {isPassed ? (
                                <CheckCircle className="w-4 h-4" />
                              ) : isCurrent ? (
                                <Loader2 className="w-4 h-4 animate-spin" />
                              ) : isFailed ? (
                                <XCircle className="w-4 h-4" />
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

                  {/* Processing Progress Bar (v2.9.3) */}
                  {IN_PROGRESS_STATUSES.includes(selectedUpload.status) && selectedUpload.totalEntries && selectedUpload.totalEntries > 0 && (
                    <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg p-3">
                      <div className="flex items-center justify-between mb-2">
                        <div className="flex items-center gap-2">
                          <Loader2 className="w-4 h-4 animate-spin text-blue-500" />
                          <span className="text-sm font-medium text-blue-700 dark:text-blue-300">처리 진행률</span>
                        </div>
                        <span className="text-sm font-bold text-blue-700 dark:text-blue-300">
                          {selectedUpload.processedEntries?.toLocaleString() || 0} / {selectedUpload.totalEntries.toLocaleString()}
                        </span>
                      </div>
                      <div className="w-full bg-blue-200 dark:bg-blue-800 rounded-full h-2.5">
                        <div
                          className="bg-blue-500 h-2.5 rounded-full transition-all duration-500"
                          style={{ width: `${Math.round(((selectedUpload.processedEntries || 0) / selectedUpload.totalEntries) * 100)}%` }}
                        />
                      </div>
                      <p className="text-xs text-blue-600 dark:text-blue-400 mt-1 text-right">
                        {Math.round(((selectedUpload.processedEntries || 0) / selectedUpload.totalEntries) * 100)}%
                      </p>
                    </div>
                  )}

                  {/* Error Message - Compact */}
                  {selectedUpload.errorMessage && (
                    <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg p-3">
                      <div className="flex items-start gap-2">
                        <XCircle className="w-4 h-4 text-red-500 mt-0.5 flex-shrink-0" />
                        <div>
                          <h4 className="text-xs font-medium text-red-800 dark:text-red-300">오류 발생</h4>
                          <p className="text-xs text-red-600 dark:text-red-400 mt-0.5">
                            {selectedUpload.errorMessage}
                          </p>
                        </div>
                      </div>
                    </div>
                  )}

                  {/* Certificate & File Info Grid - Compact */}
                  <div className="grid grid-cols-5 gap-2">
                    <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                      <span className="text-xs text-gray-500 dark:text-gray-400">형식</span>
                      <div className="mt-0.5">{getFormatBadge(selectedUpload.fileFormat)}</div>
                    </div>
                    <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                      <span className="text-xs text-gray-500 dark:text-gray-400">크기</span>
                      <p className="text-sm font-medium text-gray-900 dark:text-white mt-0.5">
                        {formatFileSize(selectedUpload.fileSize)}
                      </p>
                    </div>
                    <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                      <span className="text-xs text-gray-500 dark:text-gray-400">총 인증서</span>
                      <p className="text-sm font-medium text-gray-900 dark:text-white mt-0.5">
                        {selectedUpload.certificateCount}개
                      </p>
                    </div>
                    <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                      <span className="text-xs text-gray-500 dark:text-gray-400">업로드</span>
                      <p className="text-xs font-medium text-gray-900 dark:text-white mt-0.5">
                        {formatDate(selectedUpload.createdAt ?? '')}
                      </p>
                    </div>
                    <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                      <span className="text-xs text-gray-500 dark:text-gray-400">완료</span>
                      <p className="text-xs font-medium text-gray-900 dark:text-white mt-0.5">
                        {formatDate(selectedUpload.updatedAt ?? '')}
                      </p>
                    </div>
                  </div>

                  {/* Certificate Type Breakdown */}
                  <div className="grid grid-cols-3 md:grid-cols-6 gap-2">
                    <div className="bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-800 rounded-lg p-2 text-center">
                      <p className="text-lg font-bold text-purple-600 dark:text-purple-400">
                        {selectedUpload.cscaCount}
                      </p>
                      <span className="text-xs text-purple-700 dark:text-purple-300">CSCA</span>
                    </div>
                    <div className="bg-indigo-50 dark:bg-indigo-900/20 border border-indigo-200 dark:border-indigo-800 rounded-lg p-2 text-center">
                      <p className="text-lg font-bold text-indigo-600 dark:text-indigo-400">
                        {selectedUpload.mlscCount || 0}
                      </p>
                      <span className="text-xs text-indigo-700 dark:text-indigo-300">MLSC</span>
                    </div>
                    <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg p-2 text-center">
                      <p className="text-lg font-bold text-blue-600 dark:text-blue-400">
                        {selectedUpload.dscCount || 0}
                      </p>
                      <span className="text-xs text-blue-700 dark:text-blue-300">DSC</span>
                    </div>
                    <div className="bg-orange-50 dark:bg-orange-900/20 border border-orange-200 dark:border-orange-800 rounded-lg p-2 text-center">
                      <p className="text-lg font-bold text-orange-600 dark:text-orange-400">
                        {selectedUpload.dscNcCount || 0}
                      </p>
                      <span className="text-xs text-orange-700 dark:text-orange-300">DSC_NC</span>
                    </div>
                    <div className="bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-lg p-2 text-center">
                      <p className="text-lg font-bold text-amber-600 dark:text-amber-400">
                        {selectedUpload.crlCount}
                      </p>
                      <span className="text-xs text-amber-700 dark:text-amber-300">CRL</span>
                    </div>
                    <div className="bg-teal-50 dark:bg-teal-900/20 border border-teal-200 dark:border-teal-800 rounded-lg p-2 text-center">
                      <p className="text-lg font-bold text-teal-600 dark:text-teal-400">
                        {selectedUpload.mlCount || 0}
                      </p>
                      <span className="text-xs text-teal-700 dark:text-teal-300">ML</span>
                    </div>
                  </div>

                  {/* Validation Statistics — shared component */}
                  {selectedUpload.validation && (
                    <ValidationSummaryPanel
                      data={{
                        validCount: selectedUpload.validation.validCount,
                        invalidCount: selectedUpload.validation.invalidCount,
                        pendingCount: selectedUpload.validation.pendingCount,
                        expiredValidCount: selectedUpload.validation.expiredValidCount,
                        errorCount: selectedUpload.validation.errorCount,
                        trustChainValidCount: selectedUpload.validation.trustChainValidCount,
                        trustChainInvalidCount: selectedUpload.validation.trustChainInvalidCount,
                        cscaNotFoundCount: selectedUpload.validation.cscaNotFoundCount,
                        expiredCount: selectedUpload.validation.expiredCount,
                        validPeriodCount: selectedUpload.validation.validPeriodCount,
                        revokedCount: selectedUpload.validation.revokedCount,
                        icaoCompliantCount: selectedUpload.validation.icaoCompliantCount,
                        icaoNonCompliantCount: selectedUpload.validation.icaoNonCompliantCount,
                        icaoWarningCount: selectedUpload.validation.icaoWarningCount,
                        duplicateCount: uploadIssues?.totalDuplicates,
                        totalCertificates: selectedUpload.totalEntries,
                        processedCount: selectedUpload.processedEntries,
                      }}
                    />
                  )}

                  {/* Master List Extraction Statistics (v2.1.1) */}
                  {(selectedUpload.cscaExtractedFromMl || selectedUpload.cscaDuplicates) && (
                    <div className="bg-indigo-50 dark:bg-indigo-900/20 border border-indigo-200 dark:border-indigo-800 rounded-lg p-3">
                      <div className="flex items-center gap-2 mb-2">
                        <Database className="w-4 h-4 text-indigo-600 dark:text-indigo-400" />
                        <span className="text-xs font-semibold text-indigo-700 dark:text-indigo-300">Master List 인증서 추출 (MLSC + CSCA + LC)</span>
                        <span className="px-1.5 py-0.5 text-xs font-medium rounded bg-indigo-100 dark:bg-indigo-900/50 text-indigo-700 dark:text-indigo-300">
                          v2.1.1
                        </span>
                      </div>
                      <div className="grid grid-cols-3 gap-2">
                        <div className="bg-white dark:bg-gray-800 rounded p-2 text-center">
                          <p className="text-lg font-bold text-indigo-600 dark:text-indigo-400">
                            {selectedUpload.cscaExtractedFromMl || 0}
                          </p>
                          <span className="text-xs text-indigo-700 dark:text-indigo-300">총 추출</span>
                        </div>
                        <div className="bg-white dark:bg-gray-800 rounded p-2 text-center">
                          <p className="text-lg font-bold text-amber-600 dark:text-amber-400">
                            {selectedUpload.cscaDuplicates || 0}
                          </p>
                          <span className="text-xs text-amber-700 dark:text-amber-300">중복</span>
                        </div>
                        <div className="bg-white dark:bg-gray-800 rounded p-2 text-center">
                          <p className="text-lg font-bold text-green-600 dark:text-green-400">
                            {selectedUpload.cscaExtractedFromMl && selectedUpload.cscaExtractedFromMl > 0
                              ? ((selectedUpload.cscaExtractedFromMl - (selectedUpload.cscaDuplicates || 0)) / selectedUpload.cscaExtractedFromMl * 100).toFixed(0)
                              : '0'}%
                          </p>
                          <span className="text-xs text-green-700 dark:text-green-300">신규</span>
                        </div>
                      </div>
                    </div>
                  )}


                  {/* LDAP Storage Warning - Data Consistency Check (v2.0.0) */}
                  {selectedUpload.status === 'COMPLETED' &&
                   selectedUpload.certificateCount &&
                   selectedUpload.certificateCount > 0 &&
                   (!selectedUpload.ldapUploadedCount || selectedUpload.ldapUploadedCount === 0) && (
                    <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg p-3">
                      <div className="flex items-start gap-2">
                        <AlertTriangle className="w-5 h-5 text-red-500 mt-0.5 flex-shrink-0" />
                        <div className="flex-1">
                          <h4 className="text-sm font-semibold text-red-800 dark:text-red-300">
                            ⚠️ LDAP 저장 실패 - 데이터 불일치 감지
                          </h4>
                          <p className="text-xs text-red-600 dark:text-red-400 mt-1">
                            {selectedUpload.certificateCount}개의 인증서가 PostgreSQL에는 저장되었지만 LDAP에는 저장되지 않았습니다.
                          </p>
                          <p className="text-xs text-red-600 dark:text-red-400 mt-1">
                            이 파일을 삭제하고 다시 업로드하거나, 관리자에게 문의하세요.
                          </p>
                          <div className="mt-2 flex gap-2">
                            <span className="px-2 py-1 text-xs font-medium rounded bg-red-100 dark:bg-red-900/50 text-red-700 dark:text-red-300">
                              DB: {selectedUpload.certificateCount}개
                            </span>
                            <span className="px-2 py-1 text-xs font-medium rounded bg-red-100 dark:bg-red-900/50 text-red-700 dark:text-red-300">
                              LDAP: {selectedUpload.ldapUploadedCount || 0}개
                            </span>
                          </div>
                        </div>
                      </div>
                    </div>
                  )}

                  {/* Upload ID - Compact */}
                  <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                    <span className="text-xs text-gray-500 dark:text-gray-400">업로드 ID</span>
                    <p className="text-xs font-mono text-gray-900 dark:text-white mt-0.5 break-all">
                      {selectedUpload.id}
                    </p>
                  </div>
              </div>
              )}

              {/* Structure Tab - LDIF/Master List Structure */}
              {activeTab === 'structure' && (
                <div className="max-h-[600px] overflow-y-auto">
                  {selectedUpload.fileFormat === 'LDIF' ? (
                    <LdifStructure uploadId={selectedUpload.id} />
                  ) : (
                    <MasterListStructure uploadId={selectedUpload.id} />
                  )}
                </div>
              )}

              {/* Duplicates Tab - Duplicate Certificates Tree */}
              {activeTab === 'duplicates' && uploadIssues && (
                <div className="space-y-4">
                  {/* Header with export buttons */}
                  <div className="flex items-center justify-between pb-3 border-b border-gray-200 dark:border-gray-700">
                    <div className="flex items-center gap-2">
                      <AlertCircle className="w-5 h-5 text-yellow-600 dark:text-yellow-400" />
                      <h3 className="text-base font-semibold text-gray-900 dark:text-white">
                        중복 인증서 목록
                      </h3>
                      <span className="px-2 py-1 text-xs font-medium rounded bg-yellow-100 dark:bg-yellow-900/50 text-yellow-700 dark:text-yellow-300">
                        총 {uploadIssues.totalDuplicates}건
                      </span>
                    </div>

                    {/* CSV Export Buttons */}
                    <div className="flex items-center gap-2">
                      <button
                        onClick={() => exportDuplicateStatisticsToCsv(
                          uploadIssues.byType,
                          uploadIssues.totalDuplicates,
                          `duplicate-stats-${selectedUpload.id}.csv`
                        )}
                        className="flex items-center gap-1 px-3 py-1.5 text-xs font-medium text-yellow-700 dark:text-yellow-300 bg-white dark:bg-gray-800 border border-yellow-300 dark:border-yellow-700 rounded-lg hover:bg-yellow-50 dark:hover:bg-yellow-900/30 transition-colors"
                        title="통계 내보내기"
                      >
                        <Download className="w-3.5 h-3.5" />
                        통계
                      </button>
                      <button
                        onClick={() => exportDuplicatesToCsv(
                          uploadIssues.duplicates,
                          `duplicates-${selectedUpload.id}.csv`
                        )}
                        className="flex items-center gap-1 px-3 py-1.5 text-xs font-medium text-yellow-700 dark:text-yellow-300 bg-white dark:bg-gray-800 border border-yellow-300 dark:border-yellow-700 rounded-lg hover:bg-yellow-50 dark:hover:bg-yellow-900/30 transition-colors"
                        title="전체 내보내기"
                      >
                        <Download className="w-3.5 h-3.5" />
                        전체
                      </button>
                    </div>
                  </div>

                  {/* Summary by type */}
                  <div className="grid grid-cols-5 gap-2">
                    {uploadIssues.byType.CSCA > 0 && (
                      <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg p-3 text-center">
                        <p className="text-lg font-bold text-blue-600 dark:text-blue-400">
                          {uploadIssues.byType.CSCA}
                        </p>
                        <span className="text-xs text-blue-700 dark:text-blue-300">CSCA</span>
                      </div>
                    )}
                    {uploadIssues.byType.MLSC > 0 && (
                      <div className="bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-800 rounded-lg p-3 text-center">
                        <p className="text-lg font-bold text-purple-600 dark:text-purple-400">
                          {uploadIssues.byType.MLSC}
                        </p>
                        <span className="text-xs text-purple-700 dark:text-purple-300">MLSC</span>
                      </div>
                    )}
                    {uploadIssues.byType.DSC > 0 && (
                      <div className="bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800 rounded-lg p-3 text-center">
                        <p className="text-lg font-bold text-green-600 dark:text-green-400">
                          {uploadIssues.byType.DSC}
                        </p>
                        <span className="text-xs text-green-700 dark:text-green-300">DSC</span>
                      </div>
                    )}
                    {uploadIssues.byType.DSC_NC > 0 && (
                      <div className="bg-orange-50 dark:bg-orange-900/20 border border-orange-200 dark:border-orange-800 rounded-lg p-3 text-center">
                        <p className="text-lg font-bold text-orange-600 dark:text-orange-400">
                          {uploadIssues.byType.DSC_NC}
                        </p>
                        <span className="text-xs text-orange-700 dark:text-orange-300">DSC_NC</span>
                      </div>
                    )}
                    {uploadIssues.byType.CRL > 0 && (
                      <div className="bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-lg p-3 text-center">
                        <p className="text-lg font-bold text-amber-600 dark:text-amber-400">
                          {uploadIssues.byType.CRL}
                        </p>
                        <span className="text-xs text-amber-700 dark:text-amber-300">CRL</span>
                      </div>
                    )}
                  </div>

                  {/* Tree View - Scrollable */}
                  <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-lg p-4 max-h-[500px] overflow-y-auto">
                    <DuplicateCertificatesTree duplicates={uploadIssues.duplicates} />
                  </div>

                  {loadingIssues && (
                    <div className="bg-gray-50 dark:bg-gray-800/50 border border-gray-200 dark:border-gray-700 rounded-lg p-4">
                      <div className="flex items-center gap-2 justify-center">
                        <Loader2 className="w-4 h-4 animate-spin text-gray-400" />
                        <span className="text-sm text-gray-500 dark:text-gray-400">업로드 이슈 조회 중...</span>
                      </div>
                    </div>
                  )}
                </div>
              )}
            </div>

            {/* Footer */}
            <div className="flex justify-end gap-3 px-5 py-3 border-t border-gray-200 dark:border-gray-700">
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

      {/* Delete Confirmation Dialog */}
      {deleteDialogOpen && uploadToDelete && (
        <div className="fixed inset-0 z-50 flex items-center justify-center">
          {/* Backdrop */}
          <div
            className="absolute inset-0 bg-black/50 backdrop-blur-sm"
            onClick={closeDeleteDialog}
          />

          {/* Dialog Content */}
          <div className="relative bg-white dark:bg-gray-800 rounded-2xl shadow-2xl w-full max-w-md mx-4">
            {/* Header */}
            <div className="flex items-center gap-3 px-5 py-4 border-b border-gray-200 dark:border-gray-700">
              <div className="p-2 rounded-lg bg-red-100 dark:bg-red-900/30">
                <Trash2 className="w-5 h-5 text-red-600 dark:text-red-400" />
              </div>
              <div>
                <h2 className="text-lg font-semibold text-gray-900 dark:text-white">
                  업로드 삭제
                </h2>
                <p className="text-sm text-gray-500 dark:text-gray-400">
                  정말 삭제하시겠습니까?
                </p>
              </div>
            </div>

            {/* Body */}
            <div className="p-5 space-y-4">
              <div className="bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800 rounded-lg p-3">
                <div className="flex items-start gap-2">
                  <AlertCircle className="w-5 h-5 text-yellow-600 dark:text-yellow-400 mt-0.5 flex-shrink-0" />
                  <div>
                    <h4 className="text-sm font-medium text-yellow-800 dark:text-yellow-300">
                      경고
                    </h4>
                    <p className="text-sm text-yellow-700 dark:text-yellow-400 mt-1">
                      다음 데이터가 모두 삭제됩니다:
                    </p>
                    <ul className="text-sm text-yellow-700 dark:text-yellow-400 mt-2 ml-4 list-disc space-y-1">
                      <li>업로드 기록</li>
                      <li>인증서 데이터 (CSCA, DSC, DSC_NC)</li>
                      <li>CRL 데이터</li>
                      <li>Master List 데이터</li>
                      <li>임시 파일</li>
                    </ul>
                  </div>
                </div>
              </div>

              <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3 space-y-2">
                <div className="flex justify-between items-center">
                  <span className="text-sm text-gray-600 dark:text-gray-400">파일명</span>
                  <span className="text-sm font-medium text-gray-900 dark:text-white">
                    {uploadToDelete.fileName}
                  </span>
                </div>
                <div className="flex justify-between items-center">
                  <span className="text-sm text-gray-600 dark:text-gray-400">상태</span>
                  {uploadToDelete.status === 'FAILED' ? (
                    <span className="text-sm font-medium text-red-600 dark:text-red-400">실패</span>
                  ) : (
                    <span className="text-sm font-medium text-yellow-600 dark:text-yellow-400">대기</span>
                  )}
                </div>
                <div className="flex justify-between items-center">
                  <span className="text-sm text-gray-600 dark:text-gray-400">업로드 ID</span>
                  <span className="text-xs font-mono text-gray-900 dark:text-white">
                    {uploadToDelete.id.substring(0, 8)}...
                  </span>
                </div>
              </div>
            </div>

            {/* Footer */}
            <div className="flex justify-end gap-3 px-5 py-4 border-t border-gray-200 dark:border-gray-700">
              <button
                onClick={closeDeleteDialog}
                disabled={deleting}
                className="px-4 py-2 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors disabled:opacity-50"
              >
                취소
              </button>
              <button
                onClick={handleDeleteConfirm}
                disabled={deleting}
                className="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-white bg-red-600 hover:bg-red-700 transition-colors disabled:opacity-50"
              >
                {deleting ? (
                  <>
                    <Loader2 className="w-4 h-4 animate-spin" />
                    삭제 중...
                  </>
                ) : (
                  <>
                    <Trash2 className="w-4 h-4" />
                    삭제
                  </>
                )}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Duplicate Certificate Detail Dialog */}
      <DuplicateCertificateDialog
        duplicate={selectedDuplicate}
        isOpen={duplicateDialogOpen}
        onClose={() => {
          setDuplicateDialogOpen(false);
          setSelectedDuplicate(null);
        }}
      />
    </div>
  );
}

export default UploadHistory;
