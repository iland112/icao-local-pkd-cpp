import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import {
  Clock,
  Upload,
  CheckCircle,
  XCircle,
  AlertCircle,
  AlertTriangle,
  FileText,
  ChevronRight,
  Loader2,
  X,
  Database,
  Server,
  FileCheck,
  ShieldCheck,
  Download,
} from 'lucide-react';
import type { UploadedFile, UploadStatus, FileFormat, UploadIssues } from '@/types';
import { cn } from '@/utils/cn';
import { formatDateTime } from '@/utils/dateFormat';
import { GlossaryTerm } from '@/components/common';
import { MasterListStructure } from '@/components/MasterListStructure';
import { LdifStructure } from '@/components/LdifStructure';
import { DuplicateCertificatesTree } from '@/components/DuplicateCertificatesTree';
import { ValidationSummaryPanel } from '@/components/ValidationSummaryPanel';
import { exportDuplicatesToCsv, exportDuplicateStatisticsToCsv } from '@/utils/csvExport';

// Full status step definition (for dialog detail view)
const STATUS_STEPS: { key: UploadStatus; labelKey: string; icon: React.ReactNode }[] = [
  { key: 'PENDING', labelKey: 'monitoring:pool.idle', icon: <Clock className="w-4 h-4" /> },
  { key: 'UPLOADING', labelKey: 'upload:stepper.upload', icon: <Upload className="w-4 h-4" /> },
  { key: 'PARSING', labelKey: 'upload:stepper.parsing', icon: <FileCheck className="w-4 h-4" /> },
  { key: 'PROCESSING', labelKey: 'common:status.processing', icon: <Loader2 className="w-4 h-4" /> },
  { key: 'VALIDATING', labelKey: 'upload:stepper.validation', icon: <ShieldCheck className="w-4 h-4" /> },
  { key: 'SAVING_DB', labelKey: 'upload:stepper.dbSaving', icon: <Database className="w-4 h-4" /> },
  { key: 'SAVING_LDAP', labelKey: 'upload:stepper.ldapSaving', icon: <Server className="w-4 h-4" /> },
  { key: 'COMPLETED', labelKey: 'common:status.completed', icon: <CheckCircle className="w-4 h-4" /> },
];

// Statuses considered "in progress"
const IN_PROGRESS_STATUSES: UploadStatus[] = ['PENDING', 'UPLOADING', 'PARSING', 'PROCESSING', 'VALIDATING', 'SAVING_DB', 'SAVING_LDAP'];

interface UploadDetailModalProps {
  open: boolean;
  upload: UploadedFile;
  uploadIssues: UploadIssues | null;
  complianceViolations: Record<string, number> | undefined;
  detailDuplicateCount: number;
  loadingIssues: boolean;
  onClose: () => void;
}

