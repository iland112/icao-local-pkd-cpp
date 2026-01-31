import React from 'react';
import { X, Copy, FileText, MapPin, Database, Link2 } from 'lucide-react';
import type { UploadDuplicate } from '../types';
import { getFlagSvgPath } from '../utils/countryCode';

interface Props {
  duplicate: UploadDuplicate | null;
  isOpen: boolean;
  onClose: () => void;
}

export const DuplicateCertificateDialog: React.FC<Props> = ({
  duplicate,
  isOpen,
  onClose
}) => {
  if (!isOpen || !duplicate) return null;

  const formatDate = (dateStr: string) => {
    if (!dateStr) return 'N/A';
    const date = new Date(dateStr);
    return date.toLocaleString('ko-KR', {
      year: 'numeric',
      month: 'long',
      day: 'numeric',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit'
    });
  };

  const getCertificateTypeBadge = (type: string) => {
    const colors: Record<string, { bg: string; text: string; label: string }> = {
      CSCA: { bg: 'bg-blue-100 dark:bg-blue-900', text: 'text-blue-800 dark:text-blue-200', label: 'CSCA' },
      DSC: { bg: 'bg-green-100 dark:bg-green-900', text: 'text-green-800 dark:text-green-200', label: 'DSC' },
      DSC_NC: { bg: 'bg-orange-100 dark:bg-orange-900', text: 'text-orange-800 dark:text-orange-200', label: 'DSC_NC' },
      MLSC: { bg: 'bg-purple-100 dark:bg-purple-900', text: 'text-purple-800 dark:text-purple-200', label: 'MLSC' },
      CRL: { bg: 'bg-red-100 dark:bg-red-900', text: 'text-red-800 dark:text-red-200', label: 'CRL' }
    };

    const config = colors[type] || { bg: 'bg-gray-100', text: 'text-gray-800', label: type };

    return (
      <span className={`px-3 py-1 rounded-full text-sm font-semibold ${config.bg} ${config.text}`}>
        {config.label}
      </span>
    );
  };

  const copyToClipboard = async (text: string, label: string) => {
    try {
      await navigator.clipboard.writeText(text);
      // You might want to add a toast notification here
      console.log(`${label} copied to clipboard`);
    } catch (err) {
      console.error('Failed to copy:', err);
    }
  };

  return (
    <div className="fixed inset-0 bg-black bg-opacity-50 flex items-center justify-center z-50 p-4">
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-2xl max-w-3xl w-full max-h-[90vh] overflow-hidden flex flex-col">
        {/* Header */}
        <div className="flex items-center justify-between p-6 border-b border-gray-200 dark:border-gray-700">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-orange-100 dark:bg-orange-900/30">
              <Copy className="w-6 h-6 text-orange-600 dark:text-orange-400" />
            </div>
            <div>
              <h2 className="text-xl font-bold text-gray-900 dark:text-gray-100">
                중복 인증서 상세 정보
              </h2>
              <p className="text-sm text-gray-500 dark:text-gray-400 mt-1">
                Duplicate Certificate Details
              </p>
            </div>
          </div>
          <button
            onClick={onClose}
            className="p-2 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-lg transition-colors"
          >
            <X className="w-5 h-5 text-gray-500 dark:text-gray-400" />
          </button>
        </div>

        {/* Content */}
        <div className="flex-1 overflow-y-auto p-6 space-y-6">
          {/* Certificate Information */}
          <section>
            <h3 className="text-lg font-semibold text-gray-900 dark:text-gray-100 mb-4 flex items-center gap-2">
              <FileText className="w-5 h-5 text-blue-500" />
              인증서 정보
            </h3>
            <div className="bg-gray-50 dark:bg-gray-900 rounded-xl p-4 space-y-3">
              <div className="flex items-center gap-2">
                <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-24">
                  타입:
                </span>
                {getCertificateTypeBadge(duplicate.certificateType)}
              </div>

              <div className="flex items-start gap-2">
                <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-24 flex-shrink-0">
                  국가:
                </span>
                <div className="flex items-center gap-2">
                  {duplicate.country && getFlagSvgPath(duplicate.country) && (
                    <img
                      src={getFlagSvgPath(duplicate.country)}
                      alt={duplicate.country}
                      className="w-6 h-4 object-cover rounded shadow-sm border"
                    />
                  )}
                  <span className="text-sm text-gray-900 dark:text-gray-100">
                    {duplicate.country}
                  </span>
                </div>
              </div>

              <div className="flex items-start gap-2">
                <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-24 flex-shrink-0">
                  Subject DN:
                </span>
                <div className="flex-1 flex items-center gap-2">
                  <span className="text-sm text-gray-900 dark:text-gray-100 break-all">
                    {duplicate.subjectDn}
                  </span>
                  <button
                    onClick={() => copyToClipboard(duplicate.subjectDn, 'Subject DN')}
                    className="p-1 hover:bg-gray-200 dark:hover:bg-gray-700 rounded transition-colors flex-shrink-0"
                    title="복사"
                  >
                    <Copy className="w-4 h-4 text-gray-400" />
                  </button>
                </div>
              </div>

              <div className="flex items-start gap-2">
                <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-24 flex-shrink-0">
                  Fingerprint:
                </span>
                <div className="flex-1 flex items-center gap-2">
                  <span className="text-xs font-mono text-gray-900 dark:text-gray-100 break-all">
                    {duplicate.fingerprint}
                  </span>
                  <button
                    onClick={() => copyToClipboard(duplicate.fingerprint, 'Fingerprint')}
                    className="p-1 hover:bg-gray-200 dark:hover:bg-gray-700 rounded transition-colors flex-shrink-0"
                    title="복사"
                  >
                    <Copy className="w-4 h-4 text-gray-400" />
                  </button>
                </div>
              </div>
            </div>
          </section>

          {/* Original Upload Information */}
          <section>
            <h3 className="text-lg font-semibold text-gray-900 dark:text-gray-100 mb-4 flex items-center gap-2">
              <Database className="w-5 h-5 text-green-500" />
              원본 업로드 정보
            </h3>
            <div className="bg-green-50 dark:bg-green-900/20 rounded-xl p-4 space-y-3 border border-green-200 dark:border-green-800">
              <div className="flex items-start gap-2">
                <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-24 flex-shrink-0">
                  파일명:
                </span>
                <span className="text-sm text-gray-900 dark:text-gray-100">
                  {duplicate.firstUploadFileName || 'N/A'}
                </span>
              </div>

              <div className="flex items-start gap-2">
                <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-24 flex-shrink-0">
                  업로드 시각:
                </span>
                <span className="text-sm text-gray-900 dark:text-gray-100">
                  {formatDate(duplicate.firstUploadTimestamp || '')}
                </span>
              </div>

              <div className="flex items-start gap-2">
                <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-24 flex-shrink-0">
                  Upload ID:
                </span>
                <div className="flex-1 flex items-center gap-2">
                  <span className="text-xs font-mono text-gray-900 dark:text-gray-100">
                    {duplicate.firstUploadId}
                  </span>
                  <button
                    onClick={() => copyToClipboard(duplicate.firstUploadId, 'Upload ID')}
                    className="p-1 hover:bg-gray-200 dark:hover:bg-gray-700 rounded transition-colors flex-shrink-0"
                    title="복사"
                  >
                    <Copy className="w-4 h-4 text-gray-400" />
                  </button>
                </div>
              </div>
            </div>
          </section>

          {/* Duplicate Detection Information */}
          <section>
            <h3 className="text-lg font-semibold text-gray-900 dark:text-gray-100 mb-4 flex items-center gap-2">
              <MapPin className="w-5 h-5 text-orange-500" />
              중복 감지 정보
            </h3>
            <div className="bg-orange-50 dark:bg-orange-900/20 rounded-xl p-4 space-y-3 border border-orange-200 dark:border-orange-800">
              <div className="flex items-start gap-2">
                <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-24 flex-shrink-0">
                  소스 타입:
                </span>
                <span className="text-sm text-gray-900 dark:text-gray-100">
                  {duplicate.sourceType}
                </span>
              </div>

              {duplicate.sourceCountry && (
                <div className="flex items-start gap-2">
                  <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-24 flex-shrink-0">
                    소스 국가:
                  </span>
                  <span className="text-sm text-gray-900 dark:text-gray-100">
                    {duplicate.sourceCountry}
                  </span>
                </div>
              )}

              {duplicate.sourceFileName && (
                <div className="flex items-start gap-2">
                  <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-24 flex-shrink-0">
                    소스 파일:
                  </span>
                  <span className="text-sm text-gray-900 dark:text-gray-100">
                    {duplicate.sourceFileName}
                  </span>
                </div>
              )}

              {duplicate.sourceEntryDn && (
                <div className="flex items-start gap-2">
                  <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-24 flex-shrink-0">
                    LDAP DN:
                  </span>
                  <div className="flex-1 flex items-center gap-2">
                    <span className="text-xs font-mono text-gray-900 dark:text-gray-100 break-all">
                      {duplicate.sourceEntryDn}
                    </span>
                    <button
                      onClick={() => copyToClipboard(duplicate.sourceEntryDn!, 'LDAP DN')}
                      className="p-1 hover:bg-gray-200 dark:hover:bg-gray-700 rounded transition-colors flex-shrink-0"
                      title="복사"
                    >
                      <Copy className="w-4 h-4 text-gray-400" />
                    </button>
                  </div>
                </div>
              )}

              <div className="flex items-start gap-2">
                <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-24 flex-shrink-0">
                  감지 시각:
                </span>
                <span className="text-sm text-gray-900 dark:text-gray-100">
                  {formatDate(duplicate.detectedAt)}
                </span>
              </div>
            </div>
          </section>

          {/* Database Information */}
          <section>
            <h3 className="text-lg font-semibold text-gray-900 dark:text-gray-100 mb-4 flex items-center gap-2">
              <Link2 className="w-5 h-5 text-gray-500" />
              데이터베이스 정보
            </h3>
            <div className="bg-gray-50 dark:bg-gray-900 rounded-xl p-4 space-y-3">
              <div className="flex items-start gap-2">
                <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-32 flex-shrink-0">
                  Duplicate ID:
                </span>
                <span className="text-sm font-mono text-gray-900 dark:text-gray-100">
                  {duplicate.id}
                </span>
              </div>

              <div className="flex items-start gap-2">
                <span className="text-sm font-medium text-gray-600 dark:text-gray-400 w-32 flex-shrink-0">
                  Certificate ID:
                </span>
                <div className="flex-1 flex items-center gap-2">
                  <span className="text-xs font-mono text-gray-900 dark:text-gray-100">
                    {duplicate.certificateId}
                  </span>
                  <button
                    onClick={() => copyToClipboard(duplicate.certificateId, 'Certificate ID')}
                    className="p-1 hover:bg-gray-200 dark:hover:bg-gray-700 rounded transition-colors flex-shrink-0"
                    title="복사"
                  >
                    <Copy className="w-4 h-4 text-gray-400" />
                  </button>
                </div>
              </div>
            </div>
          </section>
        </div>

        {/* Footer */}
        <div className="flex items-center justify-end gap-3 p-6 border-t border-gray-200 dark:border-gray-700">
          <button
            onClick={onClose}
            className="px-4 py-2 bg-gray-200 dark:bg-gray-700 text-gray-700 dark:text-gray-300 rounded-lg hover:bg-gray-300 dark:hover:bg-gray-600 transition-colors font-medium"
          >
            닫기
          </button>
        </div>
      </div>
    </div>
  );
};
