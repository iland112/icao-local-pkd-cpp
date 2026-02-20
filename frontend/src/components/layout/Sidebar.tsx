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
  ChevronDown,
  ClipboardList,
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
  RefreshCw,
  Zap,
  BookOpen,
  Activity,
  ShieldX,
  FileWarning,
  Link2,
  Brain,
} from 'lucide-react';
import { useSidebarStore } from '@/stores/sidebarStore';
import { useThemeStore } from '@/stores/themeStore';
import { cn } from '@/utils/cn';
import { Dialog } from '@/components/common';
import { healthApi, ldapApi } from '@/services/api';

interface NavLeafItem {
  path: string;
  label: string;
  icon: React.ReactNode;
  external?: boolean;
}

interface NavGroupItem {
  label: string;
  icon: React.ReactNode;
  children: NavLeafItem[];
}

type NavItem = NavLeafItem | NavGroupItem;

const isGroupItem = (item: NavItem): item is NavGroupItem => 'children' in item;

interface NavSection {
  title: string;
  items: NavItem[];
}

interface SystemStatus {
  database: { status: 'checking' | 'up' | 'down'; version?: string };
  ldap: { status: 'checking' | 'up' | 'down'; responseTime?: number; host?: string; port?: number };
  app: { status: 'up'; version: string };
}

// Extract short version from full PostgreSQL version string
const getShortDbVersion = (version?: string): string => {
  if (!version) return '';
  const match = version.match(/PostgreSQL\s+(\d+\.\d+)/);
  return match ? `v${match[1]}` : version.substring(0, 20);
};

