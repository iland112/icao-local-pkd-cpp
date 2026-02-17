import {
  Search,
  Loader2,
  XCircle,
  X,
  CheckCircle,
  AlertTriangle,
  Award,
  Globe,
  FileKey,
  ListChecks,
  Zap,
} from 'lucide-react';
import { cn } from '@/utils/cn';

// Quick lookup result type (matches findByFingerprint/findBySubjectDn response)
export interface QuickLookupResult {
  success: boolean;
  validation?: {
    certificateType?: string;
    countryCode?: string;
    subjectDn?: string;
    issuerDn?: string;
    serialNumber?: string;
    validationStatus?: string;
    trustChainValid?: boolean;
    trustChainMessage?: string;
    trustChainPath?: string;
    cscaFound?: boolean;
    cscaSubjectDn?: string;
    signatureVerified?: boolean;
    signatureAlgorithm?: string;
    validityCheckPassed?: boolean;
    notBefore?: string;
    notAfter?: string;
    crlCheckStatus?: string;
    crlChecked?: boolean;
    fingerprint?: string;
    validatedAt?: string;
    pkdConformanceCode?: string;
    pkdConformanceText?: string;
    pkdVersion?: string;
  } | null;
  error?: string;
}

interface QuickLookupPanelProps {
  quickLookupDn: string;
  setQuickLookupDn: (value: string) => void;
  quickLookupFingerprint: string;
  setQuickLookupFingerprint: (value: string) => void;
  quickLookupResult: QuickLookupResult | null;
  quickLookupLoading: boolean;
  quickLookupError: string | null;
  setQuickLookupError: (value: string | null) => void;
  handleQuickLookup: () => void;
}

