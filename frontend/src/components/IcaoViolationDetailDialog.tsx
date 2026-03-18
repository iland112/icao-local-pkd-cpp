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
import { getCountryName } from '@/utils/countryNames';

/** ICAO 9303 violation category key */
type ViolationCategory = 'algorithm' | 'keySize' | 'validityPeriod' | 'keyUsage' | 'extensions' | 'dnFormat' | 'nonConformant';

/** Map backend violation message to i18n key + interpolation params */
type ViolationMapping = [RegExp, string, ((m: RegExpMatchArray) => Record<string, string>)?];

const violationMappings: ViolationMapping[] = [
  // Extensions
  [/^(\w+) missing required Basic Constraints extension$/i, 'certificate:violation.missingBasicConstraints', (m) => ({ type: m[1] })],
  [/^Missing Key Usage extension$/i, 'certificate:violation.missingKeyUsageExtension'],
  // Key Usage
  [/^CSCA must have CA=TRUE$/i, 'certificate:violation.cscaMustCaTrue'],
  [/^DSC must have CA=FALSE$/i, 'certificate:violation.dscMustCaFalse'],
  [/^MLSC must have CA=TRUE$/i, 'certificate:violation.mlscMustCaTrue'],
  [/^MLSC must be self-signed$/i, 'certificate:violation.mlscMustSelfSigned'],
  [/^Missing required Key Usage: (.+)$/i, 'certificate:violation.missingRequiredKeyUsage', (m) => ({ value: m[1] })],
  // Algorithm — new formats (Doc 9303 / BSI TR-03110)
  [/^SHA-1 is deprecated.*:\s*(.+)$/i, 'certificate:violation.sha1Deprecated', (m) => ({ value: m[1] })],
  [/^SHA-224 supported via BSI TR-03110.*:\s*(.+)$/i, 'certificate:violation.sha224BsiSupported', (m) => ({ value: m[1] })],
  [/^Signature hash algorithm not in Doc 9303.*:\s*(.+)$/i, 'certificate:violation.hashAlgorithmNotApproved', (m) => ({ value: m[1] })],
  [/^Signature hash algorithm not ICAO-approved.*:\s*(.+)$/i, 'certificate:violation.hashAlgorithmNotApproved', (m) => ({ value: m[1] })],
  [/^Public key algorithm not in Doc 9303.*:\s*(.+)$/i, 'certificate:violation.pubKeyAlgorithmNotApproved', (m) => ({ value: m[1] })],
  [/^Public key algorithm not ICAO-approved.*:\s*(.+)$/i, 'certificate:violation.pubKeyAlgorithmNotApproved', (m) => ({ value: m[1] })],
  // Key Size / Curve — new formats
  [/^RSA key size below minimum.*:\s*(.+)$/i, 'certificate:violation.rsaKeySizeBelowMin', (m) => ({ value: m[1] })],
  [/^RSA key size exceeds.*:\s*(.+)$/i, 'certificate:violation.rsaKeySizeExceedsMax', (m) => ({ value: m[1] })],
  [/^Brainpool curve supported via BSI TR-03110.*:\s*(.+)$/i, 'certificate:violation.brainpoolBsiSupported', (m) => ({ value: m[1] })],
  [/^ECDSA curve not in Doc 9303.*:\s*(.+)$/i, 'certificate:violation.ecdsaCurveNotApproved', (m) => ({ value: m[1] })],
  [/^ECDSA curve not ICAO-approved.*:\s*(.+)$/i, 'certificate:violation.ecdsaCurveNotApproved', (m) => ({ value: m[1] })],
  [/^ECDSA key size below minimum.*:\s*(.+)$/i, 'certificate:violation.ecdsaKeySizeBelowMin', (m) => ({ value: m[1] })],
  // Validity Period
  [/^CSCA validity period exceeds.*:\s*(.+)$/i, 'certificate:violation.cscaValidityExceeds', (m) => ({ value: m[1] })],
  [/^DSC validity period exceeds.*:\s*(.+)$/i, 'certificate:violation.dscValidityExceeds', (m) => ({ value: m[1] })],
  // DN Format
  [/^Subject DN missing required Country \(C\) attribute$/i, 'certificate:violation.subjectDnMissingCountry'],
  [/^Certificate has no Subject DN$/i, 'certificate:violation.noSubjectDn'],
  // DSC_NC
  [/^ICAO PKD non-conformant DSC.*$/i, 'certificate:violation.dscNcNonConformant'],
];

