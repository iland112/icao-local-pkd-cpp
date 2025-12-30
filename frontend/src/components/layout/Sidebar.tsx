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
} from 'lucide-react';
import { useSidebarStore } from '@/stores/sidebarStore';
import { useThemeStore } from '@/stores/themeStore';
import { cn } from '@/utils/cn';

interface NavItem {
  path: string;
  label: string;
  icon: React.ReactNode;
}

interface NavSection {
  title: string;
  items: NavItem[];
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

  const isActive = (path: string) => location.pathname === path;

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
    </>
  );
}

export default Sidebar;
