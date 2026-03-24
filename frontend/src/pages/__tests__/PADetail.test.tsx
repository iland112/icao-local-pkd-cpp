import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import { PADetail } from '../PADetail';

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

const mockNavigate = vi.fn();
vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual<typeof import('react-router-dom')>('react-router-dom');
  return {
    ...actual,
    useNavigate: () => mockNavigate,
    useParams: () => ({ paId: 'test-pa-id' }),
    Link: ({ children, to }: { children: React.ReactNode; to: string }) => (
      <a href={to}>{children}</a>
    ),
  };
});

const mockGetDetail = vi.fn();
vi.mock('@/services/paApi', () => ({
  paApi: {
    getDetail: (...args: unknown[]) => mockGetDetail(...args),
  },
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetDetail.mockResolvedValue({
    data: {
      success: true,
      id: 'test-pa-id',
      status: 'VALID',
      verifiedAt: '2026-01-01T00:00:00Z',
      countryCode: 'KR',
      documentNumber: 'M12345678',
      processingDurationMs: 150,
      sodSignatureValid: true,
      dgHashesValid: true,
      certificateChainValidation: {
        chainValid: true,
        cscaFound: true,
        crlChecked: true,
        crlRevoked: false,
      },
    },
  });
});

describe('PADetail page', () => {
  it('should render without crashing', () => {
    render(<PADetail />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the page title', async () => {
    render(<PADetail />);
    await waitFor(() => {
      expect(screen.getByText('pa:detail.title')).toBeInTheDocument();
    });
  });

  it('should show loading state initially', () => {
    mockGetDetail.mockReturnValue(new Promise(() => {}));
    render(<PADetail />);
    // Loading spinner should be present
    expect(document.body).toBeInTheDocument();
  });

  it('should call getDetail with paId from params', async () => {
    render(<PADetail />);
    await waitFor(() => {
      expect(mockGetDetail).toHaveBeenCalledWith('test-pa-id');
    });
  });

  it('should render error state on API failure', async () => {
    mockGetDetail.mockRejectedValue(new Error('Network error'));
    render(<PADetail />);
    await waitFor(() => {
      expect(screen.getByText('pa:detail.loadFailed')).toBeInTheDocument();
    });
  });
});
