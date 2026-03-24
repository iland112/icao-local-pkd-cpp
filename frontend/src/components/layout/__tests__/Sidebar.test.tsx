import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@/test/test-utils';
import userEvent from '@testing-library/user-event';
import { Sidebar } from '../Sidebar';
import { useSidebarStore } from '@/stores/sidebarStore';

// Mock authApi so we can control admin/permission state
vi.mock('@/services/api', () => ({
  authApi: {
    isAdmin: vi.fn().mockReturnValue(false),
    hasPermission: vi.fn().mockReturnValue(true),
    isAuthenticated: vi.fn().mockReturnValue(true),
    getStoredUser: vi.fn().mockReturnValue(null),
  },
}));

// Mock appConfig to control EAC menu visibility
vi.mock('@/utils/appConfig', () => ({
  appConfig: {
    enableEacMenu: false,
  },
}));

import { authApi } from '@/services/api';
const mockedAuthApi = vi.mocked(authApi);

beforeEach(() => {
  vi.clearAllMocks();
  mockedAuthApi.isAdmin.mockReturnValue(false);
  mockedAuthApi.hasPermission.mockReturnValue(true);
  // Reset sidebar store to default expanded state
  useSidebarStore.setState({ expanded: true, mobileOpen: false });
});

describe('Sidebar', () => {
  describe('rendering basics', () => {
    it('should render the sidebar aside element', () => {
      render(<Sidebar />);
      expect(screen.getByRole('complementary')).toBeInTheDocument();
    });

    it('should render logo text when expanded', () => {
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      render(<Sidebar />);
      expect(screen.getByText('FASTpass® SPKD')).toBeInTheDocument();
    });

    it('should render brand subtitle when expanded', () => {
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      render(<Sidebar />);
      expect(screen.getByText('by SMARTCORE Inc.')).toBeInTheDocument();
    });

    it('should render the home link', () => {
      render(<Sidebar />);
      const homeLinks = screen.getAllByRole('link');
      const homeLink = homeLinks.find((l) => l.getAttribute('href') === '/');
      expect(homeLink).toBeDefined();
    });

    it('should render the collapse/expand toggle button (desktop)', () => {
      render(<Sidebar />);
      // Toggle button is hidden on mobile, visible on desktop
      const buttons = screen.getAllByRole('button');
      expect(buttons.length).toBeGreaterThan(0);
    });
  });

  describe('expanded state', () => {
    it('should have w-64 class when expanded', () => {
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      const { container } = render(<Sidebar />);
      const aside = container.querySelector('aside');
      expect(aside?.className).toContain('w-64');
    });

    it('should show section labels when expanded', () => {
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      render(<Sidebar />);
      // Section titles use i18n keys
      expect(screen.getByText('sections.certManagement')).toBeInTheDocument();
    });
  });

  describe('collapsed state', () => {
    it('should have w-[70px] class when collapsed', () => {
      useSidebarStore.setState({ expanded: false, mobileOpen: false });
      const { container } = render(<Sidebar />);
      const aside = container.querySelector('aside');
      expect(aside?.className).toContain('w-[70px]');
    });

    it('should NOT render brand subtitle text when collapsed', () => {
      useSidebarStore.setState({ expanded: false, mobileOpen: false });
      render(<Sidebar />);
      expect(screen.queryByText('by SMARTCORE Inc.')).not.toBeInTheDocument();
    });
  });

  describe('mobile overlay', () => {
    it('should show mobile overlay when mobileOpen=true', () => {
      useSidebarStore.setState({ expanded: true, mobileOpen: true });
      const { container } = render(<Sidebar />);
      const overlay = container.querySelector('.bg-gray-900\\/50');
      expect(overlay).toBeInTheDocument();
    });

    it('should NOT show mobile overlay when mobileOpen=false', () => {
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      const { container } = render(<Sidebar />);
      const overlay = container.querySelector('.bg-gray-900\\/50');
      expect(overlay).not.toBeInTheDocument();
    });

    it('should close mobile overlay when overlay is clicked', async () => {
      const user = userEvent.setup();
      useSidebarStore.setState({ expanded: true, mobileOpen: true });
      const { container } = render(<Sidebar />);
      const overlay = container.querySelector('.bg-gray-900\\/50') as HTMLElement;
      await user.click(overlay);
      expect(useSidebarStore.getState().mobileOpen).toBe(false);
    });
  });

  describe('toggle expand button', () => {
    it('should toggle expanded state when toggle button is clicked', async () => {
      const user = userEvent.setup();
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      render(<Sidebar />);

      // The toggle button is the first role="button" in the desktop toggle area
      // Find it by its title attribute
      const collapseBtn = screen.getByTitle('sidebar.collapse');
      await user.click(collapseBtn);
      expect(useSidebarStore.getState().expanded).toBe(false);
    });

    it('should show expand button title when collapsed', () => {
      useSidebarStore.setState({ expanded: false, mobileOpen: false });
      render(<Sidebar />);
      const expandBtn = screen.getByTitle('sidebar.expand');
      expect(expandBtn).toBeInTheDocument();
    });
  });

  describe('admin-only menu items', () => {
    it('should NOT show adminOnly items when user is not admin', () => {
      mockedAuthApi.isAdmin.mockReturnValue(false);
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      render(<Sidebar />);
      // 'menu.userManagement' should be hidden for non-admins
      expect(screen.queryByText('menu.userManagement')).not.toBeInTheDocument();
    });

    it('should show adminOnly items when user is admin (after expanding section)', async () => {
      const user = userEvent.setup();
      mockedAuthApi.isAdmin.mockReturnValue(true);
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      render(<Sidebar />);
      // System admin section is visible for admin — expand it to see items
      const sectionBtn = screen.getByText('sections.systemAdmin').closest('button')!;
      await user.click(sectionBtn);
      expect(screen.getByText('menu.userManagement')).toBeInTheDocument();
    });

    it('should hide pending DSC menu item for non-admin', () => {
      mockedAuthApi.isAdmin.mockReturnValue(false);
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      render(<Sidebar />);
      expect(screen.queryByText('menu.pendingDsc')).not.toBeInTheDocument();
    });
  });

  describe('section expand/collapse', () => {
    it('should expand a section when its header button is clicked', async () => {
      const user = userEvent.setup();
      // Start with all sections collapsed — easiest way is to start with a fresh state
      // where no section is auto-expanded (no active route)
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      render(<Sidebar />);

      // The cert management section header
      const sectionBtn = screen.getByText('sections.certManagement').closest('button')!;
      // Click to toggle
      await user.click(sectionBtn);
      // After click the section should be expanded showing items
      // (certSearch is a child item)
      expect(screen.getByText('menu.certSearch')).toBeInTheDocument();
    });
  });

  describe('navigation items', () => {
    it('should render ICAO version menu link after expanding the section', async () => {
      const user = userEvent.setup();
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      render(<Sidebar />);

      // Expand ICAO integration section first
      const sectionBtn = screen.getByText('sections.icaoIntegration').closest('button')!;
      await user.click(sectionBtn);

      const links = screen.getAllByRole('link');
      const icaoLink = links.find((l) => l.getAttribute('href') === '/icao');
      expect(icaoLink).toBeDefined();
    });

    it('should render PA verify menu link after expanding the section', async () => {
      const user = userEvent.setup();
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      render(<Sidebar />);

      // Expand forgery detection section first
      const sectionBtn = screen.getByText('sections.forgeryDetection').closest('button')!;
      await user.click(sectionBtn);

      const links = screen.getAllByRole('link');
      const paLink = links.find((l) => l.getAttribute('href') === '/pa/verify');
      expect(paLink).toBeDefined();
    });
  });

  describe('active link styling', () => {
    it('should apply active styles to home link when at /', () => {
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      render(<Sidebar />);
      // BrowserRouter in test-utils renders at '/', so home nav link should be active.
      // There are two links to '/': the logo link and the home nav link.
      // The home nav link is the one inside the <nav> element with the active styling.
      const allLinks = screen.getAllByRole('link').filter((l) => l.getAttribute('href') === '/');
      // The nav home link has the dynamic active class — check that at least one has bg-blue-50
      const hasActiveLink = allLinks.some((l) => l.className.includes('bg-blue-50'));
      expect(hasActiveLink).toBe(true);
    });
  });

  describe('EAC menu flag', () => {
    it('should NOT show EAC menu when enableEacMenu=false', () => {
      mockedAuthApi.isAdmin.mockReturnValue(true);
      useSidebarStore.setState({ expanded: true, mobileOpen: false });
      render(<Sidebar />);
      expect(screen.queryByText('menu.eacDashboard')).not.toBeInTheDocument();
    });
  });

  describe('logo image', () => {
    it('should render favicon svg as logo', () => {
      render(<Sidebar />);
      const logo = screen.getByAltText('FASTpass® SPKD');
      expect(logo).toBeInTheDocument();
      expect(logo).toHaveAttribute('src', '/favicon.svg');
    });
  });
});
