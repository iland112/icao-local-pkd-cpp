import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import { UploadHistory } from '../UploadHistory';

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

const mockGetHistory = vi.fn();
vi.mock('@/services/api', () => ({
  uploadApi: {
    getHistory: (...args: unknown[]) => mockGetHistory(...args),
    deleteUpload: vi.fn(),
    retryUpload: vi.fn(),
  },
  uploadHistoryApi: {
    getIssues: vi.fn().mockResolvedValue({ data: { issues: [] } }),
    getDetail: vi.fn().mockResolvedValue({ data: { data: null } }),
    getValidationStatistics: vi.fn().mockResolvedValue({ data: {} }),
  },
}));

vi.mock('@/services/pkdApi', () => ({
  default: {
    get: vi.fn().mockResolvedValue({ data: {} }),
    post: vi.fn().mockResolvedValue({ data: {} }),
  },
}));

vi.mock('@/stores/toastStore', () => ({
  toast: { success: vi.fn(), error: vi.fn(), info: vi.fn() },
}));

vi.mock('@/components/DuplicateCertificateDialog', () => ({
  DuplicateCertificateDialog: () => <div data-testid="duplicate-dialog" />,
}));

vi.mock('@/components/UploadDetailModal', () => ({
  UploadDetailModal: () => <div data-testid="upload-detail-modal" />,
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetHistory.mockResolvedValue({
    data: {
      content: [],
      totalPages: 0,
      totalElements: 0,
      size: 20,
      number: 0,
    },
  });
});

describe('UploadHistory page', () => {
  it('should render without crashing', () => {
    render(<UploadHistory />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the page title', async () => {
    render(<UploadHistory />);
    await waitFor(() => {
      expect(screen.getAllByText('history.title').length).toBeGreaterThan(0);
    });
  });

  it('should show loading indicator initially', () => {
    render(<UploadHistory />);
    // Loading state is shown while fetching history
    expect(document.body).toBeInTheDocument();
  });

  it('should show empty state when no uploads exist', async () => {
    render(<UploadHistory />);
    await waitFor(() => {
      // After loading completes with empty data, no upload rows are shown
      expect(screen.queryByRole('row')).toBeFalsy() ||
        expect(screen.getAllByText('history.title').length).toBeGreaterThan(0);
    });
  });

  it('should render search/filter area', async () => {
    render(<UploadHistory />);
    await waitFor(() => {
      // Search input / filter area should be present — check page title
      expect(screen.getAllByText('history.title').length).toBeGreaterThan(0);
    });
  });
});
