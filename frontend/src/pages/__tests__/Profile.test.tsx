import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@/test/test-utils';

vi.mock('react-i18next', async () => {
  const actual = await vi.importActual<typeof import('react-i18next')>('react-i18next');
  return {
    ...actual,
    useTranslation: () => ({
      t: (key: string) => key,
      i18n: { language: 'ko', changeLanguage: vi.fn() },
    }),
  };
});

vi.mock('@/services/authApi', () => ({
  createAuthenticatedClient: () => ({
    put: vi.fn().mockResolvedValue({}),
  }),
}));

const mockGetStoredUser = vi.fn();
vi.mock('@/services/api', () => ({
  authApi: {
    getStoredUser: (...args: unknown[]) => mockGetStoredUser(...args),
    isAuthenticated: vi.fn().mockReturnValue(true),
  },
}));

vi.mock('@/utils/permissions', () => ({
  PERMISSION_GROUPS: [
    {
      label: 'admin:permGroup.certificates',
      permissions: [{ value: 'cert:read', label: 'admin:perms.certRead' }],
    },
  ],
  AVAILABLE_PERMISSIONS: ['cert:read'],
}));

const adminUser = {
  id: 'user-1',
  username: 'admin',
  email: 'admin@example.com',
  full_name: 'Admin User',
  is_admin: true,
  permissions: [],
};

const regularUser = {
  id: 'user-2',
  username: 'testuser',
  email: 'test@example.com',
  full_name: 'Test User',
  is_admin: false,
  permissions: [],
};

beforeEach(() => {
  vi.clearAllMocks();
});

describe('Profile page', () => {
  it('should show "cannot load user" message when no user is stored', async () => {
    mockGetStoredUser.mockReturnValue(null);
    const { Profile } = await import('../Profile');
    render(<Profile />);
    expect(screen.getByText('auth:profile.cannotLoadUser')).toBeInTheDocument();
  });

  it('should render profile title when user is logged in', async () => {
    mockGetStoredUser.mockReturnValue(adminUser);
    const { Profile } = await import('../Profile');
    render(<Profile />);
    expect(screen.getByText('auth:profile.title')).toBeInTheDocument();
  });

  it('should display username', async () => {
    mockGetStoredUser.mockReturnValue(regularUser);
    const { Profile } = await import('../Profile');
    render(<Profile />);
    expect(screen.getByText('@testuser')).toBeInTheDocument();
  });

  it('should display admin badge for admin user', async () => {
    mockGetStoredUser.mockReturnValue(adminUser);
    const { Profile } = await import('../Profile');
    render(<Profile />);
    expect(screen.getByText('admin:userManagement.adminUsers')).toBeInTheDocument();
  });

  it('should show change password button', async () => {
    mockGetStoredUser.mockReturnValue({ ...regularUser });
    const { Profile } = await import('../Profile');
    render(<Profile />);
    expect(screen.getAllByText('auth:profile.changePassword').length).toBeGreaterThan(0);
  });

  it('should show "all permissions" text for admin user', async () => {
    mockGetStoredUser.mockReturnValue(adminUser);
    const { Profile } = await import('../Profile');
    render(<Profile />);
    expect(screen.getByText('common:label.allPermissions')).toBeInTheDocument();
  });

  it('should show "no permissions" text for user with empty permissions', async () => {
    mockGetStoredUser.mockReturnValue({ ...regularUser, is_admin: false, permissions: [] });
    const { Profile } = await import('../Profile');
    render(<Profile />);
    expect(screen.getByText('common:label.noPermissions')).toBeInTheDocument();
  });
});
