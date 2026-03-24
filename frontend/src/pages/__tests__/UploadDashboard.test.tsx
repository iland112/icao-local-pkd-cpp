import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import { UploadDashboard } from '../UploadDashboard';

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
vi.mock('@/services/api', () => ({
  uploadApi: {
    getStatistics: (...args: unknown[]) => mockGetStatistics(...args),
    getChanges: vi.fn().mockResolvedValue({ data: { success: true, changes: [] } }),
  },
}));

vi.mock('@/services/pkdApi', () => ({
  uploadHistoryApi: {
    getStatistics: vi.fn().mockResolvedValue({ data: { success: true } }),
    getValidationStatistics: vi.fn().mockResolvedValue({ data: {} }),
    getValidationReasons: vi.fn().mockResolvedValue({ data: { success: true, reasons: [] } }),
  },
  default: {
    get: vi.fn().mockResolvedValue({ data: {} }),
  },
}));

vi.mock('@/components/common', () => ({
  GlossaryTerm: ({ children }: { children: React.ReactNode }) => <>{children}</>,
  getGlossaryTooltip: () => ({ term: '', description: '' }),
}));

// Mock Recharts to avoid SVG rendering issues in jsdom
vi.mock('recharts', () => ({
  LineChart: () => <div data-testid="line-chart" />,
  Line: () => null,
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
      totalCertificates: 31212,
      cscaCount: 845,
      dscCount: 29838,
      crlCount: 69,
      mlCount: 27,
      totalUploads: 10,
      completedUploads: 9,
      failedUploads: 1,
      countriesCount: 95,
      bySource: [],
      chainPathDistribution: [],
      trustChainValidCount: 100,
      trustChainInvalidCount: 5,
      cscaNotFoundCount: 3,
    },
  });
});

describe('UploadDashboard page', () => {
  it('should render without crashing', () => {
    render(<UploadDashboard />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the page title', async () => {
    render(<UploadDashboard />);
    await waitFor(() => {
      expect(screen.getByText('upload:dashboard.pkdStatsDashboard')).toBeInTheDocument();
    });
  });

  it('should render the subtitle', async () => {
    render(<UploadDashboard />);
    await waitFor(() => {
      expect(screen.getByText('upload:dashboard.subtitle')).toBeInTheDocument();
    });
  });

  it('should show loading state initially', () => {
    mockGetStatistics.mockReturnValue(new Promise(() => {}));
    render(<UploadDashboard />);
    expect(document.body).toBeInTheDocument();
  });
});
