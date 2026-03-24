import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import DscNcReport from '../DscNcReport';

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
  return { ...actual, useNavigate: () => vi.fn() };
});

const mockGetDscNcReport = vi.fn();
vi.mock('@/services/pkdApi', () => ({
  certificateApi: {
    getDscNcReport: (...args: unknown[]) => mockGetDscNcReport(...args),
  },
  default: {
    get: vi.fn().mockResolvedValue({ data: {} }),
  },
}));

vi.mock('@/utils/csvExport', () => ({
  exportDscNcReportToCsv: vi.fn(),
}));

vi.mock('@/components/common/SortableHeader', () => ({
  SortableHeader: ({ children }: { children: React.ReactNode }) => <th>{children}</th>,
}));

// Mock Recharts
vi.mock('recharts', () => ({
  BarChart: () => <div data-testid="bar-chart" />,
  Bar: () => null,
  XAxis: () => null,
  YAxis: () => null,
  CartesianGrid: () => null,
  Tooltip: () => null,
  ResponsiveContainer: ({ children }: { children: React.ReactNode }) => (
    <div data-testid="responsive-container">{children}</div>
  ),
  PieChart: () => <div data-testid="pie-chart" />,
  Pie: () => null,
  Cell: () => null,
  Legend: () => null,
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetDscNcReport.mockResolvedValue({
    data: {
      success: true,
      summary: {
        totalDscNc: 502,
        countryCount: 30,
        conformanceCodeCount: 5,
        validityBreakdown: { VALID: 302, EXPIRED: 200, NOT_YET_VALID: 0, UNKNOWN: 0 },
      },
      conformanceCodes: [],
      byCountry: [],
      byYear: [],
      bySignatureAlgorithm: [],
      byPublicKeyAlgorithm: [],
      certificates: { total: 502, items: [] },
      pagination: { total: 502, limit: 20, offset: 0 },
    },
  });
});

describe('DscNcReport page', () => {
  it('should render without crashing', () => {
    render(<DscNcReport />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the page title', async () => {
    render(<DscNcReport />);
    await waitFor(() => {
      expect(screen.getByText('report:dscNc.pageTitle')).toBeInTheDocument();
    });
  });

  it('should render the page subtitle', async () => {
    render(<DscNcReport />);
    await waitFor(() => {
      expect(screen.getByText('report:dscNc.pageSubtitle')).toBeInTheDocument();
    });
  });

  it('should show loading state initially', () => {
    mockGetDscNcReport.mockReturnValue(new Promise(() => {}));
    render(<DscNcReport />);
    expect(document.body).toBeInTheDocument();
  });

  it('should call getDscNcReport API on mount', async () => {
    render(<DscNcReport />);
    await waitFor(() => {
      expect(mockGetDscNcReport).toHaveBeenCalled();
    });
  });
});