const navSections: NavSection[] = [
  {
    title: 'ICAO PKD 연계',
    items: [
      { path: '/icao', label: 'ICAO 버전 상태', icon: <Zap className="w-4 h-4" /> },
    ],
  },
  {
    title: 'PKD Management',
    items: [
      { path: '/upload', label: '파일 업로드', icon: <Upload className="w-4 h-4" /> },
      { path: '/upload/certificate', label: '인증서 업로드', icon: <FileText className="w-4 h-4" /> },
      { path: '/pkd/certificates', label: '인증서 조회', icon: <Key className="w-4 h-4" /> },
      {
        label: '보고서',
        icon: <ClipboardList className="w-4 h-4" />,
        children: [
          { path: '/pkd/trust-chain', label: 'DSC Trust Chain 보고서', icon: <Link2 className="w-4 h-4" /> },
          { path: '/pkd/crl', label: 'CRL 보고서', icon: <FileWarning className="w-4 h-4" /> },
          { path: '/pkd/dsc-nc', label: '표준 부적합 DSC 보고서', icon: <ShieldX className="w-4 h-4" /> },
          { path: '/ai/analysis', label: 'AI 인증서 분석', icon: <Brain className="w-4 h-4" /> },
        ],
      },
      { path: '/upload-history', label: '업로드 이력', icon: <Clock className="w-4 h-4" /> },
      { path: '/sync', label: '동기화 상태', icon: <RefreshCw className="w-4 h-4" /> },
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
  {
    title: 'System Monitoring',
    items: [
      { path: '/monitoring', label: '시스템 모니터링', icon: <Activity className="w-4 h-4" /> },
    ],
  },
  {
    title: 'Admin & Security',
    items: [
      { path: '/admin/operation-audit', label: '운영 감사 로그', icon: <ShieldCheck className="w-4 h-4" /> },
      { path: '/admin/audit-log', label: '인증 감사 로그', icon: <Key className="w-4 h-4" /> },
    ],
  },
  {
    title: 'API Documentation',
    items: [
      { path: `http://${window.location.hostname}:8080/api-docs/?urls.primaryName=PKD+Management+API+v2.9.1`, label: 'PKD Management', icon: <BookOpen className="w-4 h-4" />, external: true },
      { path: `http://${window.location.hostname}:8080/api-docs/?urls.primaryName=PA+Service+API+v2.1.1`, label: 'PA Service', icon: <BookOpen className="w-4 h-4" />, external: true },
      { path: `http://${window.location.hostname}:8080/api-docs/?urls.primaryName=PKD+Relay+Service+API+v2.0.0`, label: 'PKD Relay', icon: <BookOpen className="w-4 h-4" />, external: true },
      { path: `http://${window.location.hostname}:8080/api-docs/?urls.primaryName=Monitoring+Service+API+v1.1.0`, label: 'Monitoring', icon: <BookOpen className="w-4 h-4" />, external: true },
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

  // Collapsible group state
  const [expandedGroups, setExpandedGroups] = useState<Set<string>>(new Set());

  const toggleGroup = (label: string) => {
    setExpandedGroups((prev) => {
      const next = new Set(prev);
      if (next.has(label)) next.delete(label);
      else next.add(label);
      return next;
    });
  };

  const hasActiveChild = (item: NavGroupItem): boolean =>
    item.children.some((child) => location.pathname === child.path);

  // Auto-expand groups with active children
  useEffect(() => {
    navSections.forEach((section) => {
      section.items.forEach((item) => {
        if (isGroupItem(item) && hasActiveChild(item)) {
          setExpandedGroups((prev) => {
            if (prev.has(item.label)) return prev;
            return new Set(prev).add(item.label);
          });
        }
      });
    });
  }, [location.pathname]);

  // Check system status when dialog opens
  useEffect(() => {
    if (showSystemInfo) {
      checkSystemStatus();
    }
  }, [showSystemInfo]);

  const checkSystemStatus = async () => {
    await checkDatabaseStatus();
    await checkLdapStatus();
  };

  const checkDatabaseStatus = async () => {
    setSystemStatus((prev) => ({
      ...prev,
      database: { status: 'checking' },
    }));

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
  };

  const checkLdapStatus = async () => {
    setSystemStatus((prev) => ({
      ...prev,
      ldap: { status: 'checking' },
    }));

    try {
      const ldapResponse = await ldapApi.getHealth();
      setSystemStatus((prev) => ({
        ...prev,
        ldap: {
          status: ldapResponse.data.status === 'UP' ? 'up' : 'down',
          responseTime: ldapResponse.data.responseTimeMs,
          host: ldapResponse.data.host,
          port: ldapResponse.data.port,
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
                      <li key={isGroupItem(item) ? item.label : item.path}>
                        {isGroupItem(item) ? (
                          /* Group with collapsible children */
                          <>
                            <button
                              onClick={() => {
                                if (!expanded) toggleExpanded();
                                toggleGroup(item.label);
                              }}
                              className={cn(
                                'w-full flex items-center gap-x-3 py-2 px-3 text-sm rounded-lg transition-colors',
                                hasActiveChild(item)
                                  ? 'text-blue-600 dark:text-blue-400 bg-blue-50/50 dark:bg-blue-900/10'
                                  : 'text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white hover:bg-gray-100 dark:hover:bg-gray-700',
                                !expanded && 'lg:justify-center lg:px-2'
                              )}
                              title={!expanded ? item.label : undefined}
                            >
                              {item.icon}
                              {expanded && (
                                <>
                                  <span className="flex-1 text-left">{item.label}</span>
                                  {expandedGroups.has(item.label)
                                    ? <ChevronDown className="w-3.5 h-3.5 text-gray-400" />
                                    : <ChevronRight className="w-3.5 h-3.5 text-gray-400" />
                                  }
                                </>
                              )}
                            </button>
                            {expanded && expandedGroups.has(item.label) && (
                              <ul className="mt-1 ml-4 pl-3 border-l border-gray-200 dark:border-gray-700 space-y-0.5">
                                {item.children.map((child) => (
                                  <li key={child.path}>
                                    <Link
                                      to={child.path}
                                      className={cn(
                                        'flex items-center gap-x-2.5 py-1.5 px-2.5 text-sm rounded-lg transition-colors',
                                        isActive(child.path)
                                          ? 'bg-blue-50 dark:bg-blue-900/20 text-blue-600 dark:text-blue-400'
                                          : 'text-gray-500 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white hover:bg-gray-100 dark:hover:bg-gray-700'
                                      )}
                                    >
                                      {child.icon}
                                      <span>{child.label}</span>
                                    </Link>
                                  </li>
                                ))}
                              </ul>
                            )}
                          </>
                        ) : item.external ? (
                          <a
                            href={item.path}
                            target="_blank"
                            rel="noopener noreferrer"
                            className={cn(
                              'flex items-center gap-x-3 py-2 px-3 text-sm rounded-lg transition-colors',
                              'text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white hover:bg-gray-100 dark:hover:bg-gray-700',
                              !expanded && 'lg:justify-center lg:px-2'
                            )}
                            title={!expanded ? item.label : undefined}
                          >
                            {item.icon}
                            {expanded && (
                              <>
                                <span>{item.label}</span>
                                <ExternalLink className="w-3 h-3 ml-auto" />
                              </>
                            )}
                          </a>
                        ) : (
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
                        )}
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
        size="2xl"
      >
        <div className="space-y-4">
          {/* App Info - Compact */}
          <div className="flex items-center justify-between bg-gray-50 dark:bg-gray-900 rounded-lg px-4 py-3">
            <div className="flex items-center gap-3">
              <div className="p-2 rounded-lg bg-blue-100 dark:bg-blue-900/50">
                <ShieldCheck className="w-5 h-5 text-blue-600 dark:text-blue-400" />
              </div>
              <div>
                <p className="font-semibold text-gray-900 dark:text-white">ICAO PKD Local Manager</p>
                <p className="text-xs text-gray-500 dark:text-gray-400">v{systemStatus.app.version}</p>
              </div>
            </div>
            <span className="flex items-center gap-1.5 px-2.5 py-1 rounded-full bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400 text-xs font-medium">
              <CheckCircle className="w-3.5 h-3.5" />
              Active
            </span>
          </div>

          {/* Database & LDAP - Side by Side */}
          <div className="grid grid-cols-2 gap-4">
            {/* PostgreSQL */}
            <div className="bg-gray-50 dark:bg-gray-900 rounded-lg p-4">
              <div className="flex items-center justify-between mb-3">
                <div className="flex items-center gap-2">
                  <Database className="w-4 h-4 text-blue-500" />
                  <span className="font-semibold text-sm text-gray-900 dark:text-white">PostgreSQL</span>
                </div>
                <span className={cn(
                  'flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium',
                  systemStatus.database.status === 'up' ? 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400' :
                  systemStatus.database.status === 'down' ? 'bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400' :
                  'bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-400'
                )}>
                  <StatusIcon status={systemStatus.database.status} />
                  {systemStatus.database.status === 'checking' ? '확인 중' :
                   systemStatus.database.status === 'up' ? 'Active' : 'Inactive'}
                </span>
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400 mb-3 h-4">
                {systemStatus.database.version && <span>{getShortDbVersion(systemStatus.database.version)}</span>}
              </div>
              <button
                onClick={checkDatabaseStatus}
                disabled={systemStatus.database.status === 'checking'}
                className="w-full py-1.5 px-3 text-xs font-medium rounded-lg border transition disabled:opacity-50 flex items-center justify-center gap-1.5 border-blue-200 bg-blue-50 text-blue-700 hover:bg-blue-100 dark:border-blue-600 dark:bg-blue-600/20 dark:text-blue-400 dark:hover:bg-blue-600/30"
              >
                {systemStatus.database.status === 'checking' ? (
                  <Loader2 className="w-3 h-3 animate-spin" />
                ) : (
                  <Zap className="w-3 h-3" />
                )}
                연결 테스트
              </button>
            </div>

            {/* OpenLDAP */}
            <div className="bg-gray-50 dark:bg-gray-900 rounded-lg p-4">
              <div className="flex items-center justify-between mb-3">
                <div className="flex items-center gap-2">
                  <Server className="w-4 h-4 text-orange-500" />
                  <span className="font-semibold text-sm text-gray-900 dark:text-white">OpenLDAP</span>
                </div>
                <span className={cn(
                  'flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium',
                  systemStatus.ldap.status === 'up' ? 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400' :
                  systemStatus.ldap.status === 'down' ? 'bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400' :
                  'bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-400'
                )}>
                  <StatusIcon status={systemStatus.ldap.status} />
                  {systemStatus.ldap.status === 'checking' ? '확인 중' :
                   systemStatus.ldap.status === 'up' ? 'Active' : 'Inactive'}
                </span>
              </div>
              <div className="text-xs text-gray-500 dark:text-gray-400 mb-3 h-4">
                {systemStatus.ldap.host && systemStatus.ldap.port && (
                  <span>{systemStatus.ldap.host}:{systemStatus.ldap.port} ({systemStatus.ldap.responseTime}ms)</span>
                )}
              </div>
              <button
                onClick={checkLdapStatus}
                disabled={systemStatus.ldap.status === 'checking'}
                className="w-full py-1.5 px-3 text-xs font-medium rounded-lg border transition disabled:opacity-50 flex items-center justify-center gap-1.5 border-orange-200 bg-orange-50 text-orange-700 hover:bg-orange-100 dark:border-orange-600 dark:bg-orange-600/20 dark:text-orange-400 dark:hover:bg-orange-600/30"
              >
                {systemStatus.ldap.status === 'checking' ? (
                  <Loader2 className="w-3 h-3 animate-spin" />
                ) : (
                  <Zap className="w-3 h-3" />
                )}
                연결 테스트
              </button>
            </div>
          </div>

          {/* Refresh All Button */}
          <div className="flex justify-end pt-2">
            <button
              onClick={checkSystemStatus}
              disabled={systemStatus.database.status === 'checking' || systemStatus.ldap.status === 'checking'}
              className="py-2 px-4 text-sm font-medium rounded-lg bg-blue-600 text-white hover:bg-blue-700 transition-colors flex items-center gap-2 disabled:opacity-50"
            >
              <RefreshCw className={cn('w-4 h-4', (systemStatus.database.status === 'checking' || systemStatus.ldap.status === 'checking') && 'animate-spin')} />
              전체 새로고침
            </button>
          </div>
        </div>
      </Dialog>

      {/* Help Dialog */}
      <Dialog
        isOpen={showHelp}
        onClose={() => setShowHelp(false)}
        title="도움말"
        size="3xl"
      >
        <div className="space-y-4">
          {/* Overview - Compact */}
          <div className="bg-gray-50 dark:bg-gray-900 rounded-lg px-4 py-3">
            <p className="text-sm text-gray-600 dark:text-gray-400">
              ICAO PKD Local Manager는 전자여권 인증서(CSCA, DSC)를 관리하고 Passive Authentication을 수행하는 시스템입니다.
            </p>
          </div>

          {/* Standards & Features - Side by Side */}
          <div className="grid grid-cols-2 gap-4">
            {/* Standards */}
            <div>
              <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-2">지원 표준</h4>
              <div className="grid grid-cols-2 gap-2">
                {[
                  { icon: GraduationCap, label: 'ICAO Doc 9303' },
                  { icon: FileText, label: 'RFC 5652 CMS' },
                  { icon: Key, label: 'RFC 5280 X.509' },
                  { icon: ShieldCheck, label: 'ISO 19794-5' },
                ].map((item) => (
                  <div key={item.label} className="flex items-center gap-2 p-2 bg-gray-50 dark:bg-gray-900 rounded-lg">
                    <item.icon className="w-4 h-4 text-blue-500" />
                    <span className="text-xs font-medium text-gray-700 dark:text-gray-300">{item.label}</span>
                  </div>
                ))}
              </div>
            </div>

            {/* Features */}
            <div>
              <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-2">주요 기능</h4>
              <div className="space-y-1.5">
                <div className="flex items-center gap-2 text-sm text-gray-600 dark:text-gray-400">
                  <Upload className="w-4 h-4 text-violet-500" />
                  <span>PKD 업로드 (LDIF/Master List)</span>
                </div>
                <div className="flex items-center gap-2 text-sm text-gray-600 dark:text-gray-400">
                  <ShieldCheck className="w-4 h-4 text-teal-500" />
                  <span>Passive Authentication 검증</span>
                </div>
                <div className="flex items-center gap-2 text-sm text-gray-600 dark:text-gray-400">
                  <History className="w-4 h-4 text-blue-500" />
                  <span>업로드/검증 이력 관리</span>
                </div>
              </div>
            </div>
          </div>

          {/* Links */}
          <div className="flex items-center justify-between pt-2 border-t border-gray-200 dark:border-gray-700">
            <div className="flex items-center gap-4">
              <a
                href="https://www.icao.int/publications/pages/publication.aspx?docnum=9303"
                target="_blank"
                rel="noopener noreferrer"
                className="flex items-center gap-1.5 text-sm text-blue-600 dark:text-blue-400 hover:underline"
              >
                <ExternalLink className="w-3.5 h-3.5" />
                ICAO Doc 9303
              </a>
              <a
                href="https://pkddownloadsg.icao.int/"
                target="_blank"
                rel="noopener noreferrer"
                className="flex items-center gap-1.5 text-sm text-blue-600 dark:text-blue-400 hover:underline"
              >
                <ExternalLink className="w-3.5 h-3.5" />
                ICAO PKD Download
              </a>
            </div>
          </div>
        </div>
      </Dialog>
    </>
  );
}

export default Sidebar;
