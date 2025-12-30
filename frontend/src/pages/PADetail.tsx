import { useState, useEffect } from 'react';
import { useParams, useNavigate, Link } from 'react-router-dom';
import {
  ShieldCheck,
  ArrowLeft,
  CheckCircle,
  XCircle,
  Clock,
  Loader2,
  AlertCircle,
  FileText,
  Calendar,
  Fingerprint,
} from 'lucide-react';
import { paApi } from '@/services/api';
import type { PAVerificationResult, PAStatus, StepResult } from '@/types';
import { cn } from '@/utils/cn';

export function PADetail() {
  const { paId } = useParams<{ paId: string }>();
  const navigate = useNavigate();

  const [result, setResult] = useState<PAVerificationResult | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (paId) {
      fetchPADetail();
    }
  }, [paId]);

  const fetchPADetail = async () => {
    if (!paId) return;

    setLoading(true);
    setError(null);

    try {
      const response = await paApi.getDetail(paId);
      setResult(response.data);
    } catch (err) {
      setError('PA 검증 정보를 불러오는데 실패했습니다.');
      console.error('Failed to fetch PA detail:', err);
    } finally {
      setLoading(false);
    }
  };

  const getStatusBadge = (status: PAStatus) => {
    const styles: Record<PAStatus, { bg: string; text: string; icon: React.ReactNode }> = {
      VALID: {
        bg: 'bg-green-100 dark:bg-green-900/30',
        text: 'text-green-600 dark:text-green-400',
        icon: <CheckCircle className="w-5 h-5" />,
      },
      INVALID: {
        bg: 'bg-red-100 dark:bg-red-900/30',
        text: 'text-red-600 dark:text-red-400',
        icon: <XCircle className="w-5 h-5" />,
      },
      ERROR: {
        bg: 'bg-yellow-100 dark:bg-yellow-900/30',
        text: 'text-yellow-600 dark:text-yellow-400',
        icon: <AlertCircle className="w-5 h-5" />,
      },
    };

    const style = styles[status];
    const label = {
      VALID: '검증 성공',
      INVALID: '검증 실패',
      ERROR: '오류 발생',
    }[status];

    return (
      <span className={cn('inline-flex items-center gap-2 px-4 py-2 rounded-full text-base font-medium', style.bg, style.text)}>
        {style.icon}
        {label}
      </span>
    );
  };

  const getStepStatusIcon = (step: StepResult) => {
    switch (step.status) {
      case 'SUCCESS':
        return <CheckCircle className="w-5 h-5 text-green-500" />;
      case 'FAILED':
        return <XCircle className="w-5 h-5 text-red-500" />;
      case 'SKIPPED':
        return <AlertCircle className="w-5 h-5 text-gray-400" />;
      default:
        return null;
    }
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

  if (error || !result) {
    return (
      <div className="w-full px-4 lg:px-6 py-4">
        <div className="flex flex-col items-center justify-center py-20 text-gray-500 dark:text-gray-400">
          <AlertCircle className="w-12 h-12 mb-4 opacity-50" />
          <p className="text-lg font-medium">{error || 'PA 검증 정보를 찾을 수 없습니다.'}</p>
          <button
            onClick={() => navigate('/pa/history')}
            className="mt-4 px-4 py-2 bg-blue-500 text-white rounded-lg hover:bg-blue-600 transition-colors"
          >
            목록으로 돌아가기
          </button>
        </div>
      </div>
    );
  }

  const steps = [
    { name: 'SOD 파싱', desc: 'CMS SignedData 구조 분석', result: result.sodParsing },
    { name: 'DSC 추출', desc: 'Document Signer Certificate 추출', result: result.dscExtraction },
    { name: 'CSCA 조회', desc: 'Country Signing CA 인증서 조회', result: result.cscaLookup },
    { name: 'Trust Chain 검증', desc: 'DSC → CSCA Trust Chain 검증', result: result.trustChainValidation },
    { name: 'SOD 서명 검증', desc: 'SOD CMS 서명 검증', result: result.sodSignatureValidation },
    { name: 'DG 해시 검증', desc: 'Data Group 해시 무결성 검증', result: result.dataGroupHashValidation },
    { name: 'CRL 확인', desc: 'DSC 인증서 폐기 상태 확인', result: result.crlCheck },
  ];

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
          <div className="p-3 rounded-xl bg-gradient-to-br from-teal-500 to-cyan-600 shadow-lg">
            <ShieldCheck className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">PA 검증 상세</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400 font-mono">
              {result.id}
            </p>
          </div>
          {getStatusBadge(result.status)}
        </div>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Left Column: Verification Steps */}
        <div className="lg:col-span-2">
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
            <h2 className="text-lg font-bold text-gray-900 dark:text-white mb-6 flex items-center gap-2">
              <FileText className="w-5 h-5 text-cyan-500" />
              검증 단계 (ICAO 9303)
            </h2>

            <div className="space-y-4">
              {steps.map((step, idx) => (
                <div
                  key={idx}
                  className={cn(
                    'p-4 rounded-xl border-2 transition-all',
                    step.result.status === 'SUCCESS' && 'border-green-200 dark:border-green-800 bg-green-50/50 dark:bg-green-900/10',
                    step.result.status === 'FAILED' && 'border-red-200 dark:border-red-800 bg-red-50/50 dark:bg-red-900/10',
                    step.result.status === 'SKIPPED' && 'border-gray-200 dark:border-gray-700 bg-gray-50/50 dark:bg-gray-800/50'
                  )}
                >
                  <div className="flex items-start gap-4">
                    <div className={cn(
                      'w-10 h-10 rounded-full flex items-center justify-center flex-shrink-0',
                      step.result.status === 'SUCCESS' && 'bg-green-100 dark:bg-green-900/30',
                      step.result.status === 'FAILED' && 'bg-red-100 dark:bg-red-900/30',
                      step.result.status === 'SKIPPED' && 'bg-gray-100 dark:bg-gray-700'
                    )}>
                      {getStepStatusIcon(step.result)}
                    </div>
                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2 mb-1">
                        <span className="text-xs font-bold text-gray-400">Step {idx + 1}</span>
                        <h3 className="font-bold text-gray-900 dark:text-white">{step.name}</h3>
                      </div>
                      <p className="text-sm text-gray-500 dark:text-gray-400 mb-2">{step.desc}</p>
                      <div className={cn(
                        'text-sm p-2 rounded-lg',
                        step.result.status === 'SUCCESS' && 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400',
                        step.result.status === 'FAILED' && 'bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400',
                        step.result.status === 'SKIPPED' && 'bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-400'
                      )}>
                        {step.result.message}
                      </div>
                      {step.result.details && Object.keys(step.result.details).length > 0 && (
                        <div className="mt-2 text-xs text-gray-500 dark:text-gray-400">
                          <details>
                            <summary className="cursor-pointer hover:text-gray-700 dark:hover:text-gray-300">
                              상세 정보 보기
                            </summary>
                            <pre className="mt-2 p-2 bg-gray-100 dark:bg-gray-700 rounded overflow-x-auto">
                              {JSON.stringify(step.result.details, null, 2)}
                            </pre>
                          </details>
                        </div>
                      )}
                    </div>
                  </div>
                </div>
              ))}
            </div>
          </div>
        </div>

        {/* Right Column: Summary & Info */}
        <div className="lg:col-span-1 space-y-6">
          {/* Result Summary */}
          <div className={cn(
            'rounded-2xl shadow-lg p-6',
            result.overallValid
              ? 'bg-gradient-to-br from-green-500 to-emerald-600 text-white'
              : 'bg-gradient-to-br from-red-500 to-rose-600 text-white'
          )}>
            <div className="flex items-center gap-3 mb-4">
              {result.overallValid ? (
                <CheckCircle className="w-10 h-10" />
              ) : (
                <XCircle className="w-10 h-10" />
              )}
              <div>
                <h3 className="text-xl font-bold">
                  {result.overallValid ? 'PA 검증 성공' : 'PA 검증 실패'}
                </h3>
                <p className="text-sm opacity-90">
                  {result.overallValid ? '모든 검증 단계를 통과했습니다.' : '일부 검증 단계가 실패했습니다.'}
                </p>
              </div>
            </div>
            <div className="grid grid-cols-2 gap-4 mt-4 pt-4 border-t border-white/20">
              <div>
                <p className="text-sm opacity-80">처리 시간</p>
                <p className="text-lg font-bold">{result.processingTimeMs}ms</p>
              </div>
              <div>
                <p className="text-sm opacity-80">검증 일시</p>
                <p className="text-sm font-medium">{formatDate(result.verifiedAt)}</p>
              </div>
            </div>
          </div>

          {/* Verification Info */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
            <h2 className="text-lg font-bold text-gray-900 dark:text-white mb-4 flex items-center gap-2">
              <Fingerprint className="w-5 h-5 text-purple-500" />
              검증 정보
            </h2>
            <div className="space-y-4">
              <div className="flex items-center gap-3">
                <div className="p-2 bg-gray-100 dark:bg-gray-700 rounded-lg">
                  <Calendar className="w-4 h-4 text-gray-500" />
                </div>
                <div>
                  <p className="text-xs text-gray-500 dark:text-gray-400">검증 일시</p>
                  <p className="text-sm font-medium text-gray-900 dark:text-white">
                    {formatDate(result.verifiedAt)}
                  </p>
                </div>
              </div>
              <div className="flex items-center gap-3">
                <div className="p-2 bg-gray-100 dark:bg-gray-700 rounded-lg">
                  <Clock className="w-4 h-4 text-gray-500" />
                </div>
                <div>
                  <p className="text-xs text-gray-500 dark:text-gray-400">처리 시간</p>
                  <p className="text-sm font-medium text-gray-900 dark:text-white">
                    {result.processingTimeMs}ms
                  </p>
                </div>
              </div>
            </div>
          </div>

          {/* Step Summary */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
            <h2 className="text-lg font-bold text-gray-900 dark:text-white mb-4">검증 단계 요약</h2>
            <div className="space-y-2">
              {steps.map((step, idx) => (
                <div key={idx} className="flex items-center justify-between py-2 border-b border-gray-100 dark:border-gray-700 last:border-0">
                  <span className="text-sm text-gray-600 dark:text-gray-400">Step {idx + 1}</span>
                  <span className={cn(
                    'text-xs font-medium px-2 py-1 rounded',
                    step.result.status === 'SUCCESS' && 'bg-green-100 dark:bg-green-900/30 text-green-600 dark:text-green-400',
                    step.result.status === 'FAILED' && 'bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400',
                    step.result.status === 'SKIPPED' && 'bg-gray-100 dark:bg-gray-700 text-gray-500'
                  )}>
                    {step.result.status === 'SUCCESS' ? '성공' : step.result.status === 'FAILED' ? '실패' : '건너뜀'}
                  </span>
                </div>
              ))}
            </div>
          </div>

          {/* Quick Actions */}
          <div className="space-y-3">
            <Link
              to="/pa/verify"
              className="w-full inline-flex items-center justify-center gap-2 px-4 py-3 rounded-xl text-sm font-medium text-white bg-gradient-to-r from-teal-500 to-cyan-500 hover:from-teal-600 hover:to-cyan-600 transition-all shadow-md hover:shadow-lg"
            >
              새 PA 검증
            </Link>
            <Link
              to="/pa/history"
              className="w-full inline-flex items-center justify-center gap-2 px-4 py-3 rounded-xl text-sm font-medium border border-gray-300 dark:border-gray-600 text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors"
            >
              검증 이력
            </Link>
          </div>
        </div>
      </div>
    </div>
  );
}

export default PADetail;
