import { Menu, Bell, User, Sun, Moon, LogOut, Settings, UserCircle, Shield } from 'lucide-react';
import { useSidebarStore } from '@/stores/sidebarStore';
import { useThemeStore } from '@/stores/themeStore';
import { authApi } from '@/services/api';
import { useNavigate } from 'react-router-dom';
import { useState, useRef, useEffect } from 'react';

export function Header() {
  const { toggleMobile } = useSidebarStore();
  const { darkMode, toggleTheme } = useThemeStore();
  const navigate = useNavigate();
  const user = authApi.getStoredUser();
  const [isUserMenuOpen, setIsUserMenuOpen] = useState(false);
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
    <header className="sticky top-0 z-40 bg-white dark:bg-gray-800 border-b border-gray-200 dark:border-gray-700">
      <div className="flex items-center justify-between px-4 py-1.5">
        {/* Left: Mobile menu button */}
        <div className="flex items-center gap-4">
          <button
            onClick={toggleMobile}
            className="lg:hidden p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            aria-label="메뉴 열기"
          >
            <Menu className="w-4 h-4 text-gray-600 dark:text-gray-300" />
          </button>

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
  );
}

export default Header;
