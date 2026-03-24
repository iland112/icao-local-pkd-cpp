import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import { SyncDashboard } from '../SyncDashboard';

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

const mockGetStatus = vi.fn();
const mockGetHistory = vi.fn();
const mockGetConfig = vi.fn();
const mockGetRevalidationHistory = vi.fn();

vi.mock('@/services/api', () => ({
  syncServiceApi: {
    getStatus: (...args: unknown[]) => mockGetStatus(...args),
    getHistory: (...args: unknown[]) => mockGetHistory(...args),
    getConfig: (...args: unknown[]) => mockGetConfig(...args),
    getRevalidationHistory: (...args: unknown[]) => mockGetRevalidationHistory(...args),
    triggerCheck: vi.fn(),
    triggerReconcile: vi.fn(),
    triggerRevalidation: vi.fn(),
    triggerDailySync: vi.fn(),
    updateConfig: vi.fn(),
  },
}));

vi.mock('@/components/sync/ReconciliationHistory', () => ({
  ReconciliationHistory: () => <div data-testid="reconciliation-history" />,
}));

vi.mock('@/components/sync/RevalidationHistoryTable', () => ({
  RevalidationHistoryTable: () => <div data-testid="revalidation-history" />,
}));

vi.mock('@/components/sync/RevalidationResultDialog', () => ({
  RevalidationResultDialog: () => <div data-testid="revalidation-result-dialog" />,
}));

vi.mock('@/components/sync/SyncCheckResultDialog', () => ({
  SyncCheckResultDialog: () => <div data-testid="sync-check-result-dialog" />,
}));

vi.mock('@/components/sync/DailySyncResultDialog', () => ({
  DailySyncResultDialog: () => <div data-testid="daily-sync-result-dialog" />,
}));

vi.mock('@/components/sync/ConfigSaveResultDialog', () => ({
  ConfigSaveResultDialog: () => <div data-testid="config-save-result-dialog" />,
}));

vi.mock('@/components/sync/SyncConfigDialog', () => ({
  SyncConfigDialog: () => <div data-testid="sync-config-dialog" />,
}));

vi.mock('@/components/common/SortableHeader', () => ({
  SortableHeader: ({ children }: { children: React.ReactNode }) => <th>{children}</th>,
}));

vi.mock('@/components/common', () => ({
  GlossaryTerm: ({ children }: { children: React.ReactNode }) => <>{children}</>,
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetStatus.mockResolvedValue({
    data: {
      success: true,
      data: {
        lastCheckedAt: '2026-01-01T00:00:00Z',
        status: 'SYNCED',
        dbCertCount: 31212,
        ldapCertCount: 31212,
        discrepancyCount: 0,
        details: {},
      },
    },
  });
  mockGetHistory.mockResolvedValue({
    data: {
      success: true,
      data: [],
      pagination: { total: 0, limit: 10, offset: 0, count: 0 },
    },
  });
  mockGetConfig.mockResolvedValue({
    data: {
      success: true,
      autoReconcile: false,
      maxReconcileBatchSize: 100,
      dailySyncEnabled: false,
      dailySyncHour: 3,
      dailySyncMinute: 0,
      dailySyncTime: '03:00',
      revalidateCertsOnSync: false,
    },
  });
  mockGetRevalidationHistory.mockResolvedValue({
    data: {
      success: true,
      history: [],
      total: 0,
    },
  });
});

describe('SyncDashboard page', () => {
  it('should render without crashing', () => {
    render(<SyncDashboard />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the dashboard title', async () => {
    render(<SyncDashboard />);
    await waitFor(() => {
      expect(screen.getByText('dashboard.title')).toBeInTheDocument();
    });
  });

  it('should render the dashboard subtitle', async () => {
    render(<SyncDashboard />);
    await waitFor(() => {
      expect(screen.getByText('dashboard.subtitle')).toBeInTheDocument();
    });
  });

  it('should call getStatus API on mount', async () => {
    render(<SyncDashboard />);
    await waitFor(() => {
      expect(mockGetStatus).toHaveBeenCalled();
    });
  });

  it('should call getHistory API on mount', async () => {
    render(<SyncDashboard />);
    await waitFor(() => {
      expect(mockGetHistory).toHaveBeenCalled();
    });
  });

  it('should render reconciliation history component', async () => {
    render(<SyncDashboard />);
    await waitFor(() => {
      expect(screen.getByTestId('reconciliation-history')).toBeInTheDocument();
    });
  });
});
