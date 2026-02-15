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
  FileText,
  Key,
  User,
  GraduationCap,
  Globe,
  Loader2,
  Shield,
  AlertTriangle,
  X,
} from 'lucide-react';
import { useNavigate } from 'react-router-dom';
import { healthApi, ldapApi, uploadApi, icaoApi } from '@/services/api';
import { cn } from '@/utils/cn';
import { CountryStatisticsDialog } from '@/components/CountryStatisticsDialog';

interface IcaoStatusItem {
  collection_type: string;
  detected_version: number;
  uploaded_version: number;
  version_diff: number;
  needs_update: boolean;
  status: string;
  status_message: string;
}

interface IcaoStatusResponse {
  success: boolean;
  any_needs_update: boolean;
  last_checked_at: string | null;
  status: IcaoStatusItem[];
}

// Country statistics type
interface CountryStats {
  country: string;
  csca: number;
  mlsc: number;
  dsc: number;
  dscNc: number;
  total: number;
  percentage?: number;
}

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
  const [countryData, setCountryData] = useState<CountryStats[]>([]);
  const [countryLoading, setCountryLoading] = useState(true);
  const [showCountryDialog, setShowCountryDialog] = useState(false);
  const [icaoStatus, setIcaoStatus] = useState<IcaoStatusResponse | null>(null);
  const [icaoDismissed, setIcaoDismissed] = useState(false);
  const navigate = useNavigate();

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
    loadCountryData();
    loadIcaoStatus();
  }, []);

  const loadIcaoStatus = async () => {
    try {
      const response = await icaoApi.getStatus();
      const data = response.data as IcaoStatusResponse;
      if (data.success) {
        setIcaoStatus(data);
      }
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to load ICAO status:', error);
    }
  };

  const loadCountryData = async () => {
    setCountryLoading(true);
    try {
      const response = await uploadApi.getCountryStatistics(10);
      const countries = response.data.countries;
      if (countries && countries.length > 0) {
        const maxTotal = countries[0].total;
        setCountryData(countries.map((item: any) => ({
          ...item,
          mlsc: item.mlsc || 0,
          percentage: Math.max(15, (item.total / maxTotal) * 100),
        })));
      }
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to load country statistics:', error);
    } finally {
      setCountryLoading(false);
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

  const getCountryColor = (index: number) => {
    const colors = [
      '#3B82F6', '#22C55E', '#F59E0B', '#8B5CF6', '#EF4444',
      '#06B6D4', '#EC4899', '#14B8A6', '#6366F1', '#F97316',
    ];
    return colors[index % colors.length];
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
                  <div className="text-white/90 text-sm flex items-center justify-end gap-2 mb-3">
                    <Clock className="w-4 h-4" />
                    <span>{currentTime}</span>
                  </div>
                  {/* Compact Connection Status */}
                  <div className="border-t border-white/20 pt-3 flex items-center justify-end gap-3">
                    <div className="flex items-center gap-1.5" title={dbStatus.message}>
                      <Database className="w-3.5 h-3.5" />
                      <span className="text-xs">DB</span>
                      {dbStatus.testing ? (
                        <Loader2 className="w-3 h-3 animate-spin" />
                      ) : (
                        <span className={cn(
                          'w-2 h-2 rounded-full',
                          dbStatus.connected ? 'bg-green-400' : 'bg-red-400'
                        )} />
                      )}
                    </div>
                    <div className="flex items-center gap-1.5" title={ldapStatus.message}>
                      <Server className="w-3.5 h-3.5" />
                      <span className="text-xs">LDAP</span>
                      {ldapStatus.testing ? (
                        <Loader2 className="w-3 h-3 animate-spin" />
                      ) : (
                        <span className={cn(
                          'w-2 h-2 rounded-full',
                          ldapStatus.connected ? 'bg-green-400' : 'bg-red-400'
                        )} />
                      )}
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* ICAO PKD Update Notification Banner */}
      {icaoStatus?.any_needs_update && !icaoDismissed && (
        <div className="mb-8 bg-gradient-to-r from-amber-50 to-orange-50 dark:from-amber-900/20 dark:to-orange-900/20 border border-amber-200 dark:border-amber-700 rounded-2xl shadow-lg overflow-hidden">
          <div className="px-6 py-4">
            <div className="flex items-start justify-between gap-4">
              <div className="flex items-start gap-3 flex-1">
                <div className="p-2 rounded-lg bg-amber-100 dark:bg-amber-900/50 mt-0.5">
                  <AlertTriangle className="w-5 h-5 text-amber-600 dark:text-amber-400" />
                </div>
                <div className="flex-1">
                  <h3 className="text-sm font-semibold text-amber-800 dark:text-amber-200 mb-1">
                    ICAO PKD 새 버전 감지
                  </h3>
                  <div className="flex flex-wrap gap-2 mb-2">
                    {icaoStatus.status
                      .filter((s) => s.needs_update)
                      .map((s) => (
                        <span
                          key={s.collection_type}
                          className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg text-xs font-medium bg-amber-100 dark:bg-amber-900/40 text-amber-700 dark:text-amber-300 border border-amber-200 dark:border-amber-700"
                        >
                          {s.collection_type}
                          <span className="font-bold text-orange-600 dark:text-orange-400">
                            +{s.version_diff}
                          </span>
                          <span className="text-amber-500">
                            (v{s.uploaded_version} → v{s.detected_version})
                          </span>
                        </span>
                      ))}
                  </div>
                  <div className="flex items-center gap-4">
                    <button
                      onClick={() => navigate('/icao')}
                      className="inline-flex items-center gap-1.5 text-xs font-medium text-amber-700 dark:text-amber-300 hover:text-amber-900 dark:hover:text-amber-100 transition-colors"
                    >
                      상세 보기 →
                    </button>
                    {icaoStatus.last_checked_at && (
                      <span className="text-xs text-amber-500 dark:text-amber-500 flex items-center gap-1">
                        <Clock className="w-3 h-3" />
                        마지막 확인: {new Date(icaoStatus.last_checked_at).toLocaleString('ko-KR')}
                      </span>
                    )}
                  </div>
                </div>
              </div>
              <button
                onClick={() => setIcaoDismissed(true)}
                className="p-1 rounded-lg text-amber-400 hover:text-amber-600 hover:bg-amber-100 dark:hover:bg-amber-900/50 transition-colors"
                title="닫기"
              >
                <X className="w-4 h-4" />
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Country Certificate Statistics - Top 10 in 2 columns */}
      <div className="mb-8 bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        <div className="px-6 py-4 border-b border-gray-100 dark:border-gray-700">
          <div className="flex items-center justify-between">
            <h3 className="text-lg font-bold text-gray-900 dark:text-white flex items-center gap-3">
              <div className="p-2 rounded-lg bg-gradient-to-r from-cyan-400 to-blue-500">
                <Globe className="w-5 h-5 text-white" />
              </div>
              국가별 인증서 현황 (Top 10)
            </h3>
            <div className="flex items-center gap-4">
              {/* Legend */}
              <div className="flex items-center gap-3 text-xs text-gray-600 dark:text-gray-400">
                <div className="flex items-center gap-1.5 px-2.5 py-1 rounded-lg bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800">
                  <Shield className="w-3.5 h-3.5 text-blue-500" />
                  <span className="font-medium">CSCA</span>
                </div>
                <div className="flex items-center gap-1.5 px-2.5 py-1 rounded-lg bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-800">
                  <FileText className="w-3.5 h-3.5 text-purple-500" />
                  <span className="font-medium">MLSC</span>
                </div>
                <div className="flex items-center gap-1.5 px-2.5 py-1 rounded-lg bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800">
                  <Key className="w-3.5 h-3.5 text-green-500" />
                  <span className="font-medium">DSC</span>
                </div>
              </div>
              <button
                onClick={() => setShowCountryDialog(true)}
                className="text-sm text-blue-600 dark:text-blue-400 hover:underline flex items-center gap-1 transition-colors"
              >
                <BarChart3 className="w-4 h-4" />
                상세 통계
              </button>
            </div>
          </div>
        </div>
        <div className="p-6">
          {countryLoading ? (
            <div className="flex items-center justify-center py-12">
              <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
            </div>
          ) : countryData.length > 0 ? (
            <div className="grid grid-cols-1 lg:grid-cols-2 gap-x-8 gap-y-2">
              {countryData.map((item, index) => (
                <div
                  key={item.country}
                  className="flex items-center gap-3 p-2.5 rounded-lg transition-colors hover:bg-gray-50 dark:hover:bg-gray-700/50"
                >
                  {/* Rank */}
                  <span
                    className={cn(
                      'w-6 flex-shrink-0 text-sm font-bold text-center',
                      index < 3 ? 'text-amber-500' : 'text-gray-400'
                    )}
                  >
                    {index + 1}
                  </span>
                  {/* Flag */}
                  <img
                    src={`/svg/${item.country.toLowerCase()}.svg`}
                    alt={item.country}
                    className="w-8 h-6 flex-shrink-0 object-cover rounded shadow-sm border border-gray-200 dark:border-gray-600"
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
                    <span className="w-14 flex-shrink-0 text-xs font-bold text-right text-gray-600 dark:text-gray-300">
                      {item.total.toLocaleString()}
                    </span>
                  </div>
                  {/* Certificate breakdown */}
                  <div className="hidden xl:flex items-center gap-2 text-xs text-gray-500 dark:text-gray-400">
                    <span className="flex items-center gap-1">
                      <Shield className="w-3 h-3 text-blue-500" />
                      {item.csca}
                    </span>
                    {item.mlsc > 0 && (
                      <span className="flex items-center gap-1">
                        <FileText className="w-3 h-3 text-purple-500" />
                        {item.mlsc}
                      </span>
                    )}
                    <span className="flex items-center gap-1">
                      <Key className="w-3 h-3 text-green-500" />
                      {item.dsc}
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
          <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
            {/* PKD Relay Service - Upload & Processing Card */}
            <div className="relative p-6 rounded-xl border transition-all duration-300 hover:shadow-lg overflow-hidden bg-gradient-to-br from-violet-50/50 to-purple-50/50 dark:from-gray-700/50 dark:to-gray-700/50 border-violet-100 dark:border-gray-600 hover:border-violet-300">
              <div className="absolute top-0 right-0 w-24 h-24 opacity-5 pointer-events-none">
                <Upload className="w-full h-full text-violet-500" />
              </div>
              <div className="relative z-10">
                <h4 className="font-semibold mb-2 flex items-center gap-2 text-gray-800 dark:text-gray-200">
                  <span className="shrink-0 p-2 rounded-lg bg-violet-100 dark:bg-violet-900/50">
                    <Upload className="w-5 h-5 text-violet-500" />
                  </span>
                  PKD 파일 업로드 및 처리
                </h4>
                <p className="text-sm text-gray-600 dark:text-gray-400 mb-4">
                  ICAO PKD LDIF/Master List 파일을 업로드하고, 인증서를 파싱, 검증하여 DB 및 LDAP에 저장합니다.
                </p>
                <ul className="text-sm space-y-2 text-gray-600 dark:text-gray-400 mb-4">
                  {[
                    'LDIF / Master List 파일 업로드',
                    'CSCA/DSC/CRL 파싱 및 검증',
                    'Trust Chain 검증 (DSC → CSCA)',
                    'PostgreSQL + LDAP 저장'
                  ].map((item) => (
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
                  {[
                    'SOD CMS 서명 검증',
                    'DSC → CSCA Trust Chain 검증',
                    'Data Group 해시 무결성 검증',
                    'DG1/DG2 파싱 및 시각화'
                  ].map((item) => (
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

            {/* PKD Management - Certificate Search & Export Card */}
            <div className="relative p-6 rounded-xl border transition-all duration-300 hover:shadow-lg overflow-hidden bg-gradient-to-br from-blue-50/50 to-indigo-50/50 dark:from-gray-700/50 dark:to-gray-700/50 border-blue-100 dark:border-gray-600 hover:border-blue-300">
              <div className="absolute top-0 right-0 w-24 h-24 opacity-5 pointer-events-none">
                <Database className="w-full h-full text-blue-500" />
              </div>
              <div className="relative z-10">
                <h4 className="font-semibold mb-2 flex items-center gap-2 text-gray-800 dark:text-gray-200">
                  <span className="shrink-0 p-2 rounded-lg bg-blue-100 dark:bg-blue-900/50">
                    <Database className="w-5 h-5 text-blue-500" />
                  </span>
                  인증서 관리 및 조회
                </h4>
                <p className="text-sm text-gray-600 dark:text-gray-400 mb-4">
                  저장된 CSCA/DSC/CRL 인증서를 검색하고, DER/PEM 형식으로 내보내기하거나 국가별 ZIP으로 다운로드할 수 있습니다.
                </p>
                <ul className="text-sm space-y-2 text-gray-600 dark:text-gray-400 mb-4">
                  {[
                    'LDAP 기반 실시간 인증서 검색',
                    '국가/타입별 필터링 및 정렬',
                    '단일 인증서 Export (DER/PEM)',
                    '국가별 전체 인증서 ZIP 다운로드'
                  ].map((item) => (
                    <li key={item} className="flex items-center gap-2">
                      <CheckCircle className="w-4 h-4 text-blue-500" />
                      {item}
                    </li>
                  ))}
                </ul>
                <div className="flex flex-wrap gap-2">
                  <Link
                    to="/certificates"
                    className="py-2.5 px-5 inline-flex items-center gap-x-2 text-sm font-medium rounded-lg bg-gradient-to-r from-blue-500 to-indigo-500 text-white hover:from-blue-600 hover:to-indigo-600 hover:shadow-md transition-all duration-200"
                  >
                    <Database className="w-4 h-4" />
                    인증서 검색
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
          </div>
        </div>
      </div>

      {/* Country Statistics Dialog */}
      <CountryStatisticsDialog
        isOpen={showCountryDialog}
        onClose={() => setShowCountryDialog(false)}
      />
    </div>
  );
}

export default Dashboard;