/** Translate backend violation message via i18n */
function translateViolation(msg: string, t: (key: string, opts?: Record<string, string>) => string): string {
  for (const [pattern, key, paramsFn] of violationMappings) {
    const m = msg.match(pattern);
    if (m) {
      const params = paramsFn ? paramsFn(m) : undefined;
      return t(key, params);
    }
  }
  return msg; // fallback: return original if no mapping found
}

/** Extract relevant info from icaoViolations text per category */
function extractViolationDetail(
  violations: string | undefined,
  category: string,
  t: (key: string, opts?: Record<string, string>) => string,
): string {
  if (!violations) return '-';
  const parts = violations.split('|').map(s => s.trim()).filter(Boolean);
  if (parts.length === 0) return '-';

  let match: string | undefined;
  switch (category) {
    case 'algorithm':
      match = parts.find(p => /algorithm/i.test(p) || /hash/i.test(p));
      break;
    case 'keySize':
      match = parts.find(p => /key.?size|curve|bit/i.test(p));
      break;
    case 'keyUsage':
      match = parts.find(p => /key.?usage|CA=TRUE|CA=FALSE|self-signed/i.test(p));
      break;
    case 'extensions':
      match = parts.find(p => /basic.?constraint|extension|AKI|SKI|critical/i.test(p));
      break;
    case 'dnFormat':
      match = parts.find(p => /DN|country.?code|subject|issuer/i.test(p));
      break;
    case 'validityPeriod':
      match = parts.find(p => /validity|expired|not.?yet/i.test(p));
      break;
    default:
      break;
  }

  return translateViolation(match ?? parts[0], t);
}

/** Build category column config with i18n t() */
function buildCategoryColumnConfig(t: (key: string, opts?: Record<string, string>) => string) {
  return {
    algorithm: {
      header: t('certificate:doc9303.category.algorithm'),
      getValue: (cert: ValidationResult) => {
        const fromViolation = extractViolationDetail(cert.icaoViolations, 'algorithm', t);
        if (fromViolation === '-') return cert.signatureAlgorithm || fromViolation;
        return fromViolation;
      },
    },
    keySize: {
      header: t('certificate:doc9303.category.keySize'),
      getValue: (cert: ValidationResult) => extractViolationDetail(cert.icaoViolations, 'keySize', t),
    },
    keyUsage: {
      header: t('certificate:doc9303.category.keyUsage'),
      getValue: (cert: ValidationResult) => extractViolationDetail(cert.icaoViolations, 'keyUsage', t),
    },
    extensions: {
      header: t('certificate:doc9303.category.extensions'),
      getValue: (cert: ValidationResult) => extractViolationDetail(cert.icaoViolations, 'extensions', t),
    },
    dnFormat: {
      header: 'DN',
      getValue: (cert: ValidationResult) => extractViolationDetail(cert.icaoViolations, 'dnFormat', t),
    },
    validityPeriod: {
      header: t('certificate:doc9303.category.validity'),
      getValue: (cert: ValidationResult) => {
        // Show translated violation reason (e.g., "CSCA 유효기간 초과 (ICAO 권장 15년): 16")
        const violation = extractViolationDetail(cert.icaoViolations, 'validityPeriod', t);
        if (violation !== '-') return violation;
        if (cert.isExpired) return t('certificate:violation.expired');
        if (cert.isNotYetValid) return t('certificate:violation.notYetValid');
        return '-';
      },
    },
    nonConformant: {
      header: 'NC 사유',
      getValue: (cert: ValidationResult) => {
        // Extract NC Code and Description from icaoViolations (format: "...|NC Code: ERR:...|NC Description: ...")
        const violations = cert.icaoViolations || '';
        const codeMatch = violations.match(/NC Code:\s*([^|]+)/);
        const descMatch = violations.match(/NC Description:\s*([^|]+)/);
        if (codeMatch) return codeMatch[1].trim();
        if (descMatch) return descMatch[1].trim().substring(0, 60);
        return violations.substring(0, 60) || '-';
      },
    },
  } as Record<string, { header: string; getValue: (cert: ValidationResult) => string }>;
}

