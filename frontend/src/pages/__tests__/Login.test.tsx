import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import userEvent from '@testing-library/user-event';
import { Login } from '../Login';

// Mock react-i18next to handle returnObjects calls that expect arrays
vi.mock('react-i18next', async () => {
  const actual = await vi.importActual<typeof import('react-i18next')>('react-i18next');
  return {
    ...actual,
    useTranslation: () => ({
      t: (key: string, opts?: { returnObjects?: boolean }) => {
        if (opts?.returnObjects) return [];
        return key;
      },
      i18n: {
        language: 'ko',
        changeLanguage: vi.fn(),
      },
    }),
  };
});

// Mock authApi
const mockLogin = vi.fn();
vi.mock('@/services/api', () => ({
  authApi: {
    login: (...args: unknown[]) => mockLogin(...args),
    isAuthenticated: vi.fn().mockReturnValue(false),
    getStoredUser: vi.fn().mockReturnValue(null),
  },
}));

// Mock uploadHistoryApi (used for statistics fetch on mount)
const mockGetStatistics = vi.fn();
vi.mock('@/services/pkdApi', () => ({
  uploadHistoryApi: {
    getStatistics: (...args: unknown[]) => mockGetStatistics(...args),
  },
}));

// Mock useNavigate
const mockNavigate = vi.fn();
vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual<typeof import('react-router-dom')>('react-router-dom');
  return {
    ...actual,
    useNavigate: () => mockNavigate,
  };
});

beforeEach(() => {
  vi.clearAllMocks();
  mockNavigate.mockClear();
  mockGetStatistics.mockResolvedValue({ data: { countriesCount: 0, totalCertificates: 0 } });
  localStorage.clear();
});

describe('Login page', () => {
  // i18n is mocked so t('key') returns the raw key string.
  // Login.tsx uses useTranslation(['auth', 'common']), and the mock t()
  // returns the key as-is (e.g., t('login.username') -> 'login.username').

  it('should render login form with username and password fields', () => {
    render(<Login />);

    expect(screen.getByLabelText('login.username')).toBeInTheDocument();
    expect(screen.getByLabelText('login.password')).toBeInTheDocument();
  });

  it('should render the login button', () => {
    render(<Login />);

    const loginButton = screen.getByRole('button', { name: 'login.submit' });
    expect(loginButton).toBeInTheDocument();
  });

  it('should disable login button when fields are empty', () => {
    render(<Login />);

    const loginButton = screen.getByRole('button', { name: 'login.submit' });
    expect(loginButton).toBeDisabled();
  });

  it('should enable login button when both fields have values', async () => {
    const user = userEvent.setup();
    render(<Login />);

    await user.type(screen.getByLabelText('login.username'), 'admin');
    await user.type(screen.getByLabelText('login.password'), 'admin123');

    const loginButton = screen.getByRole('button', { name: 'login.submit' });
    expect(loginButton).toBeEnabled();
  });

  it('should call login API and navigate on successful login', async () => {
    const user = userEvent.setup();
    mockLogin.mockResolvedValue({
      success: true,
      access_token: 'jwt-token-here',
      user: {
        id: '1',
        username: 'admin',
        is_admin: true,
        permissions: [],
      },
    });

    render(<Login />);

    await user.type(screen.getByLabelText('login.username'), 'admin');
    await user.type(screen.getByLabelText('login.password'), 'admin123');
    await user.click(screen.getByRole('button', { name: 'login.submit' }));

    await waitFor(() => {
      expect(mockLogin).toHaveBeenCalledWith('admin', 'admin123');
      expect(mockNavigate).toHaveBeenCalledWith('/');
    });

    // Token should be stored in localStorage
    expect(localStorage.getItem('access_token')).toBe('jwt-token-here');
  });

  it('should show error message on 401 (wrong credentials)', async () => {
    const user = userEvent.setup();
    mockLogin.mockRejectedValue({
      response: { status: 401 },
    });

    render(<Login />);

    await user.type(screen.getByLabelText('login.username'), 'admin');
    await user.type(screen.getByLabelText('login.password'), 'wrong');
    await user.click(screen.getByRole('button', { name: 'login.submit' }));

    await waitFor(() => {
      expect(screen.getByText('login.invalidCredentials')).toBeInTheDocument();
    });
  });

  it('should show network error message on connection failure', async () => {
    const user = userEvent.setup();
    mockLogin.mockRejectedValue(new Error('Network Error'));

    render(<Login />);

    await user.type(screen.getByLabelText('login.username'), 'admin');
    await user.type(screen.getByLabelText('login.password'), 'admin123');
    await user.click(screen.getByRole('button', { name: 'login.submit' }));

    await waitFor(() => {
      expect(screen.getByText('login.networkError')).toBeInTheDocument();
    });
  });

  it('should show loading state while logging in', async () => {
    const user = userEvent.setup();
    // Never resolves to keep loading state
    mockLogin.mockReturnValue(new Promise(() => {}));

    render(<Login />);

    await user.type(screen.getByLabelText('login.username'), 'admin');
    await user.type(screen.getByLabelText('login.password'), 'admin123');
    await user.click(screen.getByRole('button', { name: 'login.submit' }));

    await waitFor(() => {
      expect(screen.getByText('login.loggingIn')).toBeInTheDocument();
    });
  });

  it('should show server error message from response data', async () => {
    const user = userEvent.setup();
    mockLogin.mockRejectedValue({
      response: {
        status: 500,
        data: { message: '서버 내부 오류' },
      },
    });

    render(<Login />);

    await user.type(screen.getByLabelText('login.username'), 'admin');
    await user.type(screen.getByLabelText('login.password'), 'admin123');
    await user.click(screen.getByRole('button', { name: 'login.submit' }));

    await waitFor(() => {
      expect(screen.getByText('서버 내부 오류')).toBeInTheDocument();
    });
  });

  it('should display FastSPKD branding', () => {
    render(<Login />);

    // Mobile branding or desktop branding should be present
    expect(screen.getAllByText('FastSPKD').length).toBeGreaterThanOrEqual(1);
  });
});
