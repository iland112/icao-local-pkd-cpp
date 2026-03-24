import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import { PADashboard } from '../PADashboard';

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

const mockGetStatistics = vi.fn();
const mockGetHistory = vi.fn();
vi.mock('@/services/paApi', () => ({
  paApi: {
    getStatistics: (...args: unknown[]) => mockGetStatistics(...args),
    getHistory: (...args: unknown[]) => mockGetHistory(...args),
    getCombinedStatistics: vi.fn().mockResolvedValue({
      data: { success: true, serverPA: { totalRequests: 0, validCount: 0, invalidCount: 0, resultReportedCount: 0 }, clientPA: { totalRequests: 0, validCount: 0, invalidCount: 0, resultReportedCount: 0 } },
    }),
  },
}));

vi.mock('@/stores/themeStore', () => ({
  useThemeStore: () => ({ darkMode: false }),
}));

// Mock Recharts
vi.mock('recharts', () => ({
  PieChart: () => <div data-testid="pie-chart" />,
  Pie: () => null,
  Cell: () => null,
  AreaChart: () => <div data-testid="area-chart" />,
  Area: () => null,
  XAxis: () => null,
  YAxis: () => null,
  CartesianGrid: () => null,
  Tooltip: () => null,
  Legend: () => null,
  ResponsiveContainer: ({ children }: { children: React.ReactNode }) => (
    <div data-testid="responsive-container">{children}</div>
  ),
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetStatistics.mockResolvedValue({
    data: {
      success: true,
      statistics: {
        total: 100,
        valid: 90,
        expiredValid: 5,
        invalid: 5,
        error: 0,
        byCountry: [],
        byDocumentType: [],
        verificationsByDay: [],
        avgProcessingTimeMs: 150,
      },
    },
  });
  mockGetHistory.mockResolvedValue({
    data: {
      success: true,
      history: [],
      pagination: { total: 0, limit: 5, offset: 0 },
    },
  });
});

describe('PADashboard page', () => {
  it('should render without crashing', () => {
    render(<PADashboard />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the page title', async () => {
    render(<PADashboard />);
    await waitFor(() => {
      expect(screen.getByText('pa:dashboard.paTitle')).toBeInTheDocument();
    });
  });

  it('should render the page subtitle', async () => {
    render(<PADashboard />);
    await waitFor(() => {
      expect(screen.getByText('pa:dashboard.paSubtitle')).toBeInTheDocument();
    });
  });

  it('should call getStatistics API on mount', async () => {
    render(<PADashboard />);
    await waitFor(() => {
      expect(mockGetStatistics).toHaveBeenCalled();
    });
  });

  it('should call getHistory API on mount', async () => {
    render(<PADashboard />);
    await waitFor(() => {
      expect(mockGetHistory).toHaveBeenCalled();
    });
  });
});
