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
  formatDate: (d: string) => d || '',
  formatDateTime: (d: string) => d || '',
}));

const mockGetAllClients = vi.fn();
const mockGetAllRequests = vi.fn();

vi.mock('@/api/apiClientApi', () => ({
  apiClientApi: {
    getAll: (...args: unknown[]) => mockGetAllClients(...args),
    create: vi.fn(),
    update: vi.fn(),
    deactivate: vi.fn(),
    regenerate: vi.fn(),
    getUsage: vi.fn(),
  },
}));

vi.mock('@/api/apiClientRequestApi', () => ({
  apiClientRequestApi: {
    getAll: (...args: unknown[]) => mockGetAllRequests(...args),
    approve: vi.fn(),
    reject: vi.fn(),
  },
}));

vi.mock('@/stores/toastStore', () => ({
  toast: { success: vi.fn(), error: vi.fn(), warning: vi.fn(), info: vi.fn() },
}));

vi.mock('@/components/common', () => ({
  ConfirmDialog: () => null,
}));

vi.mock('@/components/apiClient/ApiClientDialogs', () => ({
  CreateDialog: () => null,
  EditDialog: () => null,
  DeleteDialog: () => null,
  UsageDialog: () => null,
  ApiKeyDialog: () => null,
  RequestRow: () => <tr><td>mock-request</td></tr>,
  RequestDetailDialog: () => null,
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetAllClients.mockResolvedValue({ clients: [], total: 0 });
  mockGetAllRequests.mockResolvedValue({ requests: [], total: 0 });
});

describe('ApiClientManagement page', () => {
  it('should render without crashing', async () => {
    const ApiClientManagement = (await import('../ApiClientManagement')).default;
    render(<ApiClientManagement />);
    await waitFor(() => {
      expect(screen.queryByText('admin:apiClient.title')).not.toBeNull();
    });
  });

  it('should render the page title', async () => {
    const ApiClientManagement = (await import('../ApiClientManagement')).default;
    render(<ApiClientManagement />);
    await waitFor(() => {
      expect(screen.getByText('admin:apiClient.title')).toBeInTheDocument();
    });
  });

  it('should render the register client button', async () => {
    const ApiClientManagement = (await import('../ApiClientManagement')).default;
    render(<ApiClientManagement />);
    await waitFor(() => {
      expect(screen.getByText('admin:apiClient.registerClient')).toBeInTheDocument();
    });
  });

  it('should call getAll on mount', async () => {
    const ApiClientManagement = (await import('../ApiClientManagement')).default;
    render(<ApiClientManagement />);
    await waitFor(() => {
      expect(mockGetAllClients).toHaveBeenCalled();
    });
  });

  it('should render stats cards', async () => {
    const ApiClientManagement = (await import('../ApiClientManagement')).default;
    render(<ApiClientManagement />);
    await waitFor(() => {
      expect(screen.getByText('admin:apiClient.totalClients')).toBeInTheDocument();
    });
  });

  it('should display clients when returned from API', async () => {
    mockGetAllClients.mockResolvedValue({
      clients: [
        {
          id: 'client-1',
          client_name: 'Test Client',
          api_key_prefix: 'icao_test',
          description: 'Test',
          permissions: ['cert:read'],
          allowed_endpoints: [],
          allowed_ips: [],
          rate_limit_per_minute: 60,
          rate_limit_per_hour: 1000,
          rate_limit_per_day: 10000,
          is_active: true,
          expires_at: '2027-01-01T00:00:00Z',
          last_used_at: '',
          total_requests: 0,
          created_by: 'admin',
          created_at: '2026-01-01T00:00:00Z',
          updated_at: '2026-01-01T00:00:00Z',
        },
      ],
      total: 1,
    });
    const ApiClientManagement = (await import('../ApiClientManagement')).default;
    render(<ApiClientManagement />);
    await waitFor(() => {
      expect(screen.getByText('Test Client')).toBeInTheDocument();
    });
  });
});