export function UploadDetailModal({
  open,
  upload,
  uploadIssues,
  complianceViolations,
  detailDuplicateCount,
  loadingIssues,
  onClose,
}: UploadDetailModalProps) {
  const { t } = useTranslation(['upload', 'common', 'monitoring', 'admin', 'certificate', 'icao', 'sync']);
  const [activeTab, setActiveTab] = useState<'details' | 'structure' | 'duplicates'>('details');

  if (!open) return null;

  // Parse PostgreSQL timestamp format: "2025-12-31 09:04:28.432487+09"
  const formatDuration = (startStr: string, endStr: string): string => {
    if (!startStr || !endStr) return '-';
    try {
      const toIso = (s: string) => s.replace(' ', 'T').replace(/\+(\d{2})$/, '+$1:00');
      const start = new Date(toIso(startStr));
      const end = new Date(toIso(endStr));
      if (isNaN(start.getTime()) || isNaN(end.getTime())) return '-';
      const diffMs = end.getTime() - start.getTime();
      if (diffMs < 0) return '-';
      const totalSec = Math.floor(diffMs / 1000);
      const hours = Math.floor(totalSec / 3600);
      const minutes = Math.floor((totalSec % 3600) / 60);
      const seconds = totalSec % 60;
      if (hours > 0) return t('upload:history.durationFormat', { hours, minutes, seconds });
      if (minutes > 0) return t('upload:history.durationMinSec', { minutes, seconds });
      return t('upload:history.durationSec', { seconds });
    } catch {
      return '-';
    }
  };

  const getStatusStepIndex = (status: UploadStatus): number => {
    if (status === 'FAILED') return -1;
    return STATUS_STEPS.findIndex(step => step.key === status);
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

  return (
    <div className="fixed inset-0 z-[70] flex items-center justify-center">
      {/* Backdrop */}
      <div
        className="absolute inset-0 bg-black/50 backdrop-blur-sm"
        onClick={onClose}
      />

      {/* Dialog Content - Wide layout without vertical scroll */}
      <div className="relative bg-white dark:bg-gray-800 rounded-xl shadow-xl w-full max-w-sm sm:max-w-2xl lg:max-w-6xl mx-4 max-h-[85vh] flex flex-col">
        {/* Header */}
        <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700">
          <div className="flex items-center justify-between mb-2">
            <div className="flex items-center gap-2">
              <div className="p-1.5 rounded-lg bg-gradient-to-br from-blue-500 to-indigo-600">
                <FileText className="w-4 h-4 text-white" />
              </div>
              <div>
                <h2 className="text-base font-semibold text-gray-900 dark:text-white">
                  {t('upload:history.uploadDetail')}
                </h2>
                <p className="text-xs text-gray-500 dark:text-gray-400">
                  {upload.fileName}
                </p>
              </div>
            </div>
            <button
              onClick={onClose}
              className="p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            >
              <X className="w-4 h-4 text-gray-500" />
            </button>
          </div>

          {/* Tabs - Show if Master List/LDIF file or has duplicates */}
          {((upload.fileFormat === 'ML' || upload.fileFormat === 'MASTER_LIST' || upload.fileFormat === 'LDIF') || detailDuplicateCount > 0) && (
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
                {t('admin:operationAudit.detail')}
              </button>
              {(upload.fileFormat === 'ML' ||
                upload.fileFormat === 'MASTER_LIST' ||
                upload.fileFormat === 'LDIF') && (
                <button
                  onClick={() => setActiveTab('structure')}
                  className={cn(
                    'px-4 py-2 text-sm font-medium rounded-lg transition-colors',
                    activeTab === 'structure'
                      ? 'bg-blue-100 dark:bg-blue-900/50 text-blue-700 dark:text-blue-300'
                      : 'text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700'
                  )}
                >
                  {upload.fileFormat === 'LDIF' ? t('upload:history.ldifStructure') : t('upload:history.mlStructure')}
                </button>
              )}
              {detailDuplicateCount > 0 && (
                <button
                  onClick={() => setActiveTab('duplicates')}
                  className={cn(
                    'px-4 py-2 text-sm font-medium rounded-lg transition-colors flex items-center gap-2',
                    activeTab === 'duplicates'
                      ? 'bg-yellow-100 dark:bg-yellow-900/50 text-yellow-700 dark:text-yellow-300'
                      : 'text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700'
                  )}
                >
                  {t('upload:detail.duplicateCertificates')}
                  <span className="px-1.5 py-0.5 text-xs font-medium rounded bg-yellow-200 dark:bg-yellow-900/70 text-yellow-800 dark:text-yellow-200">
                    {detailDuplicateCount.toLocaleString()}
                  </span>
                </button>
              )}
            </div>
          )}
        </div>

        {/* Body */}
        <div className="p-4 flex-1 min-h-0 overflow-y-auto">
          {/* Details Tab */}
          {activeTab === 'details' && (
          <div className="space-y-3">
              {/* Status Progress - Compact horizontal */}
              <div>
                <h3 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">{t('upload:history.progressStatus')}</h3>
                <div className="flex items-center justify-between bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3">
                  {STATUS_STEPS.map((step, index) => {
                    const currentIndex = getStatusStepIndex(upload.status);
                    const isPassed = index < currentIndex || upload.status === 'COMPLETED';
                    const isCurrent = index === currentIndex && upload.status !== 'COMPLETED';
                    const isFailed = upload.status === 'FAILED' && index === 0;

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
                          {t(step.labelKey)}
                        </span>
                      </div>
                    );
                  })}
                </div>
              </div>

              {/* Processing Progress Bar (v2.9.3) */}
              {IN_PROGRESS_STATUSES.includes(upload.status) && upload.totalEntries && upload.totalEntries > 0 && (
                <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg p-3">
                  <div className="flex items-center justify-between mb-2">
                    <div className="flex items-center gap-2">
                      <Loader2 className="w-4 h-4 animate-spin text-blue-500" />
                      <span className="text-sm font-medium text-blue-700 dark:text-blue-300">{t('upload:history.processingProgress')}</span>
                    </div>
                    <span className="text-sm font-bold text-blue-700 dark:text-blue-300">
                      {upload.processedEntries?.toLocaleString() || 0} / {upload.totalEntries.toLocaleString()}
                    </span>
                  </div>
                  <div className="w-full bg-blue-200 dark:bg-blue-800 rounded-full h-2.5">
                    <div
                      className="bg-blue-500 h-2.5 rounded-full transition-all duration-500"
                      style={{ width: `${Math.round(((upload.processedEntries || 0) / upload.totalEntries) * 100)}%` }}
                    />
                  </div>
                  <p className="text-xs text-blue-600 dark:text-blue-400 mt-1 text-right">
                    {Math.round(((upload.processedEntries || 0) / upload.totalEntries) * 100)}%
                  </p>
                </div>
              )}

              {/* Error Message - Compact */}
              {upload.errorMessage && (
                <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg p-3">
                  <div className="flex items-start gap-2">
                    <XCircle className="w-4 h-4 text-red-500 mt-0.5 flex-shrink-0" />
                    <div>
                      <h4 className="text-xs font-medium text-red-800 dark:text-red-300">{t('common:label.errorOccurred')}</h4>
                      <p className="text-xs text-red-600 dark:text-red-400 mt-0.5">
                        {upload.errorMessage}
                      </p>
                    </div>
                  </div>
                </div>
              )}

              {/* Certificate & File Info Grid - Compact */}
              <div className="grid grid-cols-6 gap-2">
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                  <span className="text-xs text-gray-500 dark:text-gray-400">{t('upload:history.format')}</span>
                  <div className="mt-0.5">{getFormatBadge(upload.fileFormat)}</div>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                  <span className="text-xs text-gray-500 dark:text-gray-400">{t('upload:history.size')}</span>
                  <p className="text-sm font-medium text-gray-900 dark:text-white mt-0.5">
                    {formatFileSize(upload.fileSize)}
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                  <span className="text-xs text-gray-500 dark:text-gray-400">{ t('certificate:search.totalCerts') }</span>
                  <p className="text-sm font-medium text-gray-900 dark:text-white mt-0.5">
                    {t('upload:history.itemCount', { num: upload.certificateCount })}
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                  <span className="text-xs text-gray-500 dark:text-gray-400">{ t('common:button.upload') }</span>
                  <p className="text-xs font-medium text-gray-900 dark:text-white mt-0.5">
                    {formatDateTime(upload.createdAt ?? '')}
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                  <span className="text-xs text-gray-500 dark:text-gray-400">{ t('common:status.finalized') }</span>
                  <p className="text-xs font-medium text-gray-900 dark:text-white mt-0.5">
                    {formatDateTime(upload.completedAt ?? upload.updatedAt ?? '')}
                  </p>
                </div>
                <div className="bg-blue-50 dark:bg-blue-900/20 rounded-lg p-2">
                  <span className="text-xs text-blue-500 dark:text-blue-400">{t('upload:history.processingTime')}</span>
                  <p className="text-sm font-bold text-blue-700 dark:text-blue-300 mt-0.5">
                    {upload.status === 'COMPLETED'
                      ? formatDuration(upload.createdAt ?? '', upload.completedAt ?? upload.updatedAt ?? '')
                      : '-'}
                  </p>
                </div>
              </div>

              {/* Certificate Type Breakdown */}
              <div className="grid grid-cols-3 md:grid-cols-6 gap-2">
                <div className="bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-800 rounded-lg p-2 text-center">
                  <p className="text-lg font-bold text-purple-600 dark:text-purple-400">
                    {(upload.cscaCount ?? 0).toLocaleString()}
                  </p>
                  <GlossaryTerm term="CSCA" className="text-xs text-purple-700 dark:text-purple-300 justify-center" />
                </div>
                <div className="bg-indigo-50 dark:bg-indigo-900/20 border border-indigo-200 dark:border-indigo-800 rounded-lg p-2 text-center">
                  <p className="text-lg font-bold text-indigo-600 dark:text-indigo-400">
                    {upload.mlscCount || 0}
                  </p>
                  <GlossaryTerm term="MLSC" className="text-xs text-indigo-700 dark:text-indigo-300 justify-center" />
                </div>
                <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg p-2 text-center">
                  <p className="text-lg font-bold text-blue-600 dark:text-blue-400">
                    {upload.dscCount || 0}
                  </p>
                  <GlossaryTerm term="DSC" className="text-xs text-blue-700 dark:text-blue-300 justify-center" />
                </div>
                <div className="bg-orange-50 dark:bg-orange-900/20 border border-orange-200 dark:border-orange-800 rounded-lg p-2 text-center">
                  <p className="text-lg font-bold text-orange-600 dark:text-orange-400">
                    {upload.dscNcCount || 0}
                  </p>
                  <GlossaryTerm term="DSC_NC" className="text-xs text-orange-700 dark:text-orange-300 justify-center" />
                </div>
                <div className="bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-lg p-2 text-center">
                  <p className="text-lg font-bold text-amber-600 dark:text-amber-400">
                    {(upload.crlCount ?? 0).toLocaleString()}
                  </p>
                  <GlossaryTerm term="CRL" className="text-xs text-amber-700 dark:text-amber-300 justify-center" />
                </div>
                <div className="bg-teal-50 dark:bg-teal-900/20 border border-teal-200 dark:border-teal-800 rounded-lg p-2 text-center">
                  <p className="text-lg font-bold text-teal-600 dark:text-teal-400">
                    {upload.mlCount || 0}
                  </p>
                  <GlossaryTerm term="ML" className="text-xs text-teal-700 dark:text-teal-300 justify-center" />
                </div>
              </div>

              {/* Validation Statistics -- shared component */}
              {upload.validation && (
                <ValidationSummaryPanel
                  data={{
                    validCount: upload.validation.validCount,
                    invalidCount: upload.validation.invalidCount,
                    pendingCount: upload.validation.pendingCount,
                    expiredValidCount: upload.validation.expiredValidCount,
                    errorCount: upload.validation.errorCount,
                    trustChainValidCount: upload.validation.trustChainValidCount,
                    trustChainInvalidCount: upload.validation.trustChainInvalidCount,
                    cscaNotFoundCount: upload.validation.cscaNotFoundCount,
                    expiredCount: upload.validation.expiredCount,
                    validPeriodCount: upload.validation.validPeriodCount,
                    revokedCount: upload.validation.revokedCount,
                    icaoCompliantCount: upload.validation.icaoCompliantCount,
                    icaoNonCompliantCount: upload.validation.icaoNonCompliantCount,
                    icaoWarningCount: upload.validation.icaoWarningCount,
                    duplicateCount: detailDuplicateCount > 0 ? detailDuplicateCount : undefined,
                    totalCertificates: upload.totalEntries,
                    processedCount: upload.processedEntries,
                    complianceViolations,
                  }}
                  uploadId={upload.id}
                />
              )}

              {/* Master List Extraction Statistics (v2.1.1) */}
              {(upload.cscaExtractedFromMl || upload.cscaDuplicates) && (
                <div className="bg-indigo-50 dark:bg-indigo-900/20 border border-indigo-200 dark:border-indigo-800 rounded-lg p-3">
                  <div className="flex items-center gap-2 mb-2">
                    <Database className="w-4 h-4 text-indigo-600 dark:text-indigo-400" />
                    <span className="text-xs font-semibold text-indigo-700 dark:text-indigo-300">{t('upload:history.mlExtraction')}</span>
                    <span className="px-1.5 py-0.5 text-xs font-medium rounded bg-indigo-100 dark:bg-indigo-900/50 text-indigo-700 dark:text-indigo-300">
                      v2.1.1
                    </span>
                  </div>
                  <div className="grid grid-cols-3 gap-2">
                    <div className="bg-white dark:bg-gray-800 rounded p-2 text-center">
                      <p className="text-lg font-bold text-indigo-600 dark:text-indigo-400">
                        {upload.cscaExtractedFromMl || 0}
                      </p>
                      <span className="text-xs text-indigo-700 dark:text-indigo-300">{t('upload:history.totalExtracted')}</span>
                    </div>
                    <div className="bg-white dark:bg-gray-800 rounded p-2 text-center">
                      <p className="text-lg font-bold text-amber-600 dark:text-amber-400">
                        {upload.cscaDuplicates || 0}
                      </p>
                      <span className="text-xs text-amber-700 dark:text-amber-300">{ t('common:status.duplicate') }</span>
                    </div>
                    <div className="bg-white dark:bg-gray-800 rounded p-2 text-center">
                      <p className="text-lg font-bold text-green-600 dark:text-green-400">
                        {upload.cscaExtractedFromMl && upload.cscaExtractedFromMl > 0
                          ? ((upload.cscaExtractedFromMl - (upload.cscaDuplicates || 0)) / upload.cscaExtractedFromMl * 100).toFixed(0)
                          : '0'}%
                      </p>
                      <span className="text-xs text-green-700 dark:text-green-300">{t('upload:history.newCerts')}</span>
                    </div>
                  </div>
                </div>
              )}


              {/* LDAP Storage Warning - Data Consistency Check (v2.0.0) */}
              {upload.status === 'COMPLETED' &&
               upload.certificateCount &&
               upload.certificateCount > 0 &&
               (!upload.ldapUploadedCount || upload.ldapUploadedCount === 0) && (
                <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg p-3">
                  <div className="flex items-start gap-2">
                    <AlertTriangle className="w-5 h-5 text-red-500 mt-0.5 flex-shrink-0" />
                    <div className="flex-1">
                      <h4 className="text-sm font-semibold text-red-800 dark:text-red-300">
                        {t('upload:history.ldapFailureTitle')}
                      </h4>
                      <p className="text-xs text-red-600 dark:text-red-400 mt-1">
                        {t('upload:history.ldapFailureDesc', { num: upload.certificateCount })}
                      </p>
                      <p className="text-xs text-red-600 dark:text-red-400 mt-1">
                        {t('upload:history.ldapFailureAction')}
                      </p>
                      <div className="mt-2 flex gap-2">
                        <span className="px-2 py-1 text-xs font-medium rounded bg-red-100 dark:bg-red-900/50 text-red-700 dark:text-red-300">
                          DB: {t('upload:history.itemCount', { num: upload.certificateCount })}
                        </span>
                        <span className="px-2 py-1 text-xs font-medium rounded bg-red-100 dark:bg-red-900/50 text-red-700 dark:text-red-300">
                          LDAP: {t('upload:history.itemCount', { num: upload.ldapUploadedCount || 0 })}
                        </span>
                      </div>
                    </div>
                  </div>
                </div>
              )}

              {/* Upload ID - Compact */}
              <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
                <span className="text-xs text-gray-500 dark:text-gray-400">{t('history.uploadId')}</span>
                <p className="text-xs font-mono text-gray-900 dark:text-white mt-0.5 break-all">
                  {upload.id}
                </p>
              </div>
          </div>
          )}

          {/* Structure Tab - LDIF/Master List Structure */}
          {activeTab === 'structure' && (
            <div className="overflow-y-auto">
              {upload.fileFormat === 'LDIF' ? (
                <LdifStructure uploadId={upload.id} />
              ) : (
                <MasterListStructure uploadId={upload.id} />
              )}
            </div>
          )}

          {/* Duplicates Tab - Duplicate Certificates Tree or computed summary */}
          {activeTab === 'duplicates' && detailDuplicateCount > 0 && uploadIssues && uploadIssues.totalDuplicates > 0 && (
            <div className="space-y-2.5">
              {/* Header with export buttons -- compact */}
              <div className="flex items-center justify-between pb-2 border-b border-gray-200 dark:border-gray-700">
                <div className="flex items-center gap-1.5">
                  <AlertCircle className="w-4 h-4 text-yellow-600 dark:text-yellow-400" />
                  <h3 className="text-sm font-semibold text-gray-900 dark:text-white">
                    {t('upload:history.duplicateList')}
                  </h3>
                  <span className="px-1.5 py-0.5 text-xs font-medium rounded bg-yellow-100 dark:bg-yellow-900/50 text-yellow-700 dark:text-yellow-300">
                    {t('upload:history.totalItemCount', { num: uploadIssues.totalDuplicates })}
                  </span>
                </div>
                <div className="flex items-center gap-1.5">
                  <button
                    onClick={() => exportDuplicateStatisticsToCsv(
                      uploadIssues.byType,
                      uploadIssues.totalDuplicates,
                      `duplicate-stats-${upload.id}.csv`
                    )}
                    className="flex items-center gap-1 px-2 py-1 text-xs font-medium text-yellow-700 dark:text-yellow-300 bg-white dark:bg-gray-800 border border-yellow-300 dark:border-yellow-700 rounded hover:bg-yellow-50 dark:hover:bg-yellow-900/30 transition-colors"
                    title={t('upload:history.exportStats')}
                  >
                    <Download className="w-3 h-3" />
                    {t('upload:detail.statistics')}
                  </button>
                  <button
                    onClick={() => exportDuplicatesToCsv(
                      uploadIssues.duplicates,
                      `duplicates-${upload.id}.csv`
                    )}
                    className="flex items-center gap-1 px-2 py-1 text-xs font-medium text-yellow-700 dark:text-yellow-300 bg-white dark:bg-gray-800 border border-yellow-300 dark:border-yellow-700 rounded hover:bg-yellow-50 dark:hover:bg-yellow-900/30 transition-colors"
                    title={t('certificate:search.exportAll')}
                  >
                    <Download className="w-3 h-3" />
                    {t('monitoring:pool.total')}
                  </button>
                </div>
              </div>

              {/* Summary by type -- compact inline */}
              <div className="flex flex-wrap gap-1.5">
                {uploadIssues.byType.CSCA > 0 && (
                  <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded px-3 py-1.5 text-center">
                    <span className="text-sm font-bold text-blue-600 dark:text-blue-400">{uploadIssues.byType.CSCA}</span>
                    <span className="text-xs text-blue-700 dark:text-blue-300 ml-1">CSCA</span>
                  </div>
                )}
                {uploadIssues.byType.MLSC > 0 && (
                  <div className="bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-800 rounded px-3 py-1.5 text-center">
                    <span className="text-sm font-bold text-purple-600 dark:text-purple-400">{uploadIssues.byType.MLSC}</span>
                    <span className="text-xs text-purple-700 dark:text-purple-300 ml-1">MLSC</span>
                  </div>
                )}
                {uploadIssues.byType.DSC > 0 && (
                  <div className="bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800 rounded px-3 py-1.5 text-center">
                    <span className="text-sm font-bold text-green-600 dark:text-green-400">{uploadIssues.byType.DSC}</span>
                    <span className="text-xs text-green-700 dark:text-green-300 ml-1">DSC</span>
                  </div>
                )}
                {uploadIssues.byType.DSC_NC > 0 && (
                  <div className="bg-orange-50 dark:bg-orange-900/20 border border-orange-200 dark:border-orange-800 rounded px-3 py-1.5 text-center">
                    <span className="text-sm font-bold text-orange-600 dark:text-orange-400">{uploadIssues.byType.DSC_NC}</span>
                    <span className="text-xs text-orange-700 dark:text-orange-300 ml-1">DSC_NC</span>
                  </div>
                )}
                {uploadIssues.byType.CRL > 0 && (
                  <div className="bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded px-3 py-1.5 text-center">
                    <span className="text-sm font-bold text-amber-600 dark:text-amber-400">{uploadIssues.byType.CRL}</span>
                    <span className="text-xs text-amber-700 dark:text-amber-300 ml-1">CRL</span>
                  </div>
                )}
              </div>

              {/* Tree View - Scrollable -- compact */}
              <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-lg p-2 max-h-[420px] overflow-y-auto">
                <DuplicateCertificatesTree duplicates={uploadIssues.duplicates} />
              </div>

              {loadingIssues && (
                <div className="bg-gray-50 dark:bg-gray-800/50 border border-gray-200 dark:border-gray-700 rounded p-3">
                  <div className="flex items-center gap-2 justify-center">
                    <Loader2 className="w-3.5 h-3.5 animate-spin text-gray-400" />
                    <span className="text-xs text-gray-500 dark:text-gray-400">{t('upload:history.loadingIssues')}</span>
                  </div>
                </div>
              )}
            </div>
          )}

          {/* Duplicates Tab - Computed summary (LDIF uploads without certificate_duplicates records) */}
          {activeTab === 'duplicates' && detailDuplicateCount > 0 && (!uploadIssues || uploadIssues.totalDuplicates === 0) && (
            <div className="space-y-2.5">
              <div className="flex items-center gap-1.5 pb-2 border-b border-gray-200 dark:border-gray-700">
                <AlertCircle className="w-4 h-4 text-yellow-600 dark:text-yellow-400" />
                <h3 className="text-sm font-semibold text-gray-900 dark:text-white">
                  {t('upload:history.duplicateList')}
                </h3>
                <span className="px-1.5 py-0.5 text-xs font-medium rounded bg-yellow-100 dark:bg-yellow-900/50 text-yellow-700 dark:text-yellow-300">
                  {t('upload:history.totalItemCount', { num: detailDuplicateCount.toLocaleString() })}
                </span>
              </div>

              {/* Summary cards */}
              <div className="bg-yellow-50 dark:bg-yellow-900/10 border border-yellow-200 dark:border-yellow-800 rounded-lg p-4">
                <div className="flex items-center gap-3 justify-center mb-3">
                  <div className="bg-slate-50 dark:bg-slate-800/50 border border-slate-200 dark:border-slate-700 rounded-lg px-4 py-2 text-center">
                    <p className="text-lg font-bold text-slate-700 dark:text-slate-300">{(upload.totalEntries || 0).toLocaleString()}</p>
                    <span className="text-xs text-slate-500">{t('upload:validationSummary.fileTotal')}</span>
                  </div>
                  <ChevronRight className="w-4 h-4 text-gray-300" />
                  <div className="bg-yellow-100 dark:bg-yellow-900/30 border border-yellow-300 dark:border-yellow-700 rounded-lg px-4 py-2 text-center">
                    <p className="text-lg font-bold text-yellow-700 dark:text-yellow-300">{detailDuplicateCount.toLocaleString()}</p>
                    <span className="text-xs text-yellow-600">{t('upload:validationSummary.duplicatePercent', { pct: upload.totalEntries ? Math.round((detailDuplicateCount / upload.totalEntries) * 100) : 0 })}</span>
                  </div>
                  <ChevronRight className="w-4 h-4 text-gray-300" />
                  <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg px-4 py-2 text-center">
                    <p className="text-lg font-bold text-blue-700 dark:text-blue-300">{((upload.totalEntries || 0) - detailDuplicateCount).toLocaleString()}</p>
                    <span className="text-xs text-blue-600">{t('upload:validationSummary.newlyProcessed')}</span>
                  </div>
                </div>

                {/* Stored breakdown by type */}
                <div className="flex flex-wrap gap-1.5 justify-center">
                  {(upload.cscaCount || 0) > 0 && (
                    <div className="bg-white dark:bg-gray-800 border border-blue-200 dark:border-blue-800 rounded px-3 py-1.5 text-center">
                      <span className="text-sm font-bold text-blue-600 dark:text-blue-400">{(upload.cscaCount ?? 0).toLocaleString()}</span>
                      <span className="text-xs text-blue-700 dark:text-blue-300 ml-1">CSCA</span>
                    </div>
                  )}
                  {(upload.mlscCount || 0) > 0 && (
                    <div className="bg-white dark:bg-gray-800 border border-purple-200 dark:border-purple-800 rounded px-3 py-1.5 text-center">
                      <span className="text-sm font-bold text-purple-600 dark:text-purple-400">{(upload.mlscCount ?? 0).toLocaleString()}</span>
                      <span className="text-xs text-purple-700 dark:text-purple-300 ml-1">MLSC</span>
                    </div>
                  )}
                  {(upload.dscCount || 0) > 0 && (
                    <div className="bg-white dark:bg-gray-800 border border-green-200 dark:border-green-800 rounded px-3 py-1.5 text-center">
                      <span className="text-sm font-bold text-green-600 dark:text-green-400">{(upload.dscCount ?? 0).toLocaleString()}</span>
                      <span className="text-xs text-green-700 dark:text-green-300 ml-1">DSC</span>
                    </div>
                  )}
                  {(upload.dscNcCount || 0) > 0 && (
                    <div className="bg-white dark:bg-gray-800 border border-orange-200 dark:border-orange-800 rounded px-3 py-1.5 text-center">
                      <span className="text-sm font-bold text-orange-600 dark:text-orange-400">{(upload.dscNcCount ?? 0).toLocaleString()}</span>
                      <span className="text-xs text-orange-700 dark:text-orange-300 ml-1">DSC_NC</span>
                    </div>
                  )}
                  {(upload.crlCount || 0) > 0 && (
                    <div className="bg-white dark:bg-gray-800 border border-amber-200 dark:border-amber-800 rounded px-3 py-1.5 text-center">
                      <span className="text-sm font-bold text-amber-600 dark:text-amber-400">{(upload.crlCount ?? 0).toLocaleString()}</span>
                      <span className="text-xs text-amber-700 dark:text-amber-300 ml-1">CRL</span>
                    </div>
                  )}
                </div>

                <p className="text-xs text-yellow-700 dark:text-yellow-400 text-center mt-3">
                  {t('upload:validationSummary.fileTotal')} {(upload.totalEntries || 0).toLocaleString()}개 중 {detailDuplicateCount.toLocaleString()}개가 이미 등록된 인증서입니다.
                </p>
              </div>
            </div>
          )}
        </div>

        {/* Footer */}
        <div className="flex justify-end gap-3 px-5 py-3 border-t border-gray-200 dark:border-gray-700">
          <button
            onClick={onClose}
            className="px-4 py-2 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
          >
            {t('icao:banner.dismiss')}
          </button>
        </div>
      </div>
    </div>
  );
}

export default UploadDetailModal;
