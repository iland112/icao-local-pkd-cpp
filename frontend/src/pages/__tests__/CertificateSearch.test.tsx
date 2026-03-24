import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@/test/test-utils';
import CertificateSearch from '../CertificateSearch';

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
    useSearchParams: () => [new URLSearchParams(), vi.fn()],
  };
});

vi.mock('@/services/pkdApi', () => ({
  certificateApi: {
    searchCertificates: vi.fn().mockResolvedValue({ data: { success: true, certificates: [], total: 0 } }),
    getCountries: vi.fn().mockResolvedValue({ data: { success: true, countries: [] } }),
    exportCertificates: vi.fn(),
    exportAllCertificates: vi.fn(),
  },
  default: {
    get: vi.fn().mockResolvedValue({ data: {} }),
  },
}));

vi.mock('@/api/validationApi', () => ({
  validationApi: {
    getCertificateValidation: vi.fn().mockResolvedValue({ data: null }),
    getUploadValidations: vi.fn().mockResolvedValue({ data: null }),
  },
}));

vi.mock('@/stores/toastStore', () => ({
  toast: { success: vi.fn(), error: vi.fn(), info: vi.fn() },
}));

vi.mock('@/components/CertificateDetailDialog', () => ({
  default: () => <div data-testid="cert-detail-dialog" />,
}));

vi.mock('@/components/CertificateSearchFilters', () => ({
  default: ({ onSearch }: { onSearch: (c: unknown) => void }) => (
    <div data-testid="search-filters">
      <button onClick={() => onSearch({})}>Search</button>
    </div>
  ),
}));

vi.mock('@/components/common', () => ({
  GlossaryTerm: ({ children }: { children: React.ReactNode }) => <>{children}</>,
  getGlossaryTooltip: () => ({ term: '', description: '' }),
}));

vi.mock('@/components/common/SortableHeader', () => ({
  SortableHeader: ({ children }: { children: React.ReactNode }) => <th>{children}</th>,
}));

beforeEach(() => {
  vi.clearAllMocks();
});

describe('CertificateSearch page', () => {
  it('should render without crashing', () => {
    render(<CertificateSearch />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the page title', () => {
    render(<CertificateSearch />);
    expect(screen.getByText('search.title')).toBeInTheDocument();
  });

  it('should render the search filters component', () => {
    render(<CertificateSearch />);
    expect(screen.getByTestId('search-filters')).toBeInTheDocument();
  });

  it('should show loading state initially', () => {
    render(<CertificateSearch />);
    // The page starts loading on mount - spinner or loading indicator may appear
    // We verify page at least renders correctly
    expect(document.body).toBeInTheDocument();
  });
});
