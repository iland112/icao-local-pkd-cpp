import { useState, useEffect } from 'react';
import { Link } from 'react-router-dom';
import {
  BarChart3,
  Upload,
  FileText,
  Clock,
  Database,
  Globe,
  Loader2,
  RefreshCw,
  Shield,
  Key,
  CheckCircle,
  XCircle,
  AlertTriangle,
  HardDrive,
  TrendingUp,
  Award,
  AlertCircle,
} from 'lucide-react';
import { uploadApi } from '@/services/api';
import type { UploadStatisticsOverview } from '@/types';
import { cn } from '@/utils/cn';

export function UploadDashboard() {
  const [stats, setStats] = useState<UploadStatisticsOverview | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetchDashboardData();
  }, []);

  const fetchDashboardData = async () => {
    setLoading(true);
    try {
      const statsResponse = await uploadApi.getStatistics();
      setStats(statsResponse.data);
    } catch (error) {
      console.error('Failed to fetch upload dashboard data:', error);
    } finally {
      setLoading(false);
    }
  };

  // Calculate percentages
  const totalCerts = stats?.totalCertificates || 0;
  const cscaPercent = totalCerts > 0 ? ((stats?.cscaCount || 0) / totalCerts * 100).toFixed(1) : '0';
  const dscPercent = totalCerts > 0 ? ((stats?.dscCount || 0) / totalCerts * 100).toFixed(1) : '0';
  const dscNcPercent = totalCerts > 0 ? ((stats?.dscNcCount || 0) / totalCerts * 100).toFixed(1) : '0';

  const totalValidation = (stats?.validation?.validCount || 0) + (stats?.validation?.invalidCount || 0) + (stats?.validation?.pendingCount || 0);
  const validPercent = totalValidation > 0 ? ((stats?.validation?.validCount || 0) / totalValidation * 100).toFixed(1) : '0';

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-violet-500 to-purple-600 shadow-lg">
            <BarChart3 className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">PKD 통계 대시보드</h1>
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
          {/* Overview Stats - Large Cards */}
          <div className="grid grid-cols-2 lg:grid-cols-4 gap-4 mb-6">
            {/* Total Certificates */}
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-blue-500">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm font-medium text-gray-500 dark:text-gray-400">전체 인증서</p>
                  <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                    {(stats?.totalCertificates ?? 0).toLocaleString()}
                  </p>
                  <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">
                    {stats?.countriesCount || 0}개국
                  </p>
                </div>
                <div className="p-3 rounded-xl bg-blue-50 dark:bg-blue-900/30">
                  <HardDrive className="w-8 h-8 text-blue-500" />
                </div>
              </div>
            </div>

            {/* Total Uploads */}
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-green-500">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm font-medium text-gray-500 dark:text-gray-400">업로드 현황</p>
                  <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                    {((stats?.successfulUploads ?? 0) + (stats?.failedUploads ?? 0)).toLocaleString()}
                  </p>
                  <div className="flex items-center gap-2 mt-1">
                    <span className="text-xs text-green-600 dark:text-green-400">
                      성공 {stats?.successfulUploads ?? 0}
                    </span>
                    <span className="text-xs text-gray-300 dark:text-gray-600">|</span>
                    <span className="text-xs text-red-600 dark:text-red-400">
                      실패 {stats?.failedUploads ?? 0}
                    </span>
                  </div>
                </div>
                <div className="p-3 rounded-xl bg-green-50 dark:bg-green-900/30">
                  <Upload className="w-8 h-8 text-green-500" />
                </div>
              </div>
            </div>

            {/* Validation Rate */}
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-violet-500">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm font-medium text-gray-500 dark:text-gray-400">검증 성공률</p>
                  <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                    {validPercent}%
                  </p>
                  <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">
                    {(stats?.validation?.validCount || 0).toLocaleString()}건 유효
                  </p>
                </div>
                <div className="p-3 rounded-xl bg-violet-50 dark:bg-violet-900/30">
                  <Award className="w-8 h-8 text-violet-500" />
                </div>
              </div>
            </div>

            {/* Countries */}
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-cyan-500">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm font-medium text-gray-500 dark:text-gray-400">등록 국가</p>
                  <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                    {(stats?.countriesCount ?? 0).toLocaleString()}
                  </p>
                  <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">
                    ICAO 회원국
                  </p>
                </div>
                <div className="p-3 rounded-xl bg-cyan-50 dark:bg-cyan-900/30">
                  <Globe className="w-8 h-8 text-cyan-500" />
                </div>
              </div>
            </div>
          </div>

          {/* Certificate Breakdown */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 mb-6">
            <div className="flex items-center gap-2 mb-5">
              <Shield className="w-5 h-5 text-violet-500" />
              <h3 className="text-lg font-bold text-gray-900 dark:text-white">인증서 유형별 현황</h3>
            </div>
            <div className="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-6 gap-4">
              {/* CSCA */}
              <div className="relative overflow-hidden rounded-xl bg-gradient-to-br from-green-50 to-emerald-100 dark:from-green-900/20 dark:to-emerald-900/30 p-4 border border-green-200 dark:border-green-800">
                <div className="flex items-center gap-2 mb-2">
                  <Shield className="w-5 h-5 text-green-600 dark:text-green-400" />
                  <span className="text-sm font-semibold text-green-700 dark:text-green-300">CSCA</span>
                </div>
                <p className="text-2xl font-bold text-green-800 dark:text-green-200">{(stats?.cscaCount ?? 0).toLocaleString()}</p>
                <p className="text-xs text-green-600 dark:text-green-400 mt-1">{cscaPercent}%</p>
                <div className="absolute -right-2 -bottom-2 opacity-10">
                  <Shield className="w-16 h-16 text-green-600" />
                </div>
              </div>

              {/* DSC */}
              <div className="relative overflow-hidden rounded-xl bg-gradient-to-br from-violet-50 to-purple-100 dark:from-violet-900/20 dark:to-purple-900/30 p-4 border border-violet-200 dark:border-violet-800">
                <div className="flex items-center gap-2 mb-2">
                  <Key className="w-5 h-5 text-violet-600 dark:text-violet-400" />
                  <span className="text-sm font-semibold text-violet-700 dark:text-violet-300">DSC</span>
                </div>
                <p className="text-2xl font-bold text-violet-800 dark:text-violet-200">{(stats?.dscCount ?? 0).toLocaleString()}</p>
                <p className="text-xs text-violet-600 dark:text-violet-400 mt-1">{dscPercent}%</p>
                <div className="absolute -right-2 -bottom-2 opacity-10">
                  <Key className="w-16 h-16 text-violet-600" />
                </div>
              </div>

              {/* DSC_NC */}
              <div className="relative overflow-hidden rounded-xl bg-gradient-to-br from-amber-50 to-orange-100 dark:from-amber-900/20 dark:to-orange-900/30 p-4 border border-amber-200 dark:border-amber-800">
                <div className="flex items-center gap-2 mb-2">
                  <AlertTriangle className="w-5 h-5 text-amber-600 dark:text-amber-400" />
                  <span className="text-sm font-semibold text-amber-700 dark:text-amber-300">DSC_NC</span>
                </div>
                <p className="text-2xl font-bold text-amber-800 dark:text-amber-200">{(stats?.dscNcCount ?? 0).toLocaleString()}</p>
                <p className="text-xs text-amber-600 dark:text-amber-400 mt-1">{dscNcPercent}%</p>
                <div className="absolute -right-2 -bottom-2 opacity-10">
                  <AlertTriangle className="w-16 h-16 text-amber-600" />
                </div>
              </div>

              {/* CRL */}
              <div className="relative overflow-hidden rounded-xl bg-gradient-to-br from-orange-50 to-red-100 dark:from-orange-900/20 dark:to-red-900/30 p-4 border border-orange-200 dark:border-orange-800">
                <div className="flex items-center gap-2 mb-2">
                  <FileText className="w-5 h-5 text-orange-600 dark:text-orange-400" />
                  <span className="text-sm font-semibold text-orange-700 dark:text-orange-300">CRL</span>
                </div>
                <p className="text-2xl font-bold text-orange-800 dark:text-orange-200">{(stats?.crlCount ?? 0).toLocaleString()}</p>
                <p className="text-xs text-orange-600 dark:text-orange-400 mt-1">폐지 목록</p>
                <div className="absolute -right-2 -bottom-2 opacity-10">
                  <FileText className="w-16 h-16 text-orange-600" />
                </div>
              </div>

              {/* Master List */}
              <div className="relative overflow-hidden rounded-xl bg-gradient-to-br from-teal-50 to-cyan-100 dark:from-teal-900/20 dark:to-cyan-900/30 p-4 border border-teal-200 dark:border-teal-800">
                <div className="flex items-center gap-2 mb-2">
                  <Database className="w-5 h-5 text-teal-600 dark:text-teal-400" />
                  <span className="text-sm font-semibold text-teal-700 dark:text-teal-300">Master List</span>
                </div>
                <p className="text-2xl font-bold text-teal-800 dark:text-teal-200">{(stats?.mlCount ?? 0).toLocaleString()}</p>
                <p className="text-xs text-teal-600 dark:text-teal-400 mt-1">ICAO ML</p>
                <div className="absolute -right-2 -bottom-2 opacity-10">
                  <Database className="w-16 h-16 text-teal-600" />
                </div>
              </div>

              {/* Countries */}
              <div className="relative overflow-hidden rounded-xl bg-gradient-to-br from-blue-50 to-indigo-100 dark:from-blue-900/20 dark:to-indigo-900/30 p-4 border border-blue-200 dark:border-blue-800">
                <div className="flex items-center gap-2 mb-2">
                  <Globe className="w-5 h-5 text-blue-600 dark:text-blue-400" />
                  <span className="text-sm font-semibold text-blue-700 dark:text-blue-300">국가</span>
                </div>
                <p className="text-2xl font-bold text-blue-800 dark:text-blue-200">{(stats?.countriesCount ?? 0).toLocaleString()}</p>
                <p className="text-xs text-blue-600 dark:text-blue-400 mt-1">등록 국가</p>
                <div className="absolute -right-2 -bottom-2 opacity-10">
                  <Globe className="w-16 h-16 text-blue-600" />
                </div>
              </div>
            </div>
          </div>

          {/* Validation Statistics */}
          {stats?.validation && (
            <div className="grid grid-cols-1 lg:grid-cols-2 gap-5">
              {/* Validation Status */}
              <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5">
                <div className="flex items-center gap-2 mb-5">
                  <CheckCircle className="w-5 h-5 text-green-500" />
                  <h3 className="text-lg font-bold text-gray-900 dark:text-white">검증 상태</h3>
                </div>
                <div className="space-y-4">
                  {/* Valid */}
                  <div className="flex items-center gap-4">
                    <div className="flex items-center gap-2 w-24">
                      <CheckCircle className="w-4 h-4 text-green-500" />
                      <span className="text-sm font-medium text-gray-700 dark:text-gray-300">유효</span>
                    </div>
                    <div className="flex-1">
                      <div className="h-8 bg-gray-100 dark:bg-gray-700 rounded-lg overflow-hidden">
                        <div
                          className="h-full bg-gradient-to-r from-green-400 to-emerald-500 rounded-lg flex items-center justify-end pr-3"
                          style={{ width: `${Math.max(Number(validPercent), 5)}%` }}
                        >
                          <span className="text-xs font-bold text-white">{(stats.validation?.validCount ?? 0).toLocaleString()}</span>
                        </div>
                      </div>
                    </div>
                    <span className="text-sm font-semibold text-green-600 dark:text-green-400 w-16 text-right">{validPercent}%</span>
                  </div>

                  {/* Invalid */}
                  <div className="flex items-center gap-4">
                    <div className="flex items-center gap-2 w-24">
                      <XCircle className="w-4 h-4 text-red-500" />
                      <span className="text-sm font-medium text-gray-700 dark:text-gray-300">무효</span>
                    </div>
                    <div className="flex-1">
                      <div className="h-8 bg-gray-100 dark:bg-gray-700 rounded-lg overflow-hidden">
                        <div
                          className="h-full bg-gradient-to-r from-red-400 to-rose-500 rounded-lg flex items-center justify-end pr-3"
                          style={{ width: `${Math.max(totalValidation > 0 ? (stats.validation?.invalidCount || 0) / totalValidation * 100 : 0, 5)}%` }}
                        >
                          <span className="text-xs font-bold text-white">{(stats.validation?.invalidCount ?? 0).toLocaleString()}</span>
                        </div>
                      </div>
                    </div>
                    <span className="text-sm font-semibold text-red-600 dark:text-red-400 w-16 text-right">
                      {totalValidation > 0 ? ((stats.validation?.invalidCount || 0) / totalValidation * 100).toFixed(1) : '0'}%
                    </span>
                  </div>

                  {/* Pending */}
                  <div className="flex items-center gap-4">
                    <div className="flex items-center gap-2 w-24">
                      <Clock className="w-4 h-4 text-yellow-500" />
                      <span className="text-sm font-medium text-gray-700 dark:text-gray-300">대기</span>
                    </div>
                    <div className="flex-1">
                      <div className="h-8 bg-gray-100 dark:bg-gray-700 rounded-lg overflow-hidden">
                        <div
                          className="h-full bg-gradient-to-r from-yellow-400 to-amber-500 rounded-lg flex items-center justify-end pr-3"
                          style={{ width: `${Math.max(totalValidation > 0 ? (stats.validation?.pendingCount || 0) / totalValidation * 100 : 0, 5)}%` }}
                        >
                          <span className="text-xs font-bold text-white">{(stats.validation?.pendingCount ?? 0).toLocaleString()}</span>
                        </div>
                      </div>
                    </div>
                    <span className="text-sm font-semibold text-yellow-600 dark:text-yellow-400 w-16 text-right">
                      {totalValidation > 0 ? ((stats.validation?.pendingCount || 0) / totalValidation * 100).toFixed(1) : '0'}%
                    </span>
                  </div>

                  {/* Error */}
                  <div className="flex items-center gap-4">
                    <div className="flex items-center gap-2 w-24">
                      <AlertCircle className="w-4 h-4 text-gray-500" />
                      <span className="text-sm font-medium text-gray-700 dark:text-gray-300">오류</span>
                    </div>
                    <div className="flex-1">
                      <div className="h-8 bg-gray-100 dark:bg-gray-700 rounded-lg overflow-hidden">
                        <div
                          className="h-full bg-gradient-to-r from-gray-400 to-gray-500 rounded-lg flex items-center justify-end pr-3"
                          style={{ width: `${Math.max(totalValidation > 0 ? (stats.validation?.errorCount || 0) / totalValidation * 100 : 0, 5)}%` }}
                        >
                          <span className="text-xs font-bold text-white">{(stats.validation?.errorCount ?? 0).toLocaleString()}</span>
                        </div>
                      </div>
                    </div>
                    <span className="text-sm font-semibold text-gray-600 dark:text-gray-400 w-16 text-right">
                      {totalValidation > 0 ? ((stats.validation?.errorCount || 0) / totalValidation * 100).toFixed(1) : '0'}%
                    </span>
                  </div>
                </div>
              </div>

              {/* Trust Chain Validation */}
              <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5">
                <div className="flex items-center gap-2 mb-5">
                  <TrendingUp className="w-5 h-5 text-violet-500" />
                  <h3 className="text-lg font-bold text-gray-900 dark:text-white">Trust Chain 검증</h3>
                </div>
                <div className="grid grid-cols-2 gap-3">
                  <div className="p-4 rounded-xl bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800">
                    <div className="flex items-center gap-2 mb-2">
                      <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400" />
                      <span className="text-sm font-medium text-green-700 dark:text-green-300">검증 성공</span>
                    </div>
                    <p className="text-2xl font-bold text-green-800 dark:text-green-200">{(stats.validation?.trustChainValidCount ?? 0).toLocaleString()}</p>
                  </div>
                  <div className="p-4 rounded-xl bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800">
                    <div className="flex items-center gap-2 mb-2">
                      <XCircle className="w-5 h-5 text-red-600 dark:text-red-400" />
                      <span className="text-sm font-medium text-red-700 dark:text-red-300">검증 실패</span>
                    </div>
                    <p className="text-2xl font-bold text-red-800 dark:text-red-200">{(stats.validation?.trustChainInvalidCount ?? 0).toLocaleString()}</p>
                  </div>
                  <div className="p-4 rounded-xl bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800">
                    <div className="flex items-center gap-2 mb-2">
                      <AlertTriangle className="w-5 h-5 text-amber-600 dark:text-amber-400" />
                      <span className="text-sm font-medium text-amber-700 dark:text-amber-300">CSCA 미발견</span>
                    </div>
                    <p className="text-2xl font-bold text-amber-800 dark:text-amber-200">{(stats.validation?.cscaNotFoundCount ?? 0).toLocaleString()}</p>
                  </div>
                  <div className="p-4 rounded-xl bg-orange-50 dark:bg-orange-900/20 border border-orange-200 dark:border-orange-800">
                    <div className="flex items-center gap-2 mb-2">
                      <Clock className="w-5 h-5 text-orange-600 dark:text-orange-400" />
                      <span className="text-sm font-medium text-orange-700 dark:text-orange-300">만료됨</span>
                    </div>
                    <p className="text-2xl font-bold text-orange-800 dark:text-orange-200">{(stats.validation?.expiredCount ?? 0).toLocaleString()}</p>
                  </div>
                  <div className="col-span-2 p-4 rounded-xl bg-gray-50 dark:bg-gray-700/50 border border-gray-200 dark:border-gray-600">
                    <div className="flex items-center justify-between">
                      <div className="flex items-center gap-2">
                        <XCircle className="w-5 h-5 text-gray-600 dark:text-gray-400" />
                        <span className="text-sm font-medium text-gray-700 dark:text-gray-300">폐지됨 (Revoked)</span>
                      </div>
                      <p className="text-xl font-bold text-gray-800 dark:text-gray-200">{(stats.validation?.revokedCount ?? 0).toLocaleString()}</p>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          )}
        </>
      )}
    </div>
  );
}

export default UploadDashboard;
