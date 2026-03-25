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

// Mock EventSource (used for SSE in this component)
const mockEventSourceInstances: { close: ReturnType<typeof vi.fn>; addEventListener: ReturnType<typeof vi.fn>; onerror?: () => void }[] = [];
const MockEventSource = vi.fn().mockImplementation(() => {
  const instance = {
    close: vi.fn(),
    addEventListener: vi.fn(),
    onerror: undefined as (() => void) | undefined,
  };
  mockEventSourceInstances.push(instance);
  return instance;
});
vi.stubGlobal('EventSource', MockEventSource);

const mockGetIcaoLdapSyncStatus = vi.fn();
const mockGetIcaoLdapSyncHistory = vi.fn();
const mockTriggerIcaoLdapSync = vi.fn();
const mockTestIcaoLdapConnection = vi.fn();
const mockGetIcaoLdapConfig = vi.fn();
const mockUpdateIcaoLdapConfig = vi.fn();

vi.mock('@/services/api', () => ({
  uploadApi: {
    getStatistics: vi.fn().mockResolvedValue({ data: {} }),
  },
}));

vi.mock('@/services/pkdApi', () => ({
  uploadHistoryApi: {
    getIssues: vi.fn().mockResolvedValue({ data: { duplicates: [], totalDuplicates: 0 } }),
    getIcaoNonCompliant: vi.fn().mockResolvedValue({ data: { items: [], total: 0 } }),
  },
}));

vi.mock('@/components/IcaoViolationDetailDialog', () => ({
  IcaoViolationDetailDialog: () => null,
}));

vi.mock('@/components/DuplicateCertificatesTree', () => ({
  DuplicateCertificatesTree: () => null,
}));

vi.mock('@/services/relayApi', () => ({
  syncApi: {
    getIcaoLdapSyncStatus: (...args: unknown[]) => mockGetIcaoLdapSyncStatus(...args),
    getIcaoLdapSyncHistory: (...args: unknown[]) => mockGetIcaoLdapSyncHistory(...args),
    triggerIcaoLdapSync: (...args: unknown[]) => mockTriggerIcaoLdapSync(...args),
    testIcaoLdapConnection: (...args: unknown[]) => mockTestIcaoLdapConnection(...args),
    getIcaoLdapConfig: (...args: unknown[]) => mockGetIcaoLdapConfig(...args),
    updateIcaoLdapConfig: (...args: unknown[]) => mockUpdateIcaoLdapConfig(...args),
  },
}));

const mockStatus = {
  data: {
    enabled: false,
    running: false,
    host: 'icao-pkd-ldap',
    port: 389,
    syncIntervalMinutes: 60,
    lastSync: null,
  },
};

beforeEach(() => {
  vi.clearAllMocks();
  mockEventSourceInstances.length = 0;
  MockEventSource.mockImplementation(() => {
    const instance = {
      close: vi.fn(),
      addEventListener: vi.fn(),
      onerror: undefined as (() => void) | undefined,
    };
    mockEventSourceInstances.push(instance);
    return instance;
  });
  mockGetIcaoLdapSyncStatus.mockResolvedValue(mockStatus);
  mockGetIcaoLdapSyncHistory.mockResolvedValue({
    data: { data: [], total: 0 },
  });
  mockGetIcaoLdapConfig.mockResolvedValue({
    data: { enabled: false, syncIntervalMinutes: 60 },
  });
});

describe('IcaoLdapSync page', () => {
  it('should render without crashing', async () => {
    const IcaoLdapSync = (await import('../IcaoLdapSync')).default;
    render(<IcaoLdapSync />);
    await waitFor(() => {
      expect(screen.queryByText('sync:icaoLdap.pageTitle')).not.toBeNull();
    });
  });

  it('should render the page title', async () => {
    const IcaoLdapSync = (await import('../IcaoLdapSync')).default;
    render(<IcaoLdapSync />);
    await waitFor(() => {
      expect(screen.getByText('sync:icaoLdap.pageTitle')).toBeInTheDocument();
    });
  });

  it('should call getIcaoLdapSyncStatus on mount', async () => {
    const IcaoLdapSync = (await import('../IcaoLdapSync')).default;
    render(<IcaoLdapSync />);
    await waitFor(() => {
      expect(mockGetIcaoLdapSyncStatus).toHaveBeenCalled();
    });
  });

  it('should call getIcaoLdapSyncHistory on mount', async () => {
    const IcaoLdapSync = (await import('../IcaoLdapSync')).default;
    render(<IcaoLdapSync />);
    await waitFor(() => {
      expect(mockGetIcaoLdapSyncHistory).toHaveBeenCalled();
    });
  });

  it('should render the sync trigger button', async () => {
    const IcaoLdapSync = (await import('../IcaoLdapSync')).default;
    render(<IcaoLdapSync />);
    await waitFor(() => {
      expect(screen.getByText('sync:icaoLdap.manualSync')).toBeInTheDocument();
    });
  });

  it('should render KPI cards', async () => {
    const IcaoLdapSync = (await import('../IcaoLdapSync')).default;
    render(<IcaoLdapSync />);
    await waitFor(() => {
      expect(screen.getByText('sync:icaoLdap.connectionStatus')).toBeInTheDocument();
    });
  });

  it('should render test connection button', async () => {
    const IcaoLdapSync = (await import('../IcaoLdapSync')).default;
    render(<IcaoLdapSync />);
    await waitFor(() => {
      expect(screen.getByText('sync:icaoLdap.connectionTest')).toBeInTheDocument();
    });
  });
});
