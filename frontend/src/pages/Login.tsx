import { useState, useEffect, type FormEvent, type CSSProperties } from 'react';
import { useNavigate } from 'react-router-dom';
import {
  Lock,
  User,
  AlertCircle,
  Loader2,
  Globe,
  ShieldCheck,
  Key,
  Upload,
  IdCard,
  Database,
  CheckCircle,
  GraduationCap,
  FileText,
  Sun,
  Moon,
} from 'lucide-react';
import { authApi } from '@/services/api';
import { uploadHistoryApi } from '@/services/pkdApi';
import { cn } from '@/utils/cn';
import { useThemeStore } from '@/stores/themeStore';

const stagger = (delay: number): CSSProperties => ({
  animation: `slideInUp 0.6s ease-out ${delay}s both`,
});

const defaultStats = { countriesCount: 0, totalCertificates: 0 };

const featureCards = [
  {
    icon: Upload,
    color: 'violet',
    title: 'PKD 파일 업로드 및 처리',
    desc: 'PKD LDIF/Master List 파일을 업로드하고, 인증서를 파싱·검증하여 DB 및 LDAP에 저장합니다.',
    items: ['LDIF / Master List 파일 업로드', 'CSCA/DSC/CRL 파싱 및 검증', 'Trust Chain 검증 (DSC → CSCA)', 'DB + LDAP 이중 저장'],
  },
  {
    icon: IdCard,
    color: 'teal',
    title: 'Passive Authentication',
    desc: '전자여권 칩의 SOD, Data Group을 업로드하여 ICAO 9303 표준에 따른 PA를 수행합니다.',
    items: ['SOD CMS 서명 검증', 'DSC → CSCA Trust Chain 검증', 'Data Group 해시 무결성 검증', 'DG1/DG2 파싱 및 시각화'],
  },
  {
    icon: Database,
    color: 'blue',
    title: '인증서 관리 및 조회',
    desc: '저장된 CSCA/DSC/CRL 인증서를 검색하고, 다양한 형식으로 내보내기할 수 있습니다.',
    items: ['LDAP 기반 실시간 인증서 검색', '국가/타입별 필터링 및 정렬', 'DER/PEM 형식 Export', 'AI 기반 인증서 포렌식 분석'],
  },
];

const standards = [
  { icon: GraduationCap, label: 'ICAO Doc 9303' },
  { icon: FileText, label: 'RFC 5280 X.509' },
  { icon: Key, label: 'RFC 5652 CMS' },
];

