import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@/test/test-utils';
import CertificateUpload from '../CertificateUpload';

// Mock react-i18next
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

// Mock react-router-dom
const mockNavigate = vi.fn();
vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual<typeof import('react-router-dom')>('react-router-dom');
  return { ...actual, useNavigate: () => mockNavigate };
});

// Mock API
vi.mock('@/services/api', () => ({
  uploadApi: {
    previewCertificate: vi.fn(),
    uploadCertificate: vi.fn(),
    getStatistics: vi.fn().mockResolvedValue({ data: { success: true, totalCertificates: 0 } }),
  },
}));

// Mock heavy child components
vi.mock('@/components/TreeViewer', () => ({
  TreeViewer: () => <div data-testid="tree-viewer" />,
}));
vi.mock('@/components/Doc9303ComplianceChecklist', () => ({
  Doc9303ComplianceChecklist: () => <div data-testid="doc9303-checklist" />,
}));
vi.mock('@/components/common/Dialog', () => ({
  Dialog: ({ children, open }: { children: React.ReactNode; open: boolean }) =>
    open ? <div data-testid="dialog">{children}</div> : null,
}));
vi.mock('@/components/common', () => ({
  GlossaryTerm: ({ children }: { children: React.ReactNode }) => <>{children}</>,
}));

import { uploadApi } from '@/services/api';

beforeEach(() => {
  vi.clearAllMocks();
  vi.mocked(uploadApi.getStatistics).mockResolvedValue({ data: { success: true, totalCertificates: 0 } } as any);
});

describe('CertificateUpload page', () => {
  it('should render without crashing', () => {
    render(<CertificateUpload />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the page title', () => {
    render(<CertificateUpload />);
    expect(screen.getByText('upload:certUpload.title')).toBeInTheDocument();
  });

  it('should render the page subtitle', () => {
    render(<CertificateUpload />);
    expect(screen.getByText('upload:certUpload.subtitle')).toBeInTheDocument();
  });

  it('should render a file input accepting certificate formats', () => {
    render(<CertificateUpload />);
    const input = document.querySelector('input[type="file"]');
    expect(input).toBeInTheDocument();
    const accept = input?.getAttribute('accept') || '';
    // Must accept certificate extensions
    expect(accept).toMatch(/\.(pem|der|crt|p7b|crl)/i);
  });

  it('should be in IDLE state initially (no preview result shown)', () => {
    render(<CertificateUpload />);
    // In IDLE state, no preview tab buttons are shown
    expect(screen.queryByTestId('tree-viewer')).not.toBeInTheDocument();
  });
});
