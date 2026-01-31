import React, { useMemo } from 'react';
import { FileText, ChevronRight, ChevronDown, Copy, Info } from 'lucide-react';
import type { UploadDuplicate } from '../types';
import { getFlagSvgPath } from '../utils/countryCode';

interface DuplicateTreeNode {
  // Root node: original certificate
  certificateId: string;
  fingerprint: string;
  subjectDn: string;
  certificateType: string;
  country: string;
  firstUpload: {
    uploadId: string;
    fileName: string;
    timestamp: string;
  };
  // Children: duplicate detections
  duplicates: UploadDuplicate[];
}

interface Props {
  duplicates: UploadDuplicate[];
  onViewDetail?: (duplicate: UploadDuplicate) => void;
}

export const DuplicateCertificatesTree: React.FC<Props> = ({
  duplicates,
  onViewDetail
}) => {
  const [expandedNodes, setExpandedNodes] = React.useState<Set<string>>(new Set());

  // Group duplicates by certificate to build tree structure
  const treeNodes = useMemo((): DuplicateTreeNode[] => {
    const grouped = new Map<string, DuplicateTreeNode>();

    duplicates.forEach(dup => {
      const key = dup.certificateId;

      if (!grouped.has(key)) {
        grouped.set(key, {
          certificateId: dup.certificateId,
          fingerprint: dup.fingerprint,
          subjectDn: dup.subjectDn,
          certificateType: dup.certificateType,
          country: dup.country,
          firstUpload: {
            uploadId: dup.firstUploadId,
            fileName: dup.firstUploadFileName || 'Unknown',
            timestamp: dup.firstUploadTimestamp || ''
          },
          duplicates: []
        });
      }

      grouped.get(key)!.duplicates.push(dup);
    });

    return Array.from(grouped.values());
  }, [duplicates]);

  const toggleNode = (certificateId: string) => {
    setExpandedNodes(prev => {
      const next = new Set(prev);
      if (next.has(certificateId)) {
        next.delete(certificateId);
      } else {
        next.add(certificateId);
      }
      return next;
    });
  };

  const getCertificateTypeBadge = (type: string) => {
    const colors: Record<string, string> = {
      CSCA: 'bg-blue-100 text-blue-800 dark:bg-blue-900 dark:text-blue-200',
      DSC: 'bg-green-100 text-green-800 dark:bg-green-900 dark:text-green-200',
      DSC_NC: 'bg-orange-100 text-orange-800 dark:bg-orange-900 dark:text-orange-200',
      MLSC: 'bg-purple-100 text-purple-800 dark:bg-purple-900 dark:text-purple-200',
      CRL: 'bg-red-100 text-red-800 dark:bg-red-900 dark:text-red-200'
    };

    return (
      <span className={`px-2 py-1 rounded text-xs font-medium ${colors[type] || 'bg-gray-100 text-gray-800'}`}>
        {type}
      </span>
    );
  };

  const formatDate = (dateStr: string) => {
    if (!dateStr) return '';
    const date = new Date(dateStr);
    return date.toLocaleString('ko-KR', {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit'
    });
  };

  if (treeNodes.length === 0) {
    return (
      <div className="text-center py-8 text-gray-500 dark:text-gray-400">
        중복된 인증서가 없습니다.
      </div>
    );
  }

  return (
    <div className="space-y-3">
      {treeNodes.map(node => {
        const isExpanded = expandedNodes.has(node.certificateId);
        const duplicateCount = node.duplicates.length;

        return (
          <div
            key={node.certificateId}
            className="border border-gray-200 dark:border-gray-700 rounded-lg overflow-hidden"
          >
            {/* Root node - Original certificate */}
            <div
              className="bg-white dark:bg-gray-800 p-4 cursor-pointer hover:bg-gray-50 dark:hover:bg-gray-750 transition-colors"
              onClick={() => toggleNode(node.certificateId)}
            >
              <div className="flex items-start gap-3">
                <div className="flex-shrink-0 mt-1">
                  {isExpanded ? (
                    <ChevronDown className="w-5 h-5 text-gray-400" />
                  ) : (
                    <ChevronRight className="w-5 h-5 text-gray-400" />
                  )}
                </div>

                <div className="flex-1 min-w-0">
                  <div className="flex items-center gap-2 mb-2">
                    {getCertificateTypeBadge(node.certificateType)}
                    {node.country && getFlagSvgPath(node.country) && (
                      <img
                        src={getFlagSvgPath(node.country)}
                        alt={node.country}
                        className="w-6 h-4 object-cover rounded shadow-sm border"
                      />
                    )}
                    <span className="text-xs text-gray-500 dark:text-gray-400">
                      {node.country}
                    </span>
                  </div>

                  <div className="text-sm font-medium text-gray-900 dark:text-gray-100 mb-1 truncate">
                    {node.subjectDn}
                  </div>

                  <div className="flex items-center gap-2 text-xs text-gray-500 dark:text-gray-400">
                    <FileText className="w-3 h-3" />
                    <span>원본 업로드: {node.firstUpload.fileName}</span>
                    <span>•</span>
                    <span>{formatDate(node.firstUpload.timestamp)}</span>
                  </div>

                  <div className="mt-2 flex items-center gap-2">
                    <Copy className="w-3 h-3 text-orange-500" />
                    <span className="text-xs font-medium text-orange-600 dark:text-orange-400">
                      {duplicateCount}회 중복 감지됨
                    </span>
                  </div>
                </div>
              </div>
            </div>

            {/* Children nodes - Duplicate detections */}
            {isExpanded && (
              <div className="bg-gray-50 dark:bg-gray-900 border-t border-gray-200 dark:border-gray-700">
                {node.duplicates.map((dup, index) => (
                  <div
                    key={dup.id}
                    className={`p-4 pl-12 ${
                      index !== node.duplicates.length - 1
                        ? 'border-b border-gray-200 dark:border-gray-700'
                        : ''
                    } hover:bg-gray-100 dark:hover:bg-gray-800 transition-colors`}
                  >
                    <div className="flex items-start justify-between gap-3">
                      <div className="flex-1 min-w-0">
                        <div className="flex items-center gap-2 mb-2">
                          <span className="text-xs font-medium text-gray-700 dark:text-gray-300">
                            중복 #{index + 1}
                          </span>
                          <span className="text-xs text-gray-500 dark:text-gray-400">
                            {dup.sourceType}
                          </span>
                          {dup.sourceCountry && (
                            <>
                              <span>•</span>
                              <span className="text-xs text-gray-500 dark:text-gray-400">
                                {dup.sourceCountry}
                              </span>
                            </>
                          )}
                        </div>

                        {dup.sourceFileName && (
                          <div className="text-xs text-gray-600 dark:text-gray-400 mb-1">
                            <span className="font-medium">파일명:</span> {dup.sourceFileName}
                          </div>
                        )}

                        {dup.sourceEntryDn && (
                          <div className="text-xs text-gray-600 dark:text-gray-400 mb-1 truncate">
                            <span className="font-medium">LDAP DN:</span> {dup.sourceEntryDn}
                          </div>
                        )}

                        <div className="text-xs text-gray-500 dark:text-gray-400">
                          감지 시각: {formatDate(dup.detectedAt)}
                        </div>
                      </div>

                      {onViewDetail && (
                        <button
                          onClick={(e) => {
                            e.stopPropagation();
                            onViewDetail(dup);
                          }}
                          className="flex-shrink-0 p-2 text-gray-400 hover:text-blue-500 dark:hover:text-blue-400 transition-colors"
                          title="상세 정보 보기"
                        >
                          <Info className="w-4 h-4" />
                        </button>
                      )}
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>
        );
      })}
    </div>
  );
};
