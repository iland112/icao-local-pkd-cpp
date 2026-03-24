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

vi.mock('@/utils/cn', () => ({ cn: (...args: string[]) => args.filter(Boolean).join(' ') }));
vi.mock('@/utils/dateFormat', () => ({
  formatDateTime: (d: unknown) => (d ? String(d) : ''),
  formatTime: (d: unknown) => (d ? String(d) : ''),
}));

const mockGetSystemOverview = vi.fn();
const mockGetServicesHealth = vi.fn();
const mockGetLoadSnapshot = vi.fn();
const mockGetLoadHistory = vi.fn();

vi.mock('@/services/monitoringApi', () => ({
  monitoringServiceApi: {
    getSystemOverview: (...args: unknown[]) => mockGetSystemOverview(...args),
    getServicesHealth: (...args: unknown[]) => mockGetServicesHealth(...args),
    getLoadSnapshot: (...args: unknown[]) => mockGetLoadSnapshot(...args),
    getLoadHistory: (...args: unknown[]) => mockGetLoadHistory(...args),
  },
}));

vi.mock('@/services/pkdApi', () => ({
  healthApi: {
    checkDatabase: vi.fn().mockResolvedValue({ data: { status: 'UP', version: '15' } }),
    checkLdap: vi.fn().mockResolvedValue({ data: { status: 'UP', responseTime: 5 } }),
  },
}));

// Mock complex monitoring chart components
vi.mock('@/components/monitoring/ActiveConnectionsCard', () => ({
  default: () => <div data-testid="active-connections-card">ActiveConnectionsCard</div>,
}));
vi.mock('@/components/monitoring/RequestRateChart', () => ({
  default: () => <div data-testid="request-rate-chart">RequestRateChart</div>,
}));
vi.mock('@/components/monitoring/LatencyTrendChart', () => ({
  default: () => <div data-testid="latency-trend-chart">LatencyTrendChart</div>,
}));
vi.mock('@/components/monitoring/ConnectionPoolChart', () => ({
  default: () => <div data-testid="connection-pool-chart">ConnectionPoolChart</div>,
}));

const mockSystemOverview = {
  data: {
    cpu: { usagePercent: 10, load1min: 1, load5min: 1, load15min: 1 },
    memory: { totalMb: 8192, usedMb: 4096, freeMb: 4096, usagePercent: 50 },
    disk: { totalGb: 100, usedGb: 40, freeGb: 60, usagePercent: 40 },
    network: { bytesSent: 1000, bytesRecv: 2000 },
  },
};

beforeEach(() => {
  vi.clearAllMocks();
  mockGetSystemOverview.mockResolvedValue(mockSystemOverview);
  mockGetServicesHealth.mockResolvedValue({
    data: [
      { name: 'pkd-management', status: 'UP', responseTimeMs: 5 },
    ],
  });
  mockGetLoadSnapshot.mockResolvedValue({
    data: {
      services: [],
      nginx: { requestsPerSecond: 0, activeConnections: 0, uniqueUsers: 0 },
      uniqueUsers: 0,
    },
  });
  mockGetLoadHistory.mockResolvedValue({ data: { points: [] } });
});

describe('MonitoringDashboard page', () => {
  it('should render without crashing', async () => {
    const MonitoringDashboard = (await import('../MonitoringDashboard')).default;
    render(<MonitoringDashboard />);
    await waitFor(() => {
      expect(screen.queryByText('title')).not.toBeNull();
    });
  });

  it('should render the dashboard title', async () => {
    const MonitoringDashboard = (await import('../MonitoringDashboard')).default;
    render(<MonitoringDashboard />);
    await waitFor(() => {
      expect(screen.getByText('title')).toBeInTheDocument();
    });
  });

  it('should call getSystemOverview on mount', async () => {
    const MonitoringDashboard = (await import('../MonitoringDashboard')).default;
    render(<MonitoringDashboard />);
    await waitFor(() => {
      expect(mockGetSystemOverview).toHaveBeenCalled();
    });
  });

  it('should call getServicesHealth on mount', async () => {
    const MonitoringDashboard = (await import('../MonitoringDashboard')).default;
    render(<MonitoringDashboard />);
    await waitFor(() => {
      expect(mockGetServicesHealth).toHaveBeenCalled();
    });
  });

  it('should render CPU metric card after data loads', async () => {
    const MonitoringDashboard = (await import('../MonitoringDashboard')).default;
    render(<MonitoringDashboard />);
    await waitFor(() => {
      expect(screen.getByText('CPU')).toBeInTheDocument();
    }, { timeout: 3000 });
  });

  it('should show error state when API fails', async () => {
    mockGetSystemOverview.mockRejectedValue(new Error('Network error'));
    mockGetServicesHealth.mockRejectedValue(new Error('Network error'));
    const MonitoringDashboard = (await import('../MonitoringDashboard')).default;
    render(<MonitoringDashboard />);
    await waitFor(() => {
      expect(screen.getByText('monitoring:error.loadFailed')).toBeInTheDocument();
    });
  });
});
