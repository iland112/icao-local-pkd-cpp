import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor, fireEvent } from '@/test/test-utils';

vi.mock('@/utils/cn', () => ({ cn: (...args: string[]) => args.filter(Boolean).join(' ') }));

const mockGetEacStatistics = vi.fn();
const mockGetEacCountries = vi.fn();
const mockSearchEacCertificates = vi.fn();

vi.mock('../../api/eacApi', () => ({
  getEacStatistics: (...args: unknown[]) => mockGetEacStatistics(...args),
  getEacCountries: (...args: unknown[]) => mockGetEacCountries(...args),
  searchEacCertificates: (...args: unknown[]) => mockSearchEacCertificates(...args),
  getEacChain: vi.fn(),
  uploadCvc: vi.fn(),
  previewCvc: vi.fn(),
  deleteEacCertificate: vi.fn(),
}));

vi.mock('@/components/TreeViewer', () => ({
  TreeViewer: () => <div data-testid="tree-viewer">TreeViewer</div>,
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockGetEacStatistics.mockResolvedValue({
    data: {
      total: 0,
      byType: { CVCA: 0, DV_DOMESTIC: 0, DV_FOREIGN: 0, IS: 0 },
      chainValid: 0,
      chainInvalid: 0,
    },
  });
  mockGetEacCountries.mockResolvedValue({ data: [] });
  mockSearchEacCertificates.mockResolvedValue({
    data: { data: [], total: 0, totalPages: 0, page: 1, pageSize: 20 },
  });
});

describe('EacDashboard page', () => {
  it('should render without crashing', async () => {
    const EacDashboard = (await import('../EacDashboard')).default;
    render(<EacDashboard />);
    await waitFor(() => {
      expect(screen.queryByText('EAC 인증서 관리')).not.toBeNull();
    });
  });

  it('should render the page heading', async () => {
    const EacDashboard = (await import('../EacDashboard')).default;
    render(<EacDashboard />);
    await waitFor(() => {
      expect(screen.getByText('EAC 인증서 관리')).toBeInTheDocument();
    });
  });

  it('should call getEacStatistics on mount', async () => {
    const EacDashboard = (await import('../EacDashboard')).default;
    render(<EacDashboard />);
    await waitFor(() => {
      expect(mockGetEacStatistics).toHaveBeenCalled();
    });
  });

  it('should call searchEacCertificates on mount', async () => {
    const EacDashboard = (await import('../EacDashboard')).default;
    render(<EacDashboard />);
    await waitFor(() => {
      expect(mockSearchEacCertificates).toHaveBeenCalled();
    });
  });

  it('should render upload button', async () => {
    const EacDashboard = (await import('../EacDashboard')).default;
    render(<EacDashboard />);
    await waitFor(() => {
      expect(screen.getByText('CVC 업로드')).toBeInTheDocument();
    });
  });

  it('should show certificate list when data is returned', async () => {
    mockSearchEacCertificates.mockResolvedValue({
      data: {
        data: [
          {
            id: 'cvc-1',
            chr: 'KRCVCA00001',
            car: 'KRCVCA00001',
            cvcType: 'CVCA',
            countryCode: 'KR',
            role: 'CVCA',
            signatureAlgorithm: 'ECDSA',
            effectiveDate: '2026-01-01',
            expirationDate: '2028-01-01',
            signatureValid: true,
            createdAt: '2026-01-01T00:00:00Z',
          },
        ],
        total: 1,
        totalPages: 1,
        page: 1,
        pageSize: 20,
      },
    });
    const EacDashboard = (await import('../EacDashboard')).default;
    render(<EacDashboard />);
    // Switch to the list tab to see the certificate table
    await waitFor(() => {
      expect(screen.getByText('인증서 목록')).toBeInTheDocument();
    });
    fireEvent.click(screen.getByText('인증서 목록'));
    await waitFor(() => {
      expect(screen.getAllByText('KRCVCA00001').length).toBeGreaterThan(0);
    });
  });
});