export function Login() {
  const navigate = useNavigate();
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const { darkMode, toggleTheme } = useThemeStore();
  const [stats, setStats] = useState(defaultStats);

  useEffect(() => {
    uploadHistoryApi.getStatistics()
      .then((res) => {
        const d = res.data;
        setStats({ countriesCount: d.countriesCount ?? 0, totalCertificates: d.totalCertificates ?? 0 });
      })
      .catch(() => {});
  }, []);

  const handleSubmit = async (e: FormEvent) => {
    e.preventDefault();
    setError('');
    setLoading(true);

    try {
      const response = await authApi.login(username, password);

      if (response.success && response.access_token) {
        localStorage.setItem('access_token', response.access_token);

        if (response.user) {
          localStorage.setItem('user', JSON.stringify(response.user));
        }

        navigate('/');
      } else {
        setError('로그인에 실패했습니다. 잠시 후 다시 시도해주세요.');
      }
    } catch (err: any) {
      if (import.meta.env.DEV) console.error('Login error:', err);

      if (err.response?.status === 401) {
        setError('사용자명 또는 비밀번호가 올바르지 않습니다.');
      } else if (err.response?.data?.message) {
        setError(err.response.data.message);
      } else {
        setError('로그인 중 오류가 발생했습니다. 네트워크 연결을 확인해주세요.');
      }
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="min-h-screen flex flex-col lg:flex-row">
      {/* ===== LEFT PANEL: HERO (hidden on mobile) ===== */}
      <div className="hidden md:flex md:w-1/2 lg:w-[55%] relative overflow-hidden">
        {/* Gradient background */}
        <div className="absolute inset-0 bg-gradient-to-br from-[#02385e] via-[#0a4a7a] to-[#1a1a2e]" />

        {/* Illustrated background: world map + passport + stamps */}
        <div
          className="absolute inset-0"
          style={{
            backgroundImage: 'url(/hero-bg.svg)',
            backgroundSize: 'cover',
            backgroundPosition: 'center',
          }}
        />

        {/* Subtle vignette overlay */}
        <div className="absolute inset-0 bg-gradient-to-t from-[#02385e]/40 via-transparent to-[#02385e]/15" />

        {/* Hero content */}
        <div className="relative flex flex-col justify-center px-8 lg:px-12 xl:px-16 py-12 w-full">
          {/* Branding */}
          <div style={stagger(0.1)}>
            <div className="inline-flex mb-6">
              <img src="/favicon.svg" alt="SPKD" className="w-16 h-16 drop-shadow-lg" />
            </div>
            <h1 className="text-3xl lg:text-4xl xl:text-5xl font-bold text-white mb-3 tracking-tight">
              SPKD
            </h1>
            <p className="text-lg lg:text-xl text-sky-200 mb-2 font-medium">
              전자여권 위·변조 검사 시스템
            </p>
            <p className="text-sm text-gray-300/70 max-w-lg leading-relaxed">
              SmartCore PKD 인증서 관리 및 Passive Authentication 검증 플랫폼.
              전자여권 인증서의 수집·검증·모니터링과 여권 칩 데이터의 위·변조 검사를 통합 수행합니다.
            </p>
          </div>

          {/* Statistics row */}
          <div className="grid grid-cols-2 gap-3 lg:gap-4 mt-10" style={stagger(0.2)}>
            {[
              { icon: Globe, value: stats.countriesCount.toLocaleString(), label: '현재 등록 국가' },
              { icon: ShieldCheck, value: stats.totalCertificates.toLocaleString(), label: '현재 관리 인증서' },
            ].map((stat) => (
              <div
                key={stat.label}
                className="bg-white/10 backdrop-blur-sm rounded-xl p-4 border border-white/15 text-center transition-colors hover:bg-white/15"
              >
                <stat.icon className="w-5 h-5 lg:w-6 lg:h-6 text-sky-300 mx-auto mb-2" />
                <p className="text-2xl lg:text-3xl font-bold text-white">{stat.value}</p>
                <p className="text-xs lg:text-sm text-gray-300 mt-1">{stat.label}</p>
              </div>
            ))}
          </div>

          {/* Feature cards */}
          <div className="grid grid-cols-1 xl:grid-cols-3 gap-3 mt-8" style={stagger(0.3)}>
            {featureCards.map((card) => (
              <div
                key={card.title}
                className="bg-white/5 backdrop-blur-sm rounded-xl p-4 lg:p-5 border border-white/10 transition-colors hover:bg-white/10"
              >
                <div className="flex items-center gap-2.5 mb-3">
                  <div className="p-2 rounded-lg bg-white/10 flex-shrink-0">
                    <card.icon className="w-5 h-5 text-sky-300" />
                  </div>
                  <h3 className="text-sm font-semibold text-white leading-tight">{card.title}</h3>
                </div>
                <p className="text-xs text-gray-300/70 mb-3 leading-relaxed">{card.desc}</p>
                <ul className="space-y-1.5">
                  {card.items.map((item) => (
                    <li key={item} className="flex items-center gap-2 text-xs text-gray-200/80">
                      <CheckCircle className="w-3.5 h-3.5 text-sky-400/70 flex-shrink-0" />
                      {item}
                    </li>
                  ))}
                </ul>
              </div>
            ))}
          </div>

          {/* Standards badges */}
          <div className="flex flex-wrap gap-2 mt-8" style={stagger(0.4)}>
            {standards.map((s) => (
              <span
                key={s.label}
                className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-medium bg-white/10 backdrop-blur-sm text-gray-200 border border-white/15"
              >
                <s.icon className="w-3.5 h-3.5" />
                {s.label}
              </span>
            ))}
          </div>

          {/* Bottom attribution */}
          <div className="mt-auto pt-10">
            <p className="text-xs text-gray-400/50">
              Powered by SmartCore Inc.
            </p>
          </div>
        </div>
      </div>

      {/* ===== RIGHT PANEL: LOGIN ===== */}
      <div className="w-full md:w-1/2 lg:w-[45%] flex items-center justify-center px-4 sm:px-6 lg:px-8 py-8 lg:py-12 bg-white dark:bg-gray-900 relative min-h-screen md:min-h-0">
        {/* Subtle background pattern */}
        <div className="absolute inset-0 opacity-[0.02] dark:opacity-[0.03]" style={{
          backgroundImage: 'radial-gradient(circle at 1px 1px, currentColor 1px, transparent 0)',
          backgroundSize: '24px 24px',
        }} />

        {/* Dark mode toggle */}
        <button
          type="button"
          onClick={toggleTheme}
          className="absolute top-5 right-5 p-2 rounded-full hover:bg-gray-100 dark:hover:bg-gray-800 text-gray-400 dark:text-gray-500 transition-colors"
          aria-label="테마 전환"
        >
          {darkMode ? <Sun className="w-4.5 h-4.5" /> : <Moon className="w-4.5 h-4.5" />}
        </button>

        <div className="w-full max-w-[380px] relative">
          {/* Mobile-only branding */}
          <div className="md:hidden text-center mb-8" style={stagger(0.1)}>
            <div className="inline-flex items-center justify-center mb-4">
              <img src="/favicon.svg" alt="SPKD" className="w-14 h-14 drop-shadow-lg" />
            </div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white tracking-tight">
              SPKD
            </h1>
            <p className="text-sm text-gray-500 dark:text-gray-400 mt-1">
              전자여권 위·변조 검사 시스템
            </p>
          </div>

          {/* Desktop: logo + title */}
          <div className="hidden md:flex flex-col items-center mb-8" style={stagger(0.1)}>
            <div className="inline-flex items-center justify-center mb-4">
              <img src="/favicon.svg" alt="SPKD" className="w-11 h-11" />
            </div>
            <h2 className="text-[22px] font-bold text-gray-900 dark:text-white tracking-tight">SPKD 로그인</h2>
            <p className="text-sm text-gray-400 dark:text-gray-500 mt-1">계정 정보를 입력해 주세요</p>
          </div>

          {/* Login Card */}
          <div style={stagger(0.2)}>
            {/* Mobile title inside card */}
            <h2 className="md:hidden text-lg font-semibold text-gray-900 dark:text-white mb-6 text-center">
              로그인
            </h2>

            {/* Error Alert */}
            {error && (
              <div className="mb-5 p-3 rounded-xl bg-red-50 dark:bg-red-900/20 border border-red-100 dark:border-red-800/50 flex items-start gap-2.5" role="alert">
                <AlertCircle className="w-4.5 h-4.5 text-red-500 dark:text-red-400 flex-shrink-0 mt-0.5" />
                <p className="text-sm text-red-600 dark:text-red-200">{error}</p>
              </div>
            )}

            {/* Login Form */}
            <form onSubmit={handleSubmit} className="space-y-4">
              {/* Username Field */}
              <div>
                <label
                  htmlFor="username"
                  className="block text-xs font-semibold text-gray-500 dark:text-gray-400 mb-1.5 uppercase tracking-wider"
                >
                  사용자명
                </label>
                <div className="relative group">
                  <div className="absolute inset-y-0 left-0 pl-3.5 flex items-center pointer-events-none">
                    <User className="h-[18px] w-[18px] text-gray-300 dark:text-gray-600 group-focus-within:text-[#02385e] dark:group-focus-within:text-sky-400 transition-colors" />
                  </div>
                  <input
                    id="username"
                    type="text"
                    autoComplete="username"
                    value={username}
                    onChange={(e) => setUsername(e.target.value)}
                    required
                    autoFocus
                    disabled={loading}
                    className={cn(
                      'block w-full pl-10 pr-4 py-2.5 border-0 rounded-lg',
                      'bg-gray-50 dark:bg-gray-800',
                      'ring-1 ring-gray-200 dark:ring-gray-700',
                      'text-gray-900 dark:text-white text-sm',
                      'placeholder-gray-400 dark:placeholder-gray-600',
                      'focus:outline-none focus:ring-2 focus:ring-[#02385e] dark:focus:ring-sky-500',
                      'focus:bg-white dark:focus:bg-gray-800',
                      'disabled:opacity-50 disabled:cursor-not-allowed',
                      'transition-all duration-150'
                    )}
                    placeholder="사용자명"
                  />
                </div>
              </div>

              {/* Password Field */}
              <div>
                <label
                  htmlFor="password"
                  className="block text-xs font-semibold text-gray-500 dark:text-gray-400 mb-1.5 uppercase tracking-wider"
                >
                  비밀번호
                </label>
                <div className="relative group">
                  <div className="absolute inset-y-0 left-0 pl-3.5 flex items-center pointer-events-none">
                    <Lock className="h-[18px] w-[18px] text-gray-300 dark:text-gray-600 group-focus-within:text-[#02385e] dark:group-focus-within:text-sky-400 transition-colors" />
                  </div>
                  <input
                    id="password"
                    type="password"
                    autoComplete="current-password"
                    value={password}
                    onChange={(e) => setPassword(e.target.value)}
                    required
                    disabled={loading}
                    className={cn(
                      'block w-full pl-10 pr-4 py-2.5 border-0 rounded-lg',
                      'bg-gray-50 dark:bg-gray-800',
                      'ring-1 ring-gray-200 dark:ring-gray-700',
                      'text-gray-900 dark:text-white text-sm',
                      'placeholder-gray-400 dark:placeholder-gray-600',
                      'focus:outline-none focus:ring-2 focus:ring-[#02385e] dark:focus:ring-sky-500',
                      'focus:bg-white dark:focus:bg-gray-800',
                      'disabled:opacity-50 disabled:cursor-not-allowed',
                      'transition-all duration-150'
                    )}
                    placeholder="비밀번호"
                  />
                </div>
              </div>

              {/* Submit Button */}
              <button
                type="submit"
                disabled={loading || !username || !password}
                className={cn(
                  'w-full py-2.5 px-4 rounded-lg font-semibold text-sm mt-2',
                  'bg-[#02385e] hover:bg-[#024b7a]',
                  'text-white',
                  'focus:outline-none focus:ring-2 focus:ring-[#02385e] focus:ring-offset-2 dark:focus:ring-offset-gray-900',
                  'disabled:opacity-40 disabled:cursor-not-allowed',
                  'active:scale-[0.98]',
                  'transition-all duration-150',
                  'flex items-center justify-center gap-2'
                )}
              >
                {loading ? (
                  <>
                    <Loader2 className="w-4 h-4 animate-spin" />
                    <span>로그인 중...</span>
                  </>
                ) : (
                  <span>로그인</span>
                )}
              </button>
            </form>

            {/* Help Text - Only shown in development mode */}
            {import.meta.env.DEV && (
              <div className="mt-5 pt-4 border-t border-gray-100 dark:border-gray-800">
                <p className="text-xs text-gray-400 dark:text-gray-500 text-center">
                  기본 계정: <span className="font-mono font-medium text-gray-600 dark:text-gray-400">admin</span> / <span className="font-mono font-medium text-gray-600 dark:text-gray-400">admin123</span>
                </p>
              </div>
            )}
          </div>

          {/* API Client Request Link */}
          <div className="mt-6 text-center" style={stagger(0.3)}>
            <p className="text-xs text-gray-400 dark:text-gray-500 mb-1">외부 시스템 연동이 필요하신가요?</p>
            <button
              type="button"
              onClick={() => navigate('/api-client-request')}
              className="inline-flex items-center gap-1.5 text-sm font-medium text-[#02385e] dark:text-sky-400 hover:underline transition-colors"
            >
              <Key className="w-3.5 h-3.5" />
              API 클라이언트 등록 요청
            </button>
          </div>

          {/* Footer */}
          <p className="mt-4 text-center text-[11px] text-gray-300 dark:text-gray-600" style={stagger(0.4)}>
            &copy; 2026 SmartCore Inc.
          </p>
        </div>
      </div>
    </div>
  );
}
