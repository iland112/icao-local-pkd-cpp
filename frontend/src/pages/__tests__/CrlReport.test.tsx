import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import CrlReport from '../CrlReport';

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

const mockGetCrlReport = vi.fn();
vi.mock('@/services/pkdApi', () => ({
  certificateApi: {
    getCrlReport: (...args: unknown[]) => mockGetCrlReport(...args),
    getCrlDetail: vi.fn().mockResolvedValue({ data: {} }),
    downloadCrl: vi.fn().mockResolvedValue({ data: new ArrayBuffer(0) }),
  },
  default: {
    get: vi.fn().mockResolvedValue({ data: {} }),
  },
}));

vi.mock('@/utils/csvExport', () => ({
  exportCrlReportToCsv: vi.fn(),
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
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetCrlReport.mockResolvedValue({
    data: {
      success: true,
      summary: {
        totalCrls: 69,
        countryCount: 50,
        validCount: 60,
        expiredCount: 9,
        totalRevokedCertificates: 170,
      },
      byCountry: [],
      bySignatureAlgorithm: [],
      byRevocationReason: [],
      crls: { total: 69, items: [] },
      pagination: { total: 69, limit: 20, offset: 0 },
    },
  });
});

describe('CrlReport page', () => {
  it('should render without crashing', () => {
    render(<CrlReport />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the page title', async () => {
    render(<CrlReport />);
    await waitFor(() => {
      expect(screen.getByText('report:crl.title')).toBeInTheDocument();
    });
  });

  it('should render the page subtitle', async () => {
    render(<CrlReport />);
    await waitFor(() => {
      expect(screen.getByText('report:crl.subtitle')).toBeInTheDocument();
    });
  });

  it('should show loading state initially', () => {
    mockGetCrlReport.mockReturnValue(new Promise(() => {}));
    render(<CrlReport />);
    expect(document.body).toBeInTheDocument();
  });

  it('should call getCrlReport on mount', async () => {
    render(<CrlReport />);
    await waitFor(() => {
      expect(mockGetCrlReport).toHaveBeenCalled();
    });
  });
});
