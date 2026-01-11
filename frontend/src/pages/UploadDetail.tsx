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
} from 'lucide-react';
import { uploadApi } from '@/services/api';
import type { UploadedFile, UploadStatus, FileFormat } from '@/types';
import { cn } from '@/utils/cn';

export function UploadDetail() {
  const { uploadId } = useParams<{ uploadId: string }>();
  const navigate = useNavigate();

  const [upload, setUpload] = useState<UploadedFile | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (uploadId) {
      fetchUploadDetail();
    }
  }, [uploadId]);

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
      console.error('Failed to fetch upload detail:', err);
    } finally {
      setLoading(false);
    }
  };

  const getStatusBadge = (status: UploadStatus) => {
    const styles: Record<UploadStatus, { bg: string; text: string; icon: React.ReactNode }> = {
      PENDING: { bg: 'bg-gray-100 dark:bg-gray-700', text: 'text-gray-600 dark:text-gray-300', icon: <Clock className="w-4 h-4" /> },
      UPLOADING: { bg: 'bg-blue-100 dark:bg-blue-900/30', text: 'text-blue-600 dark:text-blue-400', icon: <Loader2 className="w-4 h-4 animate-spin" /> },
      PARSING: { bg: 'bg-blue-100 dark:bg-blue-900/30', text: 'text-blue-600 dark:text-blue-400', icon: <Loader2 className="w-4 h-4 animate-spin" /> },
      VALIDATING: { bg: 'bg-yellow-100 dark:bg-yellow-900/30', text: 'text-yellow-600 dark:text-yellow-400', icon: <Loader2 className="w-4 h-4 animate-spin" /> },
      SAVING_DB: { bg: 'bg-indigo-100 dark:bg-indigo-900/30', text: 'text-indigo-600 dark:text-indigo-400', icon: <Loader2 className="w-4 h-4 animate-spin" /> },
      SAVING_LDAP: { bg: 'bg-purple-100 dark:bg-purple-900/30', text: 'text-purple-600 dark:text-purple-400', icon: <Loader2 className="w-4 h-4 animate-spin" /> },
      COMPLETED: { bg: 'bg-green-100 dark:bg-green-900/30', text: 'text-green-600 dark:text-green-400', icon: <CheckCircle className="w-4 h-4" /> },
      FAILED: { bg: 'bg-red-100 dark:bg-red-900/30', text: 'text-red-600 dark:text-red-400', icon: <XCircle className="w-4 h-4" /> },
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
      <span className={cn('inline-flex items-center gap-2 px-3 py-1.5 rounded-full text-sm font-medium', style.bg, style.text)}>
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
                  <div className="space-y-2">
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
                </div>
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
    </div>
  );
}

export default UploadDetail;
