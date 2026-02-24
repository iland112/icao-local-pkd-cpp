import { useState, useEffect, useCallback } from 'react';
import {
  Link2,
  CheckCircle,
  XCircle,
  AlertTriangle,
  Clock,
  Loader2,
  Search,
  ArrowRight,
  Globe,
  Shield,
  TrendingUp,
} from 'lucide-react';
import { paApi } from '@/services/paApi';
import { cn } from '@/utils/cn';
import { getFlagSvgPath } from '@/utils/countryCode';
import { QuickLookupPanel } from '@/components/pa/QuickLookupPanel';
import type { QuickLookupResult } from '@/components/pa/QuickLookupPanel';
import axios from 'axios';

// Trust Chain 분포 데이터
interface ChainPattern {
  label: string;
  description: string;
  count: number;
  color: string;
}

// 샘플 인증서
interface SampleCert {
  country: string;
  fingerprint: string;
  status: string;
  chainPattern: string;
  label: string;
}

const SAMPLE_CERTS: SampleCert[] = [
  { country: 'KR', fingerprint: '9ea82cef2c37c2b86cf52874e26cfc2327542e8378ee16773223e8bb0be19894', status: 'VALID', chainPattern: 'DSC → Root', label: '한국 DSC (Direct)' },
  { country: 'HU', fingerprint: 'e2636645b39c47f36b0e518f34a74e96f77543059f0abee3fca98079395827a8', status: 'VALID', chainPattern: 'DSC → Link → Root', label: '헝가리 DSC (Link 1)' },
  { country: 'LU', fingerprint: 'e3e0719559c4a46567395e94127a4248a1ea17fd72e20b44845f9e54e853e40a', status: 'VALID', chainPattern: 'DSC → Link → CSCA → Root', label: '룩셈부르크 DSC (Link 2)' },
  { country: 'LU', fingerprint: 'ea9f8538afb2f9700d53ae450fbe4accb54fb1c512134e3fd56593c208daafdd', status: 'VALID', chainPattern: 'DSC → Link → Link → CSCA → Root', label: '룩셈부르크 DSC (Link 3)' },
  { country: 'HU', fingerprint: 'bc1567b9da90aa19e239733d71c46e42661af816f2c6c5e1737c4273920fce26', status: 'VALID', chainPattern: 'DSC → Link → Link → Root', label: '헝가리 DSC (Link 2)' },
  { country: 'NL', fingerprint: '574ec2b9b4a4ce15f02513c3907865fe1dc9bd0b9e0924097066d7a11832d27d', status: 'VALID', chainPattern: 'DSC → Link(x4) → Root', label: '네덜란드 DSC (Link 4)' },
];

// Chain path distribution from API
interface ChainPathEntry {
  path: string;
  count: number;
}

// 통계 타입
interface ValidationStats {
  validCount: number;
  expiredValidCount: number;
  invalidCount: number;
  pendingCount: number;
  trustChainValidCount: number;
  trustChainInvalidCount: number;
  cscaNotFoundCount: number;
  chainPathDistribution: ChainPathEntry[];
}

