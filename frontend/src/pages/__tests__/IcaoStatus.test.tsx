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
  formatDateTime: (d: string) => d || '',
  formatTime: (d: string) => d || '',
}));

const mockGetStatus = vi.fn();
const mockGetHistory = vi.fn();
const mockCheckUpdates = vi.fn();

vi.mock('@/services/pkdApi', () => ({
  icaoApi: {
    getStatus: (...args: unknown[]) => mockGetStatus(...args),
    getHistory: (...args: unknown[]) => mockGetHistory(...args),
    checkUpdates: (...args: unknown[]) => mockCheckUpdates(...args),
  },
  healthApi: {
    checkDatabase: vi.fn().mockResolvedValue({ data: { status: 'UP' } }),
  },
}));

vi.mock('@/components/common/Dialog', () => ({
  Dialog: ({ children, isOpen }: { children: React.ReactNode; isOpen: boolean }) =>
    isOpen ? <div data-testid="dialog">{children}</div> : null,
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetStatus.mockResolvedValue({
    data: {
      success: true,
      count: 0,
      status: [],
      any_needs_update: false,
      last_checked_at: null,
    },
  });
  mockGetHistory.mockResolvedValue({
    data: {
      success: true,
      versions: [],
      count: 0,
    },
  });
  mockCheckUpdates.mockResolvedValue({
    data: {
      success: true,
      message: 'No new versions',
      new_version_count: 0,
      new_versions: [],
    },
  });
});

describe('IcaoStatus page', () => {
  it('should render without crashing', async () => {
    const IcaoStatus = (await import('../IcaoStatus')).default;
    render(<IcaoStatus />);
    await waitFor(() => {
      expect(screen.queryByText('icao:statusPage.pageTitle')).not.toBeNull();
    });
  });

  it('should call getStatus and getHistory on mount', async () => {
    const IcaoStatus = (await import('../IcaoStatus')).default;
    render(<IcaoStatus />);
    await waitFor(() => {
      expect(mockGetStatus).toHaveBeenCalled();
      expect(mockGetHistory).toHaveBeenCalled();
    });
  });

  it('should render the check updates button', async () => {
    const IcaoStatus = (await import('../IcaoStatus')).default;
    render(<IcaoStatus />);
    await waitFor(() => {
      expect(screen.getByText('icao:checkUpdates')).toBeInTheDocument();
    });
  });

  it('should render page title', async () => {
    const IcaoStatus = (await import('../IcaoStatus')).default;
    render(<IcaoStatus />);
    await waitFor(() => {
      expect(screen.getByText('icao:statusPage.pageTitle')).toBeInTheDocument();
    });
  });

  it('should show version status cards when status data is available', async () => {
    mockGetStatus.mockResolvedValue({
      data: {
        success: true,
        count: 1,
        status: [
          {
            collection_type: 'MASTER_LIST',
            detected_version: 100,
            uploaded_version: 99,
            upload_timestamp: '2026-01-01T00:00:00Z',
            version_diff: 1,
            needs_update: true,
            status: 'NEEDS_UPDATE',
            status_message: 'New version available',
          },
        ],
        any_needs_update: true,
        last_checked_at: '2026-01-01T00:00:00Z',
      },
    });
    const IcaoStatus = (await import('../IcaoStatus')).default;
    render(<IcaoStatus />);
    await waitFor(() => {
      expect(screen.getByText('icao:statusPage.collectionFull.MASTER_LIST')).toBeInTheDocument();
    });
  });
});
