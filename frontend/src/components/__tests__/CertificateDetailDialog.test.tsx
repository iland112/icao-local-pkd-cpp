import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import CertificateDetailDialog from '../CertificateDetailDialog';
import type { Certificate } from '../CertificateDetailDialog';

// Mock i18n
vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { language: 'ko' },
  }),
}));

// Mock certificateApi
vi.mock('@/services/pkdApi', () => ({
  certificateApi: {
    getDoc9303Checklist: vi.fn(),
  },
}));

// Mock heavy child components
vi.mock('@/components/TrustChainVisualization', () => ({
  TrustChainVisualization: () => <div data-testid="trust-chain-viz">TrustChainVisualization</div>,
}));

vi.mock('@/components/TreeViewer', () => ({
  TreeViewer: ({ data }: any) => (
    <div data-testid="tree-viewer">nodes:{data?.length ?? 0}</div>
  ),
}));

vi.mock('@/components/Doc9303ComplianceChecklist', () => ({
  Doc9303ComplianceChecklist: () => (
    <div data-testid="doc9303-checklist">Doc9303ComplianceChecklist</div>
  ),
}));

vi.mock('@/components/ai/ForensicAnalysisPanel', () => ({
  default: () => <div data-testid="forensic-panel">ForensicAnalysisPanel</div>,
}));

vi.mock('@/components/common', () => ({
  GlossaryTerm: ({ children }: any) => <span>{children}</span>,
}));

vi.mock('@/utils/certificateDisplayUtils', () => ({
  formatDate: (d: string) => d,
  formatVersion: (v: number) => `v${v}`,
  getActualCertType: (cert: any) => cert.type,
  isLinkCertificate: () => false,
  isMasterListSignerCertificate: () => false,
}));

import { certificateApi } from '@/services/pkdApi';

const makeCert = (overrides: Partial<Certificate> = {}): Certificate => ({
  dn: 'cn=test,c=KR',
  cn: 'Test Certificate',
  sn: 'AB:CD:EF',
  country: 'KR',
  type: 'DSC',
  subjectDn: 'CN=Test,C=KR',
  issuerDn: 'CN=CSCA,C=KR',
  fingerprint: 'abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890',
  validFrom: '2023-01-01',
  validTo: '2026-01-01',
  validity: 'VALID',
  isSelfSigned: false,
  signatureAlgorithm: 'SHA256withRSA',
  publicKeyAlgorithm: 'RSA',
  publicKeySize: 2048,
  ...overrides,
});

const makeProps = (overrides: Partial<any> = {}) => ({
  selectedCert: makeCert(),
  showDetailDialog: true,
  setShowDetailDialog: vi.fn(),
  detailTab: 'general' as const,
  setDetailTab: vi.fn(),
  validationResult: null,
  validationLoading: false,
  exportCertificate: vi.fn(),
  getCertTypeBadge: (type: string) => <span data-testid="cert-type-badge">{type}</span>,
  ...overrides,
});

