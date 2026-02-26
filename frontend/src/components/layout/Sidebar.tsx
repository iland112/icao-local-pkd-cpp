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
  ChevronLeft,
  ChevronRight,
  ChevronDown,
  ClipboardList,
  ExternalLink,
  FileText,
  Key,
  RefreshCw,
  Zap,
  BookOpen,
  Activity,
  ShieldX,
  FileWarning,
  Link2,
  Brain,
  KeyRound,
  Users,
} from 'lucide-react';
import { useSidebarStore } from '@/stores/sidebarStore';

import { cn } from '@/utils/cn';
import { authApi } from '@/services/api';

interface NavLeafItem {
  path: string;
  label: string;
  icon: React.ReactNode;
  external?: boolean;
  adminOnly?: boolean;
  /** Required permission — hidden when user lacks this permission (admins bypass) */
  permission?: string;
}

interface NavGroupItem {
  label: string;
  icon: React.ReactNode;
  children: NavLeafItem[];
  adminOnly?: boolean;
  /** If set, the entire group requires this permission */
  permission?: string;
}

type NavItem = NavLeafItem | NavGroupItem;

const isGroupItem = (item: NavItem): item is NavGroupItem => 'children' in item;

interface NavSection {
  title: string;
  items: NavItem[];
  adminOnly?: boolean;
}

const navSections: NavSection[] = [
  {
    title: 'PKD Management',
    items: [
      { path: '/icao', label: 'ICAO 버전 상태', icon: <Zap className="w-4 h-4" />, permission: 'icao:read' },
      { path: '/upload', label: '파일 업로드', icon: <Upload className="w-4 h-4" />, permission: 'upload:write' },
      { path: '/upload/certificate', label: '인증서 업로드', icon: <FileText className="w-4 h-4" />, permission: 'upload:write' },
      { path: '/pkd/certificates', label: '인증서 조회', icon: <Key className="w-4 h-4" />, permission: 'cert:read' },
      {
        label: '보고서',
        icon: <ClipboardList className="w-4 h-4" />,
        children: [
          { path: '/pkd/trust-chain', label: 'DSC Trust Chain 보고서', icon: <Link2 className="w-4 h-4" />, permission: 'report:read' },
          { path: '/pkd/crl', label: 'CRL 보고서', icon: <FileWarning className="w-4 h-4" />, permission: 'report:read' },
          { path: '/pkd/dsc-nc', label: '표준 부적합 DSC 보고서', icon: <ShieldX className="w-4 h-4" />, permission: 'report:read' },
          { path: '/ai/analysis', label: 'AI 인증서 분석', icon: <Brain className="w-4 h-4" />, permission: 'ai:read' },
        ],
      },
      { path: '/upload-history', label: '업로드 이력', icon: <Clock className="w-4 h-4" />, permission: 'upload:read' },
      { path: '/sync', label: '동기화 상태', icon: <RefreshCw className="w-4 h-4" />, permission: 'sync:read' },
      { path: '/upload-dashboard', label: '통계 대시보드', icon: <BarChart3 className="w-4 h-4" />, permission: 'upload:read' },
    ],
  },
  {
    title: 'Passive Auth',
    items: [
      { path: '/pa/verify', label: 'PA 검증 수행', icon: <ShieldCheck className="w-4 h-4" />, permission: 'pa:verify' },
      { path: '/pa/history', label: '검증 이력', icon: <History className="w-4 h-4" />, permission: 'pa:read' },
      { path: '/pa/dashboard', label: '통계 대시보드', icon: <PresentationIcon className="w-4 h-4" />, permission: 'pa:read' },
    ],
  },
  {
    title: 'System & Admin',
    items: [
      { path: '/monitoring', label: '시스템 모니터링', icon: <Activity className="w-4 h-4" /> },
      { path: '/admin/users', label: '사용자 관리', icon: <Users className="w-4 h-4" />, adminOnly: true },
      { path: '/admin/api-clients', label: 'API 클라이언트', icon: <KeyRound className="w-4 h-4" />, adminOnly: true },
      { path: '/admin/operation-audit', label: '운영 감사 로그', icon: <ShieldCheck className="w-4 h-4" />, adminOnly: true },
      { path: '/admin/audit-log', label: '인증 감사 로그', icon: <Key className="w-4 h-4" />, adminOnly: true },
    ],
  },
  {
    title: '',
    items: [
      {
        label: 'API Docs',
        icon: <BookOpen className="w-4 h-4" />,
        children: [
          { path: `${window.location.origin}/api-docs/?urls.primaryName=PKD+Management+API+v2.9.1`, label: 'PKD Management', icon: <BookOpen className="w-4 h-4" />, external: true },
          { path: `${window.location.origin}/api-docs/?urls.primaryName=PA+Service+API+v2.1.1`, label: 'PA Service', icon: <BookOpen className="w-4 h-4" />, external: true },
          { path: `${window.location.origin}/api-docs/?urls.primaryName=PKD+Relay+Service+API+v2.0.0`, label: 'PKD Relay', icon: <BookOpen className="w-4 h-4" />, external: true },
          { path: `${window.location.origin}/api-docs/?urls.primaryName=Monitoring+Service+API+v1.1.0`, label: 'Monitoring', icon: <BookOpen className="w-4 h-4" />, external: true },
        ],
      },
    ],
  },
];

export function Sidebar() {
  const location = useLocation();
  const { expanded, toggleExpanded, mobileOpen, setMobileOpen } = useSidebarStore();


  const isActive = (path: string) => location.pathname === path;
  const isAdmin = authApi.isAdmin();

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
              {navSections.map((section) => {
                // Helper: check if a leaf item is visible (admin + permission)
                const isLeafVisible = (item: NavLeafItem): boolean => {
                  if (item.adminOnly && !isAdmin) return false;
                  if (item.permission && !authApi.hasPermission(item.permission)) return false;
                  return true;
                };

                // Filter items: adminOnly + permission check; groups visible if any child visible
                const visibleItems = section.items.filter((item) => {
                  if (item.adminOnly && !isAdmin) return false;
                  if ('permission' in item && item.permission && !authApi.hasPermission(item.permission)) return false;
                  if (isGroupItem(item)) {
                    return item.children.some(child => isLeafVisible(child));
                  }
                  return true;
                });
                if (visibleItems.length === 0) return null;

                return (
                <li key={section.title}>
                  {/* Section Header */}
                  {expanded && section.title && (
                    <p className="px-3 pt-3 pb-1.5 text-xs font-semibold uppercase tracking-wider text-gray-400">
                      {section.title}
                    </p>
                  )}
                  {!expanded && <hr className="my-2 border-gray-200 dark:border-gray-700 lg:block hidden" />}

                  {/* Section Items */}
                  <ul className="space-y-1">
                    {visibleItems.map((item) => (
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
                                {item.children.filter(child => isLeafVisible(child)).map((child) => (
                                  <li key={child.path}>
                                    {child.external ? (
                                      <a
                                        href={child.path}
                                        target="_blank"
                                        rel="noopener noreferrer"
                                        className="flex items-center gap-x-2.5 py-1.5 px-2.5 text-sm rounded-lg transition-colors text-gray-500 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white hover:bg-gray-100 dark:hover:bg-gray-700"
                                      >
                                        {child.icon}
                                        <span>{child.label}</span>
                                        <ExternalLink className="w-3 h-3 ml-auto" />
                                      </a>
                                    ) : (
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
                                    )}
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
                );
              })}

            </ul>
          </nav>

          {/* Footer spacer */}
          <div className="flex-shrink-0 h-2" />
        </div>
      </aside>
    </>
  );
}

export default Sidebar;