interface IcaoViolationDetailDialogProps {
  open: boolean;
  onClose: () => void;
  uploadId: string;
  violations: Record<string, number>;
  /** Total non-compliant count (used when violations breakdown is empty, e.g., DSC_NC) */
  totalNonCompliantCount?: number;
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
    description: 'ICAO가 승인하지 않은 암호화 알고리즘을 사용하는 인증서',
    reference: 'Doc 9303 Part 12, Section 7.1',
    detail: '여권 보안을 위해 ICAO는 안전한 알고리즘만 허용합니다. 승인 해시: SHA-256, SHA-384, SHA-512. 승인 공개키: RSA, ECDSA. SHA-1, MD5 등 오래된 알고리즘은 위변조 위험이 있어 미준수로 판정됩니다.',
  },
  keySize: {
    icon: '🔑',
    description: '암호화 키의 크기가 ICAO 권장 범위에 맞지 않는 인증서',
    reference: 'Doc 9303 Part 12, Section 7.1',
    detail: '키가 짧으면 해킹에 취약하고, 지나치게 길면 호환성 문제가 발생합니다. RSA: 최소 2048비트, 권장 최대 4096비트. ECDSA: P-256, P-384, P-521 곡선만 허용.',
  },
  validityPeriod: {
    icon: '📅',
    description: 'ICAO 권장 최대 유효기간을 초과하는 인증서',
    reference: 'Doc 9303 Part 12, Section 7.1.1',
    detail: '유효기간이 너무 길면 암호화 기술 발전으로 보안이 약해질 수 있습니다. ICAO 권장 최대: CSCA(국가 루트 인증서) 15년, DSC(여권 서명 인증서) 3년.',
  },
  keyUsage: {
    icon: '🏷️',
    description: '인증서의 용도(Key Usage)가 유형에 맞지 않는 인증서',
    reference: 'Doc 9303 Part 12, Section 7.1.1',
    detail: '각 인증서 유형에는 허용된 용도가 정해져 있습니다. CSCA(루트): 인증서 발급 + CRL 서명 용도 필수. DSC(여권 서명): 디지털 서명 용도 필수. MLSC(마스터리스트 서명): 인증서 발급 용도 + 자체 서명 필수.',
  },
  extensions: {
    icon: '📋',
    description: '필수 확장 필드가 누락된 인증서',
    reference: 'Doc 9303 Part 12, Section 7.1.1',
    detail: 'ICAO 표준은 인증서에 특정 확장 필드를 필수로 요구합니다. CSCA/MLSC: Basic Constraints 확장 필수 (CA 인증서 표시). 모든 유형: Key Usage 확장 필수 (용도 명시).',
  },
  dnFormat: {
    icon: '📝',
    description: '인증서 소유자 정보(DN)에 국가 코드가 누락된 인증서',
    reference: 'Doc 9303 Part 12, Section 7.1.1',
    detail: '여권 인증서에는 발급 국가를 식별하기 위해 Subject DN에 국가 코드(C=XX)가 반드시 포함되어야 합니다. 국가 코드가 없으면 어느 국가의 인증서인지 확인할 수 없습니다.',
  },
  nonConformant: {
    icon: '🚫',
    description: 'ICAO PKD에서 표준 미준수(Non-Conformant)로 분류된 DSC 인증서',
    reference: 'ICAO PKD Collection 003',
    detail: 'ICAO PKD가 자체 검증 과정에서 표준 미준수로 판정한 DSC 인증서입니다. 각 인증서에는 부적합 코드(NC Code)와 상세 사유가 포함되어 있습니다. 이 인증서들은 nc-data 영역에 별도 저장됩니다.',
  },
};


