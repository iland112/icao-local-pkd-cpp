import { useState, useEffect, useCallback } from 'react';
import { useTranslation } from 'react-i18next';
import {
  X,
  ShieldAlert,
  ChevronDown,
  ChevronRight,
  AlertTriangle,
  XCircle,
  Loader2,
  ExternalLink,
  Info,
} from 'lucide-react';
import { getUploadValidations } from '@/api/validationApi';
import type { ValidationResult } from '@/types/validation';

/** ICAO 9303 violation category key */
type ViolationCategory = 'algorithm' | 'keySize' | 'validityPeriod' | 'keyUsage' | 'extensions' | 'dnFormat';

interface IcaoViolationDetailDialogProps {
  open: boolean;
  onClose: () => void;
  uploadId: string;
  violations: Record<string, number>;
  /** Pre-selected category to expand on open */
  initialCategory?: string;
}

/** Category metadata: description + ICAO reference */
const categoryInfo: Record<ViolationCategory, {
  icon: string;
  description: string;
  reference: string;
  detail: string;
}> = {
  algorithm: {
    icon: '🔐',
    description: 'ICAO Doc 9303에서 승인한 서명 알고리즘을 사용하지 않는 인증서',
    reference: 'Doc 9303 Part 12, Section 7.1',
    detail: '승인 알고리즘: SHA-256/384/512 + RSA(2048+)/ECDSA(P-256/384/521)/RSA-PSS. SHA-1, MD5 등 취약 알고리즘 사용 시 미준수로 판정됩니다.',
  },
  keySize: {
    icon: '🔑',
    description: 'ICAO 권장 최소 키 크기를 충족하지 않는 인증서',
    reference: 'Doc 9303 Part 12, Section 7.1',
    detail: '최소 요구사항: RSA 2048비트, ECDSA P-256(256비트). RSA 1024비트 등 짧은 키는 브루트포스 공격에 취약하여 미준수로 판정됩니다.',
  },
  validityPeriod: {
    icon: '📅',
    description: '유효기간이 만료되었거나 아직 유효하지 않은 인증서',
    reference: 'Doc 9303 Part 12, Section 7.1.1',
    detail: '인증서의 notBefore/notAfter 필드 기준으로 현재 시점에서 유효하지 않은 인증서입니다. 만료된 인증서로 서명된 여권은 검증 실패할 수 있습니다.',
  },
  keyUsage: {
    icon: '🏷️',
    description: 'Key Usage 확장이 인증서 유형에 맞지 않는 인증서',
    reference: 'Doc 9303 Part 12, Section 7.1.1',
    detail: 'CSCA: keyCertSign + cRLSign 필수 (CA 인증서). DSC: digitalSignature 필수 (서명 전용). Key Usage가 없거나 잘못 설정된 경우 미준수로 판정됩니다.',
  },
  extensions: {
    icon: '📋',
    description: 'X.509 확장 필드가 ICAO 표준에 부합하지 않는 인증서',
    reference: 'Doc 9303 Part 12, Section 7.1.1',
    detail: 'CSCA: Basic Constraints(CA=true, pathLen=0) 필수. DSC: Basic Constraints(CA=false) 필수. AKI/SKI 존재 여부, 알 수 없는 critical 확장 사용 여부를 검사합니다.',
  },
  dnFormat: {
    icon: '📝',
    description: 'DN(Distinguished Name) 형식이 표준에 맞지 않는 인증서',
    reference: 'Doc 9303 Part 12, Section 7.1.1',
    detail: 'issuer/subject DN에 국가 코드(C=) 필드가 없거나, ISO 3166-1 alpha-2 형식이 아닌 경우 미준수로 판정됩니다.',
  },
};

