import { Menu, Bell, User, Sun, Moon, LogOut, Settings, UserCircle, Shield, HelpCircle, ExternalLink, ChevronDown, ChevronRight, Home } from 'lucide-react';
import { useSidebarStore } from '@/stores/sidebarStore';
import { useThemeStore } from '@/stores/themeStore';
import { authApi } from '@/services/api';
import { useNavigate, useLocation, Link } from 'react-router-dom';
import { useState, useRef, useEffect } from 'react';
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

export function Header() {
  const { toggleMobile } = useSidebarStore();
  const { darkMode, toggleTheme } = useThemeStore();
  const navigate = useNavigate();
  const location = useLocation();
  const breadcrumbs = getBreadcrumbs(location.pathname);
  const user = authApi.getStoredUser();
  const [isUserMenuOpen, setIsUserMenuOpen] = useState(false);
  const [showAbout, setShowAbout] = useState(false);
  const [showLicenses, setShowLicenses] = useState(false);
  const dropdownRef = useRef<HTMLDivElement>(null);

  const handleLogout = async () => {
    try {
      await authApi.logout();
      navigate('/login');
    } catch (error) {
      console.error('Logout failed:', error);
      // Force logout even if API fails
      localStorage.clear();
      navigate('/login');
    }
  };

  // Close dropdown when clicking outside
  useEffect(() => {
    const handleClickOutside = (event: MouseEvent) => {
      if (dropdownRef.current && !dropdownRef.current.contains(event.target as Node)) {
        setIsUserMenuOpen(false);
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
            aria-label="메뉴 열기"
          >
            <Menu className="w-4 h-4 text-gray-600 dark:text-gray-300" />
          </button>

          {/* Breadcrumb (desktop only) */}
          {location.pathname !== '/login' && (
            <nav className="hidden lg:flex items-center gap-1 text-sm min-w-0" aria-label="breadcrumb">
              <Link to="/" className={`flex items-center gap-1 transition-colors flex-shrink-0 ${breadcrumbs.length === 0 ? 'font-medium text-gray-700 dark:text-gray-200' : 'text-gray-400 dark:text-gray-500 hover:text-blue-600 dark:hover:text-blue-400'}`}>
                <Home className="w-3.5 h-3.5" />
                {breadcrumbs.length === 0 && <span>Home</span>}
              </Link>
              {breadcrumbs.map((item, index) => {
                const isLast = index === breadcrumbs.length - 1;
                return (
                  <span key={index} className="flex items-center gap-1 min-w-0">
                    <ChevronRight className="w-3 h-3 text-gray-300 dark:text-gray-600 flex-shrink-0" />
                    {item.path && !isLast ? (
                      <Link to={item.path} className="text-gray-400 dark:text-gray-500 hover:text-blue-600 dark:hover:text-blue-400 transition-colors truncate">
                        {item.label}
                      </Link>
                    ) : (
                      <span className={isLast ? 'font-medium text-gray-700 dark:text-gray-200 truncate' : 'text-gray-400 dark:text-gray-500 truncate'}>
                        {item.label}
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
          {/* Theme Toggle */}
          <button
            onClick={toggleTheme}
            className="p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            title={darkMode ? '라이트 모드로 전환' : '다크 모드로 전환'}
          >
            {darkMode ? (
              <Sun className="w-4 h-4 text-yellow-500" />
            ) : (
              <Moon className="w-4 h-4 text-gray-600" />
            )}
          </button>

          {/* Notifications */}
          <button className="relative p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors">
            <Bell className="w-4 h-4 text-gray-600 dark:text-gray-300" />
            <span className="absolute top-0.5 right-0.5 w-1.5 h-1.5 bg-red-500 rounded-full"></span>
          </button>

          {/* User Menu */}
          <div className="relative inline-flex" ref={dropdownRef}>
            <button
              type="button"
              onClick={() => setIsUserMenuOpen(!isUserMenuOpen)}
              className="flex items-center gap-1.5 p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
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
                    Admin
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
                  {user?.email || 'No email'}
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
                프로필
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
                    사용자 관리
                  </button>
                  <button
                    onClick={() => {
                      navigate('/admin/audit-log');
                      setIsUserMenuOpen(false);
                    }}
                    className="w-full flex items-center gap-x-3.5 py-2 px-3 rounded-lg text-sm text-gray-800 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
                  >
                    <Settings className="w-4 h-4" />
                    로그인 이력
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
                About
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
                로그아웃
              </button>
              </div>
            )}
          </div>
        </div>
      </div>
    </header>

    {/* About Dialog */}
    <Dialog isOpen={showAbout} onClose={() => setShowAbout(false)} title="About" size="lg">
      <div className="space-y-4">
        <div>
          <h2 className="text-xl font-bold text-gray-900 dark:text-white">ICAO Local PKD</h2>
          <p className="text-sm text-gray-500 dark:text-gray-400 mt-0.5">Version: 2.22.1</p>
        </div>
        <div className="text-sm text-gray-600 dark:text-gray-400 space-y-1">
          <p>ICAO 전자여권 PKD 관리 및 Passive Authentication 시스템</p>
          <p className="text-xs text-gray-400 dark:text-gray-500">ePassport Public Key Directory Management &amp; Passive Authentication System</p>
        </div>
        <div>
          <p className="text-xs font-semibold text-gray-500 dark:text-gray-400 mb-1.5">Supported Standards:</p>
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
            Open Source Licenses ({OPEN_SOURCE_LICENSES.length})
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
          Copyright (c) SMARTCORE Inc. All rights reserved.
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
            Close
          </button>
        </div>
      </div>
    </Dialog>
    </>
  );
}

export default Header;
