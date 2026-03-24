import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';

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

vi.mock('@/utils/dateFormat', () => ({
  formatDateTime: (d: string) => d || '',
}));

vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual<typeof import('react-router-dom')>('react-router-dom');
  return { ...actual, useNavigate: () => vi.fn() };
});

const mockGet = vi.fn();
const mockPost = vi.fn();
const mockPut = vi.fn();
const mockDelete = vi.fn();

vi.mock('@/services/authApi', () => ({
  createAuthenticatedClient: () => ({
    get: (...args: unknown[]) => mockGet(...args),
    post: (...args: unknown[]) => mockPost(...args),
    put: (...args: unknown[]) => mockPut(...args),
    delete: (...args: unknown[]) => mockDelete(...args),
  }),
}));

vi.mock('@/services/api', () => ({
  authApi: {
    isAuthenticated: vi.fn().mockReturnValue(true),
    getStoredUser: vi.fn().mockReturnValue({ id: 'user-1', username: 'admin', is_admin: true, permissions: [] }),
  },
}));

vi.mock('@/utils/permissions', () => ({
  PERMISSION_GROUPS: [],
  AVAILABLE_PERMISSIONS: [],
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGet.mockResolvedValue({ data: { data: [], users: [] } });
});

describe('UserManagement page', () => {
  it('should render without crashing', async () => {
    const { UserManagement } = await import('../UserManagement');
    render(<UserManagement />);
    await waitFor(() => {
      expect(screen.queryByText('admin:userMgmt.title')).not.toBeNull();
    });
  });

  it('should render the page title', async () => {
    const { UserManagement } = await import('../UserManagement');
    render(<UserManagement />);
    await waitFor(() => {
      expect(screen.getByText('admin:userMgmt.title')).toBeInTheDocument();
    });
  });

  it('should render the add user button', async () => {
    const { UserManagement } = await import('../UserManagement');
    render(<UserManagement />);
    await waitFor(() => {
      expect(screen.getByText('admin:userMgmt.addUser')).toBeInTheDocument();
    });
  });

  it('should call get users API on mount', async () => {
    const { UserManagement } = await import('../UserManagement');
    render(<UserManagement />);
    await waitFor(() => {
      expect(mockGet).toHaveBeenCalledWith('/users');
    });
  });

  it('should display user cards when users are returned', async () => {
    mockGet.mockResolvedValue({
      data: {
        data: [
          {
            id: 'user-1',
            username: 'admin',
            email: 'admin@example.com',
            full_name: 'Admin User',
            is_admin: true,
            is_active: true,
            permissions: [],
            created_at: '2026-01-01T00:00:00Z',
          },
        ],
      },
    });
    const { UserManagement } = await import('../UserManagement');
    render(<UserManagement />);
    await waitFor(() => {
      // Username is displayed as '@admin' or full name 'Admin User'
      expect(screen.getByText('Admin User')).toBeInTheDocument();
    });
  });
});
