import { useState, useEffect } from 'react';
import { Link, useLocation } from 'react-router-dom';
import {
  Home,
  Upload,
  Clock,
  BarChart3,
  ShieldCheck,
  History,
  PresentationIcon,
  Info,
  HelpCircle,
  ChevronLeft,
  ChevronRight,
  Sun,
  Moon,
  Database,
  Server,
  CheckCircle,
  XCircle,
  Loader2,
  ExternalLink,
  FileText,
  Key,
  GraduationCap,
} from 'lucide-react';
import { useSidebarStore } from '@/stores/sidebarStore';
import { useThemeStore } from '@/stores/themeStore';
import { cn } from '@/utils/cn';
import { Dialog } from '@/components/common';
import { healthApi, ldapApi } from '@/services/api';

interface NavItem {
  path: string;
  label: string;
  icon: React.ReactNode;
}

interface NavSection {
  title: string;
  items: NavItem[];
}

interface SystemStatus {
  database: { status: 'checking' | 'up' | 'down'; version?: string };
  ldap: { status: 'checking' | 'up' | 'down'; responseTime?: number };
  app: { status: 'up'; version: string };
}

const navSections: NavSection[] = [
  {
    title: 'PKD Upload',
    items: [
      { path: '/upload', label: '파일 업로드', icon: <Upload className="w-4 h-4" /> },
      { path: '/upload-history', label: '업로드 이력', icon: <Clock className="w-4 h-4" /> },
      { path: '/upload-dashboard', label: '통계 대시보드', icon: <BarChart3 className="w-4 h-4" /> },
    ],
  },
  {
    title: 'Passive Auth',
    items: [
      { path: '/pa/verify', label: 'PA 검증 수행', icon: <ShieldCheck className="w-4 h-4" /> },
      { path: '/pa/history', label: '검증 이력', icon: <History className="w-4 h-4" /> },
      { path: '/pa/dashboard', label: '통계 대시보드', icon: <PresentationIcon className="w-4 h-4" /> },
    ],
  },
];

