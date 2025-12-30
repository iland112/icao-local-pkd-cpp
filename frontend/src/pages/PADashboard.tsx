import { useState, useEffect } from 'react';
import { Link } from 'react-router-dom';
import {
  PresentationIcon,
  ShieldCheck,
  CheckCircle,
  XCircle,
  AlertTriangle,
  Clock,
  Globe,
  TrendingUp,
  Loader2,
  ArrowRight,
  History,
  Zap,
} from 'lucide-react';
import { paApi } from '@/services/api';
import type { PAStatisticsOverview } from '@/types';
import { cn } from '@/utils/cn';

export function PADashboard() {
  const [stats, setStats] = useState<PAStatisticsOverview | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetchStatistics();
  }, []);

  const fetchStatistics = async () => {
    setLoading(true);
    try {
      const response = await paApi.getStatistics();
      setStats(response.data);
    } catch (error) {
      console.error('Failed to fetch PA statistics:', error);
    } finally {
      setLoading(false);
    }
  };

  const statCards = stats
    ? [
        {
          label: '총 검증',
          value: stats.totalVerifications,
          icon: ShieldCheck,
          color: 'from-blue-500 to-indigo-500',
          bgColor: 'bg-blue-50 dark:bg-blue-900/20',
        },
        {
          label: '성공 (VALID)',
          value: stats.validCount,
          icon: CheckCircle,
          color: 'from-green-500 to-emerald-500',
          bgColor: 'bg-green-50 dark:bg-green-900/20',
        },
        {
          label: '실패 (INVALID)',
          value: stats.invalidCount,
          icon: XCircle,
          color: 'from-red-500 to-rose-500',
          bgColor: 'bg-red-50 dark:bg-red-900/20',
        },
        {
          label: '오류 (ERROR)',
          value: stats.errorCount,
          icon: AlertTriangle,
          color: 'from-yellow-500 to-orange-500',
          bgColor: 'bg-yellow-50 dark:bg-yellow-900/20',
        },
      ]
    : [];

  const successRate = stats && stats.totalVerifications > 0
    ? ((stats.validCount / stats.totalVerifications) * 100).toFixed(1)
    : '0.0';

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-8">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-teal-500 to-cyan-600 shadow-lg">
            <PresentationIcon className="w-7 h-7 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">PA 검증 통계</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              Passive Authentication 검증 현황을 확인합니다.
            </p>
          </div>
        </div>
      </div>

      {loading ? (
        <div className="flex items-center justify-center py-20">
          <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
        </div>
      ) : (
        <>
          {/* Main Stats */}
          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4 mb-8">
            {statCards.map((card) => (
              <div
                key={card.label}
                className={cn(
                  'relative overflow-hidden rounded-2xl p-5 shadow-lg transition-all hover:shadow-xl',
                  card.bgColor
                )}
              >
                <div className="flex items-start justify-between">
                  <div>
                    <p className="text-sm font-medium text-gray-600 dark:text-gray-400">{card.label}</p>
                    <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                      {card.value.toLocaleString()}
                    </p>
                  </div>
                  <div className={cn('p-3 rounded-xl bg-gradient-to-br shadow-md', card.color)}>
                    <card.icon className="w-6 h-6 text-white" />
                  </div>
                </div>
              </div>
            ))}
          </div>

          {/* Performance Metrics */}
          <div className="grid grid-cols-1 lg:grid-cols-2 gap-6 mb-8">
            {/* Success Rate */}
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
              <div className="px-6 py-4 border-b border-gray-100 dark:border-gray-700">
                <h3 className="text-lg font-bold text-gray-900 dark:text-white flex items-center gap-3">
                  <div className="p-2 rounded-lg bg-gradient-to-r from-green-500 to-emerald-500">
                    <TrendingUp className="w-5 h-5 text-white" />
                  </div>
                  검증 성공률
                </h3>
              </div>
              <div className="p-6">
                <div className="flex items-center gap-6">
                  <div className="relative w-32 h-32">
                    <svg className="w-full h-full transform -rotate-90" viewBox="0 0 100 100">
                      <circle
                        cx="50"
                        cy="50"
                        r="40"
                        stroke="currentColor"
                        strokeWidth="8"
                        fill="none"
                        className="text-gray-200 dark:text-gray-700"
                      />
                      <circle
                        cx="50"
                        cy="50"
                        r="40"
                        stroke="currentColor"
                        strokeWidth="8"
                        fill="none"
                        strokeDasharray={`${parseFloat(successRate) * 2.51} 251`}
                        strokeLinecap="round"
                        className="text-green-500"
                      />
                    </svg>
                    <div className="absolute inset-0 flex items-center justify-center">
                      <span className="text-2xl font-bold text-gray-900 dark:text-white">{successRate}%</span>
                    </div>
                  </div>
                  <div className="flex-1 space-y-3">
                    <div className="flex items-center justify-between">
                      <span className="text-sm text-gray-600 dark:text-gray-400">성공</span>
                      <span className="font-semibold text-green-600">{stats?.validCount || 0}</span>
                    </div>
                    <div className="flex items-center justify-between">
                      <span className="text-sm text-gray-600 dark:text-gray-400">실패</span>
                      <span className="font-semibold text-red-600">{stats?.invalidCount || 0}</span>
                    </div>
                    <div className="flex items-center justify-between">
                      <span className="text-sm text-gray-600 dark:text-gray-400">오류</span>
                      <span className="font-semibold text-yellow-600">{stats?.errorCount || 0}</span>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            {/* Performance */}
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
              <div className="px-6 py-4 border-b border-gray-100 dark:border-gray-700">
                <h3 className="text-lg font-bold text-gray-900 dark:text-white flex items-center gap-3">
                  <div className="p-2 rounded-lg bg-gradient-to-r from-purple-500 to-pink-500">
                    <Zap className="w-5 h-5 text-white" />
                  </div>
                  성능 지표
                </h3>
              </div>
              <div className="p-6">
                <div className="space-y-4">
                  <div className="p-4 rounded-xl bg-gray-50 dark:bg-gray-700/50">
                    <div className="flex items-center gap-3 mb-2">
                      <Clock className="w-5 h-5 text-purple-500" />
                      <span className="text-sm font-medium text-gray-700 dark:text-gray-300">평균 처리 시간</span>
                    </div>
                    <p className="text-2xl font-bold text-gray-900 dark:text-white">
                      {stats?.averageProcessingTimeMs || 0} <span className="text-sm font-normal">ms</span>
                    </p>
                  </div>
                  <div className="p-4 rounded-xl bg-gray-50 dark:bg-gray-700/50">
                    <div className="flex items-center gap-3 mb-2">
                      <Globe className="w-5 h-5 text-blue-500" />
                      <span className="text-sm font-medium text-gray-700 dark:text-gray-300">검증된 국가</span>
                    </div>
                    <p className="text-2xl font-bold text-gray-900 dark:text-white">
                      {stats?.countriesVerified || 0} <span className="text-sm font-normal">개국</span>
                    </p>
                  </div>
                </div>
              </div>
            </div>
          </div>

          {/* Quick Actions */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
            <div className="px-6 py-4 border-b border-gray-100 dark:border-gray-700">
              <h3 className="text-lg font-bold text-gray-900 dark:text-white flex items-center gap-3">
                <div className="p-2 rounded-lg bg-gradient-to-r from-indigo-500 to-purple-500">
                  <ShieldCheck className="w-5 h-5 text-white" />
                </div>
                빠른 작업
              </h3>
            </div>
            <div className="p-6">
              <div className="grid grid-cols-1 sm:grid-cols-3 gap-4">
                <Link
                  to="/pa/verify"
                  className="flex items-center gap-3 p-4 rounded-xl border border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700 transition-all group"
                >
                  <div className="p-2 rounded-lg bg-teal-100 dark:bg-teal-900/30">
                    <ShieldCheck className="w-5 h-5 text-teal-600 dark:text-teal-400" />
                  </div>
                  <div className="flex-1">
                    <p className="font-semibold text-gray-800 dark:text-gray-200">PA 검증</p>
                    <p className="text-xs text-gray-500">새 검증 수행</p>
                  </div>
                  <ArrowRight className="w-4 h-4 text-gray-400 group-hover:text-gray-600 transition-colors" />
                </Link>

                <Link
                  to="/pa/history"
                  className="flex items-center gap-3 p-4 rounded-xl border border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700 transition-all group"
                >
                  <div className="p-2 rounded-lg bg-purple-100 dark:bg-purple-900/30">
                    <History className="w-5 h-5 text-purple-600 dark:text-purple-400" />
                  </div>
                  <div className="flex-1">
                    <p className="font-semibold text-gray-800 dark:text-gray-200">검증 이력</p>
                    <p className="text-xs text-gray-500">이력 조회</p>
                  </div>
                  <ArrowRight className="w-4 h-4 text-gray-400 group-hover:text-gray-600 transition-colors" />
                </Link>

                <button
                  onClick={fetchStatistics}
                  className="flex items-center gap-3 p-4 rounded-xl border border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700 transition-all group"
                >
                  <div className="p-2 rounded-lg bg-green-100 dark:bg-green-900/30">
                    <Globe className="w-5 h-5 text-green-600 dark:text-green-400" />
                  </div>
                  <div className="flex-1 text-left">
                    <p className="font-semibold text-gray-800 dark:text-gray-200">통계 새로고침</p>
                    <p className="text-xs text-gray-500">최신 데이터 조회</p>
                  </div>
                  <ArrowRight className="w-4 h-4 text-gray-400 group-hover:text-gray-600 transition-colors" />
                </button>
              </div>
            </div>
          </div>
        </>
      )}
    </div>
  );
}

export default PADashboard;
