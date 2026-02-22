import { useState, useEffect, useMemo } from 'react';
import {
  Brain,
  AlertCircle,
  AlertTriangle,
  CheckCircle,
  Shield,
  ShieldAlert,
  Loader2,
  RefreshCw,
  Play,
  Globe,
  TrendingUp,
  ChevronLeft,
  ChevronRight,
  Filter,
  Download,
  Key,
} from 'lucide-react';
import {
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
  AreaChart,
  Area,
  Legend,
  PieChart,
  Pie,
  Cell,
} from 'recharts';
import { aiAnalysisApi } from '@/services/aiAnalysisApi';
import type {
  AnalysisStatistics,
  AnalysisJobStatus,
  CertificateAnalysis,
  CountryMaturity,
  AlgorithmTrend,
  RiskDistribution,
  KeySizeDistribution,
  ForensicSummary,
} from '@/services/aiAnalysisApi';
import { cn } from '@/utils/cn';
import { getFlagSvgPath } from '@/utils/countryCode';
import { exportAiAnalysisReportToCsv } from '@/utils/csvExport';
import IssuerProfileCard from '@/components/ai/IssuerProfileCard';
import ExtensionComplianceChecklist from '@/components/ai/ExtensionComplianceChecklist';
import countries from 'i18n-iso-countries';
import ko from 'i18n-iso-countries/langs/ko.json';

countries.registerLocale(ko);

const getCountryName = (code: string): string => {
  return countries.getName(code, 'ko') || code;
};

const ANOMALY_LABELS_KO: Record<string, string> = {
  NORMAL: '정상',
  SUSPICIOUS: '의심',
  ANOMALOUS: '이상',
};

const RISK_LABELS_KO: Record<string, string> = {
  LOW: '낮음',
  MEDIUM: '보통',
  HIGH: '높음',
  CRITICAL: '심각',
};

const RISK_BAR_CONFIG: Record<string, { color: string; tw: string }> = {
  LOW: { color: '#22c55e', tw: 'bg-green-500' },
  MEDIUM: { color: '#eab308', tw: 'bg-yellow-500' },
  HIGH: { color: '#f97316', tw: 'bg-orange-500' },
  CRITICAL: { color: '#ef4444', tw: 'bg-red-500' },
};

const ALGORITHM_GROUPS: Record<string, string> = {
  'sha256WithRSAEncryption': 'RSA-SHA256',
  'sha384WithRSAEncryption': 'RSA-SHA384',
  'sha512WithRSAEncryption': 'RSA-SHA512',
  'sha1WithRSAEncryption': 'RSA-SHA1',
  'ecdsa-with-SHA256': 'ECDSA-SHA256',
  'ecdsa-with-SHA384': 'ECDSA-SHA384',
  'ecdsa-with-SHA512': 'ECDSA-SHA512',
  'id-RSASSA-PSS': 'RSA-PSS',
};

const TREND_COLORS = [
  '#3b82f6', '#22c55e', '#eab308', '#f97316', '#ef4444',
  '#8b5cf6', '#06b6d4', '#ec4899', '#84cc16', '#f59e0b',
];

const PIE_COLORS = ['#3b82f6', '#10b981', '#f59e0b', '#ef4444', '#8b5cf6', '#ec4899', '#06b6d4', '#84cc16', '#f97316', '#a855f7'];

