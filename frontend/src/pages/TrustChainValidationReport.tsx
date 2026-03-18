import { useTranslation } from 'react-i18next';
import { useState, useEffect } from 'react';
import {
  Link2,
  CheckCircle,
  AlertTriangle,
  Clock,
  Loader2,
  Search,
  ArrowRight,
  Globe,
  Shield,
  TrendingUp,
} from 'lucide-react';
import { cn } from '@/utils/cn';
import { getFlagSvgPath } from '@/utils/countryCode';
import { getCountryName } from '@/utils/countryNames';
import { GlossaryTerm } from '@/components/common';
import axios from 'axios';

// Trust Chain 분포 데이터
interface ChainPattern {
  label: string;
  description: string;
  count: number;
  color: string;
}

// Chain path distribution from API
interface ChainPathSample {
  fingerprint: string;
  country: string;
}

interface ChainPathEntry {
  path: string;
  count: number;
  samples?: ChainPathSample[];
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
  const { t } = useTranslation(['report', 'common']);
  // Statistics
  const [stats, setStats] = useState<ValidationStats | null>(null);
  const [statsLoading, setStatsLoading] = useState(true);


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
        ? [{ label: t('report:trustChain.trustChainValid'), description: t('report:trustChain.trustChainValidDesc'), count: stats.trustChainValidCount, color: 'bg-emerald-500' }]
        : []
    ),
    // Trust Chain 실패
    { label: t('report:trustChain.trustChainFailed'), description: t('report:trustChain.trustChainFailedDesc'), count: stats.trustChainInvalidCount - stats.cscaNotFoundCount, color: 'bg-red-500' },
    { label: t('report:trustChain.cscaNotFound'), description: t('report:trustChain.cscaNotFoundDesc'), count: stats.cscaNotFoundCount, color: 'bg-amber-500' },
  ].filter(p => p.count > 0) : [];

  // validCount from API already includes EXPIRED_VALID, so subtract to get pure VALID
  const pureValidCount = stats ? stats.validCount - stats.expiredValidCount : 0;
  const totalValidated = stats ? stats.validCount + stats.invalidCount + stats.pendingCount : 0;


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
              {t('report:trustChain.title')}
            </h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              {t('report:trustChain.reportSubtitle')}
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
              label={t('report:trustChain.totalValidationResults')}
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
              label={t('report:trustChain.pendingCscaNotFound')}
              value={stats.pendingCount.toLocaleString()}
              sub={`${totalValidated > 0 ? ((stats.pendingCount / totalValidated) * 100).toFixed(1) : 0}%`}
              borderColor="border-yellow-500"
            />
          </div>

          {/* Trust Chain Distribution */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 mb-5">
            <div className="flex items-center gap-2 mb-4">
              <TrendingUp className="w-5 h-5 text-indigo-500" />
              <h2 className="text-base font-bold text-gray-900 dark:text-white"><GlossaryTerm term="Trust Chain" label={t('report:trustChain.trustChainDistribution')} /></h2>
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

      {/* Sample Certificates - dynamically from API */}
      {stats && stats.chainPathDistribution.some(e => e.samples && e.samples.length > 0) && (
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 mb-5">
          <div className="flex items-center gap-2 mb-4">
            <Globe className="w-5 h-5 text-indigo-500" />
            <h2 className="text-base font-bold text-gray-900 dark:text-white">{ t('report:trustChain.sampleCertificates') }</h2>
            <span className="text-xs text-gray-500 dark:text-gray-400 ml-1">{t('report:trustChain.sampleCertificatesDesc')}</span>
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-2">
            {stats.chainPathDistribution.flatMap((entry) =>
              (entry.samples || []).map((sample) => (
                <a
                  key={sample.fingerprint}
                  href={`/pkd/certificates?fingerprint=${sample.fingerprint}`}
                  className={cn(
                    'flex items-center gap-2.5 px-3 py-2.5 rounded-xl border text-left transition-all hover:shadow-md',
                    'bg-gray-50 dark:bg-gray-700/50 border-gray-200 dark:border-gray-600',
                    'hover:border-indigo-300 dark:hover:border-indigo-600 hover:bg-indigo-50 dark:hover:bg-indigo-900/20',
                  )}
                >
                  {getFlagSvgPath(sample.country) && (
                    <img
                      src={getFlagSvgPath(sample.country)}
                      alt={sample.country}
                      title={getCountryName(sample.country)}
                      className="w-6 h-4 object-cover rounded shadow-sm border border-gray-200 dark:border-gray-600 flex-shrink-0"
                      onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }}
                    />
                  )}
                  <div className="min-w-0 flex-1">
                    <div className="flex items-center gap-1.5">
                      <CheckCircle className="w-3.5 h-3.5 text-emerald-500" />
                      <span className="text-xs font-semibold text-emerald-600 dark:text-emerald-400">
                        {getCountryName(sample.country)} DSC
                      </span>
                    </div>
                    <div className="flex items-center gap-1 mt-0.5 text-xs text-gray-500 dark:text-gray-400">
                      <ArrowRight className="w-2.5 h-2.5" />
                      {formatPath(entry.path)}
                    </div>
                  </div>
                  <Search className="w-3.5 h-3.5 text-gray-400 flex-shrink-0" />
                </a>
              ))
            )}
          </div>
        </div>
      )}
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
    <div className={cn('bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 border-l-4', borderColor)}>
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