export function Sidebar() {
  const location = useLocation();
  const { expanded, toggleExpanded, mobileOpen, setMobileOpen } = useSidebarStore();
  const { darkMode, toggleTheme } = useThemeStore();

  // Dialog states
  const [showSystemInfo, setShowSystemInfo] = useState(false);
  const [showHelp, setShowHelp] = useState(false);
  const [systemStatus, setSystemStatus] = useState<SystemStatus>({
    database: { status: 'checking' },
    ldap: { status: 'checking' },
    app: { status: 'up', version: '1.0.0' },
  });

  const isActive = (path: string) => location.pathname === path;

  // Check system status when dialog opens
  useEffect(() => {
    if (showSystemInfo) {
      checkSystemStatus();
    }
  }, [showSystemInfo]);

  const checkSystemStatus = async () => {
    setSystemStatus((prev) => ({
      ...prev,
      database: { status: 'checking' },
      ldap: { status: 'checking' },
    }));

    // Check database
    try {
      const dbResponse = await healthApi.checkDatabase();
      setSystemStatus((prev) => ({
        ...prev,
        database: {
          status: dbResponse.data.status === 'UP' ? 'up' : 'down',
          version: dbResponse.data.version,
        },
      }));
    } catch {
      setSystemStatus((prev) => ({
        ...prev,
        database: { status: 'down' },
      }));
    }

    // Check LDAP
    try {
      const ldapResponse = await ldapApi.getHealth();
      setSystemStatus((prev) => ({
        ...prev,
        ldap: {
          status: ldapResponse.data.status === 'UP' ? 'up' : 'down',
          responseTime: ldapResponse.data.responseTime,
        },
      }));
    } catch {
      setSystemStatus((prev) => ({
        ...prev,
        ldap: { status: 'down' },
      }));
    }
  };

  const StatusIcon = ({ status }: { status: 'checking' | 'up' | 'down' }) => {
    if (status === 'checking') {
      return <Loader2 className="w-4 h-4 text-gray-400 animate-spin" />;
    }
    if (status === 'up') {
      return <CheckCircle className="w-4 h-4 text-green-500" />;
    }
    return <XCircle className="w-4 h-4 text-red-500" />;
  };

  return (
    <>
      {/* Mobile Overlay */}
      {mobileOpen && (
        <div
          className="fixed inset-0 bg-gray-900/50 z-50 lg:hidden"
          onClick={() => setMobileOpen(false)}
        />
      )}

      {/* Sidebar */}
      <aside
        className={cn(
          'fixed inset-y-0 start-0 z-[60] bg-white dark:bg-gray-800 border-e border-gray-200 dark:border-gray-700 transition-all duration-300',
          expanded ? 'w-64' : 'w-[70px]',
          mobileOpen ? 'translate-x-0' : '-translate-x-full lg:translate-x-0'
        )}
      >
        <div className="flex flex-col h-full">
          {/* Logo */}
          <div className="px-4 pt-4 pb-2">
            <Link to="/" className="flex items-center gap-3 group">
              <div className="w-10 h-10 bg-gradient-to-br from-blue-600 to-indigo-600 rounded-xl flex items-center justify-center shadow-lg group-hover:shadow-xl transition-shadow flex-shrink-0">
                <ShieldCheck className="w-6 h-6 text-white" />
              </div>
              {expanded && (
                <div className="flex-1 min-w-0 overflow-hidden transition-opacity duration-200">
                  <h2 className="text-base font-bold text-gray-900 dark:text-white truncate">ICAO PKD</h2>
                  <p className="text-xs text-gray-500 dark:text-gray-400 truncate">Local Manager</p>
                </div>
              )}
            </Link>
          </div>

          {/* Toggle Button (Desktop) */}
          <div className="hidden lg:flex justify-end px-3 pb-2">
            <button
              onClick={toggleExpanded}
              className="p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              title={expanded ? '사이드바 축소' : '사이드바 확장'}
            >
              {expanded ? (
                <ChevronLeft className="w-4 h-4 text-gray-500" />
              ) : (
                <ChevronRight className="w-4 h-4 text-gray-500" />
              )}
            </button>
          </div>

          {/* Navigation */}
          <nav className="flex-1 overflow-y-auto px-3 py-4">
            <ul className="space-y-1.5">
              {/* Home */}
              <li>
                <Link
                  to="/"
                  className={cn(
                    'flex items-center gap-x-3 py-2.5 px-3 text-sm font-medium rounded-lg transition-colors',
                    isActive('/')
                      ? 'bg-blue-50 dark:bg-blue-900/20 text-blue-600 dark:text-blue-400 border-l-2 border-blue-600'
                      : 'text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700',
                    !expanded && 'lg:justify-center lg:px-2'
                  )}
                  title={!expanded ? 'Home' : undefined}
                >
                  <Home className="w-5 h-5 flex-shrink-0" />
                  {expanded && <span>Home</span>}
                </Link>
              </li>

              {/* Nav Sections */}
              {navSections.map((section) => (
                <li key={section.title}>
                  {/* Section Header */}
                  {expanded && (
                    <p className="px-3 pt-4 pb-2 text-xs font-semibold uppercase tracking-wider text-gray-400">
                      {section.title}
                    </p>
                  )}
                  {!expanded && <hr className="my-3 border-gray-200 dark:border-gray-700 lg:block hidden" />}

                  {/* Section Items */}
                  <ul className="space-y-1">
                    {section.items.map((item) => (
                      <li key={item.path}>
                        <Link
                          to={item.path}
                          className={cn(
                            'flex items-center gap-x-3 py-2 px-3 text-sm rounded-lg transition-colors',
                            isActive(item.path)
                              ? 'bg-blue-50 dark:bg-blue-900/20 text-blue-600 dark:text-blue-400'
                              : 'text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white hover:bg-gray-100 dark:hover:bg-gray-700',
                            !expanded && 'lg:justify-center lg:px-2'
                          )}
                          title={!expanded ? item.label : undefined}
                        >
                          {item.icon}
                          {expanded && <span>{item.label}</span>}
                        </Link>
                      </li>
                    ))}
                  </ul>
                </li>
              ))}

              {/* Divider */}
              {expanded && <hr className="my-4 border-gray-200 dark:border-gray-700" />}

              {/* System Section */}
              {expanded && (
                <p className="px-3 pt-2 pb-2 text-xs font-semibold uppercase tracking-wider text-gray-400">
                  System
                </p>
              )}

              <li>
                <button
                  onClick={() => setShowSystemInfo(true)}
                  className={cn(
                    'w-full flex items-center gap-x-3 py-2.5 px-3 text-sm font-medium rounded-lg transition-colors text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white hover:bg-gray-100 dark:hover:bg-gray-700',
                    !expanded && 'lg:justify-center lg:px-2'
                  )}
                  title={!expanded ? '시스템 정보' : undefined}
                >
                  <Info className="w-5 h-5 flex-shrink-0" />
                  {expanded && <span>시스템 정보</span>}
                </button>
              </li>

              <li>
                <button
                  onClick={() => setShowHelp(true)}
                  className={cn(
                    'w-full flex items-center gap-x-3 py-2.5 px-3 text-sm font-medium rounded-lg transition-colors text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white hover:bg-gray-100 dark:hover:bg-gray-700',
                    !expanded && 'lg:justify-center lg:px-2'
                  )}
                  title={!expanded ? '도움말' : undefined}
                >
                  <HelpCircle className="w-5 h-5 flex-shrink-0" />
                  {expanded && <span>도움말</span>}
                </button>
              </li>
            </ul>
          </nav>

          {/* Footer */}
          <div className="border-t border-gray-200 dark:border-gray-700 px-4 py-3">
            <div className={cn('flex items-center', expanded ? 'justify-between' : 'justify-center')}>
              {/* Theme Toggle */}
              <button
                onClick={toggleTheme}
                className={cn(
                  'flex items-center gap-2 px-3 py-2 text-sm rounded-lg transition-colors text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white hover:bg-gray-100 dark:hover:bg-gray-700',
                  !expanded && 'lg:px-2'
                )}
                title={!expanded ? '테마 변경' : undefined}
              >
                {darkMode ? (
                  <Moon className="w-5 h-5" />
                ) : (
                  <Sun className="w-5 h-5" />
                )}
                {expanded && (
                  <span className="text-xs">{darkMode ? 'Dark' : 'Light'}</span>
                )}
              </button>

              {/* Version */}
              {expanded && (
                <span className="text-xs text-gray-400">v1.0</span>
              )}
            </div>
          </div>
        </div>
      </aside>

      {/* System Info Dialog */}
      <Dialog
        isOpen={showSystemInfo}
        onClose={() => setShowSystemInfo(false)}
        title="시스템 정보"
        size="lg"
      >
        <div className="space-y-6">
          {/* App Info */}
          <div>
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-3 flex items-center gap-2">
              <ShieldCheck className="w-4 h-4 text-blue-500" />
              애플리케이션
            </h4>
            <div className="bg-gray-50 dark:bg-gray-900 rounded-lg p-4 space-y-2">
              <div className="flex justify-between text-sm">
                <span className="text-gray-600 dark:text-gray-400">이름</span>
                <span className="font-medium text-gray-900 dark:text-white">ICAO PKD Local Manager</span>
              </div>
              <div className="flex justify-between text-sm">
                <span className="text-gray-600 dark:text-gray-400">버전</span>
                <span className="font-medium text-gray-900 dark:text-white">{systemStatus.app.version}</span>
              </div>
              <div className="flex justify-between text-sm">
                <span className="text-gray-600 dark:text-gray-400">상태</span>
                <span className="flex items-center gap-1.5">
                  <StatusIcon status={systemStatus.app.status} />
                  <span className="font-medium text-green-600 dark:text-green-400">Active</span>
                </span>
              </div>
            </div>
          </div>

          {/* Database Status */}
          <div>
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-3 flex items-center gap-2">
              <Database className="w-4 h-4 text-blue-500" />
              PostgreSQL
            </h4>
            <div className="bg-gray-50 dark:bg-gray-900 rounded-lg p-4 space-y-2">
              <div className="flex justify-between text-sm">
                <span className="text-gray-600 dark:text-gray-400">상태</span>
                <span className="flex items-center gap-1.5">
                  <StatusIcon status={systemStatus.database.status} />
                  <span className={cn(
                    'font-medium',
                    systemStatus.database.status === 'up' ? 'text-green-600 dark:text-green-400' :
                    systemStatus.database.status === 'down' ? 'text-red-600 dark:text-red-400' :
                    'text-gray-600 dark:text-gray-400'
                  )}>
                    {systemStatus.database.status === 'checking' ? '확인 중...' :
                     systemStatus.database.status === 'up' ? 'Active' : 'Inactive'}
                  </span>
                </span>
              </div>
              {systemStatus.database.version && (
                <div className="flex justify-between text-sm">
                  <span className="text-gray-600 dark:text-gray-400">버전</span>
                  <span className="font-medium text-gray-900 dark:text-white">{systemStatus.database.version}</span>
                </div>
              )}
              <div className="flex justify-between text-sm">
                <span className="text-gray-600 dark:text-gray-400">호스트</span>
                <span className="font-medium text-gray-900 dark:text-white">postgres:5432</span>
              </div>
            </div>
          </div>

          {/* LDAP Status */}
          <div>
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-3 flex items-center gap-2">
              <Server className="w-4 h-4 text-orange-500" />
              OpenLDAP (HAProxy)
            </h4>
            <div className="bg-gray-50 dark:bg-gray-900 rounded-lg p-4 space-y-2">
              <div className="flex justify-between text-sm">
                <span className="text-gray-600 dark:text-gray-400">상태</span>
                <span className="flex items-center gap-1.5">
                  <StatusIcon status={systemStatus.ldap.status} />
                  <span className={cn(
                    'font-medium',
                    systemStatus.ldap.status === 'up' ? 'text-green-600 dark:text-green-400' :
                    systemStatus.ldap.status === 'down' ? 'text-red-600 dark:text-red-400' :
                    'text-gray-600 dark:text-gray-400'
                  )}>
                    {systemStatus.ldap.status === 'checking' ? '확인 중...' :
                     systemStatus.ldap.status === 'up' ? 'Active' : 'Inactive'}
                  </span>
                </span>
              </div>
              {systemStatus.ldap.responseTime !== undefined && (
                <div className="flex justify-between text-sm">
                  <span className="text-gray-600 dark:text-gray-400">응답 시간</span>
                  <span className="font-medium text-gray-900 dark:text-white">{systemStatus.ldap.responseTime}ms</span>
                </div>
              )}
              <div className="flex justify-between text-sm">
                <span className="text-gray-600 dark:text-gray-400">호스트</span>
                <span className="font-medium text-gray-900 dark:text-white">haproxy:389</span>
              </div>
            </div>
          </div>

          {/* Refresh Button */}
          <div className="flex justify-end">
            <button
              onClick={checkSystemStatus}
              className="py-2 px-4 text-sm font-medium rounded-lg bg-blue-600 text-white hover:bg-blue-700 transition-colors flex items-center gap-2"
            >
              <Loader2 className={cn('w-4 h-4', systemStatus.database.status === 'checking' && 'animate-spin')} />
              상태 새로고침
            </button>
          </div>
        </div>
      </Dialog>

      {/* Help Dialog */}
      <Dialog
        isOpen={showHelp}
        onClose={() => setShowHelp(false)}
        title="도움말"
        size="lg"
      >
        <div className="space-y-6">
          {/* Overview */}
          <div>
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-2">시스템 개요</h4>
            <p className="text-sm text-gray-600 dark:text-gray-400">
              ICAO PKD Local Manager는 전자여권 인증서(CSCA, DSC)를 관리하고
              Passive Authentication을 수행하는 시스템입니다.
            </p>
          </div>

          {/* Standards */}
          <div>
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-3">지원 표준</h4>
            <div className="grid grid-cols-2 gap-2">
              {[
                { icon: GraduationCap, label: 'ICAO Doc 9303', desc: 'eMRTD 표준' },
                { icon: FileText, label: 'RFC 5652', desc: 'CMS (Cryptographic Message Syntax)' },
                { icon: Key, label: 'RFC 5280', desc: 'X.509 PKI 인증서' },
                { icon: ShieldCheck, label: 'ISO/IEC 19794-5', desc: '생체인식 데이터' },
              ].map((item) => (
                <div key={item.label} className="flex items-start gap-2 p-2 bg-gray-50 dark:bg-gray-900 rounded-lg">
                  <item.icon className="w-4 h-4 text-blue-500 mt-0.5" />
                  <div>
                    <p className="text-sm font-medium text-gray-900 dark:text-white">{item.label}</p>
                    <p className="text-xs text-gray-500 dark:text-gray-400">{item.desc}</p>
                  </div>
                </div>
              ))}
            </div>
          </div>

          {/* Quick Guide */}
          <div>
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-3">주요 기능</h4>
            <ul className="space-y-2">
              <li className="flex items-start gap-2 text-sm text-gray-600 dark:text-gray-400">
                <Upload className="w-4 h-4 text-violet-500 mt-0.5" />
                <span><strong>PKD 업로드:</strong> LDIF/Master List 파일을 업로드하여 인증서를 LDAP에 저장</span>
              </li>
              <li className="flex items-start gap-2 text-sm text-gray-600 dark:text-gray-400">
                <ShieldCheck className="w-4 h-4 text-teal-500 mt-0.5" />
                <span><strong>PA 검증:</strong> SOD와 Data Group을 검증하여 전자여권 무결성 확인</span>
              </li>
              <li className="flex items-start gap-2 text-sm text-gray-600 dark:text-gray-400">
                <History className="w-4 h-4 text-blue-500 mt-0.5" />
                <span><strong>이력 관리:</strong> 업로드 및 검증 이력 조회 및 통계 확인</span>
              </li>
            </ul>
          </div>

          {/* Links */}
          <div>
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-3">참고 자료</h4>
            <div className="space-y-2">
              <a
                href="https://www.icao.int/publications/pages/publication.aspx?docnum=9303"
                target="_blank"
                rel="noopener noreferrer"
                className="flex items-center gap-2 text-sm text-blue-600 dark:text-blue-400 hover:underline"
              >
                <ExternalLink className="w-4 h-4" />
                ICAO Doc 9303 공식 문서
              </a>
              <a
                href="https://pkddownloadsg.icao.int/"
                target="_blank"
                rel="noopener noreferrer"
                className="flex items-center gap-2 text-sm text-blue-600 dark:text-blue-400 hover:underline"
              >
                <ExternalLink className="w-4 h-4" />
                ICAO PKD 다운로드 사이트
              </a>
            </div>
          </div>
        </div>
      </Dialog>
    </>
  );
}

export default Sidebar;
