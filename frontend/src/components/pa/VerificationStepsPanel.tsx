import {
  CheckCircle,
  XCircle,
  AlertTriangle,
  Loader2,
  ListChecks,
  ChevronDown,
  ChevronRight,
  Award,
  FileKey,
  FileText,
  Hash,
} from 'lucide-react';
import { cn } from '@/utils/cn';

// Verification Step 상태 타입
export type StepStatus = 'pending' | 'running' | 'success' | 'warning' | 'error';

// eslint-disable-next-line @typescript-eslint/no-explicit-any
export type StepDetails = Record<string, any>;

export interface VerificationStep {
  id: number;
  title: string;
  description: string;
  status: StepStatus;
  message?: string;
  details?: StepDetails;
  expanded?: boolean;
}

// DG 해시 상세 타입
interface DGHashDetail {
  valid: boolean;
  expectedHash: string;
  actualHash: string;
}

export function getStepStatusIcon(status: StepStatus) {
  switch (status) {
    case 'success':
      return <CheckCircle className="w-5 h-5 text-green-500" />;
    case 'error':
      return <XCircle className="w-5 h-5 text-red-500" />;
    case 'warning':
      return <AlertTriangle className="w-5 h-5 text-yellow-500" />;
    case 'running':
      return <Loader2 className="w-5 h-5 text-blue-500 animate-spin" />;
    default:
      return <div className="w-5 h-5 rounded-full border-2 border-gray-300 dark:border-gray-600" />;
  }
}

export function getStepBgColor(status: StepStatus) {
  switch (status) {
    case 'success':
      return 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800';
    case 'error':
      return 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800';
    case 'warning':
      return 'bg-yellow-50 dark:bg-yellow-900/20 border-yellow-200 dark:border-yellow-800';
    case 'running':
      return 'bg-blue-50 dark:bg-blue-900/20 border-blue-200 dark:border-blue-800';
    default:
      return 'bg-gray-50 dark:bg-gray-700/50 border-gray-200 dark:border-gray-700';
  }
}

interface VerificationStepsPanelProps {
  steps: VerificationStep[];
  expandedSteps: Set<number>;
  toggleStep: (stepId: number) => void;
}