/** Map violation category to the boolean field name in ValidationResult */
const categoryToField: Record<ViolationCategory, keyof ValidationResult> = {
  algorithm: 'icaoAlgorithmCompliant',
  keySize: 'icaoKeySizeCompliant',
  validityPeriod: 'icaoValidityPeriodCompliant',
  keyUsage: 'icaoKeyUsageCompliant',
  extensions: 'icaoExtensionsCompliant',
  dnFormat: 'icaoComplianceLevel', // fallback, filtered by icaoViolations
};

export function IcaoViolationDetailDialog({
  open,
  onClose,
  uploadId,
  violations,
  initialCategory,
}: IcaoViolationDetailDialogProps) {
  const { t } = useTranslation(['upload', 'common', 'certificate']);
  const [expandedCategory, setExpandedCategory] = useState<string | null>(initialCategory ?? null);
  const [loading, setLoading] = useState(false);
  const [certificates, setCertificates] = useState<ValidationResult[]>([]);
  const [loadedForCategory, setLoadedForCategory] = useState<string | null>(null);

  // Sort categories by count desc
  const sortedCategories = Object.entries(violations)
    .sort(([, a], [, b]) => b - a);

  const totalNonCompliant = sortedCategories.reduce((sum, [, count]) => sum + count, 0);

  // Load non-compliant certificates when category is expanded
  const loadCertificates = useCallback(async (category: string) => {
    if (!uploadId || loadedForCategory === category) return;

    setLoading(true);
    try {
      // Load all validations for this upload (paginated, up to 200)
      const response = await getUploadValidations(uploadId, { limit: 200, offset: 0 });
      if (response.validations) {
        // Filter by category violation
        const filtered = response.validations.filter((v) => {
          if (!v.icaoCompliant && v.icaoComplianceLevel === 'NON_CONFORMANT') {
            const field = categoryToField[category as ViolationCategory];
            if (field && field !== 'icaoComplianceLevel') {
              return v[field] === false;
            }
            // Fallback: check icaoViolations string
            if (v.icaoViolations) {
              return v.icaoViolations.toLowerCase().includes(category.toLowerCase());
            }
          }
          return false;
        });
        setCertificates(filtered);
        setLoadedForCategory(category);
      }
    } catch (err) {
      if (import.meta.env.DEV) console.error('Failed to load violation certificates:', err);
      setCertificates([]);
    } finally {
      setLoading(false);
    }
  }, [uploadId, loadedForCategory]);

  // Auto-load when category expanded
  useEffect(() => {
    if (expandedCategory && expandedCategory !== loadedForCategory) {
      loadCertificates(expandedCategory);
    }
  }, [expandedCategory, loadCertificates, loadedForCategory]);

  // Reset when dialog opens
  useEffect(() => {
    if (open) {
      setExpandedCategory(initialCategory ?? null);
      setCertificates([]);
      setLoadedForCategory(null);
    }
  }, [open, initialCategory]);

  const toggleCategory = (cat: string) => {
    if (expandedCategory === cat) {
      setExpandedCategory(null);
    } else {
      setExpandedCategory(cat);
      setCertificates([]);
      setLoadedForCategory(null);
    }
  };

  const violationLabel: Record<string, string> = {
    keyUsage: 'Key Usage',
    algorithm: t('upload:validationSummary.violationAlgorithm'),
    keySize: t('upload:validationSummary.violationKeySize'),
    validityPeriod: t('upload:validationSummary.violationValidityPeriod'),
    dnFormat: t('upload:validationSummary.violationDnFormat'),
    extensions: t('upload:validationSummary.violationExtensions'),
  };

  if (!open) return null;

  return (
    <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/50" onClick={onClose}>
      <div
        className="bg-white dark:bg-gray-800 rounded-lg shadow-xl max-w-sm sm:max-w-2xl lg:max-w-3xl w-full mx-4 max-h-[90vh] flex flex-col"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="flex items-center justify-between px-5 py-3 border-b border-gray-200 dark:border-gray-700 bg-red-50 dark:bg-red-900/20 rounded-t-lg">
          <div className="flex items-center gap-2">
            <ShieldAlert className="w-5 h-5 text-red-600 dark:text-red-400" />
            <div>
              <h2 className="text-sm font-semibold text-gray-900 dark:text-gray-100">
                ICAO 9303 미준수 상세
              </h2>
              <p className="text-xs text-gray-500 dark:text-gray-400">
                총 {totalNonCompliant.toLocaleString()}건의 미준수 항목이 감지되었습니다
              </p>
            </div>
          </div>
          <button onClick={onClose} className="p-1 hover:bg-red-100 dark:hover:bg-red-800/30 rounded">
            <X className="w-4 h-4 text-gray-500 dark:text-gray-400" />
          </button>
        </div>

        {/* Body */}
        <div className="overflow-y-auto flex-1 px-5 py-3 space-y-2">
          {/* Info banner */}
          <div className="flex items-start gap-2 bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg p-3">
            <Info className="w-4 h-4 text-blue-500 mt-0.5 shrink-0" />
            <p className="text-xs text-blue-700 dark:text-blue-300">
              ICAO Doc 9303은 전자여권 인증서의 표준 규격을 정의합니다.
              미준수 인증서는 일부 국경 검문소에서 검증 실패할 수 있으며,
              각 카테고리를 클릭하면 상세 설명과 해당 인증서 목록을 확인할 수 있습니다.
            </p>
          </div>

          {/* Category accordion */}
          {sortedCategories.map(([cat, count]) => {
            const info = categoryInfo[cat as ViolationCategory];
            const isExpanded = expandedCategory === cat;
            const pct = totalNonCompliant > 0 ? Math.round((count / totalNonCompliant) * 100) : 0;

            return (
              <div key={cat} className="border border-gray-200 dark:border-gray-700 rounded-lg overflow-hidden">
                {/* Category header — clickable */}
                <button
                  onClick={() => toggleCategory(cat)}
                  className="w-full flex items-center gap-3 px-4 py-2.5 hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors text-left"
                >
                  <span className="text-base">{info?.icon ?? '⚠️'}</span>
                  <div className="flex-1 min-w-0">
                    <div className="flex items-center justify-between">
                      <span className="text-sm font-medium text-gray-900 dark:text-gray-100">
                        {violationLabel[cat] ?? cat}
                      </span>
                      <div className="flex items-center gap-2">
                        <span className="text-xs text-gray-500 dark:text-gray-400">{pct}%</span>
                        <span className="bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-300 text-xs font-semibold px-2 py-0.5 rounded-full">
                          {count.toLocaleString()}건
                        </span>
                      </div>
                    </div>
                    {/* Progress bar */}
                    <div className="w-full bg-gray-200 dark:bg-gray-700 rounded-full h-1 mt-1">
                      <div
                        className="bg-red-400 dark:bg-red-500 h-1 rounded-full transition-all"
                        style={{ width: `${pct}%` }}
                      />
                    </div>
                  </div>
                  {isExpanded
                    ? <ChevronDown className="w-4 h-4 text-gray-400 shrink-0" />
                    : <ChevronRight className="w-4 h-4 text-gray-400 shrink-0" />
                  }
                </button>

                {/* Expanded detail */}
                {isExpanded && (
                  <div className="border-t border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-800/50 px-4 py-3 space-y-3">
                    {/* Description */}
                    {info && (
                      <div className="space-y-1.5">
                        <p className="text-xs text-gray-700 dark:text-gray-300">{info.description}</p>
                        <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded p-2.5">
                          <p className="text-xs text-gray-600 dark:text-gray-400 leading-relaxed">{info.detail}</p>
                          <p className="text-[10px] text-gray-400 dark:text-gray-500 mt-1.5 flex items-center gap-1">
                            <ExternalLink className="w-3 h-3" />
                            참조: {info.reference}
                          </p>
                        </div>
                      </div>
                    )}

                    {/* Certificate list */}
                    <div>
                      <h5 className="text-[10px] font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wide mb-1.5">
                        해당 인증서 목록 (최대 200건)
                      </h5>
                      {loading ? (
                        <div className="flex items-center justify-center py-4">
                          <Loader2 className="w-4 h-4 animate-spin text-gray-400" />
                          <span className="ml-2 text-xs text-gray-500">조회 중...</span>
                        </div>
                      ) : certificates.length > 0 ? (
                        <div className="max-h-60 overflow-y-auto border border-gray-200 dark:border-gray-700 rounded">
                          <table className="w-full text-xs">
                            <thead className="bg-gray-100 dark:bg-gray-700/50 sticky top-0">
                              <tr>
                                <th className="px-2 py-1.5 text-left font-medium text-gray-500 dark:text-gray-400">국가</th>
                                <th className="px-2 py-1.5 text-left font-medium text-gray-500 dark:text-gray-400">유형</th>
                                <th className="px-2 py-1.5 text-left font-medium text-gray-500 dark:text-gray-400">Subject</th>
                                <th className="px-2 py-1.5 text-left font-medium text-gray-500 dark:text-gray-400">알고리즘</th>
                                <th className="px-2 py-1.5 text-center font-medium text-gray-500 dark:text-gray-400">상태</th>
                              </tr>
                            </thead>
                            <tbody className="divide-y divide-gray-100 dark:divide-gray-700">
                              {certificates.map((cert, idx) => (
                                <tr key={cert.id || idx} className="hover:bg-white dark:hover:bg-gray-800/30">
                                  <td className="px-2 py-1.5 whitespace-nowrap">
                                    <span className="inline-flex items-center gap-1">
                                      <img
                                        src={`https://flagcdn.com/16x12/${cert.countryCode?.toLowerCase()}.png`}
                                        alt={cert.countryCode}
                                        className="w-4 h-3"
                                        onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }}
                                      />
                                      <span>{cert.countryCode}</span>
                                    </span>
                                  </td>
                                  <td className="px-2 py-1.5 whitespace-nowrap">
                                    <span className="bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300 px-1.5 py-0.5 rounded text-[10px]">
                                      {cert.certificateType}
                                    </span>
                                  </td>
                                  <td className="px-2 py-1.5 max-w-[200px] truncate text-gray-700 dark:text-gray-300" title={cert.subjectDn}>
                                    {cert.subjectDn?.replace(/^.*?CN=/, 'CN=')?.substring(0, 40)}
                                  </td>
                                  <td className="px-2 py-1.5 whitespace-nowrap text-gray-600 dark:text-gray-400">
                                    {cert.signatureAlgorithm?.replace(/WithRSAEncryption|with/gi, '') || '-'}
                                  </td>
                                  <td className="px-2 py-1.5 text-center">
                                    <span className="inline-flex items-center gap-0.5 text-red-600 dark:text-red-400">
                                      <XCircle className="w-3 h-3" />
                                    </span>
                                  </td>
                                </tr>
                              ))}
                            </tbody>
                          </table>
                        </div>
                      ) : (
                        <div className="flex items-center gap-2 py-3 justify-center text-xs text-gray-400">
                          <AlertTriangle className="w-3.5 h-3.5" />
                          <span>개별 인증서 데이터를 조회할 수 없습니다 (업로드 완료 후 확인 가능)</span>
                        </div>
                      )}
                    </div>
                  </div>
                )}
              </div>
            );
          })}
        </div>

        {/* Footer */}
        <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex justify-end">
          <button
            onClick={onClose}
            className="px-4 py-1.5 text-sm bg-gray-100 dark:bg-gray-700 hover:bg-gray-200 dark:hover:bg-gray-600 rounded text-gray-700 dark:text-gray-300 transition-colors"
          >
            닫기
          </button>
        </div>
      </div>
    </div>
  );
}
