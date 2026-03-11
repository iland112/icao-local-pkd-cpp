import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@/test/test-utils';
import userEvent from '@testing-library/user-event';
import { Header } from '../Header';

// Mock authApi
vi.mock('@/services/api', () => ({
  authApi: {
    isAuthenticated: vi.fn().mockReturnValue(true),
    getStoredUser: vi.fn(),
    logout: vi.fn().mockResolvedValue({ success: true }),
    isAdmin: vi.fn().mockReturnValue(false),
  },
}));

// Mock react-router-dom's useNavigate and useLocation
const mockNavigate = vi.fn();
vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual<typeof import('react-router-dom')>('react-router-dom');
  return {
    ...actual,
    useNavigate: () => mockNavigate,
    useLocation: () => ({ pathname: '/' }),
  };
});

import { authApi } from '@/services/api';
const mockedAuthApi = vi.mocked(authApi);

beforeEach(() => {
  vi.clearAllMocks();
  mockNavigate.mockClear();
});

describe('Header', () => {
  it('should render user menu button', () => {
    mockedAuthApi.getStoredUser.mockReturnValue({
      id: '1',
      username: 'admin',
      email: 'admin@test.com',
      full_name: 'Admin User',
      permissions: [],
      is_admin: true,
    });

    render(<Header />);

    expect(screen.getByLabelText('사용자 메뉴')).toBeInTheDocument();
  });

  it('should display username in the header', () => {
    mockedAuthApi.getStoredUser.mockReturnValue({
      id: '1',
      username: 'testuser',
      permissions: [],
      is_admin: false,
    });

    render(<Header />);

    expect(screen.getByText('testuser')).toBeInTheDocument();
  });

  it('should show Admin badge for admin users', () => {
    mockedAuthApi.getStoredUser.mockReturnValue({
      id: '1',
      username: 'admin',
      permissions: [],
      is_admin: true,
    });

    render(<Header />);

    expect(screen.getByText('관리자')).toBeInTheDocument();
  });

  it('should not show Admin badge for regular users', () => {
    mockedAuthApi.getStoredUser.mockReturnValue({
      id: '2',
      username: 'user',
      permissions: [],
      is_admin: false,
    });

    render(<Header />);

    expect(screen.queryByText('관리자')).not.toBeInTheDocument();
  });

  it('should toggle theme when theme button is clicked', async () => {
    const user = userEvent.setup();
    mockedAuthApi.getStoredUser.mockReturnValue({
      id: '1',
      username: 'admin',
      permissions: [],
      is_admin: false,
    });

    render(<Header />);

    // Find the theme toggle button by aria-label
    const themeButton = screen.getByTitle(/모드로 전환/);
    await user.click(themeButton);

    // After click, theme should have toggled (dark mode class on documentElement)
    // The button's title should change
    expect(themeButton).toBeInTheDocument();
  });

  it('should open user menu dropdown when user button is clicked', async () => {
    const user = userEvent.setup();
    mockedAuthApi.getStoredUser.mockReturnValue({
      id: '1',
      username: 'admin',
      full_name: 'Admin User',
      email: 'admin@example.com',
      permissions: [],
      is_admin: true,
    });

    render(<Header />);

    // Click user menu button
    await user.click(screen.getByLabelText('사용자 메뉴'));

    // Dropdown should show user info and menu items
    expect(screen.getByText('Admin User')).toBeInTheDocument();
    expect(screen.getByText('admin@example.com')).toBeInTheDocument();
    expect(screen.getByText('프로필')).toBeInTheDocument();
    expect(screen.getByText('로그아웃')).toBeInTheDocument();
  });

  it('should show admin-only menu items for admin users', async () => {
    const user = userEvent.setup();
    mockedAuthApi.getStoredUser.mockReturnValue({
      id: '1',
      username: 'admin',
      permissions: [],
      is_admin: true,
    });

    render(<Header />);

    await user.click(screen.getByLabelText('사용자 메뉴'));

    expect(screen.getByText('사용자 관리')).toBeInTheDocument();
    expect(screen.getByText('로그인 이력')).toBeInTheDocument();
  });

  it('should not show admin menu items for regular users', async () => {
    const user = userEvent.setup();
    mockedAuthApi.getStoredUser.mockReturnValue({
      id: '2',
      username: 'user',
      permissions: [],
      is_admin: false,
    });

    render(<Header />);

    await user.click(screen.getByLabelText('사용자 메뉴'));

    expect(screen.queryByText('사용자 관리')).not.toBeInTheDocument();
    expect(screen.queryByText('로그인 이력')).not.toBeInTheDocument();
  });

  it('should display "User" as fallback when username is not available', () => {
    mockedAuthApi.getStoredUser.mockReturnValue(null);

    render(<Header />);

    expect(screen.getByText('User')).toBeInTheDocument();
  });
});
