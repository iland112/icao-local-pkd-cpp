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

const mockGet = vi.fn();
vi.mock('@/services/authApi', () => ({
  createAuthenticatedClient: () => ({
    get: (...args: unknown[]) => mockGet(...args),
  }),
}));

vi.mock('@/config/pagination', () => ({ DEFAULT_PAGE_SIZE: 20 }));

vi.mock('@/hooks/useSortableTable', () => ({
  useSortableTable: (data: unknown[]) => ({
    sortedData: data,
    sortConfig: { key: null, direction: 'asc' },
    requestSort: vi.fn(),
  }),
}));

vi.mock('@/components/common/SortableHeader', () => ({
  SortableHeader: ({ children }: { children: React.ReactNode }) => <th>{children}</th>,
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGet.mockResolvedValue({
    data: {
      logs: [],
      total: 0,
    },
  });
});

describe('AuditLog page', () => {
  it('should render without crashing', async () => {
    const { AuditLog } = await import('../AuditLog');
    render(<AuditLog />);
    await waitFor(() => {
      expect(screen.queryByText('nav:header.loginHistory')).not.toBeNull();
    });
  });

  it('should render the audit log title', async () => {
    const { AuditLog } = await import('../AuditLog');
    render(<AuditLog />);
    await waitFor(() => {
      expect(screen.getByText('nav:header.loginHistory')).toBeInTheDocument();
    });
  });

  it('should render filter section', async () => {
    const { AuditLog } = await import('../AuditLog');
    render(<AuditLog />);
    await waitFor(() => {
      // The filter card heading
      expect(screen.getByText('common:label.filter')).toBeInTheDocument();
    });
  });

  it('should show empty state when no logs are returned', async () => {
    mockGet.mockResolvedValue({ data: { logs: [], total: 0 } });
    const { AuditLog } = await import('../AuditLog');
    render(<AuditLog />);
    await waitFor(() => {
      expect(screen.getByText('admin:auditLog.noLogs_msg')).toBeInTheDocument();
    });
  });

  it('should display log entries when data is returned', async () => {
    mockGet.mockImplementation((url: string) => {
      if (url.includes('stats')) {
        return Promise.resolve({
          data: {
            totalEvents: 5,
            byEventType: { LOGIN: 3, LOGOUT: 2 },
            topUsers: [{ username: 'admin', count: 5 }],
            failedLogins: 0,
            last24hEvents: 5,
          },
        });
      }
      return Promise.resolve({
        data: {
          logs: [
            {
              id: 'log-1',
              userId: 'user-1',
              username: 'admin',
              eventType: 'LOGIN',
              ipAddress: '127.0.0.1',
              userAgent: 'Mozilla',
              success: true,
              createdAt: '2026-01-01T00:00:00Z',
            },
          ],
          total: 1,
        },
      });
    });
    const { AuditLog } = await import('../AuditLog');
    render(<AuditLog />);
    await waitFor(() => {
      expect(screen.getByText('admin')).toBeInTheDocument();
    });
  });
});
