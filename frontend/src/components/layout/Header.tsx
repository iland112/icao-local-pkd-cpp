import { Menu, User, Sun, Moon, LogOut, Settings, UserCircle, Shield, HelpCircle, ExternalLink, ChevronDown, ChevronRight, Home, Bell, CheckCheck, Trash2, RefreshCw, Database, ShieldCheck, GitMerge, Calendar, FileKey, Globe } from 'lucide-react';
import { useSidebarStore } from '@/stores/sidebarStore';
import { useThemeStore } from '@/stores/themeStore';
import { useNotificationStore, type SystemNotification } from '@/stores/notificationStore';
import { authApi } from '@/services/api';
import { useNavigate, useLocation, Link } from 'react-router-dom';
import { useState, useRef, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Dialog } from '@/components/common';
import { getBreadcrumbs } from '@/utils/breadcrumbs';

const OPEN_SOURCE_LICENSES = [
  { name: 'React', version: '19', license: 'MIT', url: 'https://react.dev' },
  { name: 'React Router', version: '7', license: 'MIT', url: 'https://reactrouter.com' },
  { name: 'TypeScript', version: '5.9', license: 'Apache-2.0', url: 'https://www.typescriptlang.org' },
  { name: 'Vite', version: '7', license: 'MIT', url: 'https://vite.dev' },
  { name: 'Tailwind CSS', version: '4', license: 'MIT', url: 'https://tailwindcss.com' },
  { name: 'Axios', version: '1.x', license: 'MIT', url: 'https://axios-http.com' },
  { name: 'Zustand', version: '5', license: 'MIT', url: 'https://zustand.docs.pmnd.rs' },
  { name: 'TanStack Query', version: '5', license: 'MIT', url: 'https://tanstack.com/query' },
  { name: 'Recharts', version: '3', license: 'MIT', url: 'https://recharts.org' },
  { name: 'Lucide Icons', version: '0.5x', license: 'ISC', url: 'https://lucide.dev' },
  { name: 'Drogon', version: '1.x', license: 'MIT', url: 'https://drogon.org' },
  { name: 'OpenSSL', version: '3.x', license: 'Apache-2.0', url: 'https://www.openssl.org' },
  { name: 'spdlog', version: '1.x', license: 'MIT', url: 'https://github.com/gabime/spdlog' },
  { name: 'FastAPI', version: '0.11x', license: 'MIT', url: 'https://fastapi.tiangolo.com' },
  { name: 'scikit-learn', version: '1.x', license: 'BSD-3', url: 'https://scikit-learn.org' },
];

/** Get icon and color for notification type */
function getNotificationMeta(type: string) {
  switch (type) {
    case 'SYNC_CHECK_COMPLETE':
      return { icon: Database, color: 'text-blue-500', bg: 'bg-blue-100 dark:bg-blue-900/30' };
    case 'REVALIDATION_COMPLETE':
      return { icon: ShieldCheck, color: 'text-green-500', bg: 'bg-green-100 dark:bg-green-900/30' };
    case 'RECONCILE_COMPLETE':
      return { icon: GitMerge, color: 'text-purple-500', bg: 'bg-purple-100 dark:bg-purple-900/30' };
    case 'DAILY_SYNC_COMPLETE':
      return { icon: Calendar, color: 'text-emerald-500', bg: 'bg-emerald-100 dark:bg-emerald-900/30' };
    case 'DAILY_SYNC_FAILED':
      return { icon: RefreshCw, color: 'text-red-500', bg: 'bg-red-100 dark:bg-red-900/30' };
    case 'DSC_PENDING_CREATED':
      return { icon: FileKey, color: 'text-amber-500', bg: 'bg-amber-100 dark:bg-amber-900/30' };
    default:
      return { icon: Bell, color: 'text-gray-500', bg: 'bg-gray-100 dark:bg-gray-800' };
  }
}

