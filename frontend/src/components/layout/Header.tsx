import { Menu, Bell, Search, User } from 'lucide-react';
import { useSidebarStore } from '@/stores/sidebarStore';

export function Header() {
  const { toggleMobile } = useSidebarStore();

  return (
    <header className="sticky top-0 z-40 bg-white dark:bg-gray-800 border-b border-gray-200 dark:border-gray-700">
      <div className="flex items-center justify-between px-4 py-3">
        {/* Left: Mobile menu button */}
        <div className="flex items-center gap-4">
          <button
            onClick={toggleMobile}
            className="lg:hidden p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            aria-label="메뉴 열기"
          >
            <Menu className="w-5 h-5 text-gray-600 dark:text-gray-300" />
          </button>

          {/* Search */}
          <div className="hidden sm:flex items-center">
            <div className="relative">
              <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
              <input
                type="text"
                placeholder="검색..."
                className="w-64 pl-10 pr-4 py-2 text-sm bg-gray-50 dark:bg-gray-900 border border-gray-200 dark:border-gray-700 rounded-lg focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent transition-all"
              />
            </div>
          </div>
        </div>

        {/* Right: Actions */}
        <div className="flex items-center gap-2">
          {/* Notifications */}
          <button className="relative p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors">
            <Bell className="w-5 h-5 text-gray-600 dark:text-gray-300" />
            <span className="absolute top-1 right-1 w-2 h-2 bg-red-500 rounded-full"></span>
          </button>

          {/* User Menu */}
          <div className="hs-dropdown relative inline-flex">
            <button
              id="user-dropdown"
              type="button"
              className="hs-dropdown-toggle flex items-center gap-2 p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            >
              <div className="w-8 h-8 bg-gradient-to-br from-blue-500 to-indigo-500 rounded-full flex items-center justify-center">
                <User className="w-4 h-4 text-white" />
              </div>
              <span className="hidden sm:block text-sm font-medium text-gray-700 dark:text-gray-200">
                Admin
              </span>
            </button>

            <div
              className="hs-dropdown-menu transition-[opacity,margin] duration hs-dropdown-open:opacity-100 opacity-0 hidden min-w-48 bg-white dark:bg-gray-800 shadow-lg rounded-lg p-2 mt-2 border border-gray-200 dark:border-gray-700"
              aria-labelledby="user-dropdown"
            >
              <a
                href="#"
                className="flex items-center gap-x-3.5 py-2 px-3 rounded-lg text-sm text-gray-800 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700"
              >
                프로필
              </a>
              <a
                href="#"
                className="flex items-center gap-x-3.5 py-2 px-3 rounded-lg text-sm text-gray-800 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700"
              >
                설정
              </a>
              <hr className="my-2 border-gray-200 dark:border-gray-700" />
              <a
                href="#"
                className="flex items-center gap-x-3.5 py-2 px-3 rounded-lg text-sm text-red-600 hover:bg-red-50 dark:hover:bg-red-900/20"
              >
                로그아웃
              </a>
            </div>
          </div>
        </div>
      </div>
    </header>
  );
}

export default Header;
