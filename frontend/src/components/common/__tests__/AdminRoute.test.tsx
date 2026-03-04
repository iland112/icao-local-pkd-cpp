import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@/test/test-utils';
import { AdminRoute } from '../AdminRoute';

// Mock the authApi module
vi.mock('@/services/api', () => ({
  authApi: {
    isAdmin: vi.fn(),
    isAuthenticated: vi.fn().mockReturnValue(true),
    getStoredUser: vi.fn(),
  },
}));

import { authApi } from '@/services/api';
const mockedAuthApi = vi.mocked(authApi);

beforeEach(() => {
  vi.clearAllMocks();
});

describe('AdminRoute', () => {
  it('should render children when user is admin', () => {
    mockedAuthApi.isAdmin.mockReturnValue(true);

    render(
      <AdminRoute>
        <div>Admin panel</div>
      </AdminRoute>
    );

    expect(screen.getByText('Admin panel')).toBeInTheDocument();
  });

  it('should redirect to dashboard when user is not admin', () => {
    mockedAuthApi.isAdmin.mockReturnValue(false);

    render(
      <AdminRoute>
        <div>Admin only content</div>
      </AdminRoute>
    );

    // Child should not be rendered for non-admin users
    expect(screen.queryByText('Admin only content')).not.toBeInTheDocument();
  });
});
