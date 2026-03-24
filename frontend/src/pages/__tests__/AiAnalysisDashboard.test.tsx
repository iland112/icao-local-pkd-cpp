import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import AiAnalysisDashboard from '../AiAnalysisDashboard';

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

const mockGetStatistics = vi.fn();
const mockGetAnalysisStatus = vi.fn();

vi.mock('@/services/aiAnalysisApi', () => ({
  aiAnalysisApi: {
    getStatistics: (...args: unknown[]) => mockGetStatistics(...args),
    getAnalysisStatus: (...args: unknown[]) => mockGetAnalysisStatus(...args),
    getAnomalies: vi.fn().mockResolvedValue({ data: { items: [], total: 0 } }),
    getCountryMaturity: vi.fn().mockResolvedValue({ data: [] }),
    getAlgorithmTrends: vi.fn().mockResolvedValue({ data: [] }),
    getKeySizeDistribution: vi.fn().mockResolvedValue({ data: [] }),
    getRiskDistribution: vi.fn().mockResolvedValue({ data: [] }),
    getForensicSummary: vi.fn().mockResolvedValue({ data: null }),
    getIssuerProfiles: vi.fn().mockResolvedValue({ data: { success: true, profiles: [] } }),
    getExtensionAnomalies: vi.fn().mockResolvedValue({ data: { success: true, anomalies: [] } }),
    triggerAnalysis: vi.fn(),
  },
}));

vi.mock('@/stores/toastStore', () => ({
  toast: { success: vi.fn(), error: vi.fn(), info: vi.fn() },
}));

vi.mock('@/utils/csvExport', () => ({
  exportAiAnalysisReportToCsv: vi.fn(),
}));

vi.mock('@/components/ai/IssuerProfileCard', () => ({
  default: () => <div data-testid="issuer-profile-card" />,
}));

vi.mock('@/components/ai/ExtensionComplianceChecklist', () => ({
  default: () => <div data-testid="extension-compliance" />,
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
  AreaChart: () => <div data-testid="area-chart" />,
  Area: () => null,
  Legend: () => null,
  PieChart: () => <div data-testid="pie-chart" />,
  Pie: () => null,
  Cell: () => null,
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetStatistics.mockResolvedValue({
    data: {
      success: true,
      total_analyzed: 31212,
      normal_count: 30000,
      suspicious_count: 1000,
      anomalous_count: 212,
      risk_distribution: { LOW: 30000, MEDIUM: 1000, HIGH: 200, CRITICAL: 12 },
      avg_risk_score: 15.5,
      top_anomalous_countries: [],
      last_analysis_at: '2026-01-01T00:00:00Z',
      model_version: 'v1.0',
      forensic_level_distribution: null,
      avg_forensic_score: null,
    },
  });
  mockGetAnalysisStatus.mockResolvedValue({
    data: {
      success: true,
      status: 'IDLE',
      progress: 0,
      message: null,
      started_at: null,
      completed_at: null,
    },
  });
});

describe('AiAnalysisDashboard page', () => {
  it('should render without crashing', () => {
    render(<AiAnalysisDashboard />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the dashboard title', async () => {
    render(<AiAnalysisDashboard />);
    await waitFor(() => {
      expect(screen.getByText('dashboard.title')).toBeInTheDocument();
    });
  });

  it('should call getStatistics API on mount', async () => {
    render(<AiAnalysisDashboard />);
    await waitFor(() => {
      expect(mockGetStatistics).toHaveBeenCalled();
    });
  });

  it('should call getAnalysisStatus API on mount', async () => {
    render(<AiAnalysisDashboard />);
    await waitFor(() => {
      expect(mockGetAnalysisStatus).toHaveBeenCalled();
    });
  });

  it('should render issuer profile card', async () => {
    render(<AiAnalysisDashboard />);
    await waitFor(() => {
      expect(screen.getByTestId('issuer-profile-card')).toBeInTheDocument();
    });
  });

  it('should render extension compliance checklist', async () => {
    render(<AiAnalysisDashboard />);
    await waitFor(() => {
      expect(screen.getByTestId('extension-compliance')).toBeInTheDocument();
    });
  });
});
