import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@/test/test-utils';

vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual<typeof import('react-router-dom')>('react-router-dom');
  return { ...actual, useNavigate: () => vi.fn() };
});

const mockList = vi.fn();
vi.mock('@/services/csrApi', () => ({
  csrApiService: {
    list: (...args: unknown[]) => mockList(...args),
    generate: vi.fn(),
    import: vi.fn(),
    getById: vi.fn(),
    exportPem: vi.fn(),
    registerCertificate: vi.fn(),
    deleteById: vi.fn(),
    signWithCA: vi.fn(),
  },
}));

vi.mock('@/stores/toastStore', () => ({
  toast: { success: vi.fn(), error: vi.fn(), warning: vi.fn(), info: vi.fn() },
}));

vi.mock('@/components/common', () => ({
  GlossaryTerm: ({ label }: { label: string }) => <span>{label}</span>,
}));

beforeEach(() => {
  vi.clearAllMocks();
  mockList.mockResolvedValue({
    data: { success: true, data: [], total: 0 },
  });
});

describe('CsrManagement page', () => {
  it('should render without crashing', async () => {
    const CsrManagement = (await import('../CsrManagement')).default;
    render(<CsrManagement />);
    await waitFor(() => {
      expect(screen.queryByText('CSR 관리')).not.toBeNull();
    });
  });

  it('should render page heading', async () => {
    const CsrManagement = (await import('../CsrManagement')).default;
    render(<CsrManagement />);
    await waitFor(() => {
      expect(screen.getByText('CSR 관리')).toBeInTheDocument();
    });
  });

  it('should call list API on mount', async () => {
    const CsrManagement = (await import('../CsrManagement')).default;
    render(<CsrManagement />);
    await waitFor(() => {
      expect(mockList).toHaveBeenCalled();
    });
  });

  it('should render generate CSR button', async () => {
    const CsrManagement = (await import('../CsrManagement')).default;
    render(<CsrManagement />);
    await waitFor(() => {
      // CSR 생성 button
      expect(screen.getByText('CSR 생성')).toBeInTheDocument();
    });
  });

  it('should show empty list message when no CSRs', async () => {
    const CsrManagement = (await import('../CsrManagement')).default;
    render(<CsrManagement />);
    await waitFor(() => {
      expect(screen.getByText('생성된 CSR이 없습니다')).toBeInTheDocument();
    });
  });

  it('should display CSR records when returned from API', async () => {
    mockList.mockResolvedValue({
      data: {
        success: true,
        data: [
          {
            id: 'csr-1',
            subject_dn: 'CN=Test,C=KR',
            countryCode: 'KR',
            organization: 'Test Org',
            commonName: 'Test CN',
            status: 'CREATED',
            csrPem: '-----BEGIN CERTIFICATE REQUEST-----',
            createdAt: '2026-01-01T00:00:00Z',
            createdBy: 'admin',
          },
        ],
        total: 1,
      },
    });
    const CsrManagement = (await import('../CsrManagement')).default;
    render(<CsrManagement />);
    await waitFor(() => {
      expect(screen.getByText('CN=Test,C=KR')).toBeInTheDocument();
    });
  });
});
