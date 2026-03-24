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
vi.mock('@/utils/dateFormat', () => ({ formatDate: (d: string) => d || '' }));
vi.mock('@/utils/countryCode', () => ({ getFlagSvgPath: () => '/flags/KR.svg' }));
vi.mock('i18n-iso-countries', () => ({
  default: { registerLocale: vi.fn(), getName: (code: string) => code },
  registerLocale: vi.fn(),
  getName: (code: string) => code,
}));
vi.mock('i18n-iso-countries/langs/ko.json', () => ({ default: {} }));

vi.mock('@/utils/csvExport', () => ({
  exportQualityReportToCsv: vi.fn(),
}));

vi.mock('@/config/pagination', () => ({ DEFAULT_PAGE_SIZE: 20 }));

vi.mock('@/hooks/useSortableTable', () => ({
  useSortableTable: (data: unknown[]) => ({
    sortedData: data,
    sortConfig: { key: null, direction: 'asc' },
    requestSort: vi.fn(),
  }),
}));

vi.mock('@/components/common/SortableHeader', () => ({
  SortableHeader: ({ children }: { children: React.ReactNode }) => <th>{children}</th>,
}));

const mockGetQualityReport = vi.fn();

vi.mock('@/services/pkdApi', () => ({
  certificateApi: {
    getQualityReport: (...args: unknown[]) => mockGetQualityReport(...args),
  },
  healthApi: {
    checkDatabase: vi.fn().mockResolvedValue({ data: { status: 'UP' } }),
  },
}));

const mockReportData = {
  data: {
    summary: { total: 100, compliantCount: 80, nonCompliantCount: 15, warningCount: 5 },
    byCategory: [
      { category: 'algorithm', failCount: 5 },
      { category: 'keySize', failCount: 10 },
    ],
    byCountry: [{ countryCode: 'KR', total: 50, compliant: 40, nonCompliant: 8, warning: 2 }],
    byCertType: [{ certType: 'CSCA', total: 30, compliant: 25, nonCompliant: 4, warning: 1 }],
    violations: [{ violation: 'RSA key size below minimum: 1024', count: 5 }],
    certificates: {
      total: 15,
      page: 1,
      size: 20,
      items: [],
    },
  },
};

beforeEach(() => {
  vi.clearAllMocks();
  mockGetQualityReport.mockResolvedValue(mockReportData);
});

describe('CertificateQualityReport page', () => {
  it('should render without crashing', async () => {
    const CertificateQualityReport = (await import('../CertificateQualityReport')).default;
    render(<CertificateQualityReport />);
    await waitFor(() => {
      expect(screen.queryByText('report:quality.title')).not.toBeNull();
    });
  });

  it('should render the page title', async () => {
    const CertificateQualityReport = (await import('../CertificateQualityReport')).default;
    render(<CertificateQualityReport />);
    await waitFor(() => {
      expect(screen.getByText('report:quality.title')).toBeInTheDocument();
    });
  });

  it('should call getQualityReport on mount', async () => {
    const CertificateQualityReport = (await import('../CertificateQualityReport')).default;
    render(<CertificateQualityReport />);
    await waitFor(() => {
      expect(mockGetQualityReport).toHaveBeenCalled();
    });
  });

  it('should render summary cards after data loads', async () => {
    const CertificateQualityReport = (await import('../CertificateQualityReport')).default;
    render(<CertificateQualityReport />);
    await waitFor(() => {
      expect(screen.getByText('report:quality.totalCerts')).toBeInTheDocument();
    });
  });

  it('should show loading spinner initially', async () => {
    // Keep promise unresolved to see loading
    mockGetQualityReport.mockReturnValue(new Promise(() => {}));
    const CertificateQualityReport = (await import('../CertificateQualityReport')).default;
    render(<CertificateQualityReport />);
    // Loading state shows a spinner — no text to match, just ensure page renders
    expect(screen.queryByText('report:quality.title')).toBeNull();
  });

  it('should show no-data state when data is empty', async () => {
    mockGetQualityReport.mockResolvedValue({
      data: {
        summary: { total: 0, compliantCount: 0, nonCompliantCount: 0, warningCount: 0 },
        byCategory: [],
        byCountry: [],
        byCertType: [],
        violations: [],
        certificates: { total: 0, page: 1, size: 20, items: [] },
      },
    });
    const CertificateQualityReport = (await import('../CertificateQualityReport')).default;
    render(<CertificateQualityReport />);
    await waitFor(() => {
      expect(screen.getByText('report:quality.noData')).toBeInTheDocument();
    });
  });
});