describe('CertificateDetailDialog', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should render nothing when showDetailDialog is false', () => {
    const { container } = render(
      <CertificateDetailDialog {...makeProps({ showDetailDialog: false })} />
    );
    expect(container.firstChild).toBeNull();
  });

  it('should render dialog header when open', () => {
    render(<CertificateDetailDialog {...makeProps()} />);
    expect(screen.getByText('certificate:detail.certDetail')).toBeInTheDocument();
  });

  it('should show country and CN in header', () => {
    render(<CertificateDetailDialog {...makeProps()} />);
    expect(screen.getByText('KR - Test Certificate')).toBeInTheDocument();
  });

  it('should render certificate type badge', () => {
    render(<CertificateDetailDialog {...makeProps()} />);
    expect(screen.getByTestId('cert-type-badge')).toBeInTheDocument();
    expect(screen.getByText('DSC')).toBeInTheDocument();
  });

  it('should call setShowDetailDialog(false) when backdrop clicked', () => {
    const setShowDetailDialog = vi.fn();
    const { container } = render(
      <CertificateDetailDialog {...makeProps({ setShowDetailDialog })} />
    );
    const backdrop = container.querySelector('.absolute.inset-0');
    if (backdrop) fireEvent.click(backdrop);
    expect(setShowDetailDialog).toHaveBeenCalledWith(false);
  });

  it('should call setShowDetailDialog(false) when X button clicked', () => {
    const setShowDetailDialog = vi.fn();
    render(<CertificateDetailDialog {...makeProps({ setShowDetailDialog })} />);
    // The close button has aria-label = t('common:button.close') which our mock returns as key
    const closeBtn = screen.getByRole('button', { name: 'common:button.close' });
    fireEvent.click(closeBtn);
    expect(setShowDetailDialog).toHaveBeenCalledWith(false);
  });

  it('should render tab buttons: General, Details, Doc 9303, Forensic', () => {
    render(<CertificateDetailDialog {...makeProps()} />);
    expect(screen.getByText('General')).toBeInTheDocument();
    expect(screen.getByText('Details')).toBeInTheDocument();
    expect(screen.getByText('Doc 9303')).toBeInTheDocument();
    // Forensic tab uses i18n key
    expect(screen.getByText('certificate:detail.forensicTab')).toBeInTheDocument();
  });

  it('should call setDetailTab when tab clicked', () => {
    const setDetailTab = vi.fn();
    render(<CertificateDetailDialog {...makeProps({ setDetailTab })} />);
    fireEvent.click(screen.getByText('Details'));
    expect(setDetailTab).toHaveBeenCalledWith('details');
  });

  it('should show TreeViewer in details tab', () => {
    render(<CertificateDetailDialog {...makeProps({ detailTab: 'details' })} />);
    expect(screen.getByTestId('tree-viewer')).toBeInTheDocument();
  });

  it('should show validation result in general tab when available with trustChainPath', () => {
    const validationResult = {
      id: 'val-1',
      validationStatus: 'VALID',
      trustChainValid: true,
      trustChainMessage: 'Trust chain verified',
      trustChainPath: 'DSC → CSCA',
      countryCode: 'KR',
    } as any;
    // makeCert returns type 'DSC' so Trust Chain section is shown
    render(
      <CertificateDetailDialog
        {...makeProps({ detailTab: 'general', validationResult })}
      />
    );
    // TrustChainVisualization is rendered when trustChainPath is present
    expect(screen.getByTestId('trust-chain-viz')).toBeInTheDocument();
  });

  it('should show validation loading spinner when validationLoading', () => {
    render(
      <CertificateDetailDialog {...makeProps({ validationLoading: true })} />
    );
    expect(document.querySelector('.animate-spin')).toBeInTheDocument();
  });

  it('should show Doc9303 checklist in doc9303 tab after load', async () => {
    vi.mocked(certificateApi.getDoc9303Checklist).mockResolvedValue({
      data: {
        overallStatus: 'CONFORMANT',
        certificateType: 'DSC',
        passCount: 5,
        failCount: 0,
        warningCount: 0,
        naCount: 0,
        items: [],
      },
    } as any);

    render(<CertificateDetailDialog {...makeProps({ detailTab: 'doc9303' })} />);

    await waitFor(() => {
      expect(screen.getByTestId('doc9303-checklist')).toBeInTheDocument();
    });
    expect(certificateApi.getDoc9303Checklist).toHaveBeenCalledWith(
      'abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890'
    );
  });

  it('should show loading in doc9303 tab while fetching', () => {
    vi.mocked(certificateApi.getDoc9303Checklist).mockReturnValue(new Promise(() => {}));

    render(<CertificateDetailDialog {...makeProps({ detailTab: 'doc9303' })} />);

    expect(document.querySelector('.animate-spin')).toBeInTheDocument();
  });

  it('should show error message when doc9303 checklist load fails', async () => {
    vi.mocked(certificateApi.getDoc9303Checklist).mockRejectedValue({
      response: { data: { error: 'Not found' } },
    });

    render(<CertificateDetailDialog {...makeProps({ detailTab: 'doc9303' })} />);

    await waitFor(() => {
      expect(screen.getByText('Not found')).toBeInTheDocument();
    });
  });

  it('should show forensic panel in forensic tab', () => {
    render(<CertificateDetailDialog {...makeProps({ detailTab: 'forensic' })} />);
    expect(screen.getByTestId('forensic-panel')).toBeInTheDocument();
  });

  it('should render export certificate button in footer', () => {
    render(<CertificateDetailDialog {...makeProps()} />);
    expect(screen.getByText('certificate:detail.savingCert')).toBeInTheDocument();
  });

  it('should call exportCertificate with pem on export button click', () => {
    const exportCertificate = vi.fn();
    render(<CertificateDetailDialog {...makeProps({ exportCertificate })} />);
    fireEvent.click(screen.getByText('certificate:detail.savingCert'));
    expect(exportCertificate).toHaveBeenCalledWith(
      'cn=test,c=KR',
      'pem'
    );
  });

  it('should render footer close button', () => {
    render(<CertificateDetailDialog {...makeProps()} />);
    // The footer dismiss button uses t('icao:banner.dismiss')
    expect(screen.getByText('icao:banner.dismiss')).toBeInTheDocument();
  });

  it('should not fetch doc9303 checklist if tab is not doc9303', () => {
    render(<CertificateDetailDialog {...makeProps({ detailTab: 'general' })} />);
    expect(certificateApi.getDoc9303Checklist).not.toHaveBeenCalled();
  });

  it('should reset doc9303 checklist when certificate fingerprint changes', async () => {
    vi.mocked(certificateApi.getDoc9303Checklist).mockResolvedValue({
      data: { overallStatus: 'CONFORMANT', certificateType: 'DSC', passCount: 1, failCount: 0, warningCount: 0, naCount: 0, items: [] },
    } as any);

    const { rerender } = render(
      <CertificateDetailDialog {...makeProps({ detailTab: 'doc9303' })} />
    );

    await waitFor(() => screen.getByTestId('doc9303-checklist'));

    // Change certificate
    rerender(
      <CertificateDetailDialog
        {...makeProps({
          detailTab: 'doc9303',
          selectedCert: makeCert({ fingerprint: 'newfingerprint123' }),
        })}
      />
    );

    await waitFor(() => {
      expect(certificateApi.getDoc9303Checklist).toHaveBeenCalledTimes(2);
    });
  });
});
