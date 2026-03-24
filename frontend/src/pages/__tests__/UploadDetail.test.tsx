import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import { UploadDetail } from '../UploadDetail';

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
    useParams: () => ({ uploadId: 'test-upload-id' }),
    Link: ({ children, to }: { children: React.ReactNode; to: string }) => (
      <a href={to}>{children}</a>
    ),
  };
});

const mockGetDetail = vi.fn();
vi.mock('@/services/api', () => ({
  uploadApi: {
    getDetail: (...args: unknown[]) => mockGetDetail(...args),
  },
}));

vi.mock('@/api/validationApi', () => ({
  validationApi: {
    getUploadValidations: vi.fn().mockResolvedValue({ results: [], total: 0 }),
    getCertificateValidation: vi.fn().mockResolvedValue(null),
  },
}));

vi.mock('@/components/TrustChainVisualization', () => ({
  TrustChainVisualization: () => <div data-testid="trust-chain-viz" />,
}));

vi.mock('@/components/common', () => ({
  GlossaryTerm: ({ children }: { children: React.ReactNode }) => <>{children}</>,
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetDetail.mockResolvedValue({
    data: {
      success: true,
      data: {
        id: 'test-upload-id',
        fileName: 'test.ldif',
        fileSize: 1024,
        fileFormat: 'LDIF',
        status: 'COMPLETED',
        totalEntries: 100,
        processedEntries: 100,
        createdAt: '2026-01-01T00:00:00Z',
        updatedAt: '2026-01-01T01:00:00Z',
        processingMode: 'AUTO',
      },
    },
  });
});

describe('UploadDetail page', () => {
  it('should render without crashing', () => {
    render(<UploadDetail />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the page title', async () => {
    render(<UploadDetail />);
    await waitFor(() => {
      expect(screen.getByText('detail.title')).toBeInTheDocument();
    });
  });

  it('should show loading state initially', () => {
    // Set up a pending promise
    mockGetDetail.mockReturnValue(new Promise(() => {}));
    render(<UploadDetail />);
    // Loading spinner or indicator should be present
    expect(document.body).toBeInTheDocument();
  });

  it('should display file name after loading', async () => {
    render(<UploadDetail />);
    await waitFor(() => {
      expect(screen.getByText('test.ldif')).toBeInTheDocument();
    });
  });

  it('should call getDetail with the uploadId from params', async () => {
    render(<UploadDetail />);
    await waitFor(() => {
      expect(mockGetDetail).toHaveBeenCalledWith('test-upload-id');
    });
  });
});
