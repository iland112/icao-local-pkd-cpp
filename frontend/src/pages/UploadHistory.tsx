import { useTranslation } from 'react-i18next';
import { useState, useEffect, useMemo, useRef, useCallback } from 'react';
import { DEFAULT_PAGE_SIZE } from '@/config/pagination';
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
  RefreshCw,
  Trash2,
} from 'lucide-react';
import { uploadApi, uploadHistoryApi } from '@/services/api';
import pkdApi from '@/services/pkdApi';
import type { UploadedFile, UploadStatus, FileFormat, UploadIssues, UploadDuplicate } from '@/types';
import { cn } from '@/utils/cn';
import { formatDateTime } from '@/utils/dateFormat';
import { toast } from '@/stores/toastStore';
import { DuplicateCertificateDialog } from '@/components/DuplicateCertificateDialog';
import { UploadDetailModal } from '@/components/UploadDetailModal';

// Statuses considered "in progress"
const IN_PROGRESS_STATUSES: UploadStatus[] = ['PENDING', 'UPLOADING', 'PARSING', 'PROCESSING', 'VALIDATING', 'SAVING_DB', 'SAVING_LDAP'];

export function UploadHistory() {
  const { t } = useTranslation(['upload', 'common', 'monitoring']);
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
  const [complianceViolations, setComplianceViolations] = useState<Record<string, number> | undefined>();

  // Duplicate certificate detail dialog state
  const [selectedDuplicate, setSelectedDuplicate] = useState<UploadDuplicate | null>(null);
  const [duplicateDialogOpen, setDuplicateDialogOpen] = useState(false);

  // Delete confirmation dialog state
  const [deleteDialogOpen, setDeleteDialogOpen] = useState(false);
  const [uploadToDelete, setUploadToDelete] = useState<UploadedFile | null>(null);
  const [deleting, setDeleting] = useState(false);

  // Retry confirmation dialog state
  const [retryDialogOpen, setRetryDialogOpen] = useState(false);
  const [uploadToRetry, setUploadToRetry] = useState<UploadedFile | null>(null);
  const [retryingId, setRetryingId] = useState<string | null>(null);

  const pageSize = DEFAULT_PAGE_SIZE;

  const fetchUploads = useCallback(async () => {
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
  }, [page]);

  useEffect(() => {
    fetchUploads();
  }, [fetchUploads]);

  // Auto-refresh when any upload is in progress (5s interval)
  const hasInProgress = useMemo(
    () => uploads.some(u => IN_PROGRESS_STATUSES.includes(u.status)),
    [uploads]
  );
  const fetchUploadsRef = useRef(fetchUploads);
  fetchUploadsRef.current = fetchUploads;

  useEffect(() => {
    if (!hasInProgress) return;

    const interval = setInterval(() => {
      fetchUploadsRef.current();
    }, 5000);

    return () => clearInterval(interval);
  }, [hasInProgress]);

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

  // Calculate duplicate count for detail dialog:
  // uploadIssues.totalDuplicates only covers certificate_duplicates table (individual upload),
  // but LDIF uploads skip duplicates via fingerprint cache without recording to that table.
  // Fallback: totalEntries - stored certificates (CSCA+DSC+DSC_NC+MLSC+CRL)
  const detailDuplicateCount = useMemo(() => {
    if (uploadIssues?.totalDuplicates && uploadIssues.totalDuplicates > 0) {
      return uploadIssues.totalDuplicates;
    }
    if (!selectedUpload || !selectedUpload.totalEntries) return 0;
    const storedCount = (selectedUpload.cscaCount || 0) + (selectedUpload.dscCount || 0)
      + (selectedUpload.dscNcCount || 0) + (selectedUpload.mlscCount || 0)
      + (selectedUpload.crlCount || 0);
    const diff = selectedUpload.totalEntries - storedCount;
    return diff > 0 ? diff : 0;
  }, [selectedUpload, uploadIssues]);

  // Render status badge with progress info for in-progress uploads
  const renderStatusProgress = (upload: UploadedFile) => {
    const { status } = upload;

    if (status === 'FAILED') {
      return (
        <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400">
          <XCircle className="w-3 h-3" />
          {t('upload:statistics.totalFailed')}
        </span>
      );
    }

    if (status === 'COMPLETED') {
      return (
        <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400">
          <CheckCircle className="w-3 h-3" />
          {t('upload:stepper.complete')}
        </span>
      );
    }

    const hasProgress = upload.processedEntries !== undefined && upload.totalEntries && upload.totalEntries > 0;
    const progressPct = hasProgress ? Math.round((upload.processedEntries! / upload.totalEntries!) * 100) : 0;

    return (
      <div className="flex items-center gap-2">
        <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400 whitespace-nowrap">
          <Loader2 className="w-3 h-3 animate-spin" />
          {status === 'PROCESSING' ? t('common:status.processing') : t('upload:history.inProgress')}
        </span>
        {hasProgress && (
          <span className="text-xs text-gray-500 dark:text-gray-400 whitespace-nowrap">
            {progressPct}%
          </span>
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
      setDialogOpen(true);
      // Fetch compliance violations from validation statistics (not stored in uploaded_file)
      try {
        const statsResp = await pkdApi.get(`/upload/${upload.id}/validation-statistics`);
        const cv = statsResp.data?.data?.complianceViolations;
        setComplianceViolations(cv && Object.keys(cv).length > 0 ? cv : undefined);
      } catch {
        setComplianceViolations(undefined);
      }
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to fetch upload details:', error);
      // Fallback to basic upload data from history list
      setSelectedUpload(upload);
      setDialogOpen(true);
      setComplianceViolations(undefined);
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
      toast.error(t('upload:history.deleteFailed'), t('upload:history.deleteFailedMsg'));
    } finally {
      setDeleting(false);
    }
  };

  const handleRetryClick = (upload: UploadedFile) => {
    setUploadToRetry(upload);
    setRetryDialogOpen(true);
  };

  const closeRetryDialog = () => {
    setRetryDialogOpen(false);
    setUploadToRetry(null);
  };

  const handleRetryConfirm = async () => {
    if (!uploadToRetry) return;

    setRetryingId(uploadToRetry.id);
    closeRetryDialog();
    try {
      await uploadApi.retryUpload(uploadToRetry.id);
      await fetchUploads();
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to retry upload:', error);
      toast.error(t('upload:history.retryFailedTitle'), t('upload:history.retryFailedMsg'));
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
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">{t('history.title')}</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              {t('upload:history.subtitle')}
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
              {t('upload:history.newUpload')}
            </Link>
          </div>
        </div>
      </div>

      {/* Filters Card */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md mb-4 p-4">
        <div className="flex items-center gap-2 mb-3">
          <Filter className="w-4 h-4 text-blue-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">{ t('certificate:search.filterLabel') }</h3>
        </div>

        <div className="grid grid-cols-2 md:grid-cols-5 gap-3">
          {/* File Format Filter */}
          <div>
            <label htmlFor="upload-format" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('upload:history.fileType')}
            </label>
            <select
              id="upload-format"
              name="formatFilter"
              value={formatFilter}
              onChange={(e) => setFormatFilter(e.target.value as FileFormat | '')}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="">{ t('monitoring:pool.total') }</option>
              <option value="LDIF" title="LDAP Data Interchange Format">LDIF</option>
              <option value="ML" title="ICAO PKD Master List">Master List</option>
            </select>
          </div>

          {/* Status Filter */}
          <div>
            <label htmlFor="upload-status" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('upload:history.uploadStatus')}
            </label>
            <select
              id="upload-status"
              name="statusFilter"
              value={statusFilter}
              onChange={(e) => setStatusFilter(e.target.value as UploadStatus | '')}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="">{ t('monitoring:pool.total') }</option>
              <option value="COMPLETED">{ t('common:status.finalized') }</option>
              <option value="FAILED">{t('common:status.failed')}</option>
              <option value="PENDING">{ t('monitoring:pool.idle') }</option>
              <option value="UPLOADING">{t('upload:detail.uploading')}</option>
              <option value="PARSING">{t('upload:detail.parsing')}</option>
              <option value="PROCESSING">{t('common:status.processing')}</option>
              <option value="VALIDATING">{t('upload:detail.validating')}</option>
              <option value="SAVING_DB">{t('upload:detail.savingDb')}</option>
              <option value="SAVING_LDAP">{t('upload:detail.savingLdap')}</option>
            </select>
          </div>

          {/* Date From */}
          <div>
            <label htmlFor="upload-date-from" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('upload:history.startDate')}
            </label>
            <input
              id="upload-date-from"
              name="dateFrom"
              type="date"
              value={dateFrom}
              onChange={(e) => setDateFrom(e.target.value)}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          {/* Date To */}
          <div>
            <label htmlFor="upload-date-to" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('upload:history.endDate')}
            </label>
            <input
              id="upload-date-to"
              name="dateTo"
              type="date"
              value={dateTo}
              onChange={(e) => setDateTo(e.target.value)}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          {/* Search & Actions */}
          <div>
            <label htmlFor="upload-search" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('upload:history.search')}
            </label>
            <div className="flex gap-2">
              <div className="relative flex-1">
                <Search className="absolute left-2.5 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                <input
                  id="upload-search"
                  name="searchTerm"
                  type="text"
                  placeholder={t('upload:history.filenamePlaceholder')}
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
                  {t('common:button.reset')}
                </button>
              )}
            </div>
          </div>
        </div>
      </div>

      {/* Statistics Cards - Hierarchical Layout */}
      <div className="mb-4">
        {/* Main Total Card */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-blue-500">
          <div className="flex items-center gap-3 mb-4">
            <div className="p-2 rounded-lg bg-blue-50 dark:bg-blue-900/30">
              <FileText className="w-6 h-6 text-blue-500" />
            </div>
            <div className="flex-1">
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">{t('upload:history.totalUploads')}</p>
              <p className="text-2xl font-bold text-blue-600 dark:text-blue-400">{stats.total.toLocaleString()}</p>
            </div>
          </div>

          {/* Breakdown Cards - Nested */}
          <div className="grid grid-cols-3 gap-3 pt-4 border-t border-gray-200 dark:border-gray-700">
            {/* Completed */}
            <div className="bg-green-50 dark:bg-green-900/20 rounded-lg p-3 border border-green-200 dark:border-green-700">
              <div className="flex items-center gap-2 mb-1">
                <CheckCircle className="w-4 h-4 text-green-500" />
                <p className="text-xs text-gray-600 dark:text-gray-400 font-medium">{ t('common:status.finalized') }</p>
              </div>
              <p className="text-lg font-bold text-green-600 dark:text-green-400">{stats.completed.toLocaleString()}</p>
              <p className="text-xs text-gray-500 dark:text-gray-400">{stats.completedPercent}%</p>
            </div>

            {/* Failed */}
            <div className="bg-red-50 dark:bg-red-900/20 rounded-lg p-3 border border-red-200 dark:border-red-700">
              <div className="flex items-center gap-2 mb-1">
                <XCircle className="w-4 h-4 text-red-500" />
                <p className="text-xs text-gray-600 dark:text-gray-400 font-medium">{t('common:status.failed')}</p>
              </div>
              <p className="text-lg font-bold text-red-600 dark:text-red-400">{stats.failed.toLocaleString()}</p>
              <p className="text-xs text-gray-500 dark:text-gray-400">{stats.failedPercent}%</p>
            </div>

            {/* In Progress */}
            <div className="bg-yellow-50 dark:bg-yellow-900/20 rounded-lg p-3 border border-yellow-200 dark:border-yellow-700">
              <div className="flex items-center gap-2 mb-1">
                <Loader2 className="w-4 h-4 text-yellow-500" />
                <p className="text-xs text-gray-600 dark:text-gray-400 font-medium">{t('upload:history.inProgress')}</p>
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
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">{t('history.title')}</h3>
          <span className="px-2 py-0.5 text-xs rounded-full bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300">
            {t('upload:history.itemCount', { num: filteredUploads.length })}
          </span>
        </div>

        {loading ? (
          <div className="flex items-center justify-center py-20">
            <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
          </div>
        ) : filteredUploads.length === 0 ? (
          <div className="flex flex-col items-center justify-center py-20 text-gray-500 dark:text-gray-400">
            <AlertCircle className="w-12 h-12 mb-4 opacity-50" />
            <p className="text-lg font-medium">{ t('upload:history.noUploads') }</p>
            <p className="text-sm">{t('upload:history.emptyMessage')}</p>
            <Link
              to="/upload"
              className="mt-4 inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-white bg-gradient-to-r from-indigo-500 to-purple-500"
            >
              <Upload className="w-4 h-4" />
              {t('upload:history.goToUpload')}
            </Link>
          </div>
        ) : (
          <>
            <div className="overflow-x-auto">
              <table className="w-full">
                <thead className="bg-slate-100 dark:bg-gray-700">
                  <tr>
                    <th className="px-4 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">
                      {t('upload:history.fileName')}
                    </th>
                    <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                      {t('upload:history.format')}
                    </th>
                    <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                      {t('upload:history.size')}
                    </th>
                    <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                      {t('upload:history.status')}
                    </th>
                    <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                      {t('upload:history.createdAt')}
                    </th>
                    <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                      {t('upload:history.completedAt')}
                    </th>
                    <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                      {t('upload:history.action')}
                    </th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
                  {filteredUploads.map((upload) => (
                    <tr
                      key={upload.id}
                      className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors"
                    >
                      <td className="px-4 py-2">
                        <div className="flex items-center gap-2 min-w-0">
                          <FileText className="w-4 h-4 text-gray-400 flex-shrink-0" />
                          <span className="text-sm font-medium text-gray-900 dark:text-white truncate" title={upload.fileName}>
                            {upload.fileName}
                          </span>
                        </div>
                      </td>
                      <td className="px-3 py-2 text-center">{getFormatBadge(upload.fileFormat)}</td>
                      <td className="px-3 py-2 text-right text-xs text-gray-500 dark:text-gray-400 whitespace-nowrap">
                        {formatFileSize(upload.fileSize)}
                      </td>
                      <td className="px-3 py-2 text-center">{renderStatusProgress(upload)}</td>
                      <td className="px-3 py-2 text-center text-xs text-gray-500 dark:text-gray-400 whitespace-nowrap">
                        {formatDateTime(upload.createdAt ?? '')}
                      </td>
                      <td className="px-3 py-2 text-center text-xs text-gray-500 dark:text-gray-400 whitespace-nowrap">
                        {upload.status === 'COMPLETED' ? formatDateTime(upload.completedAt ?? upload.updatedAt ?? '') : '-'}
                      </td>
                      <td className="px-3 py-2 text-right">
                        <div className="flex items-center justify-end gap-1">
                          <button
                            onClick={() => handleViewDetail(upload)}
                            className="inline-flex items-center gap-1 px-2.5 py-1 rounded-md text-xs font-medium text-blue-600 dark:text-blue-400 hover:bg-blue-50 dark:hover:bg-blue-900/20 transition-colors"
                          >
                            <Eye className="w-3.5 h-3.5" />
                            {t('upload:history.detail')}
                          </button>
                          {upload.status === 'FAILED' && (
                            <button
                              onClick={() => handleRetryClick(upload)}
                              disabled={retryingId === upload.id}
                              className="inline-flex items-center gap-1 px-2.5 py-1 rounded-md text-xs font-medium text-amber-600 dark:text-amber-400 hover:bg-amber-50 dark:hover:bg-amber-900/20 transition-colors disabled:opacity-50"
                              title={t('upload:history.retryFailed')}
                            >
                              <RefreshCw className={cn("w-3.5 h-3.5", retryingId === upload.id && "animate-spin")} />
                              {t('common:button.retry')}
                            </button>
                          )}
                          {(upload.status === 'FAILED' || upload.status === 'PENDING') && (
                            <button
                              onClick={() => handleDeleteClick(upload)}
                              className="inline-flex items-center gap-1 px-2.5 py-1 rounded-md text-xs font-medium text-red-600 dark:text-red-400 hover:bg-red-50 dark:hover:bg-red-900/20 transition-colors"
                              title={t('upload:history.deleteConfirmTitle')}
                            >
                              <Trash2 className="w-3.5 h-3.5" />
                              {t('common:button.delete')}
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
                {t('upload:history.showingRange', { total: totalElements, start: page * pageSize + 1, end: Math.min((page + 1) * pageSize, totalElements) })}
              </p>
              <div className="flex items-center gap-1">
                <button
                  onClick={() => setPage((p) => Math.max(0, p - 1))}
                  disabled={page === 0}
                  className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
                  aria-label={t('common:button.prev_page')}
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
                  aria-label={t('common:button.next_page')}
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
        <UploadDetailModal
          open={dialogOpen}
          upload={selectedUpload}
          uploadIssues={uploadIssues}
          complianceViolations={complianceViolations}
          detailDuplicateCount={detailDuplicateCount}
          loadingIssues={loadingIssues}
          onClose={closeDialog}
        />
      )}

      {/* Retry Confirmation Dialog */}
      {retryDialogOpen && uploadToRetry && (
        <div className="fixed inset-0 z-[70] flex items-center justify-center">
          {/* Backdrop */}
          <div
            className="absolute inset-0 bg-black/50 backdrop-blur-sm"
            onClick={closeRetryDialog}
          />

          {/* Dialog Content */}
          <div className="relative bg-white dark:bg-gray-800 rounded-xl shadow-xl w-full max-w-md mx-4">
            {/* Header */}
            <div className="flex items-center gap-2 px-5 py-3 border-b border-gray-200 dark:border-gray-700">
              <div className="p-1.5 rounded-lg bg-amber-100 dark:bg-amber-900/30">
                <RefreshCw className="w-4 h-4 text-amber-600 dark:text-amber-400" />
              </div>
              <div>
                <h2 className="text-base font-semibold text-gray-900 dark:text-white">
                  {t('upload:history.retryReprocess')}
                </h2>
                <p className="text-xs text-gray-500 dark:text-gray-400">
                  {t('upload:history.retryFromFailPoint')}
                </p>
              </div>
            </div>

            {/* Body */}
            <div className="px-5 py-4 space-y-3">
              <div className="border rounded-lg p-3 bg-blue-50 dark:bg-blue-900/20 border-blue-200 dark:border-blue-800">
                <div className="flex items-start gap-2">
                  <AlertCircle className="w-5 h-5 mt-0.5 flex-shrink-0 text-blue-600 dark:text-blue-400" />
                  <div>
                    <h4 className="text-sm font-medium text-blue-800 dark:text-blue-300">{t('upload:history.resumeMode')}</h4>
                    <p className="text-sm text-blue-700 dark:text-blue-400 mt-1">
                      {t('upload:history.resumeModeDesc')}
                    </p>
                    {(uploadToRetry.processedEntries ?? 0) > 0 && (
                      <p className="text-sm text-blue-700 dark:text-blue-400 mt-1">
                        {t('upload:history.processedCount')}: <span className="font-semibold">{(uploadToRetry.processedEntries ?? 0).toLocaleString()}</span> / {(uploadToRetry.totalEntries ?? 0).toLocaleString()}
                      </p>
                    )}
                  </div>
                </div>
              </div>

              <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3 space-y-2">
                <div className="flex justify-between items-center">
                  <span className="text-sm text-gray-600 dark:text-gray-400">{ t('upload:history.fileName') }</span>
                  <span className="text-sm font-medium text-gray-900 dark:text-white truncate ml-4 max-w-[200px]" title={uploadToRetry.fileName}>
                    {uploadToRetry.fileName}
                  </span>
                </div>
                <div className="flex justify-between items-center">
                  <span className="text-sm text-gray-600 dark:text-gray-400">{ t('admin:apiClient.status') }</span>
                  <span className="text-sm font-medium text-red-600 dark:text-red-400">{t('common:status.failed')}</span>
                </div>
                {uploadToRetry.errorMessage && (
                  <div className="flex justify-between items-start">
                    <span className="text-sm text-gray-600 dark:text-gray-400 flex-shrink-0">{ t('sync:dashboard.error') }</span>
                    <span className="text-xs text-red-600 dark:text-red-400 ml-4 text-right break-all max-w-[220px]">
                      {uploadToRetry.errorMessage.length > 80
                        ? uploadToRetry.errorMessage.substring(0, 80) + '...'
                        : uploadToRetry.errorMessage}
                    </span>
                  </div>
                )}
              </div>
            </div>

            {/* Footer */}
            <div className="flex justify-end gap-2 px-5 py-3 border-t border-gray-200 dark:border-gray-700">
              <button
                onClick={closeRetryDialog}
                className="px-4 py-1.5 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              >
                {t('common:button.cancel')}
              </button>
              <button
                onClick={handleRetryConfirm}
                className="inline-flex items-center gap-2 px-4 py-1.5 rounded-lg text-sm font-medium text-white bg-amber-600 hover:bg-amber-700 transition-colors"
              >
                <RefreshCw className="w-4 h-4" />
                {t('upload:history.resumeReprocess')}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Delete Confirmation Dialog */}
      {deleteDialogOpen && uploadToDelete && (
        <div className="fixed inset-0 z-[70] flex items-center justify-center">
          {/* Backdrop */}
          <div
            className="absolute inset-0 bg-black/50 backdrop-blur-sm"
            onClick={closeDeleteDialog}
          />

          {/* Dialog Content */}
          <div className="relative bg-white dark:bg-gray-800 rounded-xl shadow-xl w-full max-w-md mx-4">
            {/* Header */}
            <div className="flex items-center gap-2 px-5 py-3 border-b border-gray-200 dark:border-gray-700">
              <div className="p-1.5 rounded-lg bg-red-100 dark:bg-red-900/30">
                <Trash2 className="w-4 h-4 text-red-600 dark:text-red-400" />
              </div>
              <div>
                <h2 className="text-base font-semibold text-gray-900 dark:text-white">
                  {t('upload:history.deleteConfirmTitle')}
                </h2>
                <p className="text-xs text-gray-500 dark:text-gray-400">
                  {t('upload:history.deleteConfirmMsg')}
                </p>
              </div>
            </div>

            {/* Body */}
            <div className="px-5 py-4 space-y-3">
              <div className="bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800 rounded-lg p-3">
                <div className="flex items-start gap-2">
                  <AlertCircle className="w-5 h-5 text-yellow-600 dark:text-yellow-400 mt-0.5 flex-shrink-0" />
                  <div>
                    <h4 className="text-sm font-medium text-yellow-800 dark:text-yellow-300">
                      {t('common:toast.warning')}
                    </h4>
                    <p className="text-sm text-yellow-700 dark:text-yellow-400 mt-1">
                      {t('upload:history.deleteWarningMsg')}
                    </p>
                    <ul className="text-sm text-yellow-700 dark:text-yellow-400 mt-2 ml-4 list-disc space-y-1">
                      <li>{t('upload:history.deleteItem.uploadRecord')}</li>
                      <li>{t('upload:history.deleteItem.certData')}</li>
                      <li>{t('upload:history.deleteItem.crlData')}</li>
                      <li>{t('upload:history.deleteItem.mlData')}</li>
                      <li>{t('upload:history.deleteItem.tempFiles')}</li>
                    </ul>
                  </div>
                </div>
              </div>

              <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3 space-y-2">
                <div className="flex justify-between items-center">
                  <span className="text-sm text-gray-600 dark:text-gray-400">{ t('upload:history.fileName') }</span>
                  <span className="text-sm font-medium text-gray-900 dark:text-white">
                    {uploadToDelete.fileName}
                  </span>
                </div>
                <div className="flex justify-between items-center">
                  <span className="text-sm text-gray-600 dark:text-gray-400">{ t('admin:apiClient.status') }</span>
                  {uploadToDelete.status === 'FAILED' ? (
                    <span className="text-sm font-medium text-red-600 dark:text-red-400">{t('common:status.failed')}</span>
                  ) : (
                    <span className="text-sm font-medium text-yellow-600 dark:text-yellow-400">{ t('monitoring:pool.idle') }</span>
                  )}
                </div>
                <div className="flex justify-between items-center">
                  <span className="text-sm text-gray-600 dark:text-gray-400">{t('history.uploadId')}</span>
                  <span className="text-xs font-mono text-gray-900 dark:text-white">
                    {uploadToDelete.id.substring(0, 8)}...
                  </span>
                </div>
              </div>
            </div>

            {/* Footer */}
            <div className="flex justify-end gap-2 px-5 py-3 border-t border-gray-200 dark:border-gray-700">
              <button
                onClick={closeDeleteDialog}
                disabled={deleting}
                className="px-4 py-1.5 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors disabled:opacity-50"
              >
                {t('common:button.cancel')}
              </button>
              <button
                onClick={handleDeleteConfirm}
                disabled={deleting}
                className="inline-flex items-center gap-2 px-4 py-1.5 rounded-lg text-sm font-medium text-white bg-red-600 hover:bg-red-700 transition-colors disabled:opacity-50"
              >
                {deleting ? (
                  <>
                    <Loader2 className="w-4 h-4 animate-spin" />
                    {t('upload:history.deleting')}
                  </>
                ) : (
                  <>
                    <Trash2 className="w-4 h-4" />
                    {t('common:button.delete')}
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