export function IcaoViolationDetailDialog({
  open,
  onClose,
  uploadId,
  violations,
  totalNonCompliantCount = 0,
  initialCategory,
}: IcaoViolationDetailDialogProps) {
  const { t } = useTranslation(['upload', 'common', 'certificate']);
  const categoryColumnConfig = buildCategoryColumnConfig(t);
  const [expandedCategory, setExpandedCategory] = useState<string | null>(initialCategory ?? null);
  const [loading, setLoading] = useState(false);
  const [certificates, setCertificates] = useState<ValidationResult[]>([]);
  const [loadedForCategory, setLoadedForCategory] = useState<string | null>(null);

  // If no category breakdown exists but there are non-compliant certificates (e.g., DSC_NC),
  // create a synthetic "nonConformant" category
  const effectiveViolations = Object.keys(violations).length > 0
    ? violations
    : { nonConformant: totalNonCompliantCount };

  // Sort categories by count desc
  const sortedCategories = Object.entries(effectiveViolations)
    .filter(([, count]) => count > 0)
    .sort(([, a], [, b]) => b - a);

  const totalNonCompliant = sortedCategories.reduce((sum, [, count]) => sum + count, 0);

  // Load non-compliant certificates when category is expanded (server-side filtering)
  const loadCertificates = useCallback(async (category: string) => {
    if (!uploadId || loadedForCategory === category) return;

    setLoading(true);
    try {
      // Server-side filtering by icaoCategory — returns only matching violations
      const response = await getUploadValidations(uploadId, {
        limit: 200,
        offset: 0,
        icaoCategory: category,
      });
      setCertificates(response.validations ?? []);
      setLoadedForCategory(category);
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
    nonConformant: 'ICAO PKD 부적합 (DSC_NC)',
  };

  if (!open) return null;

  return (
    <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/50" onClick={onClose}>
      <div
        className="bg-white dark:bg-gray-800 rounded-lg shadow-xl max-w-sm sm:max-w-2xl lg:max-w-4xl xl:max-w-5xl w-full mx-4 max-h-[90vh] flex flex-col"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="flex items-center justify-between px-5 py-3 border-b border-gray-200 dark:border-gray-700 bg-red-50 dark:bg-red-900/20 rounded-t-lg">
          <div className="flex items-center gap-2">
            <ShieldAlert className="w-5 h-5 text-red-600 dark:text-red-400" />
            <div>
              <h2 className="text-sm font-semibold text-gray-900 dark:text-gray-100">
                ICAO Doc 9303 미준수 상세
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
                          <p className="text-xs text-gray-400 dark:text-gray-500 mt-1.5 flex items-center gap-1">
                            <ExternalLink className="w-3 h-3" />
                            참조: {info.reference}
                          </p>
                        </div>
                      </div>
                    )}

                    {/* Certificate list */}
                    <div>
                      <h5 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wide mb-1.5">
                        해당 인증서 목록 (최대 200건)
                      </h5>
                      {loading ? (
                        <div className="flex items-center justify-center py-4">
                          <Loader2 className="w-4 h-4 animate-spin text-gray-400" />
                          <span className="ml-2 text-xs text-gray-500">조회 중...</span>
                        </div>
                      ) : certificates.length > 0 ? (
                        <div className="max-h-72 overflow-y-auto border border-gray-200 dark:border-gray-700 rounded">
                          <table className="w-full text-xs leading-tight table-fixed">
                            <thead className="bg-slate-100 dark:bg-gray-700 sticky top-0">
                              <tr>
                                <th className="px-1.5 py-1 text-center font-medium text-gray-500 dark:text-gray-400 w-[44px]">국가</th>
                                <th className="px-1.5 py-1 text-center font-medium text-gray-500 dark:text-gray-400 w-[52px]">유형</th>
                                <th className="px-1.5 py-1 text-left font-medium text-gray-500 dark:text-gray-400">Subject</th>
                                <th className="px-1.5 py-1 text-left font-medium text-gray-500 dark:text-gray-400">{categoryColumnConfig[cat]?.header ?? '상세'}</th>
                                <th className="px-1.5 py-1 text-center font-medium text-gray-500 dark:text-gray-400 w-[28px]"></th>
                              </tr>
                            </thead>
                            <tbody className="divide-y divide-gray-100 dark:divide-gray-700">
                              {certificates.map((cert, idx) => (
                                <tr key={cert.id || idx} className="hover:bg-white dark:hover:bg-gray-800/30">
                                  <td className="px-1.5 py-0.5 whitespace-nowrap text-center">
                                    <span className="inline-flex items-center gap-0.5">
                                      <img
                                        src={`https://flagcdn.com/16x12/${cert.countryCode?.toLowerCase()}.png`}
                                        alt={cert.countryCode}
                                        title={cert.countryCode ? getCountryName(cert.countryCode) : ''}
                                        className="w-3.5 h-2.5"
                                        onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }}
                                      />
                                      <span className="text-xs">{cert.countryCode}</span>
                                    </span>
                                  </td>
                                  <td className="px-1.5 py-0.5 whitespace-nowrap text-center">
                                    <span className="bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300 px-1 py-px rounded text-xs">
                                      {cert.certificateType}
                                    </span>
                                  </td>
                                  <td className="px-1.5 py-0.5 truncate text-gray-700 dark:text-gray-300" title={cert.subjectDn}>
                                    {cert.subjectDn?.replace(/^.*?CN=/, 'CN=')?.substring(0, 50)}
                                  </td>
                                  <td className="px-1.5 py-0.5 truncate text-gray-600 dark:text-gray-400" title={categoryColumnConfig[cat]?.getValue(cert)}>
                                    {categoryColumnConfig[cat]?.getValue(cert) ?? '-'}
                                  </td>
                                  <td className="px-1.5 py-0.5 text-center">
                                    <XCircle className="w-2.5 h-2.5 text-red-500 dark:text-red-400 inline-block" />
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