export default function AiAnalysisDashboard() {
  const [stats, setStats] = useState<AnalysisStatistics | null>(null);
  const [jobStatus, setJobStatus] = useState<AnalysisJobStatus | null>(null);
  const [anomalies, setAnomalies] = useState<CertificateAnalysis[]>([]);
  const [anomalyTotal, setAnomalyTotal] = useState(0);
  const [maturity, setMaturity] = useState<CountryMaturity[]>([]);
  const [trends, setTrends] = useState<AlgorithmTrend[]>([]);
  const [riskDist, setRiskDist] = useState<RiskDistribution[]>([]);
  const [keySizeDist, setKeySizeDist] = useState<KeySizeDistribution[]>([]);
  const [forensicSummary, setForensicSummary] = useState<ForensicSummary | null>(null);
  const [loading, setLoading] = useState(true);
  const [analyzing, setAnalyzing] = useState(false);

  // Filters
  const [filterCountry, setFilterCountry] = useState('');
  const [filterType, setFilterType] = useState('');
  const [filterLabel, setFilterLabel] = useState('');
  const [filterRisk, setFilterRisk] = useState('');
  const [page, setPage] = useState(1);
  const pageSize = 15;

  const fetchData = async () => {
    setLoading(true);
    try {
      const [statsRes, statusRes, maturityRes, trendsRes, riskRes, keySizeRes, forensicRes] = await Promise.allSettled([
        aiAnalysisApi.getStatistics(),
        aiAnalysisApi.getAnalysisStatus(),
        aiAnalysisApi.getCountryMaturity(),
        aiAnalysisApi.getAlgorithmTrends(),
        aiAnalysisApi.getRiskDistribution(),
        aiAnalysisApi.getKeySizeDistribution(),
        aiAnalysisApi.getForensicSummary(),
      ]);

      if (statsRes.status === 'fulfilled') setStats(statsRes.value.data);
      if (statusRes.status === 'fulfilled') setJobStatus(statusRes.value.data);
      if (maturityRes.status === 'fulfilled') setMaturity(maturityRes.value.data);
      if (trendsRes.status === 'fulfilled') setTrends(trendsRes.value.data);
      if (riskRes.status === 'fulfilled') setRiskDist(riskRes.value.data);
      if (keySizeRes.status === 'fulfilled') setKeySizeDist(keySizeRes.value.data);
      if (forensicRes.status === 'fulfilled') setForensicSummary(forensicRes.value.data);
    } catch (e) {
      console.error('Failed to fetch AI analysis data', e);
    }
    setLoading(false);
  };

  const fetchAnomalies = async () => {
    try {
      const params: Record<string, string | number> = { page, size: pageSize };
      if (filterCountry) params.country = filterCountry;
      if (filterType) params.type = filterType;
      if (filterLabel) params.label = filterLabel;
      if (filterRisk) params.risk_level = filterRisk;

      const res = await aiAnalysisApi.getAnomalies(params);
      setAnomalies(res.data.items);
      setAnomalyTotal(res.data.total);
    } catch {
      // Analysis might not have run yet
    }
  };

  useEffect(() => {
    fetchData();
  }, []);

  useEffect(() => {
    fetchAnomalies();
  }, [page, filterCountry, filterType, filterLabel, filterRisk]);

  // Polling during analysis
  useEffect(() => {
    if (!analyzing) return;
    const interval = setInterval(async () => {
      try {
        const res = await aiAnalysisApi.getAnalysisStatus();
        setJobStatus(res.data);
        if (res.data.status === 'COMPLETED' || res.data.status === 'FAILED') {
          setAnalyzing(false);
          fetchData();
          fetchAnomalies();
        }
      } catch {
        /* ignore */
      }
    }, 3000);
    return () => clearInterval(interval);
  }, [analyzing]);

  const handleTriggerAnalysis = async () => {
    try {
      await aiAnalysisApi.triggerAnalysis();
      setAnalyzing(true);
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : '분석 시작 실패';
      alert(msg);
    }
  };

  const handleExportCsv = async () => {
    try {
      const params: Record<string, string | number> = { page: 1, size: 1000 };
      if (filterCountry) params.country = filterCountry;
      if (filterType) params.type = filterType;
      if (filterLabel) params.label = filterLabel;
      if (filterRisk) params.risk_level = filterRisk;

      const res = await aiAnalysisApi.getAnomalies(params);
      const timestamp = new Date().toISOString().slice(0, 10);
      exportAiAnalysisReportToCsv(res.data.items, `ai-analysis-report-${timestamp}.csv`);
    } catch (err) {
      if (import.meta.env.DEV) console.error('CSV export failed:', err);
    }
  };

  const totalPages = Math.ceil(anomalyTotal / pageSize);
  const hasActiveFilters = filterCountry || filterType || filterLabel || filterRisk;

  // Prepare trend chart data
  const trendData = useMemo(() => {
    const algSet = new Set<string>();
    trends.forEach((t) => {
      Object.keys(t.algorithms).forEach((a) => algSet.add(ALGORITHM_GROUPS[a] || a));
    });
    return trends.map((t) => {
      const row: Record<string, number | string> = { year: t.year };
      Object.entries(t.algorithms).forEach(([alg, count]) => {
        const name = ALGORITHM_GROUPS[alg] || alg;
        row[name] = (row[name] as number || 0) + count;
      });
      return row;
    });
  }, [trends]);

  const trendAlgorithms = useMemo(() => {
    const set = new Set<string>();
    trendData.forEach((row) => {
      Object.keys(row).forEach((k) => {
        if (k !== 'year') set.add(k);
      });
    });
    return Array.from(set).sort();
  }, [trendData]);

  // Pie chart data for key size distribution
  const keySizePieData = useMemo(() => {
    return keySizeDist
      .map((d) => ({
        name: `${d.algorithm} ${d.key_size}bit`,
        value: d.count,
        percentage: d.percentage,
      }))
      .sort((a, b) => b.value - a.value)
      .slice(0, 10);
  }, [keySizeDist]);

  // Top 15 countries for maturity chart
  const top15Maturity = maturity.slice(0, 15);

  const isRunning = jobStatus?.status === 'RUNNING' || analyzing;

  if (loading) {
    return (
      <div className="w-full px-4 lg:px-6 py-4 flex items-center justify-center py-20">
        <Loader2 className="w-8 h-8 animate-spin text-purple-500" />
      </div>
    );
  }

  return (
    <div className="w-full px-4 lg:px-6 py-4 space-y-6">
      {/* Header */}
      <PageHeader
        onRefresh={fetchData}
        onExport={handleExportCsv}
        onAnalyze={handleTriggerAnalysis}
        loading={loading}
        isRunning={isRunning}
        jobStatus={jobStatus}
        lastAnalysisAt={stats?.last_analysis_at || null}
      />

      {/* Progress bar during analysis */}
      {isRunning && jobStatus && (
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-purple-500">
          <div className="flex items-center gap-2 mb-3">
            <Loader2 className="w-5 h-5 text-purple-500 animate-spin" />
            <h3 className="text-base font-bold text-gray-900 dark:text-white">분석 진행 중</h3>
            <span className="ml-auto text-sm text-purple-600 dark:text-purple-400 font-medium">
              {jobStatus.processed_certificates.toLocaleString()} / {jobStatus.total_certificates.toLocaleString()} 인증서
            </span>
          </div>
          <div className="w-full bg-purple-100 dark:bg-purple-900/30 rounded-full h-3 shadow-inner">
            <div
              className="bg-gradient-to-r from-purple-500 to-violet-600 h-3 rounded-full transition-all duration-500"
              style={{ width: `${Math.round(jobStatus.progress * 100)}%` }}
            />
          </div>
          <p className="text-xs text-gray-500 dark:text-gray-400 mt-2">
            {Math.round(jobStatus.progress * 100)}% 완료
          </p>
        </div>
      )}

      {/* Summary Cards */}
      {stats && (
        <div className="grid grid-cols-2 lg:grid-cols-4 gap-4">
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-blue-500">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm font-medium text-gray-500 dark:text-gray-400">총 분석</p>
                <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                  {stats.total_analyzed.toLocaleString()}
                </p>
                <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">분석된 인증서</p>
              </div>
              <div className="p-3 rounded-xl bg-blue-50 dark:bg-blue-900/30">
                <Brain className="w-8 h-8 text-blue-500" />
              </div>
            </div>
          </div>

          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-green-500">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm font-medium text-gray-500 dark:text-gray-400">정상</p>
                <p className="text-3xl font-bold text-green-600 dark:text-green-400 mt-1">
                  {stats.normal_count.toLocaleString()}
                </p>
                <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">이상 없음</p>
              </div>
              <div className="p-3 rounded-xl bg-green-50 dark:bg-green-900/30">
                <CheckCircle className="w-8 h-8 text-green-500" />
              </div>
            </div>
          </div>

          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-amber-500">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm font-medium text-gray-500 dark:text-gray-400">의심</p>
                <p className="text-3xl font-bold text-amber-600 dark:text-amber-400 mt-1">
                  {stats.suspicious_count.toLocaleString()}
                </p>
                <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">추가 확인 필요</p>
              </div>
              <div className="p-3 rounded-xl bg-amber-50 dark:bg-amber-900/30">
                <AlertTriangle className="w-8 h-8 text-amber-500" />
              </div>
            </div>
          </div>

          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-red-500">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm font-medium text-gray-500 dark:text-gray-400">이상</p>
                <p className="text-3xl font-bold text-red-600 dark:text-red-400 mt-1">
                  {stats.anomalous_count.toLocaleString()}
                </p>
                <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">이상 탐지됨</p>
              </div>
              <div className="p-3 rounded-xl bg-red-50 dark:bg-red-900/30">
                <ShieldAlert className="w-8 h-8 text-red-500" />
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Risk Level Proportional Bar */}
      {riskDist.length > 0 && (
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5">
          <div className="flex items-center justify-between mb-3">
            <div className="flex items-center gap-2">
              <Shield className="w-5 h-5 text-purple-500" />
              <h3 className="text-base font-bold text-gray-900 dark:text-white">위험 수준 분포</h3>
            </div>
            {stats && (
              <span className="text-sm text-gray-500 dark:text-gray-400">
                평균 위험 점수: <span className="font-bold text-purple-600 dark:text-purple-400">{stats.avg_risk_score}</span>
              </span>
            )}
          </div>
          <div className="flex rounded-full overflow-hidden h-7 shadow-inner">
            {riskDist.map((r) => {
              const cfg = RISK_BAR_CONFIG[r.risk_level];
              return (
                <div
                  key={r.risk_level}
                  className={cn(cfg?.tw || 'bg-gray-400', 'flex items-center justify-center text-xs font-medium text-white transition-all')}
                  style={{ width: `${r.percentage}%` }}
                  title={`${RISK_LABELS_KO[r.risk_level] || r.risk_level}: ${r.count.toLocaleString()} (${r.percentage}%)`}
                >
                  {r.percentage >= 8 && `${RISK_LABELS_KO[r.risk_level]} ${r.count.toLocaleString()}`}
                </div>
              );
            })}
          </div>
          <div className="flex gap-5 mt-3">
            {riskDist.map((r) => {
              const cfg = RISK_BAR_CONFIG[r.risk_level];
              return (
                <span key={r.risk_level} className="flex items-center gap-1.5 text-xs text-gray-600 dark:text-gray-400">
                  <span className={cn('w-2.5 h-2.5 rounded-full', cfg?.tw || 'bg-gray-400')} />
                  {RISK_LABELS_KO[r.risk_level]}: {r.count.toLocaleString()} ({r.percentage}%)
                </span>
              );
            })}
          </div>
        </div>
      )}

      {/* Forensic Risk Summary */}
      {forensicSummary && (
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5">
          <div className="flex items-center justify-between mb-3">
            <div className="flex items-center gap-2">
              <ShieldAlert className="w-5 h-5 text-orange-500" />
              <h3 className="text-base font-bold text-gray-900 dark:text-white">포렌식 분석 요약</h3>
            </div>
            {stats && stats.avg_forensic_score != null && (
              <span className="text-sm text-gray-500 dark:text-gray-400">
                평균 포렌식 점수: <span className="font-bold text-orange-600 dark:text-orange-400">{stats.avg_forensic_score!.toFixed(1)}</span>
              </span>
            )}
          </div>
          {/* Forensic level proportional bar */}
          {forensicSummary.forensic_level_distribution && Object.keys(forensicSummary.forensic_level_distribution).length > 0 && (
            <>
              <div className="flex rounded-full overflow-hidden h-7 shadow-inner">
                {['LOW', 'MEDIUM', 'HIGH', 'CRITICAL'].map(level => {
                  const count = forensicSummary.forensic_level_distribution[level] || 0;
                  const total = Object.values(forensicSummary.forensic_level_distribution).reduce<number>((a, b) => a + b, 0);
                  const pct = total > 0 ? (count / total) * 100 : 0;
                  if (pct === 0) return null;
                  const cfg = RISK_BAR_CONFIG[level];
                  return (
                    <div
                      key={level}
                      className={cn(cfg?.tw || 'bg-gray-400', 'flex items-center justify-center text-xs font-medium text-white transition-all')}
                      style={{ width: `${pct}%` }}
                      title={`${RISK_LABELS_KO[level] || level}: ${count.toLocaleString()} (${pct.toFixed(1)}%)`}
                    >
                      {pct >= 8 && `${RISK_LABELS_KO[level]} ${count.toLocaleString()}`}
                    </div>
                  );
                })}
              </div>
              <div className="flex gap-5 mt-3">
                {['LOW', 'MEDIUM', 'HIGH', 'CRITICAL'].map(level => {
                  const count = forensicSummary.forensic_level_distribution[level] || 0;
                  if (count === 0) return null;
                  const total = Object.values(forensicSummary.forensic_level_distribution).reduce<number>((a, b) => a + b, 0);
                  const pct = total > 0 ? ((count / total) * 100).toFixed(1) : '0';
                  const cfg = RISK_BAR_CONFIG[level];
                  return (
                    <span key={level} className="flex items-center gap-1.5 text-xs text-gray-600 dark:text-gray-400">
                      <span className={cn('w-2.5 h-2.5 rounded-full', cfg?.tw || 'bg-gray-400')} />
                      {RISK_LABELS_KO[level]}: {count.toLocaleString()} ({pct}%)
                    </span>
                  );
                })}
              </div>
            </>
          )}
          {/* Top findings */}
          {forensicSummary.top_findings && forensicSummary.top_findings.length > 0 && (
            <div className="mt-4 space-y-1.5">
              <h4 className="text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">주요 발견 사항</h4>
              {forensicSummary.top_findings.slice(0, 5).map((f, i) => (
                <div key={i} className="flex items-center gap-2 text-xs">
                  <span className="px-1.5 py-0.5 rounded font-medium text-orange-600 bg-orange-50 dark:text-orange-400 dark:bg-orange-900/20">
                    #{i + 1}
                  </span>
                  <span className="text-gray-600 dark:text-gray-400">{f.message}</span>
                  <span className="text-gray-400 dark:text-gray-500">({f.count}건)</span>
                </div>
              ))}
            </div>
          )}
        </div>
      )}

      {/* Issuer Profile + Extension Compliance */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <IssuerProfileCard />
        <ExtensionComplianceChecklist />
      </div>

      {/* Country PKI Maturity Chart (full width) */}
      {top15Maturity.length > 0 && (
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
          <div className="flex items-center gap-2 mb-4">
            <Globe className="w-5 h-5 text-purple-500" />
            <h3 className="text-base font-bold text-gray-900 dark:text-white">국가별 PKI 성숙도</h3>
            <span className="text-sm text-gray-500 dark:text-gray-400">(상위 15개국)</span>
          </div>
          <ResponsiveContainer width="100%" height={Math.max(300, top15Maturity.length * 32)}>
            <BarChart data={top15Maturity} layout="vertical" margin={{ left: 70, right: 20, top: 5, bottom: 5 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#374151" opacity={0.2} />
              <XAxis type="number" domain={[0, 100]} tick={{ fill: '#6B7280', fontSize: 12 }} />
              <YAxis
                type="category"
                dataKey="country_code"
                width={65}
                tick={((props: any) => {
                  const { x, y, payload } = props;
                  const code = payload.value;
                  const flagPath = getFlagSvgPath(code);
                  return (
                    <g transform={`translate(${x},${y})`}>
                      {flagPath && (
                        <image href={flagPath} x={-62} y={-8} width={20} height={14} style={{ borderRadius: 2 }} />
                      )}
                      <text x={-8} y={4} textAnchor="end" fill="#6B7280" fontSize={12}>
                        {code}
                      </text>
                    </g>
                  );
                }) as any}
              />
              <Tooltip
                content={({ active, payload }) => {
                  if (!active || !payload?.length) return null;
                  const item = payload[0].payload as CountryMaturity;
                  const flagPath = getFlagSvgPath(item.country_code);
                  return (
                    <div className="bg-gray-900 dark:bg-gray-700 border border-gray-700 dark:border-gray-600 rounded-lg px-3 py-2 shadow-lg">
                      <div className="flex items-center gap-2 mb-1.5">
                        {flagPath && <img src={flagPath} alt={item.country_code} className="w-5 h-3.5 object-cover rounded-sm shadow-sm" />}
                        <span className="text-sm font-semibold text-white">{item.country_code}</span>
                        <span className="text-xs text-gray-400">{getCountryName(item.country_code)}</span>
                      </div>
                      <div className="text-xs text-gray-300 space-y-0.5">
                        <div>성숙도: <span className="font-bold text-white">{item.maturity_score.toFixed(1)}점</span></div>
                        <div className="flex gap-3 mt-1">
                          <span>알고리즘: {item.algorithm_score.toFixed(0)}</span>
                          <span>키크기: {item.key_size_score.toFixed(0)}</span>
                          <span>준수: {item.compliance_score.toFixed(0)}</span>
                        </div>
                        <div className="flex gap-3">
                          <span>확장: {item.extension_score.toFixed(0)}</span>
                          <span>최신성: {item.freshness_score.toFixed(0)}</span>
                          <span className="text-gray-400">({item.certificate_count}건)</span>
                        </div>
                      </div>
                    </div>
                  );
                }}
              />
              <Bar dataKey="maturity_score" fill="#8b5cf6" radius={[0, 4, 4, 0]} />
            </BarChart>
          </ResponsiveContainer>
        </div>
      )}

      {/* Charts Row: Algorithm Trends + Key Size Pie */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        {/* Algorithm Migration Trends */}
        {trendData.length > 0 && (
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
            <div className="flex items-center gap-2 mb-4">
              <TrendingUp className="w-5 h-5 text-blue-500" />
              <h3 className="text-base font-bold text-gray-900 dark:text-white">알고리즘 마이그레이션 트렌드</h3>
            </div>
            <ResponsiveContainer width="100%" height={300}>
              <AreaChart data={trendData} margin={{ left: 10, right: 20, top: 5, bottom: 5 }}>
                <CartesianGrid strokeDasharray="3 3" stroke="#374151" opacity={0.2} />
                <XAxis dataKey="year" tick={{ fill: '#6B7280', fontSize: 11 }} />
                <YAxis tick={{ fill: '#6B7280', fontSize: 11 }} />
                <Tooltip
                  contentStyle={{ backgroundColor: '#1F2937', border: '1px solid #374151', borderRadius: '8px' }}
                  labelStyle={{ color: '#F3F4F6' }}
                  itemStyle={{ color: '#F3F4F6' }}
                />
                <Legend wrapperStyle={{ fontSize: 11 }} />
                {trendAlgorithms.map((alg, idx) => (
                  <Area
                    key={alg}
                    type="monotone"
                    dataKey={alg}
                    stackId="1"
                    stroke={TREND_COLORS[idx % TREND_COLORS.length]}
                    fill={TREND_COLORS[idx % TREND_COLORS.length]}
                    fillOpacity={0.6}
                  />
                ))}
              </AreaChart>
            </ResponsiveContainer>
          </div>
        )}

        {/* Key Size Distribution Pie */}
        {keySizePieData.length > 0 && (
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
            <div className="flex items-center gap-2 mb-4">
              <Key className="w-5 h-5 text-violet-500" />
              <h3 className="text-base font-bold text-gray-900 dark:text-white">키 크기 분포</h3>
            </div>
            <ResponsiveContainer width="100%" height={300}>
              <PieChart>
                <Pie
                  data={keySizePieData}
                  dataKey="value"
                  nameKey="name"
                  cx="50%"
                  cy="50%"
                  outerRadius={100}
                  label={((props: any) => {
                    const pct = Number(props.percentage || props.percent * 100 || 0);
                    return pct >= 3 ? `${props.name} ${pct.toFixed(0)}%` : '';
                  }) as any}
                  labelLine={false}
                >
                  {keySizePieData.map((_, i) => (
                    <Cell key={i} fill={PIE_COLORS[i % PIE_COLORS.length]} />
                  ))}
                </Pie>
                <Tooltip
                  contentStyle={{ backgroundColor: '#1F2937', border: '1px solid #374151', borderRadius: '8px' }}
                  labelStyle={{ color: '#F3F4F6' }}
                  itemStyle={{ color: '#F3F4F6' }}
                  formatter={(value?: number | string) => [`${Number(value ?? 0).toLocaleString()}건`, '인증서']}
                />
              </PieChart>
            </ResponsiveContainer>
          </div>
        )}
      </div>

      {/* Filters */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4">
        <div className="flex items-center gap-2 mb-3">
          <Filter className="w-4 h-4 text-purple-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">검색 필터</h3>
        </div>
        <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">국가</label>
            <select
              value={filterCountry}
              onChange={(e) => { setFilterCountry(e.target.value); setPage(1); }}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 dark:text-gray-200 focus:outline-none focus:ring-2 focus:ring-purple-500"
            >
              <option value="">전체</option>
              {stats?.top_anomalous_countries.map((c) => (
                <option key={c.country} value={c.country}>
                  {c.country} - {getCountryName(c.country)} ({c.total})
                </option>
              ))}
            </select>
          </div>
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">인증서 유형</label>
            <select
              value={filterType}
              onChange={(e) => { setFilterType(e.target.value); setPage(1); }}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 dark:text-gray-200 focus:outline-none focus:ring-2 focus:ring-purple-500"
            >
              <option value="">전체</option>
              <option value="CSCA">CSCA</option>
              <option value="DSC">DSC</option>
              <option value="DSC_NC">DSC_NC</option>
              <option value="MLSC">MLSC</option>
            </select>
          </div>
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">이상 수준</label>
            <select
              value={filterLabel}
              onChange={(e) => { setFilterLabel(e.target.value); setPage(1); }}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 dark:text-gray-200 focus:outline-none focus:ring-2 focus:ring-purple-500"
            >
              <option value="">전체</option>
              <option value="ANOMALOUS">이상</option>
              <option value="SUSPICIOUS">의심</option>
              <option value="NORMAL">정상</option>
            </select>
          </div>
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">위험 수준</label>
            <select
              value={filterRisk}
              onChange={(e) => { setFilterRisk(e.target.value); setPage(1); }}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 dark:text-gray-200 focus:outline-none focus:ring-2 focus:ring-purple-500"
            >
              <option value="">전체</option>
              <option value="CRITICAL">심각</option>
              <option value="HIGH">높음</option>
              <option value="MEDIUM">보통</option>
              <option value="LOW">낮음</option>
            </select>
          </div>
        </div>
        <div className="flex items-center mt-3">
          {hasActiveFilters && (
            <button
              onClick={() => {
                setFilterCountry('');
                setFilterType('');
                setFilterLabel('');
                setFilterRisk('');
                setPage(1);
              }}
              className="px-3 py-2 text-xs font-medium text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200 border border-gray-200 dark:border-gray-600 rounded-lg hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors"
            >
              초기화
            </button>
          )}
          <span className="ml-auto text-sm text-gray-500 dark:text-gray-400">
            {anomalyTotal.toLocaleString()}건
          </span>
        </div>
      </div>

      {/* Certificate Table */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        {/* Table Header */}
        <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center gap-2">
          <Brain className="w-4 h-4 text-purple-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">인증서 분석 결과</h3>
          <span className="px-2 py-0.5 text-xs rounded-full bg-purple-100 dark:bg-purple-900/30 text-purple-600 dark:text-purple-400">
            {anomalyTotal.toLocaleString()}건
          </span>
        </div>

        <div className="overflow-x-auto">
          <table className="w-full">
            <thead className="bg-gray-50 dark:bg-gray-700/50">
              <tr>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">국가</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">유형</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">이상 점수</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">이상 수준</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">위험 점수</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">위험 수준</th>
                <th className="px-5 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">주요 위험 요인</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">분석 시간</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
              {anomalies.length === 0 ? (
                <tr>
                  <td colSpan={8} className="px-5 py-16 text-center">
                    <div className="flex flex-col items-center text-gray-500 dark:text-gray-400">
                      <AlertCircle className="w-12 h-12 mb-4 opacity-50" />
                      <p className="text-lg font-medium">
                        {stats?.total_analyzed === 0 ? '분석을 먼저 실행해 주세요' : '검색 결과 없음'}
                      </p>
                      <p className="text-sm mt-1">
                        {stats?.total_analyzed === 0 ? '상단의 "분석 실행" 버튼을 클릭하세요' : '필터 조건을 변경해 보세요'}
                      </p>
                    </div>
                  </td>
                </tr>
              ) : (
                anomalies.map((item) => (
                  <tr key={item.fingerprint} className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors">
                    <td className="px-5 py-3 whitespace-nowrap">
                      <div className="flex items-center justify-center gap-2">
                        {item.country_code && getFlagSvgPath(item.country_code) && (
                          <img
                            src={getFlagSvgPath(item.country_code)}
                            alt={item.country_code}
                            className="w-6 h-4 object-cover rounded shadow-sm border border-gray-300 dark:border-gray-500"
                            onError={(e) => { e.currentTarget.style.display = 'none'; }}
                          />
                        )}
                        <span className="text-sm font-medium text-gray-900 dark:text-white">{item.country_code || '-'}</span>
                      </div>
                    </td>
                    <td className="px-5 py-3 text-center whitespace-nowrap">
                      <span className="px-2 py-0.5 rounded text-xs font-medium bg-gray-100 dark:bg-gray-700 text-gray-700 dark:text-gray-300">
                        {item.certificate_type || '-'}
                      </span>
                    </td>
                    <td className="px-5 py-3 text-center whitespace-nowrap font-mono text-sm">
                      {item.anomaly_score.toFixed(3)}
                    </td>
                    <td className="px-5 py-3 text-center whitespace-nowrap">
                      <AnomalyBadge label={item.anomaly_label} />
                    </td>
                    <td className="px-5 py-3 text-center whitespace-nowrap font-mono text-sm">
                      {item.risk_score.toFixed(1)}
                    </td>
                    <td className="px-5 py-3 text-center whitespace-nowrap">
                      <RiskBadge level={item.risk_level} />
                    </td>
                    <td className="px-5 py-3 max-w-[240px] truncate text-sm text-gray-500 dark:text-gray-400">
                      {Object.entries(item.risk_factors)
                        .sort(([, a], [, b]) => b - a)
                        .slice(0, 3)
                        .map(([k, v]) => `${k}(${v})`)
                        .join(', ')}
                    </td>
                    <td className="px-5 py-3 text-center whitespace-nowrap text-sm text-gray-500 dark:text-gray-400">
                      {item.analyzed_at
                        ? new Date(item.analyzed_at).toLocaleDateString('ko-KR')
                        : '-'}
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>

        {/* Pagination */}
        {totalPages > 1 && (
          <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex items-center justify-between">
            <p className="text-xs text-gray-500 dark:text-gray-400">
              총 {anomalyTotal.toLocaleString()}개 중{' '}
              {((page - 1) * pageSize + 1).toLocaleString()}-{Math.min(page * pageSize, anomalyTotal).toLocaleString()}개 표시
            </p>
            <div className="flex items-center gap-1">
              <button
                onClick={() => setPage((p) => Math.max(1, p - 1))}
                disabled={page <= 1}
                className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
              >
                <ChevronLeft className="w-4 h-4" />
              </button>
              <span className="px-3 text-sm text-gray-600 dark:text-gray-300">
                {page} / {totalPages}
              </span>
              <button
                onClick={() => setPage((p) => Math.min(totalPages, p + 1))}
                disabled={page >= totalPages}
                className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
              >
                <ChevronRight className="w-4 h-4" />
              </button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}

// ─── Sub Components ──────────────────────────────────────────────────────────

function PageHeader({
  onRefresh,
  onExport,
  onAnalyze,
  loading,
  isRunning,
  jobStatus,
  lastAnalysisAt,
}: {
  onRefresh: () => void;
  onExport: () => void;
  onAnalyze: () => void;
  loading: boolean;
  isRunning: boolean;
  jobStatus: AnalysisJobStatus | null;
  lastAnalysisAt: string | null;
}) {
  return (
    <div className="mb-2">
      <div className="flex items-center gap-4">
        <div className="p-3 rounded-xl bg-gradient-to-br from-purple-500 to-violet-600 shadow-lg">
          <Brain className="w-7 h-7 text-white" />
        </div>
        <div className="flex-1">
          <h1 className="text-2xl font-bold text-gray-900 dark:text-white">AI 인증서 분석</h1>
          <p className="text-sm text-gray-500 dark:text-gray-400">
            ML 기반 이상 탐지 및 패턴 분석
            {lastAnalysisAt && (
              <span className="ml-2">
                (마지막 분석: {new Date(lastAnalysisAt).toLocaleString('ko-KR')})
              </span>
            )}
          </p>
        </div>
        <div className="flex gap-2">
          <button
            onClick={onRefresh}
            disabled={loading || isRunning}
            className="inline-flex items-center gap-2 px-3 py-2 rounded-xl text-sm font-medium transition-all duration-200 text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-50"
          >
            <RefreshCw className={cn('w-4 h-4', (loading || isRunning) && 'animate-spin')} />
          </button>
          <button
            onClick={onExport}
            className="inline-flex items-center gap-2 px-4 py-2 rounded-xl text-sm font-medium text-gray-700 dark:text-gray-200 border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 transition-all duration-200"
          >
            <Download className="w-4 h-4" />
            CSV Export
          </button>
          <button
            onClick={onAnalyze}
            disabled={isRunning}
            className={cn(
              'inline-flex items-center gap-2 px-4 py-2 rounded-xl text-sm font-medium text-white transition-all duration-200 shadow-md hover:shadow-lg',
              isRunning
                ? 'bg-gray-400 cursor-not-allowed'
                : 'bg-gradient-to-r from-purple-500 to-violet-600 hover:from-purple-600 hover:to-violet-700'
            )}
          >
            {isRunning ? (
              <>
                <Loader2 className="w-4 h-4 animate-spin" />
                분석 중... {jobStatus?.progress ? `${Math.round(jobStatus.progress * 100)}%` : ''}
              </>
            ) : (
              <>
                <Play className="w-4 h-4" />
                분석 실행
              </>
            )}
          </button>
        </div>
      </div>
    </div>
  );
}

function AnomalyBadge({ label }: { label: string }) {
  const config: Record<string, { bg: string; text: string }> = {
    NORMAL: { bg: 'bg-green-100 dark:bg-green-900/30', text: 'text-green-600 dark:text-green-400' },
    SUSPICIOUS: { bg: 'bg-amber-100 dark:bg-amber-900/30', text: 'text-amber-600 dark:text-amber-400' },
    ANOMALOUS: { bg: 'bg-red-100 dark:bg-red-900/30', text: 'text-red-600 dark:text-red-400' },
  };
  const style = config[label] || { bg: 'bg-gray-100 dark:bg-gray-700', text: 'text-gray-600 dark:text-gray-400' };

  return (
    <span className={cn('inline-flex items-center px-2 py-1 rounded-full text-xs font-medium', style.bg, style.text)}>
      {ANOMALY_LABELS_KO[label] || label}
    </span>
  );
}

function RiskBadge({ level }: { level: string }) {
  const config: Record<string, { bg: string; text: string }> = {
    LOW: { bg: 'bg-green-100 dark:bg-green-900/30', text: 'text-green-600 dark:text-green-400' },
    MEDIUM: { bg: 'bg-yellow-100 dark:bg-yellow-900/30', text: 'text-yellow-600 dark:text-yellow-400' },
    HIGH: { bg: 'bg-orange-100 dark:bg-orange-900/30', text: 'text-orange-600 dark:text-orange-400' },
    CRITICAL: { bg: 'bg-red-100 dark:bg-red-900/30', text: 'text-red-600 dark:text-red-400' },
  };
  const style = config[level] || { bg: 'bg-gray-100 dark:bg-gray-700', text: 'text-gray-600 dark:text-gray-400' };

  return (
    <span className={cn('inline-flex items-center px-2 py-1 rounded-full text-xs font-medium', style.bg, style.text)}>
      {RISK_LABELS_KO[level] || level}
    </span>
  );
}
