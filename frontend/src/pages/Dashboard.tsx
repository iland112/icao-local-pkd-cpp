import { useState, useEffect } from 'react';
import { Link } from 'react-router-dom';
import {
  ShieldCheck,
  Database,
  Server,
  Upload,
  Clock,
  BarChart3,
  Calendar,
  FileText,
  Key,
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
    <div className="w-full px-4 lg:px-6 py-4 space-y-6">
      {/* Header */}
      <div className="mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-blue-500 to-indigo-600 shadow-lg">
            <ShieldCheck className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">ICAO PKD Local Manager</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              ePassport 인증서 관리 및 Passive Authentication 검증 플랫폼
            </p>
          </div>
          <div className="hidden lg:flex items-center gap-4 text-sm text-gray-500 dark:text-gray-400">
            <div className="flex items-center gap-1.5">
              <Calendar className="w-4 h-4" />
              <span>{currentDate}</span>
            </div>
            <div className="flex items-center gap-1.5">
              <Clock className="w-4 h-4" />
              <span>{currentTime}</span>
            </div>
            <div className="flex items-center gap-2 pl-3 border-l border-gray-200 dark:border-gray-700">
              <div className="flex items-center gap-1.5" title={dbStatus.message}>
                <Database className="w-3.5 h-3.5" />
                <span className="text-xs">DB</span>
                {dbStatus.testing ? (
                  <Loader2 className="w-3 h-3 animate-spin" />
                ) : (
                  <span className={cn(
                    'w-2 h-2 rounded-full',
                    dbStatus.connected ? 'bg-green-500' : 'bg-red-500'
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
                    ldapStatus.connected ? 'bg-green-500' : 'bg-red-500'
                  )} />
                )}
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* ICAO PKD Update Notification Banner */}
      {icaoStatus?.any_needs_update && !icaoDismissed && (
        <div className="bg-gradient-to-r from-amber-50 to-orange-50 dark:from-amber-900/20 dark:to-orange-900/20 border border-amber-200 dark:border-amber-700 rounded-2xl shadow-lg overflow-hidden">
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
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg overflow-hidden">
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

      {/* Country Statistics Dialog */}
      <CountryStatisticsDialog
        isOpen={showCountryDialog}
        onClose={() => setShowCountryDialog(false)}
      />
    </div>
  );
}

export default Dashboard;
