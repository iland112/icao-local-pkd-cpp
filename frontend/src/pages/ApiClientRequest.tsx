import { useState, type FormEvent } from 'react';
import { useNavigate } from 'react-router-dom';
import { KeyRound, ArrowLeft, Send, CheckCircle, AlertCircle, Loader2, Server, Monitor, Smartphone, HelpCircle } from 'lucide-react';
import { apiClientRequestApi, type SubmitApiClientRequest } from '@/api/apiClientRequestApi';
import { cn } from '@/utils/cn';
import { useThemeStore } from '@/stores/themeStore';

const AVAILABLE_PERMISSIONS = [
  { value: 'cert:read', label: '인증서 검색', desc: 'CSCA/DSC 인증서 조회' },
  { value: 'cert:export', label: '인증서 내보내기', desc: 'PEM/DER 형식 다운로드' },
  { value: 'pa:verify', label: 'PA 검증', desc: 'Passive Authentication 수행' },
  { value: 'pa:read', label: 'PA 이력 조회', desc: '검증 이력 및 결과 조회' },
  { value: 'upload:read', label: '업로드 조회', desc: '업로드 이력 및 상태 조회' },
  { value: 'upload:write', label: '파일 업로드', desc: 'LDIF/ML/인증서 파일 업로드' },
  { value: 'report:read', label: '보고서 조회', desc: 'DSC_NC/CRL/Trust Chain 보고서' },
  { value: 'ai:read', label: 'AI 분석 조회', desc: 'AI 인증서 포렌식 분석 결과' },
  { value: 'sync:read', label: 'Sync 조회', desc: 'DB-LDAP 동기화 상태' },
  { value: 'icao:read', label: 'ICAO 조회', desc: 'ICAO PKD 버전 상태' },
];

const DEVICE_TYPES = [
  { value: 'SERVER' as const, label: '서버', desc: '서버 간 통신 (고정 IP)', icon: Server },
  { value: 'DESKTOP' as const, label: '데스크톱', desc: '데스크톱 애플리케이션 (사내 IP)', icon: Monitor },
  { value: 'MOBILE' as const, label: '모바일', desc: '모바일 앱 (IP 제한 불가)', icon: Smartphone },
  { value: 'OTHER' as const, label: '기타', desc: '기타 환경', icon: HelpCircle },
];