export function TrustChainValidationReport() {
  // Statistics
  const [stats, setStats] = useState<ValidationStats | null>(null);
  const [statsLoading, setStatsLoading] = useState(true);

  // Quick lookup states
  const [quickLookupDn, setQuickLookupDn] = useState('');
  const [quickLookupFingerprint, setQuickLookupFingerprint] = useState('');
  const [quickLookupResult, setQuickLookupResult] = useState<QuickLookupResult | null>(null);
  const [quickLookupLoading, setQuickLookupLoading] = useState(false);
  const [quickLookupError, setQuickLookupError] = useState<string | null>(null);

  // Load statistics
  useEffect(() => {
    const fetchStats = async () => {
      setStatsLoading(true);
      try {
        const res = await axios.get('/api/upload/statistics');
        const v = res.data?.validation;
        if (v) {
          setStats({
            validCount: v.validCount ?? 0,
            expiredValidCount: v.expiredValidCount ?? 0,
            invalidCount: v.invalidCount ?? 0,
            pendingCount: v.pendingCount ?? 0,
            trustChainValidCount: v.trustChainValidCount ?? 0,
            trustChainInvalidCount: v.trustChainInvalidCount ?? 0,
            cscaNotFoundCount: v.cscaNotFoundCount ?? 0,
            chainPathDistribution: Array.isArray(v.chainPathDistribution) ? v.chainPathDistribution : [],
          });
        }
      } catch {
        // stats not critical
      } finally {
        setStatsLoading(false);
      }
    };
    fetchStats();
  }, []);

  // Quick lookup handler
  const handleQuickLookup = useCallback(async () => {
    if (!quickLookupDn && !quickLookupFingerprint) {
      setQuickLookupError('Subject DN 또는 Fingerprint를 입력해주세요.');
      return;
    }
    setQuickLookupLoading(true);
    setQuickLookupError(null);
    setQuickLookupResult(null);
    try {
      const params: { subjectDn?: string; fingerprint?: string } = {};
      if (quickLookupDn) params.subjectDn = quickLookupDn;
      else if (quickLookupFingerprint) params.fingerprint = quickLookupFingerprint;
      const response = await paApi.paLookup(params);
      const data = response.data as QuickLookupResult;
      setQuickLookupResult(data);
      if (!data.success) {
        setQuickLookupError(data.error || '조회 실패');
      } else if (!data.validation) {
        setQuickLookupError('해당 인증서의 검증 결과가 없습니다.');
      }
    } catch (err) {
      setQuickLookupError(err instanceof Error ? err.message : '조회 중 오류가 발생했습니다.');
    } finally {
      setQuickLookupLoading(false);
    }
  }, [quickLookupDn, quickLookupFingerprint]);

  // Sample cert click → fill fingerprint and auto-search
  const handleSampleClick = useCallback((cert: SampleCert) => {
    setQuickLookupDn('');
    setQuickLookupFingerprint(cert.fingerprint);
    setQuickLookupResult(null);
    setQuickLookupError(null);
    // Auto-search
    setTimeout(async () => {
      setQuickLookupLoading(true);
      try {
        const response = await paApi.paLookup({ fingerprint: cert.fingerprint });
        const data = response.data as QuickLookupResult;
        setQuickLookupResult(data);
        if (!data.success) setQuickLookupError(data.error || '조회 실패');
        else if (!data.validation) setQuickLookupError('해당 인증서의 검증 결과가 없습니다.');
      } catch (err) {
        setQuickLookupError(err instanceof Error ? err.message : '조회 오류');
      } finally {
        setQuickLookupLoading(false);
      }
    }, 100);
  }, []);

  // Chain path color assignment based on depth (number of arrows)
  const pathColors = [
    'bg-emerald-500',   // DSC → Root (direct, depth 1)
    'bg-blue-500',      // DSC → Link → Root (depth 2)
    'bg-indigo-500',    // depth 3
    'bg-purple-500',    // depth 4
    'bg-violet-500',    // depth 5+
  ];
  const getPathColor = (path: string) => {
    const depth = (path.match(/(→|->)/g) || []).length;
    return pathColors[Math.min(depth - 1, pathColors.length - 1)] || pathColors[0];
  };
  const formatPath = (path: string) => path.replace(/->/g, '→');

  // Chain pattern data from API chain path distribution
  const chainPatterns: ChainPattern[] = stats ? [
    // Trust Chain 유효 - broken down by path level from API
    ...(stats.chainPathDistribution.length > 0
      ? stats.chainPathDistribution.map((entry) => ({
          label: formatPath(entry.path),
          description: '',
          count: entry.count,
          color: getPathColor(entry.path),
        }))
      : stats.trustChainValidCount > 0
        ? [{ label: 'Trust Chain 유효', description: 'DSC → CSCA 신뢰 체인 검증 성공', count: stats.trustChainValidCount, color: 'bg-emerald-500' }]
        : []
    ),
    // Trust Chain 실패
    { label: 'Trust Chain 실패', description: 'CSCA 존재하나 서명/유효기간 검증 실패', count: stats.trustChainInvalidCount - stats.cscaNotFoundCount, color: 'bg-red-500' },
    // CSCA 미등록
    { label: 'CSCA 미등록', description: 'CSCA 인증서를 찾을 수 없음', count: stats.cscaNotFoundCount, color: 'bg-amber-500' },
  ].filter(p => p.count > 0) : [];

  // validCount from API already includes EXPIRED_VALID, so subtract to get pure VALID
  const pureValidCount = stats ? stats.validCount - stats.expiredValidCount : 0;
  const totalValidated = stats ? stats.validCount + stats.invalidCount + stats.pendingCount : 0;

  const getStatusIcon = (status: string) => {
    switch (status) {
      case 'VALID': return <CheckCircle className="w-3.5 h-3.5 text-emerald-500" />;
      case 'EXPIRED_VALID': return <Clock className="w-3.5 h-3.5 text-amber-500" />;
      case 'INVALID': return <XCircle className="w-3.5 h-3.5 text-red-500" />;
      case 'PENDING': return <AlertTriangle className="w-3.5 h-3.5 text-yellow-500" />;
      default: return null;
    }
  };

  const getStatusColor = (status: string) => {
    switch (status) {
      case 'VALID': return 'text-emerald-600 dark:text-emerald-400';
      case 'EXPIRED_VALID': return 'text-amber-600 dark:text-amber-400';
      case 'INVALID': return 'text-red-600 dark:text-red-400';
      case 'PENDING': return 'text-yellow-600 dark:text-yellow-400';
      default: return 'text-gray-600';
    }
  };

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-indigo-500 to-purple-600 shadow-lg">
            <Link2 className="w-7 h-7 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">
              DSC Trust Chain 보고서
            </h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              DSC 인증서의 Trust Chain 검증 결과 통계 및 샘플 인증서 조회
            </p>
          </div>
        </div>
      </div>

      {/* Statistics Cards */}
      {statsLoading ? (
        <div className="flex items-center justify-center py-8">
          <Loader2 className="w-6 h-6 animate-spin text-indigo-500" />
        </div>
      ) : stats && (
        <>
          <div className="grid grid-cols-2 md:grid-cols-4 gap-3 mb-5">
            <StatCard
              icon={<Shield className="w-5 h-5 text-blue-500" />}
              label="총 검증 결과"
              value={totalValidated.toLocaleString()}
              borderColor="border-blue-500"
            />
            <StatCard
              icon={<CheckCircle className="w-5 h-5 text-emerald-500" />}
              label="VALID"
              value={pureValidCount.toLocaleString()}
              sub={`${totalValidated > 0 ? ((pureValidCount / totalValidated) * 100).toFixed(1) : 0}%`}
              borderColor="border-emerald-500"
            />
            <StatCard
              icon={<Clock className="w-5 h-5 text-amber-500" />}
              label="EXPIRED_VALID"
              value={stats.expiredValidCount.toLocaleString()}
              sub={`${totalValidated > 0 ? ((stats.expiredValidCount / totalValidated) * 100).toFixed(1) : 0}%`}
              borderColor="border-amber-500"
            />
            <StatCard
              icon={<AlertTriangle className="w-5 h-5 text-yellow-500" />}
              label="PENDING (CSCA 미등록)"
              value={stats.pendingCount.toLocaleString()}
              sub={`${totalValidated > 0 ? ((stats.pendingCount / totalValidated) * 100).toFixed(1) : 0}%`}
              borderColor="border-yellow-500"
            />
          </div>

          {/* Trust Chain Distribution */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 mb-5">
            <div className="flex items-center gap-2 mb-4">
              <TrendingUp className="w-5 h-5 text-indigo-500" />
              <h2 className="text-base font-bold text-gray-900 dark:text-white">Trust Chain 분포</h2>
            </div>

            {/* Status Bar */}
            <div className="h-4 rounded-full overflow-hidden flex mb-4 bg-gray-100 dark:bg-gray-700">
              {totalValidated > 0 && (
                <>
                  <div
                    className="bg-emerald-500 transition-all"
                    style={{ width: `${(stats.validCount / totalValidated) * 100}%` }}
                    title={`VALID: ${stats.validCount.toLocaleString()}`}
                  />
                  <div
                    className="bg-amber-500 transition-all"
                    style={{ width: `${(stats.expiredValidCount / totalValidated) * 100}%` }}
                    title={`EXPIRED_VALID: ${stats.expiredValidCount.toLocaleString()}`}
                  />
                  <div
                    className="bg-red-500 transition-all"
                    style={{ width: `${Math.max((stats.invalidCount / totalValidated) * 100, 0.3)}%` }}
                    title={`INVALID: ${stats.invalidCount.toLocaleString()}`}
                  />
                  <div
                    className="bg-yellow-400 transition-all"
                    style={{ width: `${(stats.pendingCount / totalValidated) * 100}%` }}
                    title={`PENDING: ${stats.pendingCount.toLocaleString()}`}
                  />
                </>
              )}
            </div>
            <div className="flex flex-wrap gap-4 text-xs text-gray-600 dark:text-gray-400 mb-5">
              <span className="flex items-center gap-1.5"><span className="w-2.5 h-2.5 rounded-full bg-emerald-500" /> VALID</span>
              <span className="flex items-center gap-1.5"><span className="w-2.5 h-2.5 rounded-full bg-amber-500" /> EXPIRED_VALID</span>
              <span className="flex items-center gap-1.5"><span className="w-2.5 h-2.5 rounded-full bg-red-500" /> INVALID</span>
              <span className="flex items-center gap-1.5"><span className="w-2.5 h-2.5 rounded-full bg-yellow-400" /> PENDING</span>
            </div>

            {/* Chain Pattern Bars */}
            <div className="space-y-3">
              {chainPatterns.map((p) => {
                const maxCount = Math.max(...chainPatterns.map(c => c.count));
                const barWidth = maxCount > 0 ? (p.count / maxCount) * 100 : 0;
                return (
                  <div key={p.label}>
                    <div className="flex items-center justify-between text-sm mb-1">
                      <div>
                        <span className="font-semibold text-gray-800 dark:text-gray-200">{p.label}</span>
                        <span className="ml-2 text-xs text-gray-500 dark:text-gray-400">{p.description}</span>
                      </div>
                      <span className="font-bold text-gray-900 dark:text-white">{p.count.toLocaleString()}</span>
                    </div>
                    <div className="h-3 rounded-full bg-gray-100 dark:bg-gray-700 overflow-hidden">
                      <div className={cn('h-full rounded-full transition-all', p.color)} style={{ width: `${barWidth}%` }} />
                    </div>
                  </div>
                );
              })}
            </div>
          </div>
        </>
      )}

      {/* Sample Certificates */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 mb-5">
        <div className="flex items-center gap-2 mb-4">
          <Globe className="w-5 h-5 text-indigo-500" />
          <h2 className="text-base font-bold text-gray-900 dark:text-white">샘플 인증서 조회</h2>
          <span className="text-xs text-gray-500 dark:text-gray-400 ml-1">클릭하면 Trust Chain 결과를 즉시 조회합니다</span>
        </div>

        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-5 gap-2">
          {SAMPLE_CERTS.map((cert) => (
            <button
              key={cert.fingerprint}
              onClick={() => handleSampleClick(cert)}
              className={cn(
                'flex items-center gap-2.5 px-3 py-2.5 rounded-xl border text-left transition-all hover:shadow-md',
                'bg-gray-50 dark:bg-gray-700/50 border-gray-200 dark:border-gray-600',
                'hover:border-indigo-300 dark:hover:border-indigo-600 hover:bg-indigo-50 dark:hover:bg-indigo-900/20',
                quickLookupFingerprint === cert.fingerprint && 'border-indigo-400 dark:border-indigo-500 bg-indigo-50 dark:bg-indigo-900/20 ring-1 ring-indigo-300'
              )}
            >
              {getFlagSvgPath(cert.country) && (
                <img
                  src={getFlagSvgPath(cert.country)}
                  alt={cert.country}
                  className="w-6 h-4 object-cover rounded shadow-sm border border-gray-200 dark:border-gray-600 flex-shrink-0"
                  onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }}
                />
              )}
              <div className="min-w-0 flex-1">
                <div className="flex items-center gap-1.5">
                  {getStatusIcon(cert.status)}
                  <span className={cn('text-xs font-semibold', getStatusColor(cert.status))}>{cert.label}</span>
                </div>
                <div className="flex items-center gap-1 mt-0.5 text-[10px] text-gray-500 dark:text-gray-400">
                  <ArrowRight className="w-2.5 h-2.5" />
                  {cert.chainPattern}
                </div>
              </div>
              <Search className="w-3.5 h-3.5 text-gray-400 flex-shrink-0" />
            </button>
          ))}
        </div>
      </div>

      {/* Trust Chain Lookup (reuse QuickLookupPanel) */}
      <QuickLookupPanel
        quickLookupDn={quickLookupDn}
        setQuickLookupDn={setQuickLookupDn}
        quickLookupFingerprint={quickLookupFingerprint}
        setQuickLookupFingerprint={setQuickLookupFingerprint}
        quickLookupResult={quickLookupResult}
        quickLookupLoading={quickLookupLoading}
        quickLookupError={quickLookupError}
        setQuickLookupError={setQuickLookupError}
        handleQuickLookup={handleQuickLookup}
      />
    </div>
  );
}

// Stat card sub-component
function StatCard({ icon, label, value, sub, borderColor }: {
  icon: React.ReactNode;
  label: string;
  value: string;
  sub?: string;
  borderColor: string;
}) {
  return (
    <div className={cn('bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4', borderColor)}>
      <div className="flex items-center gap-3">
        <div className="p-2 rounded-lg bg-gray-50 dark:bg-gray-700/50">
          {icon}
        </div>
        <div>
          <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">{label}</p>
          <p className="text-xl font-bold text-gray-900 dark:text-white">{value}</p>
          {sub && <p className="text-xs text-gray-400">{sub}</p>}
        </div>
      </div>
    </div>
  );
}

export default TrustChainValidationReport;
