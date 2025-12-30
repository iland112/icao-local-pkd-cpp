import { useState, useEffect } from 'react';
import { Link } from 'react-router-dom';
import {
  BarChart3,
  Upload,
  FileText,
  CheckCircle,
  XCircle,
  Clock,
  Database,
  Globe,
  TrendingUp,
  Loader2,
  ArrowRight,
} from 'lucide-react';
import { uploadApi } from '@/services/api';
import type { UploadStatisticsOverview } from '@/types';
import { cn } from '@/utils/cn';

export function UploadDashboard() {
  const [stats, setStats] = useState<UploadStatisticsOverview | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetchStatistics();
  }, []);

  const fetchStatistics = async () => {
    setLoading(true);
    try {
      const response = await uploadApi.getStatistics();
      setStats(response.data);
    } catch (error) {
      console.error('Failed to fetch upload statistics:', error);
    } finally {
      setLoading(false);
    }
  };

  const statCards = stats
    ? [
        {
          label: '총 업로드',
          value: stats.totalUploads,
          icon: Upload,
          color: 'from-blue-500 to-indigo-500',
          bgColor: 'bg-blue-50 dark:bg-blue-900/20',
        },
        {
          label: '성공',
          value: stats.successfulUploads,
          icon: CheckCircle,
          color: 'from-green-500 to-emerald-500',
          bgColor: 'bg-green-50 dark:bg-green-900/20',
        },
        {
          label: '실패',
          value: stats.failedUploads,
          icon: XCircle,
          color: 'from-red-500 to-rose-500',
          bgColor: 'bg-red-50 dark:bg-red-900/20',
        },
        {
          label: '총 인증서',
          value: stats.totalCertificates,
          icon: FileText,
          color: 'from-purple-500 to-pink-500',
          bgColor: 'bg-purple-50 dark:bg-purple-900/20',
        },
      ]
    : [];

  const certCards = stats
    ? [
        {
          label: 'CSCA',
          value: stats.cscaCount,
          description: 'Country Signing CA',
          color: 'text-blue-600 dark:text-blue-400',
          bgColor: 'bg-blue-100 dark:bg-blue-900/30',
        },
        {
          label: 'DSC',
          value: stats.dscCount,
          description: 'Document Signer Certificate',
          color: 'text-green-600 dark:text-green-400',
          bgColor: 'bg-green-100 dark:bg-green-900/30',
        },
        {
          label: 'CRL',
          value: stats.crlCount,
          description: 'Certificate Revocation List',
          color: 'text-orange-600 dark:text-orange-400',
          bgColor: 'bg-orange-100 dark:bg-orange-900/30',
        },
        {
          label: '국가',
          value: stats.countriesCount,
          description: 'Registered Countries',
          color: 'text-purple-600 dark:text-purple-400',
          bgColor: 'bg-purple-100 dark:bg-purple-900/30',
        },
      ]
    : [];

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-8">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-violet-500 to-purple-600 shadow-lg">
            <BarChart3 className="w-7 h-7 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">업로드 통계</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              PKD 파일 업로드 및 인증서 현황을 확인합니다.
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

          {/* Certificate Breakdown */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden mb-8">
            <div className="px-6 py-4 border-b border-gray-100 dark:border-gray-700">
              <h3 className="text-lg font-bold text-gray-900 dark:text-white flex items-center gap-3">
                <div className="p-2 rounded-lg bg-gradient-to-r from-indigo-500 to-purple-500">
                  <Database className="w-5 h-5 text-white" />
                </div>
                인증서 유형별 현황
              </h3>
            </div>
            <div className="p-6">
              <div className="grid grid-cols-2 lg:grid-cols-4 gap-4">
                {certCards.map((card) => (
                  <div
                    key={card.label}
                    className={cn(
                      'p-4 rounded-xl border border-gray-100 dark:border-gray-700 hover:shadow-md transition-all',
                      card.bgColor
                    )}
                  >
                    <div className="flex items-center gap-2 mb-2">
                      <span className={cn('text-2xl font-bold', card.color)}>
                        {card.value.toLocaleString()}
                      </span>
                    </div>
                    <p className="font-semibold text-gray-800 dark:text-gray-200">{card.label}</p>
                    <p className="text-xs text-gray-500 dark:text-gray-400">{card.description}</p>
                  </div>
                ))}
              </div>
            </div>
          </div>

          {/* Quick Actions */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
            <div className="px-6 py-4 border-b border-gray-100 dark:border-gray-700">
              <h3 className="text-lg font-bold text-gray-900 dark:text-white flex items-center gap-3">
                <div className="p-2 rounded-lg bg-gradient-to-r from-teal-500 to-cyan-500">
                  <TrendingUp className="w-5 h-5 text-white" />
                </div>
                빠른 작업
              </h3>
            </div>
            <div className="p-6">
              <div className="grid grid-cols-1 sm:grid-cols-3 gap-4">
                <Link
                  to="/upload"
                  className="flex items-center gap-3 p-4 rounded-xl border border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700 transition-all group"
                >
                  <div className="p-2 rounded-lg bg-indigo-100 dark:bg-indigo-900/30">
                    <Upload className="w-5 h-5 text-indigo-600 dark:text-indigo-400" />
                  </div>
                  <div className="flex-1">
                    <p className="font-semibold text-gray-800 dark:text-gray-200">파일 업로드</p>
                    <p className="text-xs text-gray-500">LDIF / Master List</p>
                  </div>
                  <ArrowRight className="w-4 h-4 text-gray-400 group-hover:text-gray-600 transition-colors" />
                </Link>

                <Link
                  to="/upload-history"
                  className="flex items-center gap-3 p-4 rounded-xl border border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700 transition-all group"
                >
                  <div className="p-2 rounded-lg bg-amber-100 dark:bg-amber-900/30">
                    <Clock className="w-5 h-5 text-amber-600 dark:text-amber-400" />
                  </div>
                  <div className="flex-1">
                    <p className="font-semibold text-gray-800 dark:text-gray-200">업로드 이력</p>
                    <p className="text-xs text-gray-500">처리 내역 확인</p>
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

export default UploadDashboard;
