import { useState, useEffect, useMemo } from 'react';
import { Link } from 'react-router-dom';
import ReactECharts from 'echarts-for-react';
import {
  BarChart3,
  Upload,
  FileText,
  Clock,
  Database,
  Globe,
  TrendingUp,
  Loader2,
  RefreshCw,
  Shield,
  Key,
} from 'lucide-react';
import { uploadApi } from '@/services/api';
import type { UploadStatisticsOverview } from '@/types';
import { cn } from '@/utils/cn';
import { useThemeStore } from '@/stores/themeStore';

// Country statistics type
interface CountryStats {
  country: string;
  csca: number;
  dsc: number;
  dscNc: number;
  total: number;
  percentage?: number;
}

export function UploadDashboard() {
  const { darkMode } = useThemeStore();
  const [stats, setStats] = useState<UploadStatisticsOverview | null>(null);
  const [countryData, setCountryData] = useState<CountryStats[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetchDashboardData();
  }, []);

  const fetchDashboardData = async () => {
    setLoading(true);
    try {
      const [statsResponse, countryResponse] = await Promise.all([
        uploadApi.getStatistics(),
        uploadApi.getCountryStatistics(15),
      ]);
      setStats(statsResponse.data);

      // Process country data with percentage
      const countries = countryResponse.data;
      if (countries.length > 0) {
        const maxTotal = countries[0].total;
        setCountryData(countries.map((item) => ({
          ...item,
          percentage: Math.max(15, (item.total / maxTotal) * 100),
        })));
      }
    } catch (error) {
      console.error('Failed to fetch upload dashboard data:', error);
    } finally {
      setLoading(false);
    }
  };

  // Certificate type pie chart
  const certTypeChartOption = useMemo(() => {
    const colors = darkMode
      ? { csca: '#60A5FA', dsc: '#4ADE80', crl: '#FBBF24', ml: '#A78BFA' }
      : { csca: '#3B82F6', dsc: '#22C55E', crl: '#F59E0B', ml: '#8B5CF6' };

    const total = (stats?.cscaCount || 0) + (stats?.dscCount || 0) + (stats?.crlCount || 0);

    return {
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'item',
        formatter: '{b}: {c}개 ({d}%)',
      },
      legend: {
        bottom: '5%',
        left: 'center',
        textStyle: { color: darkMode ? '#9CA3AF' : '#6B7280' },
      },
      graphic: {
        type: 'text',
        left: 'center',
        top: 'center',
        style: {
          text: `총 인증서\n${total.toLocaleString()}`,
          textAlign: 'center',
          fill: darkMode ? '#F3F4F6' : '#1F2937',
          fontSize: 14,
          fontWeight: 'bold',
        },
      },
      series: [
        {
          type: 'pie',
          radius: ['50%', '70%'],
          center: ['50%', '45%'],
          avoidLabelOverlap: false,
          itemStyle: { borderRadius: 4, borderColor: 'transparent', borderWidth: 2 },
          label: { show: false },
          emphasis: {
            label: { show: true, fontSize: 14, fontWeight: 'bold' },
            itemStyle: { shadowBlur: 10, shadowOffsetX: 0, shadowColor: 'rgba(0, 0, 0, 0.3)' },
          },
          labelLine: { show: false },
          data: [
            { value: stats?.cscaCount || 0, name: 'CSCA', itemStyle: { color: colors.csca } },
            { value: stats?.dscCount || 0, name: 'DSC', itemStyle: { color: colors.dsc } },
            { value: stats?.crlCount || 0, name: 'CRL', itemStyle: { color: colors.crl } },
          ],
        },
      ],
    };
  }, [stats, darkMode]);

  // Upload status pie chart
  const uploadStatusChartOption = useMemo(() => {
    const colors = darkMode
      ? { completed: '#4ADE80', processing: '#60A5FA', failed: '#F87171' }
      : { completed: '#22C55E', processing: '#3B82F6', failed: '#EF4444' };

    const total = (stats?.successfulUploads || 0) + (stats?.failedUploads || 0);

    return {
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'item',
        formatter: '{b}: {c}건 ({d}%)',
      },
      legend: {
        bottom: '5%',
        left: 'center',
        textStyle: { color: darkMode ? '#9CA3AF' : '#6B7280' },
      },
      graphic: {
        type: 'text',
        left: 'center',
        top: 'center',
        style: {
          text: `총 업로드\n${total.toLocaleString()}`,
          textAlign: 'center',
          fill: darkMode ? '#F3F4F6' : '#1F2937',
          fontSize: 14,
          fontWeight: 'bold',
        },
      },
      series: [
        {
          type: 'pie',
          radius: ['50%', '70%'],
          center: ['50%', '45%'],
          avoidLabelOverlap: false,
          itemStyle: { borderRadius: 4, borderColor: 'transparent', borderWidth: 2 },
          label: { show: false },
          emphasis: {
            label: { show: true, fontSize: 14, fontWeight: 'bold' },
            itemStyle: { shadowBlur: 10, shadowOffsetX: 0, shadowColor: 'rgba(0, 0, 0, 0.3)' },
          },
          labelLine: { show: false },
          data: [
            { value: stats?.successfulUploads || 0, name: '성공', itemStyle: { color: colors.completed } },
            { value: stats?.failedUploads || 0, name: '실패', itemStyle: { color: colors.failed } },
          ],
        },
      ],
    };
  }, [stats, darkMode]);

  const getCountryColor = (index: number) => {
    const colors = [
      '#3B82F6', '#22C55E', '#F59E0B', '#8B5CF6', '#EF4444',
      '#06B6D4', '#EC4899', '#14B8A6', '#6366F1', '#F97316',
    ];
    return colors[index % colors.length];
  };

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-8">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-violet-500 to-purple-600 shadow-lg">
            <BarChart3 className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">PKD 대시보드</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              PKD 파일 업로드 및 인증서 현황을 시각화합니다.
            </p>
          </div>
          {/* Quick Actions */}
          <div className="flex gap-2">
            <Link
              to="/upload"
              className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl text-sm font-medium text-white bg-gradient-to-r from-violet-500 to-purple-500 hover:from-violet-600 hover:to-purple-600 transition-all duration-200 shadow-md hover:shadow-lg"
            >
              <Upload className="w-4 h-4" />
              파일 업로드
            </Link>
            <Link
              to="/upload-history"
              className="inline-flex items-center gap-2 px-4 py-2 rounded-xl text-sm font-medium transition-all duration-200 border text-gray-700 dark:text-gray-300 border-gray-300 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700"
            >
              <Clock className="w-4 h-4" />
              업로드 이력
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
            {/* 총 인증서 */}
            <div className="group relative rounded-2xl p-5 transition-all duration-300 hover:shadow-xl hover:-translate-y-1 bg-white dark:bg-gray-800 shadow-lg">
              <div className="absolute left-0 top-4 bottom-4 w-1 rounded-full bg-gradient-to-b from-violet-400 to-purple-500"></div>
              <div className="pl-4">
                <div className="flex items-start justify-between">
                  <div className="flex-1">
                    <h3 className="text-xs font-semibold uppercase tracking-wider mb-2 text-gray-500 dark:text-gray-400">
                      총 인증서
                    </h3>
                    <div className="text-3xl font-bold text-violet-500">
                      {stats?.totalCertificates.toLocaleString() || 0}
                    </div>
                    <p className="text-xs mt-1 text-gray-400">CSCA + DSC + CRL</p>
                  </div>
                  <div className="flex-shrink-0 p-3 rounded-xl bg-violet-50 dark:bg-violet-900/30">
                    <Database className="w-8 h-8 text-violet-500" />
                  </div>
                </div>
              </div>
            </div>

            {/* CSCA 인증서 */}
            <div className="group relative rounded-2xl p-5 transition-all duration-300 hover:shadow-xl hover:-translate-y-1 bg-white dark:bg-gray-800 shadow-lg">
              <div className="absolute left-0 top-4 bottom-4 w-1 rounded-full bg-gradient-to-b from-blue-400 to-indigo-500"></div>
              <div className="pl-4">
                <div className="flex items-start justify-between">
                  <div className="flex-1">
                    <h3 className="text-xs font-semibold uppercase tracking-wider mb-2 text-gray-500 dark:text-gray-400">
                      CSCA
                    </h3>
                    <div className="text-3xl font-bold text-blue-500">
                      {stats?.cscaCount.toLocaleString() || 0}
                    </div>
                    <p className="text-xs mt-1 text-gray-400">Country Signing CA</p>
                  </div>
                  <div className="flex-shrink-0 p-3 rounded-xl bg-blue-50 dark:bg-blue-900/30">
                    <Shield className="w-8 h-8 text-blue-500" />
                  </div>
                </div>
              </div>
            </div>

            {/* DSC 인증서 */}
            <div className="group relative rounded-2xl p-5 transition-all duration-300 hover:shadow-xl hover:-translate-y-1 bg-white dark:bg-gray-800 shadow-lg">
              <div className="absolute left-0 top-4 bottom-4 w-1 rounded-full bg-gradient-to-b from-green-400 to-emerald-500"></div>
              <div className="pl-4">
                <div className="flex items-start justify-between">
                  <div className="flex-1">
                    <h3 className="text-xs font-semibold uppercase tracking-wider mb-2 text-gray-500 dark:text-gray-400">
                      DSC
                    </h3>
                    <div className="text-3xl font-bold text-green-500">
                      {stats?.dscCount.toLocaleString() || 0}
                    </div>
                    <p className="text-xs mt-1 text-gray-400">Document Signer</p>
                  </div>
                  <div className="flex-shrink-0 p-3 rounded-xl bg-green-50 dark:bg-green-900/30">
                    <Key className="w-8 h-8 text-green-500" />
                  </div>
                </div>
              </div>
            </div>

            {/* 등록 국가 */}
            <div className="group relative rounded-2xl p-5 transition-all duration-300 hover:shadow-xl hover:-translate-y-1 bg-white dark:bg-gray-800 shadow-lg">
              <div className="absolute left-0 top-4 bottom-4 w-1 rounded-full bg-gradient-to-b from-amber-400 to-orange-500"></div>
              <div className="pl-4">
                <div className="flex items-start justify-between">
                  <div className="flex-1">
                    <h3 className="text-xs font-semibold uppercase tracking-wider mb-2 text-gray-500 dark:text-gray-400">
                      등록 국가
                    </h3>
                    <div className="text-3xl font-bold text-amber-500">
                      {stats?.countriesCount || 0}
                    </div>
                    <p className="text-xs mt-1 text-gray-400">PKD 등록 국가 수</p>
                  </div>
                  <div className="flex-shrink-0 p-3 rounded-xl bg-amber-50 dark:bg-amber-900/30">
                    <Globe className="w-8 h-8 text-amber-500" />
                  </div>
                </div>
              </div>
            </div>
          </div>

          {/* Charts Row */}
          <div className="grid grid-cols-1 lg:grid-cols-2 gap-5 mb-5">
            {/* Certificate Type Chart */}
            <div className="rounded-2xl transition-all duration-300 hover:shadow-xl bg-white dark:bg-gray-800 shadow-lg">
              <div className="p-5">
                <div className="flex items-center gap-3 mb-4">
                  <div className="p-2.5 rounded-xl bg-blue-50 dark:bg-blue-900/30">
                    <FileText className="w-5 h-5 text-blue-500" />
                  </div>
                  <h2 className="text-lg font-bold text-gray-900 dark:text-white">인증서 유형 분포</h2>
                </div>
                <div className="h-72">
                  <ReactECharts
                    option={certTypeChartOption}
                    style={{ height: '100%', width: '100%' }}
                    opts={{ renderer: 'svg' }}
                  />
                </div>
              </div>
            </div>

            {/* Upload Status Chart */}
            <div className="rounded-2xl transition-all duration-300 hover:shadow-xl bg-white dark:bg-gray-800 shadow-lg">
              <div className="p-5">
                <div className="flex items-center gap-3 mb-4">
                  <div className="p-2.5 rounded-xl bg-green-50 dark:bg-green-900/30">
                    <TrendingUp className="w-5 h-5 text-green-500" />
                  </div>
                  <h2 className="text-lg font-bold text-gray-900 dark:text-white">업로드 상태 분포</h2>
                </div>
                <div className="h-72">
                  <ReactECharts
                    option={uploadStatusChartOption}
                    style={{ height: '100%', width: '100%' }}
                    opts={{ renderer: 'svg' }}
                  />
                </div>
              </div>
            </div>
          </div>

          {/* Country Distribution */}
          <div className="grid grid-cols-1 gap-5">
            {/* Countries with Most Certificates */}
            <div className="rounded-2xl transition-all duration-300 hover:shadow-xl bg-white dark:bg-gray-800 shadow-lg">
              <div className="p-5">
                <div className="flex items-center gap-3 mb-4">
                  <div className="p-2.5 rounded-xl bg-cyan-50 dark:bg-cyan-900/30">
                    <Globe className="w-5 h-5 text-cyan-500" />
                  </div>
                  <h2 className="text-lg font-bold text-gray-900 dark:text-white">
                    국가별 인증서 현황
                  </h2>
                </div>
                {countryData.length > 0 ? (
                  <div className="space-y-2 max-h-80 overflow-y-auto pr-2">
                    {countryData.map((item, index) => (
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
                        <img
                          src={`/svg/${item.country.toLowerCase()}.svg`}
                          alt={item.country}
                          className="w-7 h-5 flex-shrink-0 object-cover rounded shadow-sm border border-gray-200 dark:border-gray-600"
                          onError={(e) => {
                            (e.target as HTMLImageElement).style.display = 'none';
                          }}
                        />
                        {/* Country Code */}
                        <span className="w-10 flex-shrink-0 font-mono font-semibold text-sm text-gray-700 dark:text-gray-300">
                          {item.country}
                        </span>
                        {/* Progress Bar */}
                        <div className="flex-1 flex items-center gap-2">
                          <div className="flex-1 h-5 rounded-full overflow-hidden bg-gray-100 dark:bg-gray-700">
                            <div
                              className="h-full rounded-full transition-all duration-500"
                              style={{
                                width: `${item.percentage}%`,
                                background: getCountryColor(index),
                              }}
                            />
                          </div>
                          <span className="w-12 flex-shrink-0 text-xs font-bold text-right text-gray-600 dark:text-gray-300">
                            {item.total.toLocaleString()}
                          </span>
                        </div>
                      </div>
                    ))}
                  </div>
                ) : (
                  <div className="text-center py-12">
                    <Globe className="w-12 h-12 mx-auto text-gray-300 dark:text-gray-600 mb-3" />
                    <p className="text-sm text-gray-500 dark:text-gray-400">
                      업로드된 인증서가 없습니다
                    </p>
                    <Link
                      to="/upload"
                      className="mt-3 inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-white bg-gradient-to-r from-violet-500 to-purple-500"
                    >
                      <Upload className="w-4 h-4" />
                      파일 업로드
                    </Link>
                  </div>
                )}
              </div>
            </div>
          </div>
        </>
      )}
    </div>
  );
}

export default UploadDashboard;
