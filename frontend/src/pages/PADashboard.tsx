import { useState, useEffect, useMemo } from 'react';
import { Link } from 'react-router-dom';
import {
  PieChart,
  Pie,
  Cell,
  AreaChart,
  Area,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
} from 'recharts';
import {
  PresentationIcon,
  ShieldCheck,
  CheckCircle,
  Clock,
  Globe,
  TrendingUp,
  Loader2,
  RefreshCw,
  Calendar,
} from 'lucide-react';
import { paApi } from '@/services/paApi';
import type { PAStatisticsOverview, PAHistoryItem } from '@/types';
import { cn } from '@/utils/cn';
import { getFlagSvgPath, getAlpha2Code } from '@/utils/countryCode';
import { useThemeStore } from '@/stores/themeStore';

export function PADashboard() {
  const { darkMode } = useThemeStore();
  const [stats, setStats] = useState<PAStatisticsOverview | null>(null);
  const [recentVerifications, setRecentVerifications] = useState<PAHistoryItem[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetchDashboardData();
  }, []);

  const fetchDashboardData = async () => {
    setLoading(true);
    try {
      const [statsResponse, historyResponse] = await Promise.all([
        paApi.getStatistics(),
        paApi.getHistory({ page: 0, size: 100 }),
      ]);

      // Map backend statistics format to PAStatisticsOverview
      const raw = statsResponse.data;
      const byStatus = raw.byStatus ?? {};
      setStats({
        totalVerifications: raw.totalVerifications ?? 0,
        validCount: byStatus['VALID'] ?? 0,
        invalidCount: byStatus['INVALID'] ?? 0,
        errorCount: byStatus['ERROR'] ?? 0,
        averageProcessingTimeMs: 0,
        countriesVerified: raw.byCountry?.length ?? 0,
      });

      // Backend returns { success, total, page, size, data: [...] }
      const items = historyResponse.data.data ?? [];
      setRecentVerifications(items);
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to fetch PA dashboard data:', error);
    } finally {
      setLoading(false);
    }
  };

  // Calculate derived statistics from recent verifications
  const derivedStats = useMemo(() => {
    if (!recentVerifications.length) {
      return {
        countryStats: {} as Record<string, number>,
        dailyStats: [] as { date: string; valid: number; invalid: number; error: number }[],
        todayCount: 0,
        lastVerification: 'N/A',
      };
    }

    // Country stats - normalize to 2-letter codes to combine KOR/KR
    const countryStats: Record<string, number> = {};
    recentVerifications.forEach((r) => {
      if (r.issuingCountry) {
        // Normalize country code: convert 3-letter to 2-letter (KOR -> kr, KR -> kr)
        const alpha2 = getAlpha2Code(r.issuingCountry);
        if (alpha2) {
          // Use uppercase for display consistency
          const normalizedCode = alpha2.toUpperCase();
          countryStats[normalizedCode] = (countryStats[normalizedCode] || 0) + 1;
        } else {
          // Fallback: use original code if conversion fails
          countryStats[r.issuingCountry] = (countryStats[r.issuingCountry] || 0) + 1;
        }
      }
    });

    // Daily stats (last 30 days)
    const dailyMap: Record<string, { valid: number; invalid: number; error: number }> = {};
    const last30Days: string[] = [];
    for (let i = 29; i >= 0; i--) {
      const date = new Date();
      date.setDate(date.getDate() - i);
      const dateStr = date.toISOString().split('T')[0];
      last30Days.push(dateStr);
      dailyMap[dateStr] = { valid: 0, invalid: 0, error: 0 };
    }

    recentVerifications.forEach((r) => {
      if (r.verificationTimestamp) {
        // Handle both ISO format (2026-01-02T16:35:28) and PostgreSQL format (2026-01-02 16:35:28.313491+09)
        const dateStr = r.verificationTimestamp.split(/[T\s]/)[0];
        if (dailyMap[dateStr]) {
          if (r.status === 'VALID') dailyMap[dateStr].valid++;
          else if (r.status === 'INVALID') dailyMap[dateStr].invalid++;
          else if (r.status === 'ERROR') dailyMap[dateStr].error++;
        }
      }
    });

    const dailyStats = last30Days.map((date) => ({
      date,
      ...dailyMap[date],
    }));

    // Today count
    const today = new Date().toISOString().split('T')[0];
    const todayCount = recentVerifications.filter(
      (r) => r.verificationTimestamp?.split(/[T\s]/)[0] === today
    ).length;

    // Last verification time
    let lastVerification = 'N/A';
    if (recentVerifications.length > 0 && recentVerifications[0].verificationTimestamp) {
      const last = new Date(recentVerifications[0].verificationTimestamp);
      const now = new Date();
      const diffMinutes = Math.floor((now.getTime() - last.getTime()) / 1000 / 60);
      if (diffMinutes < 60) {
        lastVerification = `${diffMinutes}분 전`;
      } else if (diffMinutes < 1440) {
        lastVerification = `${Math.floor(diffMinutes / 60)}시간 전`;
      } else {
        lastVerification = `${Math.floor(diffMinutes / 1440)}일 전`;
      }
    }

    return { countryStats, dailyStats, todayCount, lastVerification };
  }, [recentVerifications]);

  // Get top 10 countries sorted by count
  const topCountries = useMemo(() => {
    const entries = Object.entries(derivedStats.countryStats);
    if (entries.length === 0) return [];
    const sorted = entries.sort((a, b) => b[1] - a[1]).slice(0, 10);
    const maxCount = sorted[0][1];
    return sorted.map(([country, count]) => ({
      country,
      count,
      percentage: Math.max(15, (count / maxCount) * 100),
    }));
  }, [derivedStats.countryStats]);

  const successRate =
    stats && stats.totalVerifications > 0
      ? ((stats.validCount / stats.totalVerifications) * 100).toFixed(1)
      : '0.0';

  // Chart colors (dark mode aware)
  const chartColors = useMemo(() => {
    return darkMode
      ? { valid: '#4ADE80', invalid: '#F87171', error: '#FBBF24' }
      : { valid: '#86EFAC', invalid: '#FCA5A5', error: '#FDE68A' };
  }, [darkMode]);

  // Pie chart data for verification status
  const statusChartData = useMemo(() => [
    { name: 'Valid', value: stats?.validCount || 0 },
    { name: 'Invalid', value: stats?.invalidCount || 0 },
    { name: 'Error', value: stats?.errorCount || 0 },
  ], [stats]);

  const statusTotal = (stats?.validCount || 0) + (stats?.invalidCount || 0) + (stats?.errorCount || 0);

  // Area chart data for daily trend (add formatted date label)
  const trendChartData = useMemo(() => {
    return derivedStats.dailyStats.map((d) => {
      const date = new Date(d.date);
      return {
        ...d,
        label: `${date.getMonth() + 1}/${date.getDate()}`,
      };
    });
  }, [derivedStats.dailyStats]);

  const getCountryColor = (index: number) => {
    const colors = [
      '#06B6D4', '#0891B2', '#0E7490', '#14B8A6', '#0D9488',
      '#6366F1', '#4F46E5', '#8B5CF6', '#7C3AED', '#A855F7',
    ];
    return colors[index % colors.length];
  };

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-8">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-teal-500 to-cyan-600 shadow-lg">
            <PresentationIcon className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">
              Passive Authentication 대시보드
            </h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              전자여권 검증 통계 및 추이를 시각화합니다.
            </p>
          </div>
          {/* Quick Actions */}
          <div className="flex gap-2">
            <Link
              to="/pa/verify"
              className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl text-sm font-medium text-white bg-gradient-to-r from-teal-500 to-cyan-500 hover:from-teal-600 hover:to-cyan-600 transition-all duration-200 shadow-md hover:shadow-lg"
            >
              <ShieldCheck className="w-4 h-4" />
              새 검증 수행
            </Link>
            <Link
              to="/pa/history"
              className="inline-flex items-center gap-2 px-4 py-2 rounded-xl text-sm font-medium transition-all duration-200 border text-gray-700 dark:text-gray-300 border-gray-300 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700"
            >
              <Clock className="w-4 h-4" />
              검증 이력
            </Link>
            <button
              onClick={fetchDashboardData}
              disabled={loading}
              className="inline-flex items-center gap-2 px-3 py-2 rounded-xl text-sm font-medium transition-all duration-200 text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700"
            >
              <RefreshCw className={cn('w-4 h-4', loading && 'animate-spin')} />
              새로고침
            </button>
          </div>
        </div>
      </div>

      {loading ? (
        <div className="flex items-center justify-center py-20">
          <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
        </div>
      ) : (
        <>
          {/* Summary Statistics */}
          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-5 mb-6">
            {/* 총 검증 건수 */}
            <div className="group relative rounded-2xl p-5 transition-all duration-300 hover:shadow-xl hover:-translate-y-1 bg-white dark:bg-gray-800 shadow-lg">
              <div className="absolute left-0 top-4 bottom-4 w-1 rounded-full bg-gradient-to-b from-teal-400 to-cyan-500"></div>
              <div className="pl-4">
                <div className="flex items-start justify-between">
                  <div className="flex-1">
                    <h3 className="text-xs font-semibold uppercase tracking-wider mb-2 text-gray-500 dark:text-gray-400">
                      총 검증 건수
                    </h3>
                    <div className="text-3xl font-bold text-teal-500">
                      {(stats?.totalVerifications ?? 0).toLocaleString()}
                    </div>
                    <p className="text-xs mt-1 text-gray-400">전체 검증 수행 건수</p>
                  </div>
                  <div className="flex-shrink-0 p-3 rounded-xl bg-teal-50 dark:bg-teal-900/30">
                    <ShieldCheck className="w-8 h-8 text-teal-500" />
                  </div>
                </div>
              </div>
            </div>

            {/* 검증 성공률 */}
            <div className="group relative rounded-2xl p-5 transition-all duration-300 hover:shadow-xl hover:-translate-y-1 bg-white dark:bg-gray-800 shadow-lg">
              <div className="absolute left-0 top-4 bottom-4 w-1 rounded-full bg-gradient-to-b from-green-400 to-emerald-500"></div>
              <div className="pl-4">
                <div className="flex items-start justify-between">
                  <div className="flex-1">
                    <h3 className="text-xs font-semibold uppercase tracking-wider mb-2 text-gray-500 dark:text-gray-400">
                      검증 성공률
                    </h3>
                    <div className="text-3xl font-bold text-green-500">{successRate}%</div>
                    <p className="text-xs mt-1 text-gray-400">
                      {stats?.validCount || 0} / {stats?.totalVerifications || 0} 건
                    </p>
                  </div>
                  <div className="flex-shrink-0 p-3 rounded-xl bg-green-50 dark:bg-green-900/30">
                    <CheckCircle className="w-8 h-8 text-green-500" />
                  </div>
                </div>
              </div>
            </div>

            {/* 검증 국가 수 */}
            <div className="group relative rounded-2xl p-5 transition-all duration-300 hover:shadow-xl hover:-translate-y-1 bg-white dark:bg-gray-800 shadow-lg">
              <div className="absolute left-0 top-4 bottom-4 w-1 rounded-full bg-gradient-to-b from-blue-400 to-indigo-500"></div>
              <div className="pl-4">
                <div className="flex items-start justify-between">
                  <div className="flex-1">
                    <h3 className="text-xs font-semibold uppercase tracking-wider mb-2 text-gray-500 dark:text-gray-400">
                      검증 국가 수
                    </h3>
                    <div className="text-3xl font-bold text-blue-500">
                      {Object.keys(derivedStats.countryStats).length}
                    </div>
                    <p className="text-xs mt-1 text-gray-400">서로 다른 발급 국가</p>
                  </div>
                  <div className="flex-shrink-0 p-3 rounded-xl bg-blue-50 dark:bg-blue-900/30">
                    <Globe className="w-8 h-8 text-blue-500" />
                  </div>
                </div>
              </div>
            </div>

            {/* 오늘 검증 건수 */}
            <div className="group relative rounded-2xl p-5 transition-all duration-300 hover:shadow-xl hover:-translate-y-1 bg-white dark:bg-gray-800 shadow-lg">
              <div className="absolute left-0 top-4 bottom-4 w-1 rounded-full bg-gradient-to-b from-amber-400 to-orange-500"></div>
              <div className="pl-4">
                <div className="flex items-start justify-between">
                  <div className="flex-1">
                    <h3 className="text-xs font-semibold uppercase tracking-wider mb-2 text-gray-500 dark:text-gray-400">
                      오늘 검증 건수
                    </h3>
                    <div className="text-3xl font-bold text-amber-500">{derivedStats.todayCount}</div>
                    <p className="text-xs mt-1 text-gray-400">최근: {derivedStats.lastVerification}</p>
                  </div>
                  <div className="flex-shrink-0 p-3 rounded-xl bg-amber-50 dark:bg-amber-900/30">
                    <Calendar className="w-8 h-8 text-amber-500" />
                  </div>
                </div>
              </div>
            </div>
          </div>

          {/* Charts Row 1 */}
          <div className="grid grid-cols-1 lg:grid-cols-2 gap-5 mb-5">
            {/* Verification Status Chart */}
            <div className="rounded-2xl transition-all duration-300 hover:shadow-xl bg-white dark:bg-gray-800 shadow-lg">
              <div className="p-5">
                <div className="flex items-center gap-3 mb-4">
                  <div className="p-2.5 rounded-xl bg-green-50 dark:bg-green-900/30">
                    <TrendingUp className="w-5 h-5 text-green-500" />
                  </div>
                  <h2 className="text-lg font-bold text-gray-900 dark:text-white">검증 결과 분포</h2>
                </div>
                <div className="h-72 relative">
                  <ResponsiveContainer width="100%" height="100%">
                    <PieChart>
                      <Pie
                        data={statusChartData}
                        dataKey="value"
                        nameKey="name"
                        cx="50%"
                        cy="45%"
                        innerRadius="50%"
                        outerRadius="70%"
                        paddingAngle={2}
                        cornerRadius={4}
                      >
                        {[chartColors.valid, chartColors.invalid, chartColors.error].map((color, i) => (
                          <Cell key={i} fill={color} />
                        ))}
                      </Pie>
                      <Tooltip
                        formatter={(value: number | undefined, name: string | undefined) => [`${(value ?? 0).toLocaleString()}건`, name ?? '']}
                        contentStyle={{
                          backgroundColor: darkMode ? '#1F2937' : '#FFFFFF',
                          border: `1px solid ${darkMode ? '#374151' : '#E5E7EB'}`,
                          borderRadius: '8px',
                          color: darkMode ? '#F3F4F6' : '#1F2937',
                        }}
                      />
                      <Legend
                        verticalAlign="bottom"
                        wrapperStyle={{ color: darkMode ? '#9CA3AF' : '#6B7280' }}
                      />
                    </PieChart>
                  </ResponsiveContainer>
                  {/* Center label */}
                  <div className="absolute inset-0 flex items-center justify-center pointer-events-none" style={{ paddingBottom: '10%' }}>
                    <div className="text-center">
                      <div className="text-xs text-gray-500 dark:text-gray-400">총 검증</div>
                      <div className="text-lg font-bold text-gray-900 dark:text-gray-100">
                        {statusTotal.toLocaleString()}
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            {/* Country Distribution */}
            <div className="rounded-2xl transition-all duration-300 hover:shadow-xl bg-white dark:bg-gray-800 shadow-lg">
              <div className="p-5">
                <div className="flex items-center gap-3 mb-4">
                  <div className="p-2.5 rounded-xl bg-cyan-50 dark:bg-cyan-900/30">
                    <Globe className="w-5 h-5 text-cyan-500" />
                  </div>
                  <h2 className="text-lg font-bold text-gray-900 dark:text-white">
                    국가별 검증 건수 (Top 10)
                  </h2>
                </div>
                {/* Country List with Flags and Progress Bars */}
                <div className="space-y-2 max-h-72 overflow-y-auto pr-2">
                  {topCountries.length > 0 ? (
                    topCountries.map((item, index) => (
                      <div
                        key={item.country}
                        className="flex items-center gap-3 p-2 rounded-lg transition-colors hover:bg-gray-50 dark:hover:bg-gray-700"
                      >
                        {/* Rank */}
                        <span
                          className={cn(
                            'w-5 flex-shrink-0 text-xs font-bold text-center',
                            index < 3 ? 'text-amber-500' : 'text-gray-400'
                          )}
                        >
                          {index + 1}
                        </span>
                        {/* Flag */}
                        {getFlagSvgPath(item.country) && (
                          <img
                            src={getFlagSvgPath(item.country)}
                            alt={item.country}
                            className="w-7 h-5 flex-shrink-0 object-cover rounded shadow-sm border border-gray-200 dark:border-gray-600"
                            onError={(e) => {
                              (e.target as HTMLImageElement).style.display = 'none';
                            }}
                          />
                        )}
                        {/* Country Code */}
                        <span className="w-20 flex-shrink-0 font-mono font-semibold text-sm text-gray-700 dark:text-gray-300">
                          {item.country}
                        </span>
                        {/* Progress Bar with Count */}
                        <div className="flex-1 flex items-center gap-2">
                          <div className="flex-1 h-6 rounded-full overflow-hidden bg-gray-100 dark:bg-gray-700">
                            <div
                              className="h-full rounded-full transition-all duration-500"
                              style={{
                                width: `${item.percentage}%`,
                                background: getCountryColor(index),
                              }}
                            />
                          </div>
                          <span className="w-14 flex-shrink-0 text-xs font-bold text-right text-gray-600 dark:text-gray-300">
                            {item.count.toLocaleString()}
                          </span>
                        </div>
                      </div>
                    ))
                  ) : (
                    <div className="text-center py-8">
                      <p className="text-sm text-gray-400">데이터가 없습니다</p>
                    </div>
                  )}
                </div>
              </div>
            </div>
          </div>

          {/* Charts Row 2 - Daily Trend */}
          <div className="grid grid-cols-1 gap-5">
            <div className="rounded-2xl transition-all duration-300 hover:shadow-xl bg-white dark:bg-gray-800 shadow-lg">
              <div className="p-5">
                <div className="flex items-center gap-3 mb-4">
                  <div className="p-2.5 rounded-xl bg-teal-50 dark:bg-teal-900/30">
                    <TrendingUp className="w-5 h-5 text-teal-500" />
                  </div>
                  <h2 className="text-lg font-bold text-gray-900 dark:text-white">
                    일별 검증 추이 (최근 30일)
                  </h2>
                </div>
                <div className="h-72">
                  <ResponsiveContainer width="100%" height="100%">
                    <AreaChart data={trendChartData} margin={{ top: 10, right: 16, left: 0, bottom: 0 }}>
                      <defs>
                        <linearGradient id="gradValid" x1="0" y1="0" x2="0" y2="1">
                          <stop offset="0%" stopColor={chartColors.valid} stopOpacity={0.4} />
                          <stop offset="100%" stopColor={chartColors.valid} stopOpacity={0.1} />
                        </linearGradient>
                        <linearGradient id="gradInvalid" x1="0" y1="0" x2="0" y2="1">
                          <stop offset="0%" stopColor={chartColors.invalid} stopOpacity={0.4} />
                          <stop offset="100%" stopColor={chartColors.invalid} stopOpacity={0.1} />
                        </linearGradient>
                        <linearGradient id="gradError" x1="0" y1="0" x2="0" y2="1">
                          <stop offset="0%" stopColor={chartColors.error} stopOpacity={0.4} />
                          <stop offset="100%" stopColor={chartColors.error} stopOpacity={0.1} />
                        </linearGradient>
                      </defs>
                      <CartesianGrid
                        strokeDasharray="3 3"
                        stroke={darkMode ? '#374151' : '#E5E7EB'}
                        vertical={false}
                      />
                      <XAxis
                        dataKey="label"
                        tick={{ fill: darkMode ? '#9CA3AF' : '#6B7280', fontSize: 10 }}
                        tickLine={false}
                        axisLine={false}
                        angle={-45}
                        textAnchor="end"
                        height={50}
                      />
                      <YAxis
                        tick={{ fill: darkMode ? '#9CA3AF' : '#6B7280' }}
                        tickLine={false}
                        axisLine={false}
                        allowDecimals={false}
                      />
                      <Tooltip
                        contentStyle={{
                          backgroundColor: darkMode ? '#1F2937' : '#FFFFFF',
                          border: `1px solid ${darkMode ? '#374151' : '#E5E7EB'}`,
                          borderRadius: '8px',
                          color: darkMode ? '#F3F4F6' : '#1F2937',
                        }}
                        formatter={(value: number | undefined, name: string | undefined) => [`${(value ?? 0).toLocaleString()}건`, name ?? '']}
                        labelFormatter={(label) => `${label}`}
                      />
                      <Legend
                        verticalAlign="bottom"
                        wrapperStyle={{ color: darkMode ? '#9CA3AF' : '#6B7280' }}
                      />
                      <Area
                        type="monotone"
                        dataKey="valid"
                        name="Valid"
                        stroke={chartColors.valid}
                        strokeWidth={2}
                        fill="url(#gradValid)"
                        dot={false}
                      />
                      <Area
                        type="monotone"
                        dataKey="invalid"
                        name="Invalid"
                        stroke={chartColors.invalid}
                        strokeWidth={2}
                        fill="url(#gradInvalid)"
                        dot={false}
                      />
                      <Area
                        type="monotone"
                        dataKey="error"
                        name="Error"
                        stroke={chartColors.error}
                        strokeWidth={2}
                        fill="url(#gradError)"
                        dot={false}
                      />
                    </AreaChart>
                  </ResponsiveContainer>
                </div>
              </div>
            </div>
          </div>
        </>
      )}
    </div>
  );
}

export default PADashboard;