export default function ApiClientRequest() {
  const navigate = useNavigate();
  const { darkMode } = useThemeStore();
  const [submitted, setSubmitted] = useState(false);
  const [requestId, setRequestId] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState('');

  const [form, setForm] = useState<SubmitApiClientRequest>({
    requester_name: '',
    requester_org: '',
    requester_contact_phone: '',
    requester_contact_email: '',
    request_reason: '',
    client_name: '',
    description: '',
    device_type: 'SERVER',
    permissions: ['cert:read', 'pa:verify'],
    allowed_ips: [],
  });
  const [ipsText, setIpsText] = useState('');

  const showIpField = form.device_type === 'SERVER' || form.device_type === 'DESKTOP';

  const handleSubmit = async (e: FormEvent) => {
    e.preventDefault();
    if (!form.requester_name || !form.requester_org || !form.requester_contact_email || !form.request_reason || !form.client_name) return;

    setSubmitting(true);
    setError('');
    try {
      const req: SubmitApiClientRequest = {
        ...form,
        allowed_ips: showIpField && ipsText ? ipsText.split(',').map(s => s.trim()).filter(Boolean) : [],
      };
      const res = await apiClientRequestApi.submit(req);
      if (res.success && res.request_id) {
        setRequestId(res.request_id);
        setSubmitted(true);
      } else {
        setError(res.message || '요청 제출에 실패했습니다.');
      }
    } catch (err: any) {
      const msg = err?.response?.data?.message || err?.message || '요청 제출 중 오류가 발생했습니다.';
      setError(msg);
    } finally {
      setSubmitting(false);
    }
  };

  const updateField = (field: keyof SubmitApiClientRequest, value: string | number | string[]) => {
    setForm(prev => ({ ...prev, [field]: value }));
  };

  // Success page
  if (submitted) {
    return (
      <div className={cn('min-h-screen flex items-center justify-center px-4 py-6 sm:py-8', darkMode ? 'bg-gray-900' : 'bg-gray-50')}>
        <div className={cn('max-w-lg w-full rounded-2xl shadow-lg p-6 sm:p-8 text-center', darkMode ? 'bg-gray-800' : 'bg-white')}>
          <div className="w-14 h-14 sm:w-16 sm:h-16 mx-auto bg-green-100 dark:bg-green-900/30 rounded-full flex items-center justify-center mb-5 sm:mb-6">
            <CheckCircle className="w-7 h-7 sm:w-8 sm:h-8 text-green-600 dark:text-green-400" />
          </div>
          <h2 className="text-xl sm:text-2xl font-bold text-gray-900 dark:text-white mb-2">요청이 접수되었습니다</h2>
          <p className="text-sm sm:text-base text-gray-500 dark:text-gray-400 mb-5 sm:mb-6">
            관리자가 요청을 검토한 후 승인 또는 거절합니다.<br />
            승인 시 등록하신 이메일로 API Key가 전달됩니다.
          </p>
          <div className={cn('rounded-xl p-4 mb-5 sm:mb-6 text-left', darkMode ? 'bg-gray-700' : 'bg-gray-50')}>
            <p className="text-xs text-gray-500 dark:text-gray-400 mb-1">요청 ID</p>
            <p className="text-xs sm:text-sm font-mono text-gray-900 dark:text-white break-all">{requestId}</p>
          </div>
          <button
            onClick={() => navigate('/login')}
            className="w-full sm:w-auto px-5 py-3 sm:py-2.5 bg-[#02385e] hover:bg-[#024b7a] text-white rounded-xl font-medium transition-colors"
          >
            로그인 페이지로
          </button>
        </div>
      </div>
    );
  }

  return (
    <div className={cn('min-h-screen px-4 py-6 sm:py-8 lg:py-6', darkMode ? 'bg-gray-900' : 'bg-gray-50')}>
      <div className="max-w-2xl lg:max-w-6xl mx-auto">
        {/* Header */}
        <div className="mb-5 sm:mb-6">
          <button
            onClick={() => navigate('/login')}
            className="flex items-center gap-1.5 text-sm text-gray-500 dark:text-gray-400 hover:text-gray-700 dark:hover:text-gray-200 mb-3 transition-colors"
          >
            <ArrowLeft className="w-4 h-4" />
            로그인 페이지로 돌아가기
          </button>
          <div className="flex items-center gap-3">
            <div className="p-2.5 sm:p-3 rounded-xl bg-gradient-to-br from-blue-500 to-indigo-600 shadow-lg flex-shrink-0">
              <KeyRound className="w-6 h-6 sm:w-7 sm:h-7 text-white" />
            </div>
            <div className="min-w-0">
              <h1 className="text-xl sm:text-2xl font-bold text-gray-900 dark:text-white truncate">API 클라이언트 등록 요청</h1>
              <p className="text-xs sm:text-sm text-gray-500 dark:text-gray-400">외부 시스템 연동을 위한 API Key 발급을 요청합니다</p>
            </div>
          </div>
        </div>

        {/* Form */}
        <form onSubmit={handleSubmit}>
          {/* Desktop: 2-column layout, Mobile: stacked */}
          <div className="grid grid-cols-1 lg:grid-cols-2 gap-4 lg:gap-5">
            {/* Left Column: Requester Info */}
            <div className={cn('rounded-2xl shadow-lg overflow-hidden lg:h-fit', darkMode ? 'bg-gray-800' : 'bg-white')}>
              <div className="px-4 sm:px-5 py-3 border-b border-gray-200 dark:border-gray-700">
                <h2 className="text-sm sm:text-base font-semibold text-gray-900 dark:text-white">요청자 정보</h2>
              </div>
              <div className="px-4 sm:px-5 py-4 space-y-3">
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                  <Field label="요청자 이름" required value={form.requester_name} onChange={v => updateField('requester_name', v)} placeholder="홍길동" />
                  <Field label="소속 (기관·부서)" required value={form.requester_org} onChange={v => updateField('requester_org', v)} placeholder="법무부 출입국·외국인정책본부" />
                </div>
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                  <Field label="연락처 (전화번호)" value={form.requester_contact_phone || ''} onChange={v => updateField('requester_contact_phone', v)} placeholder="02-1234-5678" />
                  <Field label="이메일" required type="email" value={form.requester_contact_email} onChange={v => updateField('requester_contact_email', v)} placeholder="hong@example.go.kr" />
                </div>
                <div>
                  <label htmlFor="req-reason" className="block text-xs sm:text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                    요청 사유 <span className="text-red-500">*</span>
                  </label>
                  <textarea
                    id="req-reason"
                    name="request_reason"
                    value={form.request_reason}
                    onChange={e => updateField('request_reason', e.target.value)}
                    required
                    rows={3}
                    placeholder="API 연동 목적 및 사유를 상세히 기재해 주세요"
                    className={cn(
                      'w-full px-3 py-2.5 sm:py-2 rounded-lg text-sm',
                      darkMode ? 'bg-gray-700 border-gray-600 text-white' : 'bg-gray-50 border-gray-200 text-gray-900',
                      'border focus:ring-2 focus:ring-blue-500 focus:border-transparent'
                    )}
                  />
                </div>

                {/* Client Name + Description (desktop: in left column to balance height) */}
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3 pt-2 border-t border-gray-100 dark:border-gray-700">
                  <Field label="클라이언트 이름" required value={form.client_name} onChange={v => updateField('client_name', v)} placeholder="출입국관리시스템" />
                  <Field label="설명" value={form.description || ''} onChange={v => updateField('description', v)} placeholder="API 사용 용도 및 연동 시스템 설명" />
                </div>

                {/* Device Type */}
                <div>
                  <span className="block text-xs sm:text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                    사용 기기 타입 <span className="text-red-500">*</span>
                  </span>
                  <div className="grid grid-cols-2 sm:grid-cols-4 gap-1.5 sm:gap-2">
                    {DEVICE_TYPES.map(dt => {
                      const Icon = dt.icon;
                      const selected = form.device_type === dt.value;
                      return (
                        <button
                          key={dt.value}
                          type="button"
                          onClick={() => updateField('device_type', dt.value)}
                          className={cn(
                            'flex flex-col items-center gap-1 p-2 sm:p-2.5 rounded-xl border-2 transition-all text-center',
                            selected
                              ? 'border-blue-500 bg-blue-50 dark:bg-blue-900/20'
                              : 'border-gray-200 dark:border-gray-600 hover:border-gray-300 dark:hover:border-gray-500'
                          )}
                        >
                          <Icon className={cn('w-4 h-4 sm:w-5 sm:h-5', selected ? 'text-blue-600 dark:text-blue-400' : 'text-gray-400')} />
                          <span className={cn('text-xs sm:text-sm font-medium', selected ? 'text-blue-700 dark:text-blue-300' : 'text-gray-700 dark:text-gray-300')}>{dt.label}</span>
                        </button>
                      );
                    })}
                  </div>
                </div>

                {/* Allowed IPs — only for SERVER/DESKTOP */}
                {showIpField && (
                  <div>
                    <Field
                      label="허용 IP (콤마 구분)"
                      value={ipsText}
                      onChange={setIpsText}
                      placeholder="비워두면 관리자가 설정합니다 (예: 10.0.0.0/24, 192.168.1.100)"
                    />
                    <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">
                      제안하신 IP는 관리자가 승인 시 최종 확인합니다.
                    </p>
                  </div>
                )}
                {form.device_type === 'MOBILE' && (
                  <div className="flex items-start gap-2 p-2.5 bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-xl">
                    <AlertCircle className="w-4 h-4 text-amber-500 flex-shrink-0 mt-0.5" />
                    <p className="text-xs text-amber-700 dark:text-amber-300">
                      모바일 기기는 IP가 유동적이므로 IP 기반 접근 제한이 적용되지 않습니다.
                    </p>
                  </div>
                )}
              </div>
            </div>

            {/* Right Column: Permissions + Submit */}
            <div className="flex flex-col gap-4 lg:gap-5">
              <div className={cn('rounded-2xl shadow-lg overflow-hidden flex-1', darkMode ? 'bg-gray-800' : 'bg-white')}>
                <div className="px-4 sm:px-5 py-3 border-b border-gray-200 dark:border-gray-700">
                  <h2 className="text-sm sm:text-base font-semibold text-gray-900 dark:text-white">요청 권한</h2>
                </div>
                <div className="px-4 sm:px-5 py-4">
                  <div className="grid grid-cols-1 sm:grid-cols-2 gap-2">
                    {AVAILABLE_PERMISSIONS.map(p => (
                      <label key={p.value} htmlFor={`req-perm-${p.value}`} className={cn(
                        'flex items-start gap-2.5 p-2.5 rounded-lg cursor-pointer transition-colors',
                        form.permissions.includes(p.value)
                          ? (darkMode ? 'bg-blue-900/20 border border-blue-700' : 'bg-blue-50 border border-blue-200')
                          : (darkMode ? 'bg-gray-700/50 border border-gray-600' : 'bg-gray-50 border border-gray-200')
                      )}>
                        <input
                          id={`req-perm-${p.value}`}
                          type="checkbox"
                          checked={form.permissions.includes(p.value)}
                          onChange={e => {
                            const perms = e.target.checked
                              ? [...form.permissions, p.value]
                              : form.permissions.filter(x => x !== p.value);
                            updateField('permissions', perms);
                          }}
                          className="mt-0.5 w-4 h-4 rounded border-gray-300"
                        />
                        <div className="min-w-0">
                          <span className="text-sm font-medium text-gray-900 dark:text-white">{p.label}</span>
                          <p className="text-xs text-gray-500 dark:text-gray-400">{p.desc}</p>
                        </div>
                      </label>
                    ))}
                  </div>
                </div>

                {/* Info banner */}
                <div className="px-4 sm:px-5 py-2.5 bg-blue-50 dark:bg-blue-900/10 border-t border-gray-200 dark:border-gray-700">
                  <p className="text-xs text-blue-600 dark:text-blue-400">
                    Rate Limit, 허용 엔드포인트, 사용 기간 등 고급 설정은 관리자가 승인 시 설정합니다.
                  </p>
                </div>
              </div>

              {/* Error */}
              {error && (
                <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl flex items-start gap-2">
                  <AlertCircle className="w-4 h-4 text-red-500 flex-shrink-0 mt-0.5" />
                  <p className="text-sm text-red-700 dark:text-red-300">{error}</p>
                </div>
              )}

              {/* Submit */}
              <div className="flex flex-col-reverse sm:flex-row sm:justify-end gap-3">
                <button
                  type="button"
                  onClick={() => navigate('/login')}
                  className="w-full sm:w-auto px-5 py-3 sm:py-2.5 text-gray-700 dark:text-gray-300 bg-gray-100 dark:bg-gray-700 rounded-xl hover:bg-gray-200 dark:hover:bg-gray-600 font-medium transition-colors text-center"
                >
                  취소
                </button>
                <button
                  type="submit"
                  disabled={submitting || !form.requester_name || !form.requester_org || !form.requester_contact_email || !form.request_reason || !form.client_name}
                  className={cn(
                    'w-full sm:w-auto flex items-center justify-center gap-2 px-5 py-3 sm:py-2.5 rounded-xl font-medium transition-all',
                    'bg-[#02385e] hover:bg-[#024b7a] text-white',
                    'disabled:opacity-40 disabled:cursor-not-allowed',
                    'active:scale-[0.98]'
                  )}
                >
                  {submitting ? (
                    <><Loader2 className="w-4 h-4 animate-spin" /><span>제출 중...</span></>
                  ) : (
                    <><Send className="w-4 h-4" /><span>등록 요청</span></>
                  )}
                </button>
              </div>
            </div>
          </div>
        </form>

        {/* Footer */}
        <p className="mt-4 text-center text-[11px] text-gray-300 dark:text-gray-600">
          &copy; 2026 SmartCore Inc.
        </p>
      </div>
    </div>
  );
}

function Field({ label, value, onChange, placeholder, type = 'text', required }: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  placeholder?: string;
  type?: string;
  required?: boolean;
}) {
  const fieldId = `req-${label.replace(/\s+/g, '-').toLowerCase()}`;
  return (
    <div>
      <label htmlFor={fieldId} className="block text-xs sm:text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
        {label} {required && <span className="text-red-500">*</span>}
      </label>
      <input
        id={fieldId}
        name={fieldId}
        type={type}
        value={value}
        onChange={e => onChange(e.target.value)}
        placeholder={placeholder}
        required={required}
        className="w-full px-3 py-2.5 sm:py-2 bg-gray-50 dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500 focus:border-transparent"
      />
    </div>
  );
}
