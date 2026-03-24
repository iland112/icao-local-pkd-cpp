import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { CurrentCertificateCard } from '../CurrentCertificateCard';
import type { CertificateMetadata, IcaoComplianceStatus } from '@/types';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => {
      const map: Record<string, string> = {
        'common:status.expired': '만료',
      };
      return map[key] ?? key;
    },
    i18n: { language: 'ko' },
  }),
}));

vi.mock('@/utils/dateFormat', () => ({
  formatDate: (d: string) => d,
  formatDateTime: (d: string) => d,
}));

// GlossaryTerm renders label ?? term (matching the actual component behavior)
vi.mock('@/components/common', () => ({
  GlossaryTerm: ({ term, label }: { term?: string; label?: string }) => (
    <span>{label ?? term}</span>
  ),
}));

// IcaoComplianceBadge is a dependency — render a simple stub
vi.mock('../IcaoComplianceBadge', () => ({
  IcaoComplianceBadge: ({ compliance }: { compliance: IcaoComplianceStatus }) => (
    <span data-testid="compliance-badge">{compliance.complianceLevel}</span>
  ),
}));

const baseCert: CertificateMetadata = {
  subjectDn: 'CN=Test DSC,C=KR',
  issuerDn: 'CN=Test CSCA,C=KR',
  serialNumber: '0123456789ABCDEF',
  countryCode: 'KR',
  certificateType: 'DSC',
  isSelfSigned: false,
  isLinkCertificate: false,
  signatureAlgorithm: 'SHA256withRSA',
  publicKeyAlgorithm: 'RSA',
  keySize: 2048,
  isCa: false,
  keyUsage: ['digitalSignature'],
  extendedKeyUsage: [],
  notBefore: '2024-01-01',
  notAfter: '2026-01-01',
  isExpired: false,
  fingerprintSha256: 'abc123def456',
  fingerprintSha1: 'aabbcc',
};

const baseCompliance: IcaoComplianceStatus = {
  isCompliant: true,
  complianceLevel: 'CONFORMANT',
  violations: [],
  keyUsageCompliant: true,
  algorithmCompliant: true,
  keySizeCompliant: true,
  validityPeriodCompliant: true,
  dnFormatCompliant: true,
  extensionsCompliant: true,
};

describe('CurrentCertificateCard', () => {
  describe('full mode (default)', () => {
    it('should render certificate type badge', () => {
      render(<CurrentCertificateCard certificate={baseCert} />);
      // certificateType appears in the badge
      expect(screen.getAllByText('DSC').length).toBeGreaterThan(0);
    });

    it('should render subject DN', () => {
      render(<CurrentCertificateCard certificate={baseCert} />);
      expect(screen.getByText('CN=Test DSC,C=KR')).toBeInTheDocument();
    });

    it('should render country code', () => {
      render(<CurrentCertificateCard certificate={baseCert} />);
      expect(screen.getByText('KR')).toBeInTheDocument();
    });

    it('should render serial number', () => {
      render(<CurrentCertificateCard certificate={baseCert} />);
      expect(screen.getByText('0123456789ABCDEF')).toBeInTheDocument();
    });

    it('should render signature algorithm', () => {
      render(<CurrentCertificateCard certificate={baseCert} />);
      expect(screen.getByText('SHA256withRSA')).toBeInTheDocument();
    });

    it('should render key size info', () => {
      render(<CurrentCertificateCard certificate={baseCert} />);
      expect(screen.getByText(/RSA.*2048.*bits/)).toBeInTheDocument();
    });

    it('should render fingerprint', () => {
      render(<CurrentCertificateCard certificate={baseCert} />);
      expect(screen.getByText('abc123def456')).toBeInTheDocument();
    });

    it('should render key usage badges', () => {
      render(<CurrentCertificateCard certificate={baseCert} />);
      expect(screen.getByText('digitalSignature')).toBeInTheDocument();
    });

    it('should not render key usage section when empty', () => {
      const cert = { ...baseCert, keyUsage: [] };
      render(<CurrentCertificateCard certificate={cert} />);
      expect(screen.queryByText('Key Usage')).not.toBeInTheDocument();
    });

    it('should render compliance badge when compliance prop is provided', () => {
      render(<CurrentCertificateCard certificate={baseCert} compliance={baseCompliance} />);
      expect(screen.getByTestId('compliance-badge')).toBeInTheDocument();
    });

    it('should not render compliance badge when compliance prop is omitted', () => {
      render(<CurrentCertificateCard certificate={baseCert} />);
      expect(screen.queryByTestId('compliance-badge')).not.toBeInTheDocument();
    });

    it('should show "(만료)" label when certificate is expired', () => {
      const cert = { ...baseCert, isExpired: true };
      render(<CurrentCertificateCard certificate={cert} />);
      expect(screen.getByText(/만료/)).toBeInTheDocument();
    });

    it('should show "Self-signed" label for self-signed certificates', () => {
      const cert = { ...baseCert, isSelfSigned: true };
      render(<CurrentCertificateCard certificate={cert} />);
      expect(screen.getByText('Self-signed')).toBeInTheDocument();
    });

    it('should show "Link Cert" label for link certificates', () => {
      const cert = { ...baseCert, isLinkCertificate: true };
      render(<CurrentCertificateCard certificate={cert} />);
      expect(screen.getByText('Link Cert')).toBeInTheDocument();
    });
  });

  describe('compact mode', () => {
    it('should render certificate type in compact mode', () => {
      render(<CurrentCertificateCard certificate={baseCert} compact />);
      expect(screen.getByText('DSC')).toBeInTheDocument();
    });

    it('should render country code in compact mode', () => {
      render(<CurrentCertificateCard certificate={baseCert} compact />);
      expect(screen.getByText('KR')).toBeInTheDocument();
    });

    it('should render subject DN in compact mode', () => {
      render(<CurrentCertificateCard certificate={baseCert} compact />);
      expect(screen.getByText('CN=Test DSC,C=KR')).toBeInTheDocument();
    });

    it('should render compliance badge in compact mode when compliance provided', () => {
      render(<CurrentCertificateCard certificate={baseCert} compliance={baseCompliance} compact />);
      expect(screen.getByTestId('compliance-badge')).toBeInTheDocument();
    });

    it('should not render serial/algorithm details in compact mode', () => {
      render(<CurrentCertificateCard certificate={baseCert} compact />);
      expect(screen.queryByText('SHA256withRSA')).not.toBeInTheDocument();
    });
  });

  it('should apply correct color class for CSCA type', () => {
    const cert = { ...baseCert, certificateType: 'CSCA' };
    const { container } = render(<CurrentCertificateCard certificate={cert} />);
    expect(container.querySelector('.text-blue-700')).toBeInTheDocument();
  });

  it('should apply correct color class for DSC_NC type', () => {
    const cert = { ...baseCert, certificateType: 'DSC_NC' };
    const { container } = render(<CurrentCertificateCard certificate={cert} />);
    expect(container.querySelector('.text-amber-700')).toBeInTheDocument();
  });

  it('should apply correct color class for MLSC type', () => {
    const cert = { ...baseCert, certificateType: 'MLSC' };
    const { container } = render(<CurrentCertificateCard certificate={cert} />);
    expect(container.querySelector('.text-purple-700')).toBeInTheDocument();
  });
});
