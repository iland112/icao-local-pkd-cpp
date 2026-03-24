import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import { PAHistory } from '../PAHistory';

vi.mock('react-i18next', async () => {
  const actual = await vi.importActual<typeof import('react-i18next')>('react-i18next');
  return {
    ...actual,
    useTranslation: () => ({
      t: (key: string, opts?: { returnObjects?: boolean }) => {
        if (opts?.returnObjects) return [];
        return key;
      },
      i18n: { language: 'ko', changeLanguage: vi.fn() },
    }),
  };
});

vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual<typeof import('react-router-dom')>('react-router-dom');
  return {
    ...actual,
    useNavigate: () => vi.fn(),
    Link: ({ children, to }: { children: React.ReactNode; to: string }) => (
      <a href={to}>{children}</a>
    ),
  };
});

const mockGetHistory = vi.fn();
const mockGetStatistics = vi.fn();
vi.mock('@/services/paApi', () => ({
  paApi: {
    getHistory: (...args: unknown[]) => mockGetHistory(...args),
    getStatistics: (...args: unknown[]) => mockGetStatistics(...args),
    getCombinedStatistics: vi.fn().mockResolvedValue({ data: { success: true } }),
  },
}));

vi.mock('@/components/ClientPATable', () => ({
  ClientPATable: () => <div data-testid="client-pa-table" />,
}));

vi.mock('@/components/PADetailModal', () => ({
  PADetailModal: () => <div data-testid="pa-detail-modal" />,
}));

vi.mock('@/components/common/SortableHeader', () => ({
  SortableHeader: ({ children }: { children: React.ReactNode }) => <th>{children}</th>,
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetHistory.mockResolvedValue({
    data: {
      success: true,
      history: [],
      pagination: { total: 0, limit: 10, offset: 0, page: 0, totalPages: 0 },
    },
  });
  mockGetStatistics.mockResolvedValue({
    data: {
      success: true,
      statistics: {
        total: 0,
        valid: 0,
        expiredValid: 0,
        invalid: 0,
        error: 0,
      },
    },
  });
});

describe('PAHistory page', () => {
  it('should render without crashing', () => {
    render(<PAHistory />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the page title', async () => {
    render(<PAHistory />);
    await waitFor(() => {
      expect(screen.getAllByText('pa:history.title').length).toBeGreaterThan(0);
    });
  });

  it('should render the page subtitle', async () => {
    render(<PAHistory />);
    await waitFor(() => {
      expect(screen.getByText('pa:history.pageSubtitle')).toBeInTheDocument();
    });
  });

  it('should call getHistory API on mount', async () => {
    render(<PAHistory />);
    await waitFor(() => {
      expect(mockGetHistory).toHaveBeenCalled();
    });
  });

  it('should call getStatistics API on mount', async () => {
    render(<PAHistory />);
    await waitFor(() => {
      expect(mockGetStatistics).toHaveBeenCalled();
    });
  });
});