export function Header() {
  const { toggleMobile } = useSidebarStore();
  const { darkMode, toggleTheme } = useThemeStore();
  const { notifications, unreadCount, markAsRead, markAllAsRead, clearAll } = useNotificationStore();
  const navigate = useNavigate();
  const location = useLocation();
  const { t, i18n } = useTranslation(['nav', 'common']);
  const breadcrumbs = getBreadcrumbs(location.pathname);
  const user = authApi.getStoredUser();
  const [isUserMenuOpen, setIsUserMenuOpen] = useState(false);
  const [isNotificationOpen, setIsNotificationOpen] = useState(false);
  const [showAbout, setShowAbout] = useState(false);
  const [showLicenses, setShowLicenses] = useState(false);
  const dropdownRef = useRef<HTMLDivElement>(null);
  const notificationRef = useRef<HTMLDivElement>(null);

  /** Format relative time */
  function formatRelativeTime(timestamp: string): string {
    const diff = Date.now() - new Date(timestamp).getTime();
    const seconds = Math.floor(diff / 1000);
    if (seconds < 60) return t('common:time.justNow');
    const minutes = Math.floor(seconds / 60);
    if (minutes < 60) return t('common:time.minutesAgo', { num: minutes });
    const hours = Math.floor(minutes / 60);
    if (hours < 24) return t('common:time.hoursAgo', { num: hours });
    const days = Math.floor(hours / 24);
    return t('common:time.daysAgo', { num: days });
  }

  const toggleLanguage = () => {
    const nextLng = i18n.language === 'ko' ? 'en' : 'ko';
    i18n.changeLanguage(nextLng);
  };

  const handleLogout = async () => {
    try {
      await authApi.logout();
      navigate('/login');
    } catch (error) {
      if (import.meta.env.DEV) console.error('Logout failed:', error);
      // Force logout even if API fails
      localStorage.clear();
      navigate('/login');
    }
  };

  // Close dropdowns when clicking outside
  useEffect(() => {
    const handleClickOutside = (event: MouseEvent) => {
      if (dropdownRef.current && !dropdownRef.current.contains(event.target as Node)) {
        setIsUserMenuOpen(false);
      }
      if (notificationRef.current && !notificationRef.current.contains(event.target as Node)) {
        setIsNotificationOpen(false);
      }
    };

    document.addEventListener('mousedown', handleClickOutside);
    return () => {
      document.removeEventListener('mousedown', handleClickOutside);
    };
  }, []);

  return (
    <>
    <header className="sticky top-0 z-40 bg-white dark:bg-gray-800 border-b border-gray-200 dark:border-gray-700">
      <div className="flex items-center justify-between px-4 py-1.5">
        {/* Left: Mobile menu button + Breadcrumb */}
        <div className="flex items-center gap-2 min-w-0">
          <button
            onClick={toggleMobile}
            className="lg:hidden p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors flex-shrink-0"
            aria-label={t('header.openMenu')}
          >
            <Menu className="w-4 h-4 text-gray-600 dark:text-gray-300" />
          </button>

          {/* Breadcrumb (desktop only) */}
          {location.pathname !== '/login' && (
            <nav className="hidden lg:flex items-center gap-1 text-sm min-w-0" aria-label="breadcrumb">
              <Link to="/" className={`flex items-center gap-1 transition-colors flex-shrink-0 ${breadcrumbs.length === 0 ? 'font-medium text-gray-700 dark:text-gray-200' : 'text-gray-400 dark:text-gray-500 hover:text-blue-600 dark:hover:text-blue-400'}`}>
                <Home className="w-3.5 h-3.5" />
                {breadcrumbs.length === 0 && <span>{t('home')}</span>}
              </Link>
              {breadcrumbs.map((item, index) => {
                const isLast = index === breadcrumbs.length - 1;
                const label = t(item.labelKey);
                return (
                  <span key={item.labelKey} className="flex items-center gap-1 min-w-0">
                    <ChevronRight className="w-3 h-3 text-gray-300 dark:text-gray-600 flex-shrink-0" />
                    {item.path && !isLast ? (
                      <Link to={item.path} className="text-gray-400 dark:text-gray-500 hover:text-blue-600 dark:hover:text-blue-400 transition-colors truncate">
                        {label}
                      </Link>
                    ) : (
                      <span className={isLast ? 'font-medium text-gray-700 dark:text-gray-200 truncate' : 'text-gray-400 dark:text-gray-500 truncate'}>
                        {label}
                      </span>
                    )}
                  </span>
                );
              })}
            </nav>
          )}
        </div>

        {/* Right: Actions */}
        <div className="flex items-center gap-1">
          {/* Language Toggle */}
          <button
            onClick={toggleLanguage}
            className="p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            title={i18n.language === 'ko' ? 'English' : '한국어'}
            aria-label={i18n.language === 'ko' ? 'Switch to English' : '한국어로 전환'}
          >
            <Globe className="w-4 h-4 text-gray-600 dark:text-gray-300" />
          </button>

          {/* Notification Bell */}
          <div className="relative inline-flex" ref={notificationRef}>
            <button
              type="button"
              onClick={() => {
                setIsNotificationOpen(!isNotificationOpen);
                setIsUserMenuOpen(false);
              }}
              className="relative p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              title={t('header.notifications')}
              aria-label={t('header.notifications')}
            >
              <Bell className="w-4 h-4 text-gray-600 dark:text-gray-300" />
              {unreadCount > 0 && (
                <span className="absolute -top-0.5 -right-0.5 flex items-center justify-center min-w-[16px] h-4 px-1 text-[10px] font-bold text-white bg-red-500 rounded-full">
                  {unreadCount > 99 ? '99+' : unreadCount}
                </span>
              )}
            </button>

            {isNotificationOpen && (
              <div className="absolute right-0 top-full mt-2 w-80 bg-white dark:bg-gray-800 shadow-lg rounded-lg border border-gray-200 dark:border-gray-700 z-50">
                {/* Header */}
                <div className="flex items-center justify-between px-3 py-2 border-b border-gray-200 dark:border-gray-700">
                  <span className="text-sm font-semibold text-gray-900 dark:text-white">{t('header.notifications')}</span>
                  <div className="flex items-center gap-1">
                    {unreadCount > 0 && (
                      <button
                        onClick={markAllAsRead}
                        className="p-1 rounded hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
                        title={t('header.markAllRead')}
                      >
                        <CheckCheck className="w-3.5 h-3.5 text-gray-500 dark:text-gray-400" />
                      </button>
                    )}
                    {notifications.length > 0 && (
                      <button
                        onClick={clearAll}
                        className="p-1 rounded hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
                        title={t('header.clearAll')}
                      >
                        <Trash2 className="w-3.5 h-3.5 text-gray-500 dark:text-gray-400" />
                      </button>
                    )}
                  </div>
                </div>

                {/* Notification List */}
                <div className="max-h-80 overflow-y-auto">
                  {notifications.length === 0 ? (
                    <div className="px-4 py-8 text-center">
                      <Bell className="w-8 h-8 text-gray-300 dark:text-gray-600 mx-auto mb-2" />
                      <p className="text-sm text-gray-500 dark:text-gray-400">{t('header.noNotifications')}</p>
                    </div>
                  ) : (
                    notifications.map((n: SystemNotification) => {
                      const meta = getNotificationMeta(n.type);
                      const IconComponent = meta.icon;
                      return (
                        <button
                          key={n.id}
                          onClick={() => markAsRead(n.id)}
                          className={`w-full text-left px-3 py-2.5 border-b border-gray-100 dark:border-gray-700/50 last:border-0 hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors ${
                            !n.read ? 'bg-blue-50/50 dark:bg-blue-900/10' : ''
                          }`}
                        >
                          <div className="flex gap-2.5">
                            <div className={`flex-shrink-0 w-7 h-7 rounded-full ${meta.bg} flex items-center justify-center mt-0.5`}>
                              <IconComponent className={`w-3.5 h-3.5 ${meta.color}`} />
                            </div>
                            <div className="min-w-0 flex-1">
                              <div className="flex items-center gap-1.5">
                                <p className="text-xs font-medium text-gray-900 dark:text-white truncate">{n.title}</p>
                                {!n.read && (
                                  <span className="flex-shrink-0 w-1.5 h-1.5 bg-blue-500 rounded-full" />
                                )}
                              </div>
                              <p className="text-[11px] text-gray-500 dark:text-gray-400 truncate mt-0.5">{n.message}</p>
                              <p className="text-[10px] text-gray-400 dark:text-gray-500 mt-0.5">{formatRelativeTime(n.timestamp)}</p>
                            </div>
                          </div>
                        </button>
                      );
                    })
                  )}
                </div>
              </div>
            )}
          </div>

          {/* Theme Toggle */}
          <button
            onClick={toggleTheme}
            className="p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            title={darkMode ? t('header.lightMode') : t('header.darkMode')}
            aria-label={darkMode ? t('header.lightMode') : t('header.darkMode')}
          >
            {darkMode ? (
              <Sun className="w-4 h-4 text-yellow-500" />
            ) : (
              <Moon className="w-4 h-4 text-gray-600" />
            )}
          </button>

          {/* User Menu */}
          <div className="relative inline-flex" ref={dropdownRef}>
            <button
              type="button"
              onClick={() => setIsUserMenuOpen(!isUserMenuOpen)}
              className="flex items-center gap-1.5 p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              aria-label={t('header.userMenu')}
              aria-expanded={isUserMenuOpen}
            >
              <div className="w-6 h-6 bg-gradient-to-br from-blue-500 to-indigo-500 rounded-full flex items-center justify-center">
                <User className="w-3 h-3 text-white" />
              </div>
              <div className="hidden sm:flex flex-col items-start">
                <span className="text-xs font-medium text-gray-700 dark:text-gray-200">
                  {user?.username || 'User'}
                </span>
                {user?.is_admin && (
                  <span className="flex items-center gap-0.5 text-[10px] text-blue-600 dark:text-blue-400 leading-none">
                    <Shield className="w-2.5 h-2.5" />
                    {t('nav:header.adminBadge')}
                  </span>
                )}
              </div>
            </button>

            {isUserMenuOpen && (
              <div className="absolute right-0 top-full mt-2 min-w-56 bg-white dark:bg-gray-800 shadow-lg rounded-lg p-2 border border-gray-200 dark:border-gray-700 z-50"
            >
              {/* User Info */}
              <div className="px-3 py-2 border-b border-gray-200 dark:border-gray-700 mb-2">
                <p className="text-sm font-medium text-gray-900 dark:text-white">
                  {user?.full_name || user?.username}
                </p>
                <p className="text-xs text-gray-500 dark:text-gray-400">
                  {user?.email || t('nav:header.noEmail')}
                </p>
              </div>

              {/* Menu Items */}
              <button
                onClick={() => {
                  navigate('/profile');
                  setIsUserMenuOpen(false);
                }}
                className="w-full flex items-center gap-x-3.5 py-2 px-3 rounded-lg text-sm text-gray-800 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              >
                <UserCircle className="w-4 h-4" />
                {t('header.profile')}
              </button>

              {user?.is_admin && (
                <>
                  <button
                    onClick={() => {
                      navigate('/admin/users');
                      setIsUserMenuOpen(false);
                    }}
                    className="w-full flex items-center gap-x-3.5 py-2 px-3 rounded-lg text-sm text-gray-800 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
                  >
                    <Shield className="w-4 h-4" />
                    {t('header.userManagement')}
                  </button>
                  <button
                    onClick={() => {
                      navigate('/admin/audit-log');
                      setIsUserMenuOpen(false);
                    }}
                    className="w-full flex items-center gap-x-3.5 py-2 px-3 rounded-lg text-sm text-gray-800 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
                  >
                    <Settings className="w-4 h-4" />
                    {t('header.loginHistory')}
                  </button>
                </>
              )}

              <button
                onClick={() => {
                  setShowAbout(true);
                  setIsUserMenuOpen(false);
                }}
                className="w-full flex items-center gap-x-3.5 py-2 px-3 rounded-lg text-sm text-gray-800 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              >
                <HelpCircle className="w-4 h-4" />
                {t('header.about')}
              </button>

              <hr className="my-2 border-gray-200 dark:border-gray-700" />

              {/* Logout */}
              <button
                onClick={() => {
                  handleLogout();
                  setIsUserMenuOpen(false);
                }}
                className="w-full flex items-center gap-x-3.5 py-2 px-3 rounded-lg text-sm text-red-600 dark:text-red-400 hover:bg-red-50 dark:hover:bg-red-900/20 transition-colors"
              >
                <LogOut className="w-4 h-4" />
                {t('header.logout')}
              </button>
              </div>
            )}
          </div>
        </div>
      </div>
    </header>

    {/* About Dialog */}
    <Dialog isOpen={showAbout} onClose={() => setShowAbout(false)} title={t('about.title')} size="lg">
      <div className="space-y-4">
        <div>
          <h2 className="text-xl font-bold text-gray-900 dark:text-white">SPKD</h2>
          <p className="text-sm text-gray-500 dark:text-gray-400 mt-0.5">Version: {__APP_VERSION__}</p>
        </div>
        <div className="text-sm text-gray-600 dark:text-gray-400 space-y-1">
          <p>{t('about.subtitle')}</p>
          <p className="text-xs text-gray-400 dark:text-gray-500">{t('about.subtitleEn')}</p>
        </div>
        <div>
          <p className="text-xs font-semibold text-gray-500 dark:text-gray-400 mb-1.5">{t('about.supportedStandards')}</p>
          <p className="text-xs text-gray-600 dark:text-gray-400">
            ICAO Doc 9303 (Part 10 &amp; 11), RFC 5280 (X.509), RFC 5652 (CMS), ISO 19794-5 (Face Image)
          </p>
        </div>
        {/* Open Source Licenses */}
        <div>
          <button
            onClick={() => setShowLicenses(!showLicenses)}
            className="flex items-center gap-1.5 text-xs font-semibold text-gray-500 dark:text-gray-400 hover:text-gray-700 dark:hover:text-gray-300 transition-colors"
          >
            {showLicenses ? <ChevronDown className="w-3.5 h-3.5" /> : <ChevronRight className="w-3.5 h-3.5" />}
            {t('about.openSourceLicenses')} ({OPEN_SOURCE_LICENSES.length})
          </button>
          {showLicenses && (
            <div className="mt-2 max-h-40 overflow-y-auto rounded-lg border border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-900/50">
              <table className="w-full text-xs">
                <tbody>
                  {OPEN_SOURCE_LICENSES.map((lib) => (
                    <tr key={lib.name} className="border-b border-gray-200 dark:border-gray-700 last:border-0">
                      <td className="px-2.5 py-1.5">
                        <a href={lib.url} target="_blank" rel="noopener noreferrer" className="text-blue-600 dark:text-blue-400 hover:underline font-medium">
                          {lib.name}
                        </a>
                      </td>
                      <td className="px-2 py-1.5 text-gray-500 dark:text-gray-400">{lib.version}</td>
                      <td className="px-2.5 py-1.5 text-right">
                        <span className="px-1.5 py-0.5 bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400 rounded text-[10px] font-medium">
                          {lib.license}
                        </span>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </div>

        <p className="text-sm text-gray-700 dark:text-gray-300">
          {t('about.copyright')}
        </p>
        <div className="flex items-center justify-between pt-2 border-t border-gray-200 dark:border-gray-700">
          <div className="flex items-center gap-4">
            <a href="https://www.icao.int/publications/pages/publication.aspx?docnum=9303" target="_blank" rel="noopener noreferrer" className="flex items-center gap-1.5 text-xs text-blue-600 dark:text-blue-400 hover:underline">
              <ExternalLink className="w-3 h-3" />
              ICAO Doc 9303
            </a>
            <a href="https://pkddownloadsg.icao.int/" target="_blank" rel="noopener noreferrer" className="flex items-center gap-1.5 text-xs text-blue-600 dark:text-blue-400 hover:underline">
              <ExternalLink className="w-3 h-3" />
              ICAO PKD Download
            </a>
          </div>
          <button onClick={() => setShowAbout(false)} className="px-4 py-1.5 text-sm font-medium text-gray-700 dark:text-gray-300 bg-gray-200 dark:bg-gray-700 rounded-lg hover:bg-gray-300 dark:hover:bg-gray-600 transition-colors">
            {t('common:button.close')}
          </button>
        </div>
      </div>
    </Dialog>
    </>
  );
}

export default Header;
