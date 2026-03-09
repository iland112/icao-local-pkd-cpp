import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import userEvent from '@testing-library/user-event';
import { Login } from '../Login';

// Mock authApi
const mockLogin = vi.fn();
vi.mock('@/services/api', () => ({
  authApi: {
    login: (...args: unknown[]) => mockLogin(...args),
    isAuthenticated: vi.fn().mockReturnValue(false),
    getStoredUser: vi.fn().mockReturnValue(null),
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
  localStorage.clear();
});

describe('Login page', () => {
  it('should render login form with username and password fields', () => {
    render(<Login />);

    expect(screen.getByLabelText('사용자명')).toBeInTheDocument();
    expect(screen.getByLabelText('비밀번호')).toBeInTheDocument();
  });

  it('should render the login button', () => {
    render(<Login />);

    const loginButton = screen.getByRole('button', { name: '로그인' });
    expect(loginButton).toBeInTheDocument();
  });

  it('should disable login button when fields are empty', () => {
    render(<Login />);

    const loginButton = screen.getByRole('button', { name: '로그인' });
    expect(loginButton).toBeDisabled();
  });

  it('should enable login button when both fields have values', async () => {
    const user = userEvent.setup();
    render(<Login />);

    await user.type(screen.getByLabelText('사용자명'), 'admin');
    await user.type(screen.getByLabelText('비밀번호'), 'admin123');

    const loginButton = screen.getByRole('button', { name: '로그인' });
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

    await user.type(screen.getByLabelText('사용자명'), 'admin');
    await user.type(screen.getByLabelText('비밀번호'), 'admin123');
    await user.click(screen.getByRole('button', { name: '로그인' }));

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

    await user.type(screen.getByLabelText('사용자명'), 'admin');
    await user.type(screen.getByLabelText('비밀번호'), 'wrong');
    await user.click(screen.getByRole('button', { name: '로그인' }));

    await waitFor(() => {
      expect(screen.getByText('사용자명 또는 비밀번호가 올바르지 않습니다.')).toBeInTheDocument();
    });
  });

  it('should show network error message on connection failure', async () => {
    const user = userEvent.setup();
    mockLogin.mockRejectedValue(new Error('Network Error'));

    render(<Login />);

    await user.type(screen.getByLabelText('사용자명'), 'admin');
    await user.type(screen.getByLabelText('비밀번호'), 'admin123');
    await user.click(screen.getByRole('button', { name: '로그인' }));

    await waitFor(() => {
      expect(screen.getByText('로그인 중 오류가 발생했습니다. 네트워크 연결을 확인해주세요.')).toBeInTheDocument();
    });
  });

  it('should show loading state while logging in', async () => {
    const user = userEvent.setup();
    // Never resolves to keep loading state
    mockLogin.mockReturnValue(new Promise(() => {}));

    render(<Login />);

    await user.type(screen.getByLabelText('사용자명'), 'admin');
    await user.type(screen.getByLabelText('비밀번호'), 'admin123');
    await user.click(screen.getByRole('button', { name: '로그인' }));

    await waitFor(() => {
      expect(screen.getByText('로그인 중...')).toBeInTheDocument();
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

    await user.type(screen.getByLabelText('사용자명'), 'admin');
    await user.type(screen.getByLabelText('비밀번호'), 'admin123');
    await user.click(screen.getByRole('button', { name: '로그인' }));

    await waitFor(() => {
      expect(screen.getByText('서버 내부 오류')).toBeInTheDocument();
    });
  });

  it('should display SPKD branding', () => {
    render(<Login />);

    // Mobile branding or desktop branding should be present
    expect(screen.getAllByText('SPKD').length).toBeGreaterThanOrEqual(1);
  });
});
