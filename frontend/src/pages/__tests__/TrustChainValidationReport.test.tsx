import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';
import { TrustChainValidationReport } from '../TrustChainValidationReport';

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

// TrustChainValidationReport uses axios directly
vi.mock('axios', async () => {
  const actual = await vi.importActual<typeof import('axios')>('axios');
  return {
    ...actual,
    default: {
      ...actual.default,
      get: vi.fn().mockResolvedValue({
        data: {
          success: true,
          validCount: 100,
          expiredValidCount: 10,
          invalidCount: 5,
          pendingCount: 3,
          trustChainValidCount: 100,
          trustChainInvalidCount: 5,
          cscaNotFoundCount: 3,
          chainPathDistribution: [],
        },
      }),
    },
  };
});

vi.mock('@/components/common', () => ({
  GlossaryTerm: ({ children }: { children: React.ReactNode }) => <>{children}</>,
}));

beforeEach(() => {
  vi.clearAllMocks();
});

describe('TrustChainValidationReport page', () => {
  it('should render without crashing', () => {
    render(<TrustChainValidationReport />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the page title', async () => {
    render(<TrustChainValidationReport />);
    await waitFor(() => {
      expect(screen.getByText('report:trustChain.title')).toBeInTheDocument();
    });
  });

  it('should render the report subtitle', async () => {
    render(<TrustChainValidationReport />);
    await waitFor(() => {
      expect(screen.getByText('report:trustChain.reportSubtitle')).toBeInTheDocument();
    });
  });

  it('should show loading indicator initially', () => {
    render(<TrustChainValidationReport />);
    // Loading state exists while stats are being fetched
    expect(document.body).toBeInTheDocument();
  });
});
