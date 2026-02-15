import { useState, useEffect } from 'react';
import { useParams, useNavigate, Link } from 'react-router-dom';
import {
  FileText,
  ArrowLeft,
  CheckCircle,
  XCircle,
  Clock,
  Loader2,
  AlertCircle,
  Database,
  Shield,
  Globe,
  Hash,
  Calendar,
  HardDrive,
  Eye,
  X,
} from 'lucide-react';
import { uploadApi } from '@/services/api';
import type { UploadedFile, UploadStatus, FileFormat } from '@/types';
import { cn } from '@/utils/cn';
import { validationApi } from '@/api/validationApi';
import type { ValidationResult } from '@/types/validation';
import { TrustChainVisualization } from '@/components/TrustChainVisualization';

export function UploadDetail() {
  const { uploadId } = useParams<{ uploadId: string }>();
  const navigate = useNavigate();

  const [upload, setUpload] = useState<UploadedFile | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  // Sprint 3 Task 3.6: Validation results
  const [showValidationDialog, setShowValidationDialog] = useState(false);
  const [validationResults, setValidationResults] = useState<ValidationResult[]>([]);
  const [validationLoading, setValidationLoading] = useState(false);
  const [validationTotal, setValidationTotal] = useState(0);
  const [validationPage, setValidationPage] = useState(0);
  const [validationLimit] = useState(20);
  const [selectedValidation, setSelectedValidation] = useState<ValidationResult | null>(null);

  useEffect(() => {
    if (uploadId) {
      fetchUploadDetail();
    }
  }, [uploadId]);

  // Auto-refresh when upload is in progress (3s interval)
  useEffect(() => {
    if (!upload || !['PENDING', 'UPLOADING', 'PARSING', 'PROCESSING', 'VALIDATING', 'SAVING_DB', 'SAVING_LDAP'].includes(upload.status)) return;

    const interval = setInterval(() => {
      fetchUploadDetail();
    }, 3000);

    return () => clearInterval(interval);
  }, [upload?.status, uploadId]);

  const fetchUploadDetail = async () => {
    if (!uploadId) return;

    setLoading(true);
    setError(null);

    try {
      const response = await uploadApi.getDetail(uploadId);
      if (response.data?.success && response.data.data) {
        setUpload(response.data.data);
      } else {
        setError('업로드 정보를 찾을 수 없습니다.');
      }
    } catch (err) {
      setError('업로드 정보를 불러오는데 실패했습니다.');
      if (import.meta.env.DEV) console.error('Failed to fetch upload detail:', err);
    } finally {
      setLoading(false);
    }
  };

  // Sprint 3 Task 3.6: Fetch validation results
  const fetchValidationResults = async (page: number = 0) => {
    if (!uploadId) return;

    setValidationLoading(true);
    try {
      const response = await validationApi.getUploadValidations(uploadId, {
        limit: validationLimit,
        offset: page * validationLimit,
      });

      if (response.success) {
        setValidationResults(response.validations);
        setValidationTotal(response.total);
        setValidationPage(page);
      }
    } catch (err) {
      if (import.meta.env.DEV) console.error('Failed to fetch validation results:', err);
    } finally {
      setValidationLoading(false);
    }
  };

  // Open validation dialog
  const openValidationDialog = () => {
    setShowValidationDialog(true);
    fetchValidationResults(0);
  };

  // Statuses considered "in progress"
  const IN_PROGRESS_STATUSES: UploadStatus[] = ['PENDING', 'UPLOADING', 'PARSING', 'PROCESSING', 'VALIDATING', 'SAVING_DB', 'SAVING_LDAP'];

  const getStatusBadge = (status: UploadStatus) => {
    const styles: Record<UploadStatus, { bg: string; text: string; icon: React.ReactNode }> = {
      PENDING: { bg: 'bg-gray-100 dark:bg-gray-700', text: 'text-gray-600 dark:text-gray-300', icon: <Clock className="w-4 h-4" /> },
      UPLOADING: { bg: 'bg-blue-100 dark:bg-blue-900/30', text: 'text-blue-600 dark:text-blue-400', icon: <Loader2 className="w-4 h-4 animate-spin" /> },
      PARSING: { bg: 'bg-blue-100 dark:bg-blue-900/30', text: 'text-blue-600 dark:text-blue-400', icon: <Loader2 className="w-4 h-4 animate-spin" /> },
      PROCESSING: { bg: 'bg-cyan-100 dark:bg-cyan-900/30', text: 'text-cyan-600 dark:text-cyan-400', icon: <Loader2 className="w-4 h-4 animate-spin" /> },
      VALIDATING: { bg: 'bg-yellow-100 dark:bg-yellow-900/30', text: 'text-yellow-600 dark:text-yellow-400', icon: <Loader2 className="w-4 h-4 animate-spin" /> },
      SAVING_DB: { bg: 'bg-indigo-100 dark:bg-indigo-900/30', text: 'text-indigo-600 dark:text-indigo-400', icon: <Loader2 className="w-4 h-4 animate-spin" /> },
      SAVING_LDAP: { bg: 'bg-purple-100 dark:bg-purple-900/30', text: 'text-purple-600 dark:text-purple-400', icon: <Loader2 className="w-4 h-4 animate-spin" /> },
      COMPLETED: { bg: 'bg-green-100 dark:bg-green-900/30', text: 'text-green-600 dark:text-green-400', icon: <CheckCircle className="w-4 h-4" /> },
      FAILED: { bg: 'bg-red-100 dark:bg-red-900/30', text: 'text-red-600 dark:text-red-400', icon: <XCircle className="w-4 h-4" /> },
    };

    const style = styles[status];
    const label: Record<UploadStatus, string> = {
      PENDING: '대기',
      UPLOADING: '업로드 중',
      PARSING: '파싱 중',
      PROCESSING: '처리 중',
      VALIDATING: '검증 중',
      SAVING_DB: 'DB 저장 중',
      SAVING_LDAP: 'LDAP 저장 중',
      COMPLETED: '완료',
      FAILED: '실패',
    };

    return (
      <span className={cn('inline-flex items-center gap-2 px-3 py-1.5 rounded-full text-sm font-medium', style.bg, style.text)}>
        {style.icon}
        {label[status]}
      </span>
    );
  };

  const getFormatBadge = (format: FileFormat) => {
    const isLdif = format === 'LDIF';
    return (
      <span
        className={cn(
          'inline-flex items-center px-3 py-1.5 rounded-lg text-sm font-medium',
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
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  const formatDate = (dateString: string): string => {
    return new Date(dateString).toLocaleString('ko-KR', {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    });
  };

  if (loading) {
    return (
      <div className="w-full px-4 lg:px-6 py-4">
        <div className="flex items-center justify-center py-20">
          <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
        </div>
      </div>
    );
  }

  if (error || !upload) {
    return (
      <div className="w-full px-4 lg:px-6 py-4">
        <div className="flex flex-col items-center justify-center py-20 text-gray-500 dark:text-gray-400">
          <AlertCircle className="w-12 h-12 mb-4 opacity-50" />
          <p className="text-lg font-medium">{error || '업로드 정보를 찾을 수 없습니다.'}</p>
          <button
            onClick={() => navigate('/upload-history')}
            className="mt-4 px-4 py-2 bg-blue-500 text-white rounded-lg hover:bg-blue-600 transition-colors"
          >
            목록으로 돌아가기
          </button>
        </div>
      </div>
    );
  }

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-8">
        <div className="flex items-center gap-4">
          <button
            onClick={() => navigate(-1)}
            className="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
          >
            <ArrowLeft className="w-5 h-5 text-gray-500" />
          </button>
          <div className="p-3 rounded-xl bg-gradient-to-br from-amber-500 to-orange-600 shadow-lg">
            <FileText className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">업로드 상세</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400 font-mono">
              {upload.id}
            </p>
          </div>
          {getStatusBadge(upload.status)}
        </div>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Left Column: File Info */}
        <div className="lg:col-span-2 space-y-6">
          {/* Basic Info Card */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
            <h2 className="text-lg font-bold text-gray-900 dark:text-white mb-4 flex items-center gap-2">
              <FileText className="w-5 h-5 text-blue-500" />
              파일 정보
            </h2>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              <div className="space-y-4">
                <div>
                  <label className="text-sm text-gray-500 dark:text-gray-400">파일명</label>
                  <p className="font-medium text-gray-900 dark:text-white">{upload.fileName}</p>
                </div>
                <div>
                  <label className="text-sm text-gray-500 dark:text-gray-400">파일 형식</label>
                  <div className="mt-1">{getFormatBadge(upload.fileFormat)}</div>
                </div>
                <div>
                  <label className="text-sm text-gray-500 dark:text-gray-400">처리 모드</label>
                  <p className="font-medium text-gray-900 dark:text-white">
                    <span className={cn(
                      'px-2 py-1 rounded text-xs',
                      upload.processingMode === 'AUTO'
                        ? 'bg-green-100 dark:bg-green-900/30 text-green-600 dark:text-green-400'
                        : 'bg-blue-100 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400'
                    )}>
                      {upload.processingMode === 'AUTO' ? '자동' : '수동'}
                    </span>
                  </p>
                </div>
              </div>
              <div className="space-y-4">
                <div className="flex items-center gap-2">
                  <HardDrive className="w-4 h-4 text-gray-400" />
                  <div>
                    <label className="text-sm text-gray-500 dark:text-gray-400">파일 크기</label>
                    <p className="font-medium text-gray-900 dark:text-white">{formatFileSize(upload.fileSize)}</p>
                  </div>
                </div>
                <div className="flex items-center gap-2">
                  <Hash className="w-4 h-4 text-gray-400" />
                  <div>
                    <label className="text-sm text-gray-500 dark:text-gray-400">파일 해시 (SHA-256)</label>
                    <p className="font-mono text-xs text-gray-600 dark:text-gray-300 break-all">{upload.fileHash}</p>
                  </div>
                </div>
              </div>
            </div>
          </div>

          {/* Timeline Card */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
            <h2 className="text-lg font-bold text-gray-900 dark:text-white mb-4 flex items-center gap-2">
              <Calendar className="w-5 h-5 text-purple-500" />
              처리 타임라인
            </h2>
            <div className="space-y-4">
              <div className="flex items-center gap-4">
                <div className="w-10 h-10 rounded-full bg-blue-100 dark:bg-blue-900/30 flex items-center justify-center">
                  <Clock className="w-5 h-5 text-blue-500" />
                </div>
                <div>
                  <p className="font-medium text-gray-900 dark:text-white">업로드 시작</p>
                  <p className="text-sm text-gray-500">{formatDate(upload.uploadedAt || upload.createdAt || '')}</p>
                </div>
              </div>
              {/* Processing in-progress timeline entry */}
              {IN_PROGRESS_STATUSES.includes(upload.status) && upload.status !== 'PENDING' && (
                <div className="flex items-center gap-4">
                  <div className="w-10 h-10 rounded-full bg-blue-100 dark:bg-blue-900/30 flex items-center justify-center">
                    <Loader2 className="w-5 h-5 text-blue-500 animate-spin" />
                  </div>
                  <div>
                    <p className="font-medium text-gray-900 dark:text-white">
                      {upload.status === 'PROCESSING' ? '인증서 처리 중' : '처리 진행 중'}
                    </p>
                    {upload.totalEntries && upload.totalEntries > 0 && (
                      <p className="text-sm text-blue-600 dark:text-blue-400">
                        {(upload.processedEntries || 0).toLocaleString()} / {upload.totalEntries.toLocaleString()} 엔트리
                      </p>
                    )}
                  </div>
                </div>
              )}
              {upload.completedAt && (
                <div className="flex items-center gap-4">
                  <div className={cn(
                    'w-10 h-10 rounded-full flex items-center justify-center',
                    upload.status === 'COMPLETED'
                      ? 'bg-green-100 dark:bg-green-900/30'
                      : 'bg-red-100 dark:bg-red-900/30'
                  )}>
                    {upload.status === 'COMPLETED' ? (
                      <CheckCircle className="w-5 h-5 text-green-500" />
                    ) : (
                      <XCircle className="w-5 h-5 text-red-500" />
                    )}
                  </div>
                  <div>
                    <p className="font-medium text-gray-900 dark:text-white">
                      {upload.status === 'COMPLETED' ? '처리 완료' : '처리 실패'}
                    </p>
                    <p className="text-sm text-gray-500">{formatDate(upload.completedAt)}</p>
                  </div>
                </div>
              )}
            </div>
          </div>

          {/* Error Message */}
          {upload.errorMessage && (
            <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-2xl p-6">
              <h2 className="text-lg font-bold text-red-700 dark:text-red-400 mb-2 flex items-center gap-2">
                <AlertCircle className="w-5 h-5" />
                오류 메시지
              </h2>
              <p className="text-red-600 dark:text-red-300">{upload.errorMessage}</p>
            </div>
          )}
        </div>

        {/* Right Column: Statistics */}
        <div className="lg:col-span-1">
          {upload.statistics ? (
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
              <h2 className="text-lg font-bold text-gray-900 dark:text-white mb-4 flex items-center gap-2">
                <Database className="w-5 h-5 text-indigo-500" />
                처리 통계
              </h2>

              <div className="space-y-4">
                {/* Total Certificates */}
                <div className="p-4 bg-gray-50 dark:bg-gray-700/50 rounded-xl">
                  <div className="flex items-center justify-between mb-2">
                    <span className="text-sm text-gray-500 dark:text-gray-400">전체 인증서</span>
                    <span className="text-2xl font-bold text-gray-900 dark:text-white">
                      {upload.statistics.totalCertificates}
                    </span>
                  </div>
                </div>

                {/* Certificate Types */}
                <div className="grid grid-cols-2 gap-3">
                  <div className="p-3 bg-blue-50 dark:bg-blue-900/20 rounded-xl">
                    <div className="flex items-center gap-2 mb-1">
                      <Shield className="w-4 h-4 text-blue-500" />
                      <span className="text-xs text-blue-600 dark:text-blue-400">CSCA</span>
                    </div>
                    <span className="text-xl font-bold text-blue-700 dark:text-blue-300">
                      {upload.statistics.cscaCount}
                    </span>
                  </div>
                  <div className="p-3 bg-green-50 dark:bg-green-900/20 rounded-xl">
                    <div className="flex items-center gap-2 mb-1">
                      <Shield className="w-4 h-4 text-green-500" />
                      <span className="text-xs text-green-600 dark:text-green-400">DSC</span>
                    </div>
                    <span className="text-xl font-bold text-green-700 dark:text-green-300">
                      {upload.statistics.dscCount}
                    </span>
                  </div>
                  <div className="p-3 bg-purple-50 dark:bg-purple-900/20 rounded-xl">
                    <div className="flex items-center gap-2 mb-1">
                      <Globe className="w-4 h-4 text-purple-500" />
                      <span className="text-xs text-purple-600 dark:text-purple-400">CRL</span>
                    </div>
                    <span className="text-xl font-bold text-purple-700 dark:text-purple-300">
                      {upload.statistics.crlCount}
                    </span>
                  </div>
                </div>

                {/* Validation Results */}
                <div className="border-t border-gray-200 dark:border-gray-700 pt-4">
                  <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">검증 결과</h3>
                  <div className="space-y-2 mb-3">
                    <div className="flex items-center justify-between">
                      <span className="text-sm text-gray-500">유효</span>
                      <span className="font-medium text-green-600 dark:text-green-400">
                        {upload.statistics.validCount}
                      </span>
                    </div>
                    <div className="flex items-center justify-between">
                      <span className="text-sm text-gray-500">무효</span>
                      <span className="font-medium text-red-600 dark:text-red-400">
                        {upload.statistics.invalidCount}
                      </span>
                    </div>
                    <div className="flex items-center justify-between">
                      <span className="text-sm text-gray-500">건너뜀</span>
                      <span className="font-medium text-gray-600 dark:text-gray-400">
                        {upload.statistics.skippedCount}
                      </span>
                    </div>
                  </div>
                  {/* Sprint 3 Task 3.6: View Validation Details Button */}
                  {(upload.statistics.validCount > 0 || upload.statistics.invalidCount > 0) && (
                    <button
                      onClick={openValidationDialog}
                      className="w-full inline-flex items-center justify-center gap-2 px-3 py-2 rounded-lg text-sm font-medium text-blue-600 dark:text-blue-400 bg-blue-50 dark:bg-blue-900/30 hover:bg-blue-100 dark:hover:bg-blue-900/50 transition-colors border border-blue-200 dark:border-blue-800"
                    >
                      <Eye className="w-4 h-4" />
                      상세 결과 보기
                    </button>
                  )}
                </div>
              </div>
            </div>
          ) : IN_PROGRESS_STATUSES.includes(upload.status) ? (
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
              <h2 className="text-lg font-bold text-gray-900 dark:text-white mb-4 flex items-center gap-2">
                <Loader2 className="w-5 h-5 text-blue-500 animate-spin" />
                처리 진행 중
              </h2>
              <div className="space-y-4">
                {/* Progress Bar */}
                {upload.totalEntries && upload.totalEntries > 0 && (
                  <div>
                    <div className="flex items-center justify-between mb-2">
                      <span className="text-sm text-gray-500 dark:text-gray-400">진행률</span>
                      <span className="text-sm font-bold text-blue-600 dark:text-blue-400">
                        {Math.round(((upload.processedEntries || 0) / upload.totalEntries) * 100)}%
                      </span>
                    </div>
                    <div className="w-full bg-gray-200 dark:bg-gray-600 rounded-full h-3">
                      <div
                        className="bg-gradient-to-r from-blue-500 to-cyan-500 h-3 rounded-full transition-all duration-500"
                        style={{ width: `${Math.round(((upload.processedEntries || 0) / upload.totalEntries) * 100)}%` }}
                      />
                    </div>
                    <p className="text-xs text-gray-500 dark:text-gray-400 mt-1">
                      {(upload.processedEntries || 0).toLocaleString()} / {upload.totalEntries.toLocaleString()}
                    </p>
                  </div>
                )}

                {/* Live Certificate Counts */}
                <div className="grid grid-cols-2 gap-3">
                  {(upload.cscaCount ?? 0) > 0 && (
                    <div className="p-3 bg-blue-50 dark:bg-blue-900/20 rounded-xl">
                      <div className="flex items-center gap-2 mb-1">
                        <Shield className="w-4 h-4 text-blue-500" />
                        <span className="text-xs text-blue-600 dark:text-blue-400">CSCA</span>
                      </div>
                      <span className="text-xl font-bold text-blue-700 dark:text-blue-300">
                        {upload.cscaCount}
                      </span>
                    </div>
                  )}
                  {(upload.dscCount ?? 0) > 0 && (
                    <div className="p-3 bg-green-50 dark:bg-green-900/20 rounded-xl">
                      <div className="flex items-center gap-2 mb-1">
                        <Shield className="w-4 h-4 text-green-500" />
                        <span className="text-xs text-green-600 dark:text-green-400">DSC</span>
                      </div>
                      <span className="text-xl font-bold text-green-700 dark:text-green-300">
                        {upload.dscCount}
                      </span>
                    </div>
                  )}
                  {(upload.dscNcCount ?? 0) > 0 && (
                    <div className="p-3 bg-orange-50 dark:bg-orange-900/20 rounded-xl">
                      <div className="flex items-center gap-2 mb-1">
                        <Shield className="w-4 h-4 text-orange-500" />
                        <span className="text-xs text-orange-600 dark:text-orange-400">DSC_NC</span>
                      </div>
                      <span className="text-xl font-bold text-orange-700 dark:text-orange-300">
                        {upload.dscNcCount}
                      </span>
                    </div>
                  )}
                  {(upload.crlCount ?? 0) > 0 && (
                    <div className="p-3 bg-purple-50 dark:bg-purple-900/20 rounded-xl">
                      <div className="flex items-center gap-2 mb-1">
                        <Globe className="w-4 h-4 text-purple-500" />
                        <span className="text-xs text-purple-600 dark:text-purple-400">CRL</span>
                      </div>
                      <span className="text-xl font-bold text-purple-700 dark:text-purple-300">
                        {upload.crlCount}
                      </span>
                    </div>
                  )}
                </div>

                {/* No counts yet */}
                {(upload.cscaCount ?? 0) === 0 && (upload.dscCount ?? 0) === 0 && (upload.crlCount ?? 0) === 0 && (
                  <p className="text-sm text-gray-500 dark:text-gray-400 text-center py-2">
                    인증서 처리가 시작되면 카운트가 표시됩니다.
                  </p>
                )}
              </div>
            </div>
          ) : (
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
              <div className="flex flex-col items-center justify-center py-8 text-gray-500 dark:text-gray-400">
                <Database className="w-12 h-12 mb-4 opacity-50" />
                <p className="text-sm">통계 정보가 없습니다.</p>
                <p className="text-xs">처리가 완료되면 표시됩니다.</p>
              </div>
            </div>
          )}

          {/* Quick Actions */}
          <div className="mt-6 space-y-3">
            <Link
              to="/upload"
              className="w-full inline-flex items-center justify-center gap-2 px-4 py-3 rounded-xl text-sm font-medium text-white bg-gradient-to-r from-indigo-500 to-purple-500 hover:from-indigo-600 hover:to-purple-600 transition-all shadow-md hover:shadow-lg"
            >
              새 파일 업로드
            </Link>
            <Link
              to="/upload-history"
              className="w-full inline-flex items-center justify-center gap-2 px-4 py-3 rounded-xl text-sm font-medium border border-gray-300 dark:border-gray-600 text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors"
            >
              업로드 목록
            </Link>
          </div>
        </div>
      </div>

      {/* Sprint 3 Task 3.6: Validation Results Dialog */}
      {showValidationDialog && (
        <div className="fixed inset-0 z-50 flex items-center justify-center p-4">
          {/* Backdrop */}
          <div
            className="absolute inset-0 bg-black/50 backdrop-blur-sm"
            onClick={() => setShowValidationDialog(false)}
          />

          {/* Dialog Content */}
          <div className="relative bg-white dark:bg-gray-800 rounded-2xl shadow-2xl w-full max-w-6xl max-h-[90vh] flex flex-col">
            {/* Header */}
            <div className="flex items-center justify-between px-6 py-4 border-b border-gray-200 dark:border-gray-700">
              <div className="flex items-center gap-3">
                <div className="p-2 rounded-lg bg-gradient-to-br from-blue-500 to-indigo-600">
                  <Shield className="w-5 h-5 text-white" />
                </div>
                <div>
                  <h2 className="text-lg font-semibold text-gray-900 dark:text-white">
                    Validation Results
                  </h2>
                  <p className="text-sm text-gray-500 dark:text-gray-400">
                    Total: {validationTotal} certificates
                  </p>
                </div>
              </div>
              <button
                onClick={() => setShowValidationDialog(false)}
                className="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              >
                <X className="w-5 h-5 text-gray-500" />
              </button>
            </div>

            {/* Content */}
            <div className="flex-1 overflow-y-auto p-6">
              {validationLoading ? (
                <div className="flex items-center justify-center py-12">
                  <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
                </div>
              ) : validationResults.length > 0 ? (
                <div className="space-y-3">
                  {validationResults.map((result) => (
                    <div
                      key={result.id}
                      className="border border-gray-200 dark:border-gray-700 rounded-lg p-4 hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors"
                    >
                      <div className="flex items-start justify-between">
                        <div className="flex-1 space-y-2">
                          {/* Header */}
                          <div className="flex items-center gap-2">
                            {result.trustChainValid ? (
                              <CheckCircle className="w-4 h-4 text-green-500 flex-shrink-0" />
                            ) : (
                              <XCircle className="w-4 h-4 text-red-500 flex-shrink-0" />
                            )}
                            <span className="font-medium text-gray-900 dark:text-white">
                              {result.subjectDn.match(/CN=([^,]+)/)?.[1] || 'Unknown'}
                            </span>
                            <span className={cn(
                              'px-2 py-0.5 rounded text-xs font-medium',
                              result.certificateType === 'CSCA'
                                ? 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400'
                                : 'bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400'
                            )}>
                              {result.certificateType}
                            </span>
                            <span className="px-2 py-0.5 rounded text-xs font-medium bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300">
                              {result.countryCode}
                            </span>
                          </div>

                          {/* Trust Chain Path */}
                          {result.trustChainPath && (
                            <div className="ml-6">
                              <TrustChainVisualization
                                trustChainPath={result.trustChainPath}
                                trustChainValid={result.trustChainValid}
                                compact={true}
                              />
                            </div>
                          )}

                          {/* Message */}
                          {result.trustChainMessage && (
                            <p className="ml-6 text-sm text-gray-600 dark:text-gray-400">
                              {result.trustChainMessage}
                            </p>
                          )}
                        </div>

                        {/* View Details Button */}
                        <button
                          onClick={() => setSelectedValidation(result)}
                          className="px-3 py-1.5 rounded-lg text-sm font-medium text-blue-600 dark:text-blue-400 hover:bg-blue-50 dark:hover:bg-blue-900/30 transition-colors"
                        >
                          Details
                        </button>
                      </div>
                    </div>
                  ))}

                  {/* Pagination */}
                  {validationTotal > validationLimit && (
                    <div className="flex items-center justify-between pt-4 border-t border-gray-200 dark:border-gray-700">
                      <p className="text-sm text-gray-600 dark:text-gray-400">
                        Showing {validationPage * validationLimit + 1} - {Math.min((validationPage + 1) * validationLimit, validationTotal)} of {validationTotal}
                      </p>
                      <div className="flex items-center gap-2">
                        <button
                          onClick={() => fetchValidationResults(validationPage - 1)}
                          disabled={validationPage === 0}
                          className="px-3 py-1.5 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors disabled:opacity-50 disabled:cursor-not-allowed"
                        >
                          Previous
                        </button>
                        <button
                          onClick={() => fetchValidationResults(validationPage + 1)}
                          disabled={(validationPage + 1) * validationLimit >= validationTotal}
                          className="px-3 py-1.5 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors disabled:opacity-50 disabled:cursor-not-allowed"
                        >
                          Next
                        </button>
                      </div>
                    </div>
                  )}
                </div>
              ) : (
                <div className="flex flex-col items-center justify-center py-12 text-gray-500 dark:text-gray-400">
                  <Shield className="w-12 h-12 mb-3 opacity-50" />
                  <p className="text-sm">No validation results found</p>
                </div>
              )}
            </div>
          </div>
        </div>
      )}

      {/* Validation Detail Dialog */}
      {selectedValidation && (
        <div className="fixed inset-0 z-[60] flex items-center justify-center p-4">
          <div
            className="absolute inset-0 bg-black/50 backdrop-blur-sm"
            onClick={() => setSelectedValidation(null)}
          />
          <div className="relative bg-white dark:bg-gray-800 rounded-2xl shadow-2xl w-full max-w-2xl max-h-[90vh] overflow-y-auto p-6">
            <div className="flex items-center justify-between mb-4">
              <h3 className="text-lg font-semibold text-gray-900 dark:text-white">
                Validation Details
              </h3>
              <button
                onClick={() => setSelectedValidation(null)}
                className="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              >
                <X className="w-5 h-5 text-gray-500" />
              </button>
            </div>

            <div className="space-y-4">
              {/* Trust Chain Visualization */}
              {selectedValidation.trustChainPath && (
                <div>
                  <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2">Trust Chain Path</h4>
                  <TrustChainVisualization
                    trustChainPath={selectedValidation.trustChainPath}
                    trustChainValid={selectedValidation.trustChainValid}
                    compact={false}
                  />
                </div>
              )}

              {/* Certificate Info */}
              <div>
                <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2">Certificate Information</h4>
                <div className="space-y-2 text-sm">
                  <div className="grid grid-cols-[140px_1fr] gap-2">
                    <span className="text-gray-600 dark:text-gray-400">Type:</span>
                    <span className="text-gray-900 dark:text-white">{selectedValidation.certificateType}</span>
                  </div>
                  <div className="grid grid-cols-[140px_1fr] gap-2">
                    <span className="text-gray-600 dark:text-gray-400">Country:</span>
                    <span className="text-gray-900 dark:text-white">{selectedValidation.countryCode}</span>
                  </div>
                  <div className="grid grid-cols-[140px_1fr] gap-2">
                    <span className="text-gray-600 dark:text-gray-400">Subject DN:</span>
                    <span className="text-gray-900 dark:text-white break-all">{selectedValidation.subjectDn}</span>
                  </div>
                  <div className="grid grid-cols-[140px_1fr] gap-2">
                    <span className="text-gray-600 dark:text-gray-400">Issuer DN:</span>
                    <span className="text-gray-900 dark:text-white break-all">{selectedValidation.issuerDn}</span>
                  </div>
                  <div className="grid grid-cols-[140px_1fr] gap-2">
                    <span className="text-gray-600 dark:text-gray-400">Serial Number:</span>
                    <span className="text-gray-900 dark:text-white font-mono">{selectedValidation.serialNumber}</span>
                  </div>
                </div>
              </div>

              {/* Validation Status */}
              <div>
                <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2">Validation Status</h4>
                <div className="space-y-2 text-sm">
                  <div className="grid grid-cols-[140px_1fr] gap-2">
                    <span className="text-gray-600 dark:text-gray-400">Overall Status:</span>
                    <span className={cn(
                      'font-medium',
                      selectedValidation.validationStatus === 'VALID' ? 'text-green-600 dark:text-green-400'
                        : selectedValidation.validationStatus === 'EXPIRED_VALID' ? 'text-amber-600 dark:text-amber-400'
                        : 'text-red-600 dark:text-red-400'
                    )}>
                      {selectedValidation.validationStatus === 'EXPIRED_VALID' ? 'EXPIRED_VALID (만료-유효)' : selectedValidation.validationStatus}
                    </span>
                  </div>
                  <div className="grid grid-cols-[140px_1fr] gap-2">
                    <span className="text-gray-600 dark:text-gray-400">Trust Chain:</span>
                    <span className={cn(
                      'font-medium',
                      selectedValidation.trustChainValid ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'
                    )}>
                      {selectedValidation.trustChainValid ? 'Valid' : 'Invalid'}
                    </span>
                  </div>
                  <div className="grid grid-cols-[140px_1fr] gap-2">
                    <span className="text-gray-600 dark:text-gray-400">CSCA Found:</span>
                    <span className={cn(
                      'font-medium',
                      selectedValidation.cscaFound ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'
                    )}>
                      {selectedValidation.cscaFound ? 'Yes' : 'No'}
                    </span>
                  </div>
                  {selectedValidation.cscaSubjectDn && (
                    <div className="grid grid-cols-[140px_1fr] gap-2">
                      <span className="text-gray-600 dark:text-gray-400">CSCA Subject:</span>
                      <span className="text-gray-900 dark:text-white break-all">{selectedValidation.cscaSubjectDn}</span>
                    </div>
                  )}
                </div>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default UploadDetail;
