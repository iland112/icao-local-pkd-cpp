import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@/test/test-utils';
import { PrivateRoute } from '../PrivateRoute';

// Mock the authApi module
vi.mock('@/services/api', () => ({
  authApi: {
    isAuthenticated: vi.fn(),
    getStoredUser: vi.fn(),
  },
}));

// Import the mocked module to control return values
import { authApi } from '@/services/api';
const mockedAuthApi = vi.mocked(authApi);

// Track navigation by checking the final rendered location
// (BrowserRouter from test-utils handles this)

beforeEach(() => {
  localStorage.clear();
  vi.clearAllMocks();
});

describe('PrivateRoute', () => {
  it('should render children when user is authenticated', () => {
    mockedAuthApi.isAuthenticated.mockReturnValue(true);
    // Create a valid JWT (exp in the future) — PrivateRoute checks token format
    const header = btoa(JSON.stringify({ alg: 'HS256', typ: 'JWT' }));
    const payload = btoa(JSON.stringify({ sub: 'user1', exp: Math.floor(Date.now() / 1000) + 3600 }));
    localStorage.setItem('access_token', `${header}.${payload}.fake-sig`);

    render(
      <PrivateRoute>
        <div>Protected content</div>
      </PrivateRoute>
    );

    expect(screen.getByText('Protected content')).toBeInTheDocument();
  });

  it('should redirect to /login when not authenticated', () => {
    mockedAuthApi.isAuthenticated.mockReturnValue(false);

    render(
      <PrivateRoute>
        <div>Should not see this</div>
      </PrivateRoute>
    );

    // The child should not be rendered
    expect(screen.queryByText('Should not see this')).not.toBeInTheDocument();
  });

  it('should redirect to /login when token is expired', () => {
    mockedAuthApi.isAuthenticated.mockReturnValue(true);

    // Create an expired JWT (exp in the past)
    const header = btoa(JSON.stringify({ alg: 'HS256', typ: 'JWT' }));
    const payload = btoa(JSON.stringify({
      sub: 'user1',
      exp: Math.floor(Date.now() / 1000) - 3600, // 1 hour ago
    }));
    const signature = 'fake-signature';
    const expiredToken = `${header}.${payload}.${signature}`;
    localStorage.setItem('access_token', expiredToken);

    render(
      <PrivateRoute>
        <div>Protected content</div>
      </PrivateRoute>
    );

    // Should redirect (child not shown)
    expect(screen.queryByText('Protected content')).not.toBeInTheDocument();
    // localStorage should be cleared
    expect(localStorage.getItem('access_token')).toBeNull();
    expect(localStorage.getItem('user')).toBeNull();
  });

  it('should render children when token is valid (not expired)', () => {
    mockedAuthApi.isAuthenticated.mockReturnValue(true);

    // Create a valid JWT (exp in the future)
    const header = btoa(JSON.stringify({ alg: 'HS256', typ: 'JWT' }));
    const payload = btoa(JSON.stringify({
      sub: 'user1',
      exp: Math.floor(Date.now() / 1000) + 3600, // 1 hour from now
    }));
    const signature = 'fake-signature';
    const validToken = `${header}.${payload}.${signature}`;
    localStorage.setItem('access_token', validToken);

    render(
      <PrivateRoute>
        <div>Valid session content</div>
      </PrivateRoute>
    );

    expect(screen.getByText('Valid session content')).toBeInTheDocument();
  });

  it('should treat token without exp claim as valid', () => {
    mockedAuthApi.isAuthenticated.mockReturnValue(true);

    // JWT without exp claim
    const header = btoa(JSON.stringify({ alg: 'HS256' }));
    const payload = btoa(JSON.stringify({ sub: 'user1' }));
    const signature = 'fake-signature';
    const noExpToken = `${header}.${payload}.${signature}`;
    localStorage.setItem('access_token', noExpToken);

    render(
      <PrivateRoute>
        <div>No exp content</div>
      </PrivateRoute>
    );

    expect(screen.getByText('No exp content')).toBeInTheDocument();
  });

  it('should redirect when token is malformed (not 3 parts)', () => {
    mockedAuthApi.isAuthenticated.mockReturnValue(true);
    localStorage.setItem('access_token', 'malformed-token');

    render(
      <PrivateRoute>
        <div>Should not see</div>
      </PrivateRoute>
    );

    expect(screen.queryByText('Should not see')).not.toBeInTheDocument();
  });
});
