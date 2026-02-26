import { Outlet } from 'react-router-dom';
import { Sidebar } from './Sidebar';
import { Header } from './Header';
import { Footer } from './Footer';
import { useSidebarStore } from '@/stores/sidebarStore';
import { cn } from '@/utils/cn';

export function Layout() {
  const { expanded } = useSidebarStore();

  return (
    <div className="min-h-screen bg-gray-50 dark:bg-gray-900">
      {/* Sidebar */}
      <Sidebar />

      {/* Main Content Wrapper */}
      <div
        className={cn(
          'flex flex-col min-h-screen transition-all duration-300',
          expanded ? 'lg:ps-64' : 'lg:ps-[70px]'
        )}
      >
        {/* Header */}
        <Header />

        {/* Main Content */}
        <main className="flex-1 pb-10">
          <Outlet />
        </main>

        {/* Footer - fixed to bottom */}
        <div className="fixed bottom-0 right-0 left-0 z-30">
          <div className={cn(
            'transition-all duration-300',
            expanded ? 'lg:ps-64' : 'lg:ps-[70px]'
          )}>
            <Footer />
          </div>
        </div>
      </div>
    </div>
  );
}

export default Layout;
