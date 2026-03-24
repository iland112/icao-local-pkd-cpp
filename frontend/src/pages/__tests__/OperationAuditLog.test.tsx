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

vi.mock('@/utils/cn', () => ({ cn: (...args: string[]) => args.filter(Boolean).join(' ') }));

const mockGetAuditLogs = vi.fn();
const mockGetAuditStatistics = vi.fn();

vi.mock('@/services/auditApi', () => ({
  getAuditLogs: (...args: unknown[]) => mockGetAuditLogs(...args),
  getAuditStatistics: (...args: unknown[]) => mockGetAuditStatistics(...args),
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
  mockGetAuditLogs.mockResolvedValue({
    data: [],
    total: 0,
    limit: 20,
    offset: 0,
    success: true,
  });
  mockGetAuditStatistics.mockResolvedValue({
    data: {
      statistics: {
        totalOperations: 0,
        successfulOperations: 0,
        failedOperations: 0,
        operationsByType: {},
        topUsers: [],
        averageDurationMs: 0,
      },
    },
  });
});

describe('OperationAuditLog page', () => {
  it('should render without crashing', async () => {
    const { OperationAuditLog } = await import('../OperationAuditLog');
    render(<OperationAuditLog />);
    await waitFor(() => {
      expect(screen.queryByText('admin:operationAudit.title')).not.toBeNull();
    });
  });

  it('should render the operation audit log title', async () => {
    const { OperationAuditLog } = await import('../OperationAuditLog');
    render(<OperationAuditLog />);
    await waitFor(() => {
      expect(screen.getByText('admin:operationAudit.title')).toBeInTheDocument();
    });
  });

  it('should show empty state when no logs are returned', async () => {
    const { OperationAuditLog } = await import('../OperationAuditLog');
    render(<OperationAuditLog />);
    await waitFor(() => {
      expect(screen.getByText('admin:operationAudit.noOperationLogs')).toBeInTheDocument();
    });
  });

  it('should show statistics cards', async () => {
    const { OperationAuditLog } = await import('../OperationAuditLog');
    render(<OperationAuditLog />);
    await waitFor(() => {
      expect(screen.getByText('admin:operationAudit.totalOperations')).toBeInTheDocument();
    });
  });

  it('should display log entries when data is returned', async () => {
    mockGetAuditLogs.mockResolvedValue({
      data: [
        {
          id: 'op-1',
          userId: 'user-1',
          username: 'admin',
          operationType: 'FILE_UPLOAD',
          resourceId: 'res-1',
          resourceType: 'UPLOAD',
          success: true,
          durationMs: 100,
          ipAddress: '127.0.0.1',
          createdAt: '2026-01-01T00:00:00Z',
        },
      ],
      total: 1,
      limit: 20,
      offset: 0,
      success: true,
    });
    const { OperationAuditLog } = await import('../OperationAuditLog');
    render(<OperationAuditLog />);
    await waitFor(() => {
      expect(screen.getByText('admin')).toBeInTheDocument();
    });
  });

  it('should call getAuditLogs on mount', async () => {
    const { OperationAuditLog } = await import('../OperationAuditLog');
    render(<OperationAuditLog />);
    await waitFor(() => {
      expect(mockGetAuditLogs).toHaveBeenCalled();
    });
  });
});
