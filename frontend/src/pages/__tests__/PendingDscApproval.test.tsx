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
}));

const mockGetList = vi.fn();
const mockGetStats = vi.fn();

vi.mock('@/api/pendingDscApi', () => ({
  pendingDscApi: {
    getList: (...args: unknown[]) => mockGetList(...args),
    getStats: (...args: unknown[]) => mockGetStats(...args),
    approve: vi.fn(),
    reject: vi.fn(),
  },
}));

vi.mock('@/stores/toastStore', () => ({
  toast: { success: vi.fn(), error: vi.fn(), warning: vi.fn(), info: vi.fn() },
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetList.mockResolvedValue({
    data: { success: true, data: [], total: 0 },
  });
  mockGetStats.mockResolvedValue({
    data: {
      success: true,
      data: { pendingCount: 0, approvedCount: 0, rejectedCount: 0, totalCount: 0 },
    },
  });
});

describe('PendingDscApproval page', () => {
  it('should render without crashing', async () => {
    const PendingDscApproval = (await import('../PendingDscApproval')).default;
    render(<PendingDscApproval />);
    await waitFor(() => {
      expect(screen.queryByText('admin:pendingDsc.title')).not.toBeNull();
    });
  });

  it('should render the page title', async () => {
    const PendingDscApproval = (await import('../PendingDscApproval')).default;
    render(<PendingDscApproval />);
    await waitFor(() => {
      expect(screen.getByText('admin:pendingDsc.title')).toBeInTheDocument();
    });
  });

  it('should call getList and getStats on mount', async () => {
    const PendingDscApproval = (await import('../PendingDscApproval')).default;
    render(<PendingDscApproval />);
    await waitFor(() => {
      expect(mockGetList).toHaveBeenCalled();
      expect(mockGetStats).toHaveBeenCalled();
    });
  });

  it('should render stats cards', async () => {
    mockGetStats.mockResolvedValue({
      data: {
        success: true,
        data: { pendingCount: 3, approvedCount: 10, rejectedCount: 2, totalCount: 15 },
      },
    });
    const PendingDscApproval = (await import('../PendingDscApproval')).default;
    render(<PendingDscApproval />);
    await waitFor(() => {
      expect(screen.getAllByText('admin:pendingDsc.pendingCount').length).toBeGreaterThan(0);
    });
  });

  it('should show empty state when no items are returned', async () => {
    const PendingDscApproval = (await import('../PendingDscApproval')).default;
    render(<PendingDscApproval />);
    await waitFor(() => {
      expect(screen.getByText('common:table.noData')).toBeInTheDocument();
    });
  });

  it('should display DSC items in table when data is returned', async () => {
    mockGetList.mockResolvedValue({
      data: {
        success: true,
        data: [
          {
            id: 'dsc-1',
            fingerprint_sha256: 'abc123',
            subject_dn: 'CN=Test DSC,C=KR',
            issuer_dn: 'CN=Test CSCA,C=KR',
            country_code: 'KR',
            serial_number: '01',
            not_before: '2026-01-01T00:00:00Z',
            not_after: '2028-01-01T00:00:00Z',
            signature_algorithm: 'SHA256withRSA',
            public_key_algorithm: 'RSA',
            public_key_size: 2048,
            is_self_signed: false,
            validation_status: 'VALID',
            pa_verification_id: 'pa-1',
            verification_status: 'VALID',
            status: 'PENDING',
            created_at: '2026-01-01T00:00:00Z',
          },
        ],
        total: 1,
      },
    });
    const PendingDscApproval = (await import('../PendingDscApproval')).default;
    render(<PendingDscApproval />);
    await waitFor(() => {
      expect(screen.getByText('KR')).toBeInTheDocument();
    });
  });
});
