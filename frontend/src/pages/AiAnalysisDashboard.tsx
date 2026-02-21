import { useState, useEffect, useMemo } from 'react';
import {
  Brain,
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
} from 'recharts';
import { aiAnalysisApi } from '@/services/aiAnalysisApi';
import type {
  AnalysisStatistics,
  AnalysisJobStatus,
  CertificateAnalysis,
  CountryMaturity,
  AlgorithmTrend,
  RiskDistribution,
} from '@/services/aiAnalysisApi';
import { cn } from '@/utils/cn';
import { getFlagSvgPath } from '@/utils/countryCode';
import countries from 'i18n-iso-countries';
import ko from 'i18n-iso-countries/langs/ko.json';

countries.registerLocale(ko);

const getCountryName = (code: string): string => {
  return countries.getName(code, 'ko') || code;
};

const RISK_COLORS: Record<string, string> = {
  LOW: '#22c55e',
  MEDIUM: '#eab308',
  HIGH: '#f97316',
  CRITICAL: '#ef4444',
};

const ANOMALY_COLORS: Record<string, string> = {
  NORMAL: '#22c55e',
  SUSPICIOUS: '#eab308',
  ANOMALOUS: '#ef4444',
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

// Simplify algorithm names for charts
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

export default function AiAnalysisDashboard() {
  const [stats, setStats] = useState<AnalysisStatistics | null>(null);
  const [jobStatus, setJobStatus] = useState<AnalysisJobStatus | null>(null);
  const [anomalies, setAnomalies] = useState<CertificateAnalysis[]>([]);
  const [anomalyTotal, setAnomalyTotal] = useState(0);
  const [maturity, setMaturity] = useState<CountryMaturity[]>([]);
  const [trends, setTrends] = useState<AlgorithmTrend[]>([]);
  const [riskDist, setRiskDist] = useState<RiskDistribution[]>([]);
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
      const [statsRes, statusRes, maturityRes, trendsRes, riskRes] = await Promise.allSettled([
        aiAnalysisApi.getStatistics(),
        aiAnalysisApi.getAnalysisStatus(),
        aiAnalysisApi.getCountryMaturity(),
        aiAnalysisApi.getAlgorithmTrends(),
        aiAnalysisApi.getRiskDistribution(),
      ]);

      if (statsRes.status === 'fulfilled') setStats(statsRes.value.data);
      if (statusRes.status === 'fulfilled') setJobStatus(statusRes.value.data);
      if (maturityRes.status === 'fulfilled') setMaturity(maturityRes.value.data);
      if (trendsRes.status === 'fulfilled') setTrends(trendsRes.value.data);
      if (riskRes.status === 'fulfilled') setRiskDist(riskRes.value.data);
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

  const totalPages = Math.ceil(anomalyTotal / pageSize);

  // Prepare trend chart data
  const trendData = useMemo(() => {
    // Collect all algorithm names across years
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

  // Top 15 countries for maturity chart
  const top15Maturity = maturity.slice(0, 15);

  if (loading) {
    return (
      <div className="flex items-center justify-center h-96">
        <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
        <span className="ml-3 text-gray-500">AI 분석 데이터 로딩 중...</span>
      </div>
    );
  }

  const isRunning = jobStatus?.status === 'RUNNING' || analyzing;

  return (
    <div className="space-y-6 p-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <Brain className="w-7 h-7 text-purple-600" />
          <div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">
              AI 인증서 분석
            </h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              ML 기반 이상 탐지 및 패턴 분석
              {stats?.last_analysis_at && (
                <span className="ml-2">
                  (마지막 분석: {new Date(stats.last_analysis_at).toLocaleString('ko-KR')})
                </span>
              )}
            </p>
          </div>
        </div>
        <div className="flex items-center gap-2">
          <button
            onClick={fetchData}
            disabled={isRunning}
            className="px-3 py-2 text-sm border rounded-lg hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50"
          >
            <RefreshCw className={cn('w-4 h-4', isRunning && 'animate-spin')} />
          </button>
          <button
            onClick={handleTriggerAnalysis}
            disabled={isRunning}
            className={cn(
              'flex items-center gap-2 px-4 py-2 text-sm font-medium text-white rounded-lg',
              isRunning
                ? 'bg-gray-400 cursor-not-allowed'
                : 'bg-purple-600 hover:bg-purple-700'
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

      {/* Progress bar during analysis */}
      {isRunning && jobStatus && (
        <div className="bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-800 rounded-lg p-4">
          <div className="flex justify-between text-sm mb-2">
            <span className="font-medium text-purple-700 dark:text-purple-300">
              분석 진행 중
            </span>
            <span className="text-purple-600">
              {jobStatus.processed_certificates.toLocaleString()} / {jobStatus.total_certificates.toLocaleString()} 인증서
            </span>
          </div>
          <div className="w-full bg-purple-200 rounded-full h-2">
            <div
              className="bg-purple-600 h-2 rounded-full transition-all duration-500"
              style={{ width: `${Math.round(jobStatus.progress * 100)}%` }}
            />
          </div>
        </div>
      )}

      {/* Summary Cards */}
      {stats && (
        <div className="grid grid-cols-2 md:grid-cols-5 gap-4">
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border p-4">
            <div className="text-xs text-gray-500 mb-1">총 분석</div>
            <div className="text-2xl font-bold">{stats.total_analyzed.toLocaleString()}</div>
          </div>
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border p-4">
            <div className="flex items-center gap-1 text-xs text-green-600 mb-1">
              <CheckCircle className="w-3 h-3" /> 정상
            </div>
            <div className="text-2xl font-bold text-green-600">
              {stats.normal_count.toLocaleString()}
            </div>
          </div>
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border p-4">
            <div className="flex items-center gap-1 text-xs text-yellow-600 mb-1">
              <AlertTriangle className="w-3 h-3" /> 의심
            </div>
            <div className="text-2xl font-bold text-yellow-600">
              {stats.suspicious_count.toLocaleString()}
            </div>
          </div>
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border p-4">
            <div className="flex items-center gap-1 text-xs text-red-600 mb-1">
              <ShieldAlert className="w-3 h-3" /> 이상
            </div>
            <div className="text-2xl font-bold text-red-600">
              {stats.anomalous_count.toLocaleString()}
            </div>
          </div>
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border p-4">
            <div className="flex items-center gap-1 text-xs text-gray-500 mb-1">
              <Shield className="w-3 h-3" /> 평균 위험 점수
            </div>
            <div className="text-2xl font-bold">{stats.avg_risk_score}</div>
          </div>
        </div>
      )}

      {/* Risk Level Proportional Bar */}
      {riskDist.length > 0 && (
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border p-4">
          <h3 className="text-sm font-semibold mb-3 text-gray-700 dark:text-gray-300">
            위험 수준 분포
          </h3>
          <div className="flex rounded-lg overflow-hidden h-8">
            {riskDist.map((r) => (
              <div
                key={r.risk_level}
                className="flex items-center justify-center text-xs font-medium text-white"
                style={{
                  width: `${r.percentage}%`,
                  backgroundColor: RISK_COLORS[r.risk_level] || '#9ca3af',
                  minWidth: r.percentage > 0 ? '2rem' : 0,
                }}
                title={`${RISK_LABELS_KO[r.risk_level] || r.risk_level}: ${r.count.toLocaleString()} (${r.percentage}%)`}
              >
                {r.percentage >= 5 && `${RISK_LABELS_KO[r.risk_level]} ${r.percentage}%`}
              </div>
            ))}
          </div>
          <div className="flex gap-4 mt-2 text-xs text-gray-500">
            {riskDist.map((r) => (
              <div key={r.risk_level} className="flex items-center gap-1">
                <div
                  className="w-2.5 h-2.5 rounded-full"
                  style={{ backgroundColor: RISK_COLORS[r.risk_level] }}
                />
                {RISK_LABELS_KO[r.risk_level]}: {r.count.toLocaleString()}
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Charts Row */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        {/* Country Maturity */}
        {top15Maturity.length > 0 && (
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border p-4">
            <h3 className="text-sm font-semibold mb-3 text-gray-700 dark:text-gray-300">
              <Globe className="w-4 h-4 inline-block mr-1" />
              국가별 PKI 성숙도 (상위 15)
            </h3>
            <ResponsiveContainer width="100%" height={360}>
              <BarChart data={top15Maturity} layout="vertical" margin={{ left: 50, right: 20 }}>
                <CartesianGrid strokeDasharray="3 3" />
                <XAxis type="number" domain={[0, 100]} />
                <YAxis
                  dataKey="country_code"
                  type="category"
                  tick={{ fontSize: 11 }}
                  width={50}
                  tickFormatter={(v: string) => v}
                />
                <Tooltip
                  formatter={(v?: number | string) => [`${Number(v ?? 0).toFixed(1)}점`, '성숙도']}
                  labelFormatter={(label: string) =>
                    `${getCountryName(label)} (${label})`
                  }
                />
                <Bar dataKey="maturity_score" fill="#8b5cf6" radius={[0, 4, 4, 0]} />
              </BarChart>
            </ResponsiveContainer>
          </div>
        )}

        {/* Algorithm Migration Trends */}
        {trendData.length > 0 && (
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border p-4">
            <h3 className="text-sm font-semibold mb-3 text-gray-700 dark:text-gray-300">
              <TrendingUp className="w-4 h-4 inline-block mr-1" />
              알고리즘 마이그레이션 트렌드
            </h3>
            <ResponsiveContainer width="100%" height={360}>
              <AreaChart data={trendData} margin={{ left: 10, right: 20 }}>
                <CartesianGrid strokeDasharray="3 3" />
                <XAxis dataKey="year" tick={{ fontSize: 11 }} />
                <YAxis tick={{ fontSize: 11 }} />
                <Tooltip />
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
      </div>

      {/* Anomaly Table */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border">
        <div className="p-4 border-b">
          <div className="flex items-center justify-between mb-3">
            <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">
              <Filter className="w-4 h-4 inline-block mr-1" />
              인증서 분석 결과
            </h3>
            <span className="text-xs text-gray-500">
              총 {anomalyTotal.toLocaleString()}건
            </span>
          </div>

          {/* Filters */}
          <div className="flex flex-wrap gap-2">
            <select
              value={filterCountry}
              onChange={(e) => { setFilterCountry(e.target.value); setPage(1); }}
              className="text-xs border rounded-md px-2 py-1.5 dark:bg-gray-700 dark:border-gray-600"
            >
              <option value="">전체 국가</option>
              {stats?.top_anomalous_countries.map((c) => (
                <option key={c.country} value={c.country}>
                  {getCountryName(c.country)} ({c.country})
                </option>
              ))}
            </select>
            <select
              value={filterType}
              onChange={(e) => { setFilterType(e.target.value); setPage(1); }}
              className="text-xs border rounded-md px-2 py-1.5 dark:bg-gray-700 dark:border-gray-600"
            >
              <option value="">전체 유형</option>
              <option value="CSCA">CSCA</option>
              <option value="DSC">DSC</option>
              <option value="DSC_NC">DSC_NC</option>
              <option value="MLSC">MLSC</option>
            </select>
            <select
              value={filterLabel}
              onChange={(e) => { setFilterLabel(e.target.value); setPage(1); }}
              className="text-xs border rounded-md px-2 py-1.5 dark:bg-gray-700 dark:border-gray-600"
            >
              <option value="">전체 이상 수준</option>
              <option value="ANOMALOUS">이상</option>
              <option value="SUSPICIOUS">의심</option>
              <option value="NORMAL">정상</option>
            </select>
            <select
              value={filterRisk}
              onChange={(e) => { setFilterRisk(e.target.value); setPage(1); }}
              className="text-xs border rounded-md px-2 py-1.5 dark:bg-gray-700 dark:border-gray-600"
            >
              <option value="">전체 위험 수준</option>
              <option value="CRITICAL">심각</option>
              <option value="HIGH">높음</option>
              <option value="MEDIUM">보통</option>
              <option value="LOW">낮음</option>
            </select>
            {(filterCountry || filterType || filterLabel || filterRisk) && (
              <button
                onClick={() => {
                  setFilterCountry('');
                  setFilterType('');
                  setFilterLabel('');
                  setFilterRisk('');
                  setPage(1);
                }}
                className="text-xs text-blue-600 hover:text-blue-800"
              >
                필터 초기화
              </button>
            )}
          </div>
        </div>

        {/* Table */}
        <div className="overflow-x-auto">
          <table className="w-full text-xs">
            <thead className="bg-gray-50 dark:bg-gray-700">
              <tr>
                <th className="px-3 py-2 text-left font-medium text-gray-600 dark:text-gray-300">국가</th>
                <th className="px-3 py-2 text-left font-medium text-gray-600 dark:text-gray-300">유형</th>
                <th className="px-3 py-2 text-left font-medium text-gray-600 dark:text-gray-300">이상 점수</th>
                <th className="px-3 py-2 text-left font-medium text-gray-600 dark:text-gray-300">이상 수준</th>
                <th className="px-3 py-2 text-left font-medium text-gray-600 dark:text-gray-300">위험 점수</th>
                <th className="px-3 py-2 text-left font-medium text-gray-600 dark:text-gray-300">위험 수준</th>
                <th className="px-3 py-2 text-left font-medium text-gray-600 dark:text-gray-300">주요 위험 요인</th>
                <th className="px-3 py-2 text-left font-medium text-gray-600 dark:text-gray-300">분석 시간</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-100 dark:divide-gray-700">
              {anomalies.length === 0 ? (
                <tr>
                  <td colSpan={8} className="px-3 py-8 text-center text-gray-400">
                    {stats?.total_analyzed === 0
                      ? '분석을 먼저 실행해 주세요.'
                      : '조건에 맞는 결과가 없습니다.'}
                  </td>
                </tr>
              ) : (
                anomalies.map((item) => (
                  <tr key={item.fingerprint} className="hover:bg-gray-50 dark:hover:bg-gray-750">
                    <td className="px-3 py-2">
                      <div className="flex items-center gap-1">
                        {item.country_code && (
                          <img
                            src={getFlagSvgPath(item.country_code)}
                            alt={item.country_code}
                            className="w-4 h-3"
                            onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }}
                          />
                        )}
                        <span>{item.country_code || '-'}</span>
                      </div>
                    </td>
                    <td className="px-3 py-2">
                      <span className="px-1.5 py-0.5 rounded bg-gray-100 dark:bg-gray-700 text-gray-700 dark:text-gray-300">
                        {item.certificate_type || '-'}
                      </span>
                    </td>
                    <td className="px-3 py-2 font-mono">{item.anomaly_score.toFixed(3)}</td>
                    <td className="px-3 py-2">
                      <span
                        className="px-1.5 py-0.5 rounded text-white text-[10px] font-medium"
                        style={{ backgroundColor: ANOMALY_COLORS[item.anomaly_label] }}
                      >
                        {ANOMALY_LABELS_KO[item.anomaly_label] || item.anomaly_label}
                      </span>
                    </td>
                    <td className="px-3 py-2 font-mono">{item.risk_score.toFixed(1)}</td>
                    <td className="px-3 py-2">
                      <span
                        className="px-1.5 py-0.5 rounded text-white text-[10px] font-medium"
                        style={{ backgroundColor: RISK_COLORS[item.risk_level] }}
                      >
                        {RISK_LABELS_KO[item.risk_level] || item.risk_level}
                      </span>
                    </td>
                    <td className="px-3 py-2 max-w-[200px] truncate text-gray-500">
                      {Object.entries(item.risk_factors)
                        .sort(([, a], [, b]) => b - a)
                        .slice(0, 3)
                        .map(([k, v]) => `${k}(${v})`)
                        .join(', ')}
                    </td>
                    <td className="px-3 py-2 text-gray-400">
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
          <div className="flex items-center justify-between px-4 py-3 border-t">
            <span className="text-xs text-gray-500">
              {((page - 1) * pageSize + 1).toLocaleString()}-
              {Math.min(page * pageSize, anomalyTotal).toLocaleString()} / {anomalyTotal.toLocaleString()}
            </span>
            <div className="flex items-center gap-1">
              <button
                onClick={() => setPage((p) => Math.max(1, p - 1))}
                disabled={page === 1}
                className="p-1 rounded hover:bg-gray-100 disabled:opacity-30"
              >
                <ChevronLeft className="w-4 h-4" />
              </button>
              <span className="text-xs px-2">
                {page} / {totalPages}
              </span>
              <button
                onClick={() => setPage((p) => Math.min(totalPages, p + 1))}
                disabled={page === totalPages}
                className="p-1 rounded hover:bg-gray-100 disabled:opacity-30"
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
