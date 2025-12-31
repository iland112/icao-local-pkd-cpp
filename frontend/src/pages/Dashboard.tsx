import { useState, useEffect } from 'react';
import { Link } from 'react-router-dom';
import {
  ShieldCheck,
  Database,
  Server,
  Upload,
  Clock,
  BarChart3,
  CheckCircle,
  IdCard,
  History,
  PresentationIcon,
  Calendar,
  Zap,
  FileText,
  Key,
  User,
  GraduationCap,
  XCircle,
  AlertTriangle,
  HardDrive,
  Globe,
} from 'lucide-react';
import { healthApi, ldapApi, uploadApi } from '@/services/api';
import { cn } from '@/utils/cn';
import type { UploadStatisticsOverview } from '@/types';

interface ConnectionStatus {
  connected: boolean;
  message: string;
  testing: boolean;
}

export function Dashboard() {
  const [currentDate, setCurrentDate] = useState('');
  const [currentTime, setCurrentTime] = useState('');
  const [dbStatus, setDbStatus] = useState<ConnectionStatus>({
    connected: false,
    message: '연결 상태를 확인하세요.',
    testing: false,
  });
  const [ldapStatus, setLdapStatus] = useState<ConnectionStatus>({
    connected: false,
    message: '연결 상태를 확인하세요.',
    testing: false,
  });
  const [uploadStats, setUploadStats] = useState<UploadStatisticsOverview | null>(null);

  useEffect(() => {
    const updateDateTime = () => {
      const now = new Date();
      setCurrentDate(now.toLocaleDateString('ko-KR', {
        year: 'numeric',
        month: 'long',
        day: 'numeric',
        weekday: 'long',
      }));
      setCurrentTime(now.toLocaleTimeString('ko-KR'));
    };

    updateDateTime();
    const interval = setInterval(updateDateTime, 1000);
    return () => clearInterval(interval);
  }, []);

  // Auto-check connections and load stats on mount
  useEffect(() => {
    testDatabaseConnection();
    testLdapConnection();
    loadUploadStats();
  }, []);

  const loadUploadStats = async () => {
    try {
      const response = await uploadApi.getStatistics();
      setUploadStats(response.data);
    } catch (error) {
      console.error('Failed to load upload statistics:', error);
    }
  };

  const testDatabaseConnection = async () => {
    setDbStatus((prev) => ({ ...prev, testing: true }));
    try {
      const response = await healthApi.checkDatabase();
      setDbStatus({
        connected: response.data.status === 'UP',
        message: response.data.status === 'UP'
          ? `PostgreSQL ${response.data.version || ''} 연결됨`
          : '연결 실패',
        testing: false,
      });
    } catch {
      setDbStatus({
        connected: false,
        message: '연결 실패: 서버에 연결할 수 없습니다.',
        testing: false,
      });
    }
  };

  const testLdapConnection = async () => {
    setLdapStatus((prev) => ({ ...prev, testing: true }));
    try {
      const response = await ldapApi.getHealth();
      setLdapStatus({
        connected: response.data.status === 'UP',
        message: response.data.status === 'UP'
          ? `OpenLDAP 연결됨 (${response.data.responseTime}ms)`
          : '연결 실패',
        testing: false,
      });
    } catch {
      setLdapStatus({
        connected: false,
        message: '연결 실패: 서버에 연결할 수 없습니다.',
        testing: false,
      });
    }
  };

  return (
    <div className="w-full px-4 lg:px-6 py-6">
      {/* Header Section */}
      <div className="mb-8">
        <div className="relative overflow-hidden rounded-2xl shadow-2xl">
          {/* Background Gradient */}
          <div className="absolute inset-0 bg-gradient-to-br from-blue-600 via-indigo-600 to-purple-700"></div>
          {/* Decorative Elements */}
          <div className="absolute inset-0 opacity-30">
            <div className="absolute top-0 right-0 w-96 h-96 bg-white/10 rounded-full blur-3xl transform translate-x-1/2 -translate-y-1/2"></div>
            <div className="absolute bottom-0 left-0 w-64 h-64 bg-purple-400/20 rounded-full blur-2xl transform -translate-x-1/3 translate-y-1/3"></div>
          </div>
          {/* Content */}
          <div className="relative p-6 lg:p-8 text-white">
            <div className="flex items-center justify-between">
              <div className="flex-1">
                <h1 className="text-3xl lg:text-4xl font-bold mb-3 flex items-center gap-4">
                  <div className="p-3 bg-white/20 backdrop-blur-sm rounded-xl">
                    <ShieldCheck className="w-8 h-8" />
                  </div>
                  ICAO PKD Local Manager
                </h1>
                <p className="text-blue-100 text-lg mb-1">
                  ICAO Public Key Directory 로컬 평가 및 관리 시스템
                </p>
                <p className="text-blue-200/80 text-sm">
                  ePassport 인증서 관리 및 Passive Authentication 검증 플랫폼
                </p>
                {/* Standards Badges */}
                <div className="flex flex-wrap gap-2 mt-5">
                  {[
                    { icon: GraduationCap, label: 'ICAO Doc 9303' },
                    { icon: FileText, label: 'RFC 5652 CMS' },
                    { icon: Key, label: 'RFC 5280 X.509' },
                    { icon: User, label: 'ISO/IEC 19794-5' },
                  ].map((badge) => (
                    <span
                      key={badge.label}
                      className="inline-flex items-center px-3 py-1.5 rounded-lg text-xs font-medium bg-white/10 backdrop-blur-sm text-white border border-white/20 hover:bg-white/20 transition-colors"
                    >
                      <badge.icon className="w-4 h-4 mr-1.5" />
                      {badge.label}
                    </span>
                  ))}
                </div>
              </div>
              <div className="text-right hidden lg:block ml-8">
                <div className="bg-white/10 backdrop-blur-sm rounded-xl p-4 border border-white/20">
                  <div className="text-white/90 text-sm flex items-center justify-end gap-2 mb-2">
                    <Calendar className="w-4 h-4" />
                    <span>{currentDate}</span>
                  </div>
                  <div className="text-white/90 text-sm flex items-center justify-end gap-2">
                    <Clock className="w-4 h-4" />
                    <span>{currentTime}</span>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* System Connection Status */}
      <div className="mb-8 bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        <div className="px-6 py-4 border-b border-gray-100 dark:border-gray-700">
          <h3 className="text-lg font-bold text-gray-900 dark:text-white flex items-center gap-3">
            <div className="p-2 rounded-lg bg-gradient-to-r from-emerald-400 to-teal-500">
              <Zap className="w-5 h-5 text-white" />
            </div>
            시스템 연결 상태
          </h3>
        </div>
        <div className="p-6">
          <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
            {/* PostgreSQL Status */}
            <div className={cn(
              'relative p-6 rounded-xl border transition-all duration-300 hover:shadow-lg overflow-hidden',
              'bg-gradient-to-br from-blue-50/50 to-indigo-50/50 dark:from-gray-700/50 dark:to-gray-700/50 border-blue-100 dark:border-gray-600',
              dbStatus.connected ? 'hover:border-blue-300' : 'hover:border-red-300'
            )}>
              <div className="absolute top-0 right-0 w-24 h-24 opacity-5 pointer-events-none">
                <Database className="w-full h-full text-blue-500" />
              </div>
              <div className="relative z-10 flex items-start gap-4">
                <span className={cn(
                  'shrink-0 p-2.5 rounded-lg transition-colors',
                  dbStatus.connected
                    ? 'bg-green-100 dark:bg-green-900/50'
                    : 'bg-red-100 dark:bg-red-900/50'
                )}>
                  <Database className={cn(
                    'w-6 h-6',
                    dbStatus.connected ? 'text-green-500' : 'text-red-500'
                  )} />
                </span>
                <div className="flex-1 min-w-0">
                  <div className="flex items-center gap-2 mb-1">
                    <h4 className="font-semibold text-gray-800 dark:text-gray-200">PostgreSQL</h4>
                    <span className={cn(
                      'inline-flex items-center gap-x-1 py-0.5 px-2 rounded-full text-xs font-medium',
                      dbStatus.connected
                        ? 'bg-green-100 text-green-800 dark:bg-green-800/30 dark:text-green-400'
                        : 'bg-red-100 text-red-800 dark:bg-red-800/30 dark:text-red-400'
                    )}>
                      <span className="relative flex size-1.5">
                        <span className={cn(
                          'absolute inline-flex size-full rounded-full opacity-75 animate-ping',
                          dbStatus.connected ? 'bg-green-400' : 'bg-red-400'
                        )}></span>
                        <span className={cn(
                          'relative inline-flex rounded-full size-1.5',
                          dbStatus.connected ? 'bg-green-500' : 'bg-red-500'
                        )}></span>
                      </span>
                      {dbStatus.connected ? 'Active' : 'Inactive'}
                    </span>
                  </div>
                  <p className="text-sm text-gray-600 dark:text-gray-400 mb-3">{dbStatus.message}</p>
                  <button
                    onClick={testDatabaseConnection}
                    disabled={dbStatus.testing}
                    className="py-2 px-4 inline-flex items-center gap-x-2 text-sm font-medium rounded-lg border transition disabled:opacity-50 border-blue-200 bg-blue-50 text-blue-700 hover:bg-blue-100 dark:border-blue-600 dark:bg-blue-600/20 dark:text-blue-400 dark:hover:bg-blue-600/30"
                  >
                    {dbStatus.testing ? (
                      <svg className="animate-spin size-4" viewBox="0 0 24 24" fill="none">
                        <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4"></circle>
                        <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
                      </svg>
                    ) : (
                      <Zap className="w-4 h-4" />
                    )}
                    {dbStatus.testing ? '테스트 중...' : '연결 테스트'}
                  </button>
                </div>
              </div>
            </div>

            {/* OpenLDAP Status */}
            <div className={cn(
              'relative p-6 rounded-xl border transition-all duration-300 hover:shadow-lg overflow-hidden',
              'bg-gradient-to-br from-orange-50/50 to-amber-50/50 dark:from-gray-700/50 dark:to-gray-700/50 border-orange-100 dark:border-gray-600',
              ldapStatus.connected ? 'hover:border-orange-300' : 'hover:border-red-300'
            )}>
              <div className="absolute top-0 right-0 w-24 h-24 opacity-5 pointer-events-none">
                <Server className="w-full h-full text-orange-500" />
              </div>
              <div className="relative z-10 flex items-start gap-4">
                <span className={cn(
                  'shrink-0 p-2.5 rounded-lg transition-colors',
                  ldapStatus.connected
                    ? 'bg-green-100 dark:bg-green-900/50'
                    : 'bg-red-100 dark:bg-red-900/50'
                )}>
                  <Server className={cn(
                    'w-6 h-6',
                    ldapStatus.connected ? 'text-green-500' : 'text-red-500'
                  )} />
                </span>
                <div className="flex-1 min-w-0">
                  <div className="flex items-center gap-2 mb-1">
                    <h4 className="font-semibold text-gray-800 dark:text-gray-200">OpenLDAP</h4>
                    <span className={cn(
                      'inline-flex items-center gap-x-1 py-0.5 px-2 rounded-full text-xs font-medium',
                      ldapStatus.connected
                        ? 'bg-green-100 text-green-800 dark:bg-green-800/30 dark:text-green-400'
                        : 'bg-red-100 text-red-800 dark:bg-red-800/30 dark:text-red-400'
                    )}>
                      <span className="relative flex size-1.5">
                        <span className={cn(
                          'absolute inline-flex size-full rounded-full opacity-75 animate-ping',
                          ldapStatus.connected ? 'bg-green-400' : 'bg-red-400'
                        )}></span>
                        <span className={cn(
                          'relative inline-flex rounded-full size-1.5',
                          ldapStatus.connected ? 'bg-green-500' : 'bg-red-500'
                        )}></span>
                      </span>
                      {ldapStatus.connected ? 'Active' : 'Inactive'}
                    </span>
                  </div>
                  <p className="text-sm text-gray-600 dark:text-gray-400 mb-3">{ldapStatus.message}</p>
                  <button
                    onClick={testLdapConnection}
                    disabled={ldapStatus.testing}
                    className="py-2 px-4 inline-flex items-center gap-x-2 text-sm font-medium rounded-lg border transition disabled:opacity-50 border-orange-200 bg-orange-50 text-orange-700 hover:bg-orange-100 dark:border-orange-600 dark:bg-orange-600/20 dark:text-orange-400 dark:hover:bg-orange-600/30"
                  >
                    {ldapStatus.testing ? (
                      <svg className="animate-spin size-4" viewBox="0 0 24 24" fill="none">
                        <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4"></circle>
                        <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
                      </svg>
                    ) : (
                      <Zap className="w-4 h-4" />
                    )}
                    {ldapStatus.testing ? '테스트 중...' : '연결 테스트'}
                  </button>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* Certificate & Validation Statistics */}
      {uploadStats && (
        <div className="mb-8 bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
          <div className="px-6 py-4 border-b border-gray-100 dark:border-gray-700">
            <h3 className="text-lg font-bold text-gray-900 dark:text-white flex items-center gap-3">
              <div className="p-2 rounded-lg bg-gradient-to-r from-blue-500 to-cyan-500">
                <BarChart3 className="w-5 h-5 text-white" />
              </div>
              인증서 및 검증 통계
            </h3>
          </div>
          <div className="p-6">
            {/* Certificate Counts */}
            <div className="mb-6">
              <h4 className="text-sm font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wide mb-4">인증서 현황</h4>
              <div className="grid grid-cols-2 md:grid-cols-4 lg:grid-cols-6 gap-4">
                <div className="p-4 rounded-xl bg-gradient-to-br from-blue-50 to-blue-100 dark:from-blue-900/20 dark:to-blue-800/20 border border-blue-200 dark:border-blue-700">
                  <div className="flex items-center gap-2 mb-2">
                    <HardDrive className="w-4 h-4 text-blue-600 dark:text-blue-400" />
                    <span className="text-xs font-medium text-blue-600 dark:text-blue-400">전체</span>
                  </div>
                  <p className="text-2xl font-bold text-blue-700 dark:text-blue-300">{(uploadStats.totalCertificates ?? 0).toLocaleString()}</p>
                </div>
                <div className="p-4 rounded-xl bg-gradient-to-br from-green-50 to-green-100 dark:from-green-900/20 dark:to-green-800/20 border border-green-200 dark:border-green-700">
                  <div className="flex items-center gap-2 mb-2">
                    <ShieldCheck className="w-4 h-4 text-green-600 dark:text-green-400" />
                    <span className="text-xs font-medium text-green-600 dark:text-green-400">CSCA</span>
                  </div>
                  <p className="text-2xl font-bold text-green-700 dark:text-green-300">{(uploadStats.cscaCount ?? 0).toLocaleString()}</p>
                </div>
                <div className="p-4 rounded-xl bg-gradient-to-br from-violet-50 to-violet-100 dark:from-violet-900/20 dark:to-violet-800/20 border border-violet-200 dark:border-violet-700">
                  <div className="flex items-center gap-2 mb-2">
                    <Key className="w-4 h-4 text-violet-600 dark:text-violet-400" />
                    <span className="text-xs font-medium text-violet-600 dark:text-violet-400">DSC</span>
                  </div>
                  <p className="text-2xl font-bold text-violet-700 dark:text-violet-300">{(uploadStats.dscCount ?? 0).toLocaleString()}</p>
                </div>
                <div className="p-4 rounded-xl bg-gradient-to-br from-amber-50 to-amber-100 dark:from-amber-900/20 dark:to-amber-800/20 border border-amber-200 dark:border-amber-700">
                  <div className="flex items-center gap-2 mb-2">
                    <AlertTriangle className="w-4 h-4 text-amber-600 dark:text-amber-400" />
                    <span className="text-xs font-medium text-amber-600 dark:text-amber-400">DSC_NC</span>
                  </div>
                  <p className="text-2xl font-bold text-amber-700 dark:text-amber-300">{(uploadStats.dscNcCount ?? 0).toLocaleString()}</p>
                </div>
                <div className="p-4 rounded-xl bg-gradient-to-br from-orange-50 to-orange-100 dark:from-orange-900/20 dark:to-orange-800/20 border border-orange-200 dark:border-orange-700">
                  <div className="flex items-center gap-2 mb-2">
                    <FileText className="w-4 h-4 text-orange-600 dark:text-orange-400" />
                    <span className="text-xs font-medium text-orange-600 dark:text-orange-400">CRL</span>
                  </div>
                  <p className="text-2xl font-bold text-orange-700 dark:text-orange-300">{(uploadStats.crlCount ?? 0).toLocaleString()}</p>
                </div>
                <div className="p-4 rounded-xl bg-gradient-to-br from-cyan-50 to-cyan-100 dark:from-cyan-900/20 dark:to-cyan-800/20 border border-cyan-200 dark:border-cyan-700">
                  <div className="flex items-center gap-2 mb-2">
                    <Globe className="w-4 h-4 text-cyan-600 dark:text-cyan-400" />
                    <span className="text-xs font-medium text-cyan-600 dark:text-cyan-400">국가</span>
                  </div>
                  <p className="text-2xl font-bold text-cyan-700 dark:text-cyan-300">{(uploadStats.countriesCount ?? 0).toLocaleString()}</p>
                </div>
              </div>
            </div>

            {/* Validation Statistics */}
            {uploadStats.validation && (
              <div>
                <h4 className="text-sm font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wide mb-4">검증 결과</h4>
                <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
                  {/* Overall Validation Status */}
                  <div className="p-5 rounded-xl bg-gray-50 dark:bg-gray-700/50 border border-gray-200 dark:border-gray-600">
                    <h5 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-4">검증 상태</h5>
                    <div className="grid grid-cols-2 gap-3">
                      <div className="flex items-center gap-3 p-3 rounded-lg bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-700">
                        <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400" />
                        <div>
                          <p className="text-xs text-green-600 dark:text-green-400">유효</p>
                          <p className="text-lg font-bold text-green-700 dark:text-green-300">{(uploadStats.validation?.validCount ?? 0).toLocaleString()}</p>
                        </div>
                      </div>
                      <div className="flex items-center gap-3 p-3 rounded-lg bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-700">
                        <XCircle className="w-5 h-5 text-red-600 dark:text-red-400" />
                        <div>
                          <p className="text-xs text-red-600 dark:text-red-400">무효</p>
                          <p className="text-lg font-bold text-red-700 dark:text-red-300">{(uploadStats.validation?.invalidCount ?? 0).toLocaleString()}</p>
                        </div>
                      </div>
                      <div className="flex items-center gap-3 p-3 rounded-lg bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-700">
                        <Clock className="w-5 h-5 text-yellow-600 dark:text-yellow-400" />
                        <div>
                          <p className="text-xs text-yellow-600 dark:text-yellow-400">대기</p>
                          <p className="text-lg font-bold text-yellow-700 dark:text-yellow-300">{(uploadStats.validation?.pendingCount ?? 0).toLocaleString()}</p>
                        </div>
                      </div>
                      <div className="flex items-center gap-3 p-3 rounded-lg bg-gray-100 dark:bg-gray-600/50 border border-gray-300 dark:border-gray-500">
                        <AlertTriangle className="w-5 h-5 text-gray-600 dark:text-gray-400" />
                        <div>
                          <p className="text-xs text-gray-600 dark:text-gray-400">오류</p>
                          <p className="text-lg font-bold text-gray-700 dark:text-gray-300">{(uploadStats.validation?.errorCount ?? 0).toLocaleString()}</p>
                        </div>
                      </div>
                    </div>
                  </div>

                  {/* Trust Chain & Expiration */}
                  <div className="p-5 rounded-xl bg-gray-50 dark:bg-gray-700/50 border border-gray-200 dark:border-gray-600">
                    <h5 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-4">Trust Chain 검증</h5>
                    <div className="space-y-3">
                      <div className="flex items-center justify-between p-3 rounded-lg bg-white dark:bg-gray-600/50 border border-gray-200 dark:border-gray-500">
                        <div className="flex items-center gap-2">
                          <CheckCircle className="w-4 h-4 text-green-500" />
                          <span className="text-sm text-gray-700 dark:text-gray-300">Trust Chain 성공</span>
                        </div>
                        <span className="font-bold text-green-600 dark:text-green-400">{(uploadStats.validation?.trustChainValidCount ?? 0).toLocaleString()}</span>
                      </div>
                      <div className="flex items-center justify-between p-3 rounded-lg bg-white dark:bg-gray-600/50 border border-gray-200 dark:border-gray-500">
                        <div className="flex items-center gap-2">
                          <XCircle className="w-4 h-4 text-red-500" />
                          <span className="text-sm text-gray-700 dark:text-gray-300">Trust Chain 실패</span>
                        </div>
                        <span className="font-bold text-red-600 dark:text-red-400">{(uploadStats.validation?.trustChainInvalidCount ?? 0).toLocaleString()}</span>
                      </div>
                      <div className="flex items-center justify-between p-3 rounded-lg bg-white dark:bg-gray-600/50 border border-gray-200 dark:border-gray-500">
                        <div className="flex items-center gap-2">
                          <AlertTriangle className="w-4 h-4 text-amber-500" />
                          <span className="text-sm text-gray-700 dark:text-gray-300">CSCA 미발견</span>
                        </div>
                        <span className="font-bold text-amber-600 dark:text-amber-400">{(uploadStats.validation?.cscaNotFoundCount ?? 0).toLocaleString()}</span>
                      </div>
                      <div className="flex items-center justify-between p-3 rounded-lg bg-white dark:bg-gray-600/50 border border-gray-200 dark:border-gray-500">
                        <div className="flex items-center gap-2">
                          <Clock className="w-4 h-4 text-orange-500" />
                          <span className="text-sm text-gray-700 dark:text-gray-300">만료됨</span>
                        </div>
                        <span className="font-bold text-orange-600 dark:text-orange-400">{(uploadStats.validation?.expiredCount ?? 0).toLocaleString()}</span>
                      </div>
                      <div className="flex items-center justify-between p-3 rounded-lg bg-white dark:bg-gray-600/50 border border-gray-200 dark:border-gray-500">
                        <div className="flex items-center gap-2">
                          <XCircle className="w-4 h-4 text-red-500" />
                          <span className="text-sm text-gray-700 dark:text-gray-300">폐지됨</span>
                        </div>
                        <span className="font-bold text-red-600 dark:text-red-400">{(uploadStats.validation?.revokedCount ?? 0).toLocaleString()}</span>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            )}
          </div>
        </div>
      )}

      {/* Main Features */}
      <div className="mb-8 bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        <div className="px-6 py-4 border-b border-gray-100 dark:border-gray-700">
          <h3 className="text-lg font-bold text-gray-900 dark:text-white flex items-center gap-3">
            <div className="p-2 rounded-lg bg-gradient-to-r from-violet-500 to-purple-500">
              <Database className="w-5 h-5 text-white" />
            </div>
            주요 기능
          </h3>
        </div>
        <div className="p-6">
          <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
            {/* PKD Upload Card */}
            <div className="relative p-6 rounded-xl border transition-all duration-300 hover:shadow-lg overflow-hidden bg-gradient-to-br from-violet-50/50 to-purple-50/50 dark:from-gray-700/50 dark:to-gray-700/50 border-violet-100 dark:border-gray-600 hover:border-violet-300">
              <div className="absolute top-0 right-0 w-24 h-24 opacity-5 pointer-events-none">
                <Database className="w-full h-full text-violet-500" />
              </div>
              <div className="relative z-10">
                <h4 className="font-semibold mb-2 flex items-center gap-2 text-gray-800 dark:text-gray-200">
                  <span className="shrink-0 p-2 rounded-lg bg-violet-100 dark:bg-violet-900/50">
                    <Database className="w-5 h-5 text-violet-500" />
                  </span>
                  PKD Upload
                </h4>
                <p className="text-sm text-gray-600 dark:text-gray-400 mb-4">
                  ICAO PKD에서 다운로드한 LDIF, Master List 파일을 업로드하고, CSCA/DSC/CRL 인증서를 파싱하여 검증 후 LDAP 서버에 저장합니다.
                </p>
                <ul className="text-sm space-y-2 text-gray-600 dark:text-gray-400 mb-4">
                  {['LDIF / Master List 파일 파싱', '인증서 Trust Chain 검증', 'LDAP 자동 등록 + 실시간 진행 상황 (SSE)'].map((item) => (
                    <li key={item} className="flex items-center gap-2">
                      <CheckCircle className="w-4 h-4 text-violet-500" />
                      {item}
                    </li>
                  ))}
                </ul>
                <div className="flex flex-wrap gap-2">
                  <Link
                    to="/upload"
                    className="py-2.5 px-5 inline-flex items-center gap-x-2 text-sm font-medium rounded-lg bg-gradient-to-r from-violet-600 to-purple-600 text-white hover:from-violet-700 hover:to-purple-700 hover:shadow-md transition-all duration-200"
                  >
                    <Upload className="w-4 h-4" />
                    파일 업로드
                  </Link>
                  <Link
                    to="/upload-history"
                    className="py-2 px-4 inline-flex items-center gap-x-2 text-sm font-medium rounded-lg border transition border-gray-200 bg-white text-gray-700 hover:bg-gray-50 dark:border-gray-600 dark:bg-gray-700/50 dark:text-gray-300 dark:hover:bg-gray-600/50"
                  >
                    <Clock className="w-4 h-4" />
                    업로드 이력
                  </Link>
                  <Link
                    to="/upload-dashboard"
                    className="py-2 px-4 inline-flex items-center gap-x-2 text-sm font-medium rounded-lg border transition border-gray-200 bg-white text-gray-700 hover:bg-gray-50 dark:border-gray-600 dark:bg-gray-700/50 dark:text-gray-300 dark:hover:bg-gray-600/50"
                  >
                    <BarChart3 className="w-4 h-4" />
                    통계
                  </Link>
                </div>
              </div>
            </div>

            {/* Passive Authentication Card */}
            <div className="relative p-6 rounded-xl border transition-all duration-300 hover:shadow-lg overflow-hidden bg-gradient-to-br from-teal-50/50 to-cyan-50/50 dark:from-gray-700/50 dark:to-gray-700/50 border-teal-100 dark:border-gray-600 hover:border-teal-300">
              <div className="absolute top-0 right-0 w-24 h-24 opacity-5 pointer-events-none">
                <IdCard className="w-full h-full text-teal-500" />
              </div>
              <div className="relative z-10">
                <h4 className="font-semibold mb-2 flex items-center gap-2 text-gray-800 dark:text-gray-200">
                  <span className="shrink-0 p-2 rounded-lg bg-teal-100 dark:bg-teal-900/50">
                    <IdCard className="w-5 h-5 text-teal-500" />
                  </span>
                  Passive Authentication
                </h4>
                <p className="text-sm text-gray-600 dark:text-gray-400 mb-4">
                  전자여권 칩의 SOD(Security Object Document), DG(Data Group)를 업로드하여 ICAO 9303 표준에 따른 Passive Authentication을 수행합니다.
                </p>
                <ul className="text-sm space-y-2 text-gray-600 dark:text-gray-400 mb-4">
                  {['SOD CMS 서명 검증', 'DSC → CSCA Trust Chain 검증', 'Data Group 해시 무결성 검증'].map((item) => (
                    <li key={item} className="flex items-center gap-2">
                      <CheckCircle className="w-4 h-4 text-teal-500" />
                      {item}
                    </li>
                  ))}
                </ul>
                <div className="flex flex-wrap gap-2">
                  <Link
                    to="/pa/verify"
                    className="py-2.5 px-5 inline-flex items-center gap-x-2 text-sm font-medium rounded-lg bg-gradient-to-r from-teal-500 to-cyan-500 text-white hover:from-teal-600 hover:to-cyan-600 hover:shadow-md transition-all duration-200"
                  >
                    <ShieldCheck className="w-4 h-4" />
                    PA 검증
                  </Link>
                  <Link
                    to="/pa/history"
                    className="py-2 px-4 inline-flex items-center gap-x-2 text-sm font-medium rounded-lg border transition border-gray-200 bg-white text-gray-700 hover:bg-gray-50 dark:border-gray-600 dark:bg-gray-700/50 dark:text-gray-300 dark:hover:bg-gray-600/50"
                  >
                    <History className="w-4 h-4" />
                    검증 이력
                  </Link>
                  <Link
                    to="/pa/dashboard"
                    className="py-2 px-4 inline-flex items-center gap-x-2 text-sm font-medium rounded-lg border transition border-gray-200 bg-white text-gray-700 hover:bg-gray-50 dark:border-gray-600 dark:bg-gray-700/50 dark:text-gray-300 dark:hover:bg-gray-600/50"
                  >
                    <PresentationIcon className="w-4 h-4" />
                    통계
                  </Link>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

export default Dashboard;