export function QuickLookupPanel({
  quickLookupDn,
  setQuickLookupDn,
  quickLookupFingerprint,
  setQuickLookupFingerprint,
  quickLookupResult,
  quickLookupLoading,
  quickLookupError,
  setQuickLookupError,
  handleQuickLookup,
}: QuickLookupPanelProps) {
  return (
    <div className="max-w-3xl space-y-5">
      {/* Input Card */}
      <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-5">
        <div className="flex items-center gap-3 mb-4">
          <div className="p-2.5 rounded-xl bg-teal-50 dark:bg-teal-900/30">
            <Search className="w-5 h-5 text-teal-500" />
          </div>
          <div>
            <h2 className="text-lg font-bold text-gray-900 dark:text-white">Trust Chain 조회</h2>
            <p className="text-xs text-gray-500 dark:text-gray-400">
              파일 업로드 시 수행된 Trust Chain 검증 결과를 DSC Subject DN 또는 Fingerprint로 조회합니다.
            </p>
          </div>
        </div>

        <div className="space-y-3">
          <div>
            <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
              DSC Subject DN
            </label>
            <input
              type="text"
              value={quickLookupDn}
              onChange={(e) => { setQuickLookupDn(e.target.value); setQuickLookupFingerprint(''); }}
              placeholder="/C=KR/O=Government of Korea/CN=Document Signer..."
              className="w-full px-3 py-2 rounded-lg border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-700 text-sm text-gray-900 dark:text-gray-100 focus:ring-2 focus:ring-teal-500 focus:border-transparent"
            />
          </div>
          <div className="text-center text-xs text-gray-400">또는</div>
          <div>
            <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
              SHA-256 Fingerprint
            </label>
            <input
              type="text"
              value={quickLookupFingerprint}
              onChange={(e) => { setQuickLookupFingerprint(e.target.value); setQuickLookupDn(''); }}
              placeholder="a1b2c3d4e5f6..."
              className="w-full px-3 py-2 rounded-lg border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-700 text-sm font-mono text-gray-900 dark:text-gray-100 focus:ring-2 focus:ring-teal-500 focus:border-transparent"
            />
          </div>
          <button
            onClick={handleQuickLookup}
            disabled={quickLookupLoading || (!quickLookupDn && !quickLookupFingerprint)}
            className={cn(
              'w-full flex items-center justify-center gap-2 px-4 py-2.5 rounded-lg text-sm font-medium transition-all',
              quickLookupLoading || (!quickLookupDn && !quickLookupFingerprint)
                ? 'bg-gray-300 dark:bg-gray-600 text-gray-500 cursor-not-allowed'
                : 'bg-teal-500 hover:bg-teal-600 text-white shadow-md'
            )}
          >
            {quickLookupLoading ? (
              <><Loader2 className="w-4 h-4 animate-spin" /> 조회 중...</>
            ) : (
              <><Search className="w-4 h-4" /> Trust Chain 결과 조회</>
            )}
          </button>
        </div>
      </div>

      {/* Error Message */}
      {quickLookupError && (
        <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg flex items-center gap-2 text-red-700 dark:text-red-400">
          <XCircle className="w-5 h-5 shrink-0" />
          <span className="text-sm">{quickLookupError}</span>
          <button onClick={() => setQuickLookupError(null)} className="ml-auto">
            <X className="w-4 h-4" />
          </button>
        </div>
      )}

      {/* Result Card */}
      {quickLookupResult?.validation && (
        <div className="space-y-4">
          {/* Overall Status */}
          <div className={cn(
            'rounded-2xl p-4',
            quickLookupResult.validation.validationStatus === 'VALID' || quickLookupResult.validation.validationStatus === 'EXPIRED_VALID'
              ? 'bg-gradient-to-r from-green-500 to-emerald-500 text-white'
              : quickLookupResult.validation.validationStatus === 'INVALID'
              ? 'bg-gradient-to-r from-red-500 to-rose-500 text-white'
              : 'bg-gradient-to-r from-yellow-500 to-orange-500 text-white'
          )}>
            <div className="flex items-center gap-3">
              {quickLookupResult.validation.validationStatus === 'VALID' || quickLookupResult.validation.validationStatus === 'EXPIRED_VALID' ? (
                <Award className="w-10 h-10" />
              ) : quickLookupResult.validation.validationStatus === 'INVALID' ? (
                <XCircle className="w-10 h-10" />
              ) : (
                <AlertTriangle className="w-10 h-10" />
              )}
              <div>
                <h2 className="text-lg font-bold">
                  Trust Chain: {quickLookupResult.validation.validationStatus}
                </h2>
                <div className="flex items-center gap-4 mt-0.5 text-sm opacity-90">
                  {quickLookupResult.validation.countryCode && (
                    <span className="flex items-center gap-1">
                      <Globe className="w-3.5 h-3.5" />
                      {quickLookupResult.validation.countryCode}
                    </span>
                  )}
                  {quickLookupResult.validation.certificateType && (
                    <span className="flex items-center gap-1">
                      <FileKey className="w-3.5 h-3.5" />
                      {quickLookupResult.validation.certificateType}
                    </span>
                  )}
                </div>
              </div>
            </div>
          </div>

          {/* Detail Card */}
          <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-5 space-y-3">
            <h3 className="text-sm font-bold text-gray-900 dark:text-white flex items-center gap-2">
              <ListChecks className="w-4 h-4 text-teal-500" />
              검증 상세 결과
            </h3>

            {/* Trust Chain */}
            <div className={cn(
              'p-3 rounded-lg border text-xs',
              quickLookupResult.validation.trustChainValid
                ? 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
                : 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800'
            )}>
              <div className="flex items-center gap-2 mb-1.5">
                {quickLookupResult.validation.trustChainValid
                  ? <CheckCircle className="w-4 h-4 text-green-500" />
                  : <XCircle className="w-4 h-4 text-red-500" />}
                <span className="font-semibold">Trust Chain 검증</span>
              </div>
              {quickLookupResult.validation.trustChainPath && (
                <div className="ml-6 text-gray-600 dark:text-gray-400">
                  경로: {quickLookupResult.validation.trustChainPath}
                </div>
              )}
              {quickLookupResult.validation.trustChainMessage && (
                <div className="ml-6 text-gray-600 dark:text-gray-400">
                  {quickLookupResult.validation.trustChainMessage}
                </div>
              )}
            </div>

            {/* CSCA Info */}
            <div className={cn(
              'p-3 rounded-lg border text-xs',
              quickLookupResult.validation.cscaFound
                ? 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
                : 'bg-yellow-50 dark:bg-yellow-900/20 border-yellow-200 dark:border-yellow-800'
            )}>
              <div className="flex items-center gap-2 mb-1.5">
                {quickLookupResult.validation.cscaFound
                  ? <CheckCircle className="w-4 h-4 text-green-500" />
                  : <AlertTriangle className="w-4 h-4 text-yellow-500" />}
                <span className="font-semibold">CSCA 조회</span>
              </div>
              {quickLookupResult.validation.cscaSubjectDn && (
                <div className="ml-6">
                  <span className="text-gray-500">CSCA DN: </span>
                  <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                    {quickLookupResult.validation.cscaSubjectDn}
                  </code>
                </div>
              )}
            </div>

            {/* Signature Verification */}
            <div className={cn(
              'p-3 rounded-lg border text-xs',
              quickLookupResult.validation.signatureVerified
                ? 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
                : 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800'
            )}>
              <div className="flex items-center gap-2">
                {quickLookupResult.validation.signatureVerified
                  ? <CheckCircle className="w-4 h-4 text-green-500" />
                  : <XCircle className="w-4 h-4 text-red-500" />}
                <span className="font-semibold">서명 검증</span>
                {quickLookupResult.validation.signatureAlgorithm && (
                  <code className="ml-2 font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded">
                    {quickLookupResult.validation.signatureAlgorithm}
                  </code>
                )}
              </div>
            </div>

            {/* Validity Period */}
            <div className={cn(
              'p-3 rounded-lg border text-xs',
              quickLookupResult.validation.validityCheckPassed
                ? 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
                : 'bg-yellow-50 dark:bg-yellow-900/20 border-yellow-200 dark:border-yellow-800'
            )}>
              <div className="flex items-center gap-2 mb-1.5">
                {quickLookupResult.validation.validityCheckPassed
                  ? <CheckCircle className="w-4 h-4 text-green-500" />
                  : <AlertTriangle className="w-4 h-4 text-yellow-500" />}
                <span className="font-semibold">유효기간</span>
              </div>
              {(quickLookupResult.validation.notBefore || quickLookupResult.validation.notAfter) && (
                <div className="ml-6 text-gray-600 dark:text-gray-400">
                  {quickLookupResult.validation.notBefore} ~ {quickLookupResult.validation.notAfter}
                </div>
              )}
            </div>

            {/* CRL Check */}
            <div className={cn(
              'p-3 rounded-lg border text-xs',
              quickLookupResult.validation.crlCheckStatus === 'REVOKED'
                ? 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800'
                : 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
            )}>
              <div className="flex items-center gap-2">
                {quickLookupResult.validation.crlCheckStatus === 'REVOKED'
                  ? <XCircle className="w-4 h-4 text-red-500" />
                  : <CheckCircle className="w-4 h-4 text-green-500" />}
                <span className="font-semibold">CRL 검사</span>
                <span className="text-gray-500 ml-1">({quickLookupResult.validation.crlCheckStatus || 'NOT_CHECKED'})</span>
              </div>
            </div>

            {/* Non-Conformant Reason (DSC_NC only) */}
            {quickLookupResult.validation.certificateType === 'DSC_NC' && (
              <div className="p-3 rounded-lg bg-orange-50 dark:bg-orange-900/20 border border-orange-200 dark:border-orange-800 text-xs">
                <div className="flex items-center gap-2 mb-1.5">
                  <AlertTriangle className="w-4 h-4 text-orange-500" />
                  <span className="font-semibold text-orange-700 dark:text-orange-300">Non-Conformant (비준수)</span>
                </div>
                {quickLookupResult.validation.pkdConformanceCode && (
                  <div className="ml-6 mb-1">
                    <span className="text-gray-500">코드: </span>
                    <code className="font-mono bg-orange-100 dark:bg-orange-800/40 px-1.5 py-0.5 rounded text-orange-700 dark:text-orange-300">
                      {quickLookupResult.validation.pkdConformanceCode}
                    </code>
                  </div>
                )}
                {quickLookupResult.validation.pkdConformanceText && (
                  <div className="ml-6 mb-1 text-gray-600 dark:text-gray-400">
                    <span className="text-gray-500">사유: </span>
                    {quickLookupResult.validation.pkdConformanceText}
                  </div>
                )}
                {quickLookupResult.validation.pkdVersion && (
                  <div className="ml-6 text-gray-500">
                    PKD Version: {quickLookupResult.validation.pkdVersion}
                  </div>
                )}
                {!quickLookupResult.validation.pkdConformanceCode && !quickLookupResult.validation.pkdConformanceText && (
                  <div className="ml-6 text-gray-500">
                    ICAO PKD에서 Non-Conformant로 분류된 인증서입니다.
                  </div>
                )}
              </div>
            )}

            {/* Certificate Info */}
            <div className="p-3 rounded-lg bg-gray-50 dark:bg-gray-700/50 border border-gray-200 dark:border-gray-700 text-xs space-y-1.5">
              <div className="font-semibold text-gray-700 dark:text-gray-300 mb-1">인증서 정보</div>
              {quickLookupResult.validation.subjectDn && (
                <div className="flex items-start gap-2">
                  <span className="text-gray-500 shrink-0 w-16">Subject:</span>
                  <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                    {quickLookupResult.validation.subjectDn}
                  </code>
                </div>
              )}
              {quickLookupResult.validation.issuerDn && (
                <div className="flex items-start gap-2">
                  <span className="text-gray-500 shrink-0 w-16">Issuer:</span>
                  <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                    {quickLookupResult.validation.issuerDn}
                  </code>
                </div>
              )}
              {quickLookupResult.validation.fingerprint && (
                <div className="flex items-start gap-2">
                  <span className="text-gray-500 shrink-0 w-16">Fingerprint:</span>
                  <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                    {quickLookupResult.validation.fingerprint}
                  </code>
                </div>
              )}
              {quickLookupResult.validation.validatedAt && (
                <div className="flex items-start gap-2">
                  <span className="text-gray-500 shrink-0 w-16">검증일시:</span>
                  <span className="text-gray-600 dark:text-gray-400">{quickLookupResult.validation.validatedAt}</span>
                </div>
              )}
            </div>

            {/* SOD/DG Note */}
            <div className="p-3 rounded-lg bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 text-xs text-blue-700 dark:text-blue-300">
              <div className="flex items-start gap-2">
                <Zap className="w-4 h-4 shrink-0 mt-0.5" />
                <div>
                  <span className="font-semibold">간편 검증 모드: </span>
                  SOD 서명 검증, Data Group 해시 검증은 전체 검증 모드에서만 수행됩니다.
                  위 결과는 파일 업로드 시 수행된 Trust Chain 검증 결과입니다.
                </div>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default QuickLookupPanel;
