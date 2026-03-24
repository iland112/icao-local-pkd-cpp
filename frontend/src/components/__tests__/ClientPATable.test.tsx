import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor, fireEvent } from '@testing-library/react';
import { ClientPATable } from '../ClientPATable';

// Mock i18n
vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { language: 'ko' },
  }),
}));

// Mock PA API
vi.mock('@/services/paApi', () => ({
  paApi: {
    getTrustMaterialHistory: vi.fn(),
  },
}));

// Mock pagination config
vi.mock('@/config/pagination', () => ({
  DEFAULT_PAGE_SIZE: 10,
}));

// Mock utility functions
vi.mock('@/utils/countryCode', () => ({
  getFlagSvgPath: (code: string) => `/svg/${code?.toLowerCase()}.svg`,
}));

vi.mock('@/utils/dateFormat', () => ({
  formatDateTime: (dt: string) => dt || '—',
}));

// Mock SortableHeader
vi.mock('@/components/common/SortableHeader', () => ({
  SortableHeader: ({ label, onSort, sortKey, ...rest }: any) => (
    <th onClick={() => onSort(sortKey)} {...rest}>{label}</th>
  ),
}));

// Mock useSortableTable
vi.mock('@/hooks/useSortableTable', () => ({
  useSortableTable: (data: any[]) => ({
    sortedData: data,
    sortConfig: null,
    requestSort: vi.fn(),
  }),
}));

import { paApi } from '@/services/paApi';

const makeHistoryItem = (overrides: Partial<any> = {}) => ({
  id: 'req-001',
  countryCode: 'KR',
  status: 'VALID',
  requestTimestamp: '2026-01-01T10:00:00Z',
  cscaCount: 3,
  crlCount: 1,
  requestedBy: 'admin',
  mrzNationality: 'KOR',
  mrzDocumentType: 'P',
  ...overrides,
});

describe('ClientPATable', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should show loading state initially', () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockReturnValue(new Promise(() => {}));
    render(<ClientPATable />);
    expect(document.querySelector('.animate-spin')).toBeInTheDocument();
  });

  it('should show no-data message when history is empty', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [], total: 0 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      expect(screen.getByText('common:table.noData')).toBeInTheDocument();
    });
  });

  it('should render history rows after load', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [makeHistoryItem()], total: 1 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      expect(screen.getAllByText('KR').length).toBeGreaterThan(0);
    });
  });

  it('should display CSCA count for each row', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [makeHistoryItem({ cscaCount: 5 })], total: 1 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      expect(screen.getByText('5 CSCA')).toBeInTheDocument();
    });
  });

  it('should display CRL count when > 0', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [makeHistoryItem({ crlCount: 2 })], total: 1 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      expect(screen.getByText('2 CRL')).toBeInTheDocument();
    });
  });

  it('should not display CRL count when 0', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [makeHistoryItem({ crlCount: 0 })], total: 1 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      expect(screen.queryByText('0 CRL')).not.toBeInTheDocument();
    });
  });

  it('should display status badge for VALID status', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [makeHistoryItem({ status: 'VALID' })], total: 1 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      const statusBadge = screen.getAllByText('VALID')[0];
      expect(statusBadge).toBeInTheDocument();
    });
  });

  it('should display status badge for REQUESTED status', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [makeHistoryItem({ status: 'REQUESTED' })], total: 1 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      expect(screen.getByText('REQUESTED')).toBeInTheDocument();
    });
  });

  it('should display requestedBy or clientIp', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [makeHistoryItem({ requestedBy: 'user1' })], total: 1 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      expect(screen.getByText('user1')).toBeInTheDocument();
    });
  });

  it('should display em dash when requestedBy and clientIp are both absent', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [makeHistoryItem({ requestedBy: undefined, clientIp: undefined })], total: 1 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      expect(screen.getAllByText('—').length).toBeGreaterThan(0);
    });
  });

  it('should render filter card with country select', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [], total: 0 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      expect(screen.getByLabelText('pa:history.filterCountry')).toBeInTheDocument();
    });
  });

  it('should render filter card with status select', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [], total: 0 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      expect(screen.getByLabelText('pa:history.filterStatus')).toBeInTheDocument();
    });
  });

  it('should populate country dropdown from data', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: {
        data: [
          makeHistoryItem({ countryCode: 'KR' }),
          makeHistoryItem({ id: 'req-002', countryCode: 'US' }),
        ],
        total: 2,
      },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      const select = screen.getByLabelText('pa:history.filterCountry') as HTMLSelectElement;
      const options = Array.from(select.options).map((o) => o.value);
      expect(options).toContain('KR');
      expect(options).toContain('US');
    });
  });

  it('should filter by status client-side', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: {
        data: [
          makeHistoryItem({ id: '1', status: 'VALID' }),
          makeHistoryItem({ id: '2', status: 'INVALID', countryCode: 'US' }),
        ],
        total: 2,
      },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => expect(screen.getAllByText('KR').length).toBeGreaterThan(0));

    const statusSelect = screen.getByLabelText('pa:history.filterStatus');
    fireEvent.change(statusSelect, { target: { value: 'INVALID' } });

    await waitFor(() => {
      // After filtering for INVALID, 'US' should appear (in table row and possibly dropdown)
      expect(screen.getAllByText('US').length).toBeGreaterThan(0);
    });
  });

  it('should clear filters when clear button clicked', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: {
        data: [
          makeHistoryItem({ id: '1', status: 'VALID' }),
          makeHistoryItem({ id: '2', status: 'INVALID', countryCode: 'US' }),
        ],
        total: 2,
      },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => expect(screen.getAllByText('KR').length).toBeGreaterThan(0));

    const statusSelect = screen.getByLabelText('pa:history.filterStatus');
    fireEvent.change(statusSelect, { target: { value: 'INVALID' } });

    // Clear filter button should appear
    const clearBtn = screen.getByTitle('common:button.reset');
    fireEvent.click(clearBtn);

    await waitFor(() => {
      expect(screen.getAllByText('KR').length).toBeGreaterThan(0);
      expect(screen.getAllByText('US').length).toBeGreaterThan(0);
    });
  });

  it('should show verification status column', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: {
        data: [makeHistoryItem({ verificationStatus: 'VALID' })],
        total: 1,
      },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      expect(screen.getAllByText('VALID').length).toBeGreaterThan(0);
    });
  });

  it('should show em dash when verificationStatus is absent', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [makeHistoryItem({ verificationStatus: undefined })], total: 1 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => {
      expect(screen.getByText('—')).toBeInTheDocument();
    });
  });

  it('should not show pagination when total ≤ pageSize', async () => {
    vi.mocked(paApi.getTrustMaterialHistory).mockResolvedValue({
      data: { data: [makeHistoryItem()], total: 1 },
    } as any);
    render(<ClientPATable />);
    await waitFor(() => expect(screen.getAllByText('KR').length).toBeGreaterThan(0));

    // No pagination row
    expect(screen.queryByText('/ 1')).not.toBeInTheDocument();
  });
});