export function VerificationStepsPanel({
  steps,
  expandedSteps,
  toggleStep,
}: VerificationStepsPanelProps) {
  return (
    <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg overflow-hidden">
      <div className="px-5 py-4 bg-gradient-to-r from-indigo-500 to-purple-500">
        <div className="flex items-center gap-3">
          <ListChecks className="w-6 h-6 text-white" />
          <h2 className="text-lg font-bold text-white">검증 단계 (ICAO 9303)</h2>
        </div>
      </div>
      <div className="p-4 space-y-2">
        {steps.map((step: VerificationStep) => (
          <div
            key={step.id}
            className={cn(
              'rounded-xl border transition-all duration-200',
              getStepBgColor(step.status)
            )}
          >
            {/* Step Header */}
            <div
              className="flex items-center gap-3 p-3 cursor-pointer"
              onClick={() => toggleStep(step.id)}
            >
              {getStepStatusIcon(step.status)}
              <div className="flex-grow">
                <h3 className="font-semibold text-gray-900 dark:text-white text-sm">{step.title}</h3>
                <p className="text-xs text-gray-500 dark:text-gray-400">{step.description}</p>
              </div>
              {(step.message || step.id === 8) && (
                expandedSteps.has(step.id) ? (
                  <ChevronDown className="w-4 h-4 text-gray-400" />
                ) : (
                  <ChevronRight className="w-4 h-4 text-gray-400" />
                )
              )}
            </div>

            {/* Step Details (Expanded) */}
            {expandedSteps.has(step.id) && (step.message || step.id === 8) && (
              <div className="px-4 pb-3 pt-0">
                {step.message ? (
                  <div className={cn(
                    'text-sm mb-2',
                    step.status === 'success' ? 'text-green-700 dark:text-green-400' :
                    step.status === 'error' ? 'text-red-700 dark:text-red-400' :
                    step.status === 'warning' ? 'text-yellow-700 dark:text-yellow-400' :
                    'text-gray-600 dark:text-gray-400'
                  )}>
                    {step.message}
                  </div>
                ) : null}



                {/* Step 1: SOD 파싱 상세 정보 */}
                {step.id === 1 && step.details && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-2">
                    <div className="grid grid-cols-2 gap-2">
                      <div>
                        <span className="text-gray-500">해시 알고리즘:</span>
                        <code className="ml-1 font-mono bg-blue-100 dark:bg-blue-900/30 px-1.5 py-0.5 rounded text-blue-700 dark:text-blue-300">
                          {String(step.details.hashAlgorithm || '')}
                        </code>
                      </div>
                      <div>
                        <span className="text-gray-500">서명 알고리즘:</span>
                        <code className="ml-1 font-mono bg-purple-100 dark:bg-purple-900/30 px-1.5 py-0.5 rounded text-purple-700 dark:text-purple-300">
                          {String(step.details.signatureAlgorithm || '')}
                        </code>
                      </div>
                    </div>
                  </div>
                )}

                {/* Step 2: DSC 추출 상세 정보 */}
                {step.id === 2 && step.details && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-1.5">
                    <div className="flex items-start gap-2">
                      <span className="text-gray-500 shrink-0 w-16">주체(Subject):</span>
                      <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                        {step.details.subject || ''}
                      </code>
                    </div>
                    <div className="flex items-start gap-2">
                      <span className="text-gray-500 shrink-0 w-16">일련번호:</span>
                      <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded">
                        {step.details.serial || ''}
                      </code>
                    </div>
                    <div className="flex items-start gap-2">
                      <span className="text-gray-500 shrink-0 w-16">발급자:</span>
                      <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                        {step.details.issuer || ''}
                      </code>
                    </div>
                  </div>
                )}

                {/* Step 3: Trust Chain 검증 상세 정보 (성공) */}
                {step.id === 3 && step.details && step.status !== 'error' && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs">
                    <div className="font-semibold text-gray-700 dark:text-gray-300 mb-2">Trust Chain 경로:</div>
                    <div className="flex flex-col items-center gap-1">
                      {/* CSCA (Root) */}
                      <div className="w-full p-2 bg-green-100 dark:bg-green-900/30 rounded border border-green-300 dark:border-green-700">
                        <div className="flex items-center gap-2">
                          <Award className="w-4 h-4 text-green-600 dark:text-green-400" />
                          <span className="font-semibold text-green-700 dark:text-green-300">CSCA (Root)</span>
                        </div>
                        <code className="block mt-1 text-xs font-mono break-all text-gray-600 dark:text-gray-400">
                          {step.details.cscaSubject}
                        </code>
                      </div>
                      {/* Arrow */}
                      <div className="text-gray-400 dark:text-gray-500">&#8595; 서명</div>
                      {/* DSC (Leaf) */}
                      <div className="w-full p-2 bg-blue-100 dark:bg-blue-900/30 rounded border border-blue-300 dark:border-blue-700">
                        <div className="flex items-center gap-2">
                          <FileKey className="w-4 h-4 text-blue-600 dark:text-blue-400" />
                          <span className="font-semibold text-blue-700 dark:text-blue-300">DSC (Document Signer)</span>
                          {step.details.dscExpired && (
                            <span className="px-1.5 py-0.5 rounded text-[10px] font-medium bg-orange-200 dark:bg-orange-900/50 text-orange-700 dark:text-orange-300">
                              만료됨
                            </span>
                          )}
                        </div>
                        <code className="block mt-1 text-xs font-mono break-all text-gray-600 dark:text-gray-400">
                          {step.details.dscSubject}
                        </code>
                        {step.details.notBefore && step.details.notAfter && (
                          <div className="mt-1 text-xs text-gray-500">
                            유효기간: {step.details.notBefore} ~ {step.details.notAfter}
                          </div>
                        )}
                      </div>

                      {/* Certificate Expiration Warning/Info */}
                      {step.details.expirationStatus && step.details.expirationStatus !== 'VALID' && (
                        <div className={cn(
                          'w-full mt-2 p-2 rounded text-xs',
                          step.details.expirationStatus === 'EXPIRED'
                            ? 'bg-orange-100 dark:bg-orange-900/30 border border-orange-300 dark:border-orange-700'
                            : 'bg-yellow-100 dark:bg-yellow-900/30 border border-yellow-300 dark:border-yellow-700'
                        )}>
                          <div className="flex items-center gap-2">
                            <AlertTriangle className={cn(
                              'w-4 h-4',
                              step.details.expirationStatus === 'EXPIRED'
                                ? 'text-orange-600 dark:text-orange-400'
                                : 'text-yellow-600 dark:text-yellow-400'
                            )} />
                            <span className={cn(
                              'font-semibold',
                              step.details.expirationStatus === 'EXPIRED'
                                ? 'text-orange-700 dark:text-orange-300'
                                : 'text-yellow-700 dark:text-yellow-300'
                            )}>
                              {step.details.expirationStatus === 'EXPIRED' ? '인증서 만료' : '인증서 만료 임박'}
                            </span>
                            {step.details.validAtSigningTime && (
                              <span className="px-1.5 py-0.5 rounded text-[10px] font-medium bg-green-200 dark:bg-green-900/50 text-green-700 dark:text-green-300">
                                &#10003; 서명 당시 유효
                              </span>
                            )}
                          </div>
                          {step.details.expirationMessage && (
                            <p className="mt-1 text-gray-600 dark:text-gray-400">
                              {step.details.expirationMessage}
                            </p>
                          )}
                        </div>
                      )}
                      {/* DSC Non-Conformant Warning (ICAO PKD nc-data) */}
                      {step.details.dscNonConformant && (
                        <div className="w-full mt-2 p-2 rounded text-xs bg-amber-100 dark:bg-amber-900/30 border border-amber-300 dark:border-amber-700">
                          <div className="flex items-center gap-2">
                            <AlertTriangle className="w-4 h-4 text-amber-600 dark:text-amber-400" />
                            <span className="font-semibold text-amber-700 dark:text-amber-300">Non-Conformant DSC (ICAO PKD)</span>
                          </div>
                          {step.details.pkdConformanceCode && (
                            <p className="mt-1 text-gray-600 dark:text-gray-400">
                              <span className="font-mono font-medium">{step.details.pkdConformanceCode}</span>
                              {step.details.pkdConformanceText && (
                                <span> &mdash; {step.details.pkdConformanceText}</span>
                              )}
                            </p>
                          )}
                        </div>
                      )}
                    </div>
                  </div>
                )}
                {/* Step 3: Trust Chain 검증 상세 정보 (실패) */}
                {step.id === 3 && step.status === 'error' && step.details && (
                  <div className="mt-2 space-y-2">
                    {/* 검증 대상 인증서 정보 */}
                    {(step.details.dscSubject || step.details.cscaSubject) && (
                      <div className="p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-1.5">
                        <div className="font-semibold text-gray-700 dark:text-gray-300 mb-1">검증 대상 인증서:</div>
                        {step.details.dscSubject && (
                          <div className="flex items-start gap-2">
                            <span className="text-gray-500 shrink-0 w-12">DSC:</span>
                            <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                              {step.details.dscSubject}
                            </code>
                          </div>
                        )}
                        {step.details.cscaSubject && (
                          <div className="flex items-start gap-2">
                            <span className="text-gray-500 shrink-0 w-12">CSCA:</span>
                            <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                              {step.details.cscaSubject}
                            </code>
                          </div>
                        )}
                        {step.details.notBefore && step.details.notAfter && (
                          <div className="flex items-start gap-2">
                            <span className="text-gray-500 shrink-0 w-12">유효기간:</span>
                            <span className="text-gray-600 dark:text-gray-400">
                              {step.details.notBefore} ~ {step.details.notAfter}
                            </span>
                          </div>
                        )}
                      </div>
                    )}
                    {/* 실패 원인 */}
                    <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg text-xs">
                      <div className="flex items-start gap-2 text-red-600 dark:text-red-400">
                        <XCircle className="w-4 h-4 shrink-0 mt-0.5" />
                        <div>
                          <span className="font-semibold">실패 원인: </span>
                          {step.details.error
                            ? String(step.details.error)
                            : 'DSC 인증서가 CSCA에 의해 서명되지 않았거나 서명 검증에 실패했습니다.'}
                        </div>
                      </div>
                    </div>
                  </div>
                )}

                {/* Step 4: CSCA 조회 상세 정보 */}
                {step.id === 4 && step.details && step.status === 'success' && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs">
                    <div className="flex items-start gap-2">
                      <span className="text-gray-500 shrink-0">CSCA DN:</span>
                      <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                        {step.details.dn || ''}
                      </code>
                    </div>
                  </div>
                )}
                {step.id === 4 && step.status === 'error' && step.details && (
                  <div className="mt-2 p-3 bg-red-50 dark:bg-red-900/20 rounded-lg text-xs space-y-1.5">
                    {step.details.dscSubject && (
                      <div className="flex items-start gap-2 text-gray-600 dark:text-gray-400">
                        <span className="text-gray-500 shrink-0">DSC 발급자:</span>
                        <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                          {step.details.dscSubject}
                        </code>
                      </div>
                    )}
                    {step.details.error && (
                      <div className="text-red-600 dark:text-red-400">
                        <span className="font-semibold">원인:</span> {String(step.details.error)}
                      </div>
                    )}
                  </div>
                )}

                {/* Step 5: SOD 서명 검증 상세 정보 (성공) */}
                {step.id === 5 && step.status === 'success' && step.details && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs">
                    <div className="grid grid-cols-2 gap-2">
                      {step.details.signatureAlgorithm && (
                        <div>
                          <span className="text-gray-500">서명 알고리즘:</span>
                          <code className="ml-1 font-mono bg-purple-100 dark:bg-purple-900/30 px-1.5 py-0.5 rounded text-purple-700 dark:text-purple-300">
                            {String(step.details.signatureAlgorithm)}
                          </code>
                        </div>
                      )}
                      {step.details.hashAlgorithm && (
                        <div>
                          <span className="text-gray-500">해시 알고리즘:</span>
                          <code className="ml-1 font-mono bg-blue-100 dark:bg-blue-900/30 px-1.5 py-0.5 rounded text-blue-700 dark:text-blue-300">
                            {String(step.details.hashAlgorithm)}
                          </code>
                        </div>
                      )}
                    </div>
                  </div>
                )}
                {/* Step 5: SOD 서명 검증 상세 정보 (실패) */}
                {step.id === 5 && step.status === 'error' && (
                  <div className="mt-2 space-y-2">
                    {step.details && (step.details.signatureAlgorithm || step.details.hashAlgorithm) && (
                      <div className="p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs">
                        <div className="grid grid-cols-2 gap-2">
                          {step.details.signatureAlgorithm && (
                            <div>
                              <span className="text-gray-500">서명 알고리즘:</span>
                              <code className="ml-1 font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded">
                                {String(step.details.signatureAlgorithm)}
                              </code>
                            </div>
                          )}
                          {step.details.hashAlgorithm && (
                            <div>
                              <span className="text-gray-500">해시 알고리즘:</span>
                              <code className="ml-1 font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded">
                                {String(step.details.hashAlgorithm)}
                              </code>
                            </div>
                          )}
                        </div>
                      </div>
                    )}
                    <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg text-xs">
                      <div className="flex items-start gap-2 text-red-600 dark:text-red-400">
                        <XCircle className="w-4 h-4 shrink-0 mt-0.5" />
                        <div>
                          <span className="font-semibold">실패 원인: </span>
                          {step.details?.error
                            ? String(step.details.error)
                            : 'SOD의 서명이 DSC 공개키로 검증되지 않았습니다.'}
                        </div>
                      </div>
                    </div>
                  </div>
                )}

                {/* Step 6: DG 해시 검증 상세 정보 */}
                {step.id === 6 && step.details?.dgDetails ? (() => {
                  const dgDetails = step.details.dgDetails as Record<string, DGHashDetail>;
                  return (
                    <div className="space-y-2 mt-2">
                      {Object.entries(dgDetails).map(([dgName, detail]) => (
                        <div key={dgName} className={cn(
                          'p-3 rounded-lg text-xs',
                          detail.valid
                            ? 'bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800'
                            : 'bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
                        )}>
                          <div className="flex items-center justify-between mb-2">
                            <div className="flex items-center gap-2">
                              <span className="font-semibold text-sm">{dgName}</span>
                              {dgName === 'DG1' ? <span className="text-gray-500">기계판독영역 (MRZ)</span> : null}
                              {dgName === 'DG2' ? <span className="text-gray-500">얼굴 생체이미지</span> : null}
                              {dgName === 'DG3' ? <span className="text-gray-500">지문 생체정보</span> : null}
                              {dgName === 'DG14' ? <span className="text-gray-500">보안 옵션</span> : null}
                            </div>
                            <div className={cn(
                              'px-2 py-0.5 rounded-full text-xs font-medium',
                              detail.valid ? 'bg-green-200 text-green-700 dark:bg-green-800 dark:text-green-200' : 'bg-red-200 text-red-700 dark:bg-red-800 dark:text-red-200'
                            )}>
                              {detail.valid ? (
                                <span className="flex items-center gap-1"><CheckCircle className="w-3 h-3" /> 일치</span>
                              ) : (
                                <span className="flex items-center gap-1"><XCircle className="w-3 h-3" /> 불일치</span>
                              )}
                            </div>
                          </div>
                          <div className="space-y-2 mt-2">
                            <div>
                              <div className="flex items-center gap-1 text-gray-500 mb-1">
                                <FileText className="w-3 h-3" />
                                <span>예상 해시 (SOD):</span>
                              </div>
                              <code className="block font-mono bg-gray-200 dark:bg-gray-600 px-2 py-1 rounded break-all text-xs">{detail.expectedHash}</code>
                            </div>
                            <div>
                              <div className="flex items-center gap-1 text-gray-500 mb-1">
                                <Hash className="w-3 h-3" />
                                <span>계산된 해시:</span>
                              </div>
                              <code className="block font-mono bg-gray-200 dark:bg-gray-600 px-2 py-1 rounded break-all text-xs">{detail.actualHash}</code>
                            </div>
                          </div>
                          {detail.valid ? (
                            <div className="mt-2 flex items-center gap-1 text-green-600 dark:text-green-400 font-medium">
                              <CheckCircle className="w-3 h-3" />
                              <span>해시 일치 - 데이터 무결성 검증 완료</span>
                            </div>
                          ) : null}
                        </div>
                      ))}
                    </div>
                  );
                })() : null}

                {/* Step 7: CRL 검사 상세 정보 */}
                {step.id === 7 && step.details && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-1">
                    {step.details.description && (
                      <div className="text-gray-600 dark:text-gray-400">
                        {step.details.description}
                      </div>
                    )}
                    {step.details.detailedDescription && (
                      <div className="text-gray-500 dark:text-gray-500 text-xs">
                        {step.details.detailedDescription}
                      </div>
                    )}
                  </div>
                )}

                {/* Step 8: DSC 자동 등록 */}
                {step.id === 8 && step.details && (
                  <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-1.5">
                    {step.details.fingerprint && (
                      <div className="flex items-center gap-2">
                        <span className="text-gray-500 dark:text-gray-400 shrink-0">Fingerprint:</span>
                        <code className="font-mono text-gray-700 dark:text-gray-300 break-all">{step.details.fingerprint}</code>
                      </div>
                    )}
                    {step.details.certificateId && (
                      <div className="flex items-center gap-2">
                        <span className="text-gray-500 dark:text-gray-400 shrink-0">Certificate ID:</span>
                        <code className="font-mono text-gray-700 dark:text-gray-300">{step.details.certificateId}</code>
                      </div>
                    )}
                    {step.details.countryCode && (
                      <div className="flex items-center gap-2">
                        <span className="text-gray-500 dark:text-gray-400 shrink-0">Country:</span>
                        <span className="text-gray-700 dark:text-gray-300">{step.details.countryCode}</span>
                      </div>
                    )}
                    {step.details.newlyRegistered === false && (
                      <div className="text-gray-500 dark:text-gray-400 italic">
                        이미 Local PKD에 등록된 DSC 인증서입니다.
                      </div>
                    )}
                  </div>
                )}
              </div>
            )}
          </div>
        ))}
      </div>
    </div>
  );
}

export default VerificationStepsPanel;
