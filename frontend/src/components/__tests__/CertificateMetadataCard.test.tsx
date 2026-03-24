import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import CertificateMetadataCard from '../CertificateMetadataCard';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => {
      const map: Record<string, string> = {
        'certificate:metadata.noMetadata': '메타데이터 없음',
        'certificate:metadata.algorithmInfo': '알고리즘 정보',
        'certificate:metadata.publicKeyInfo': '공개키 정보',
        'certificate:metadata.caInfo': 'CA 정보',
        'certificate:metadata.identifiers': '식별자',
        'certificate:metadata.distributionPoints': '배포 지점',
      };
      return map[key] ?? key;
    },
    i18n: { language: 'ko' },
  }),
}));

// Mock clipboard API
Object.defineProperty(navigator, 'clipboard', {
  value: { writeText: vi.fn().mockResolvedValue(undefined) },
  writable: true,
});

describe('CertificateMetadataCard', () => {
  describe('no metadata placeholder', () => {
    it('should show placeholder when no relevant fields are provided', () => {
      render(<CertificateMetadataCard certificate={{}} />);
      expect(screen.getByText('메타데이터 없음')).toBeInTheDocument();
    });

    it('should not show placeholder when signatureAlgorithm is provided', () => {
      render(<CertificateMetadataCard certificate={{ signatureAlgorithm: 'SHA256withRSA' }} />);
      expect(screen.queryByText('메타데이터 없음')).not.toBeInTheDocument();
    });
  });

  describe('algorithm information section', () => {
    it('should render algorithm info section header', () => {
      render(<CertificateMetadataCard certificate={{ signatureAlgorithm: 'SHA256withRSA' }} />);
      expect(screen.getByText('알고리즘 정보')).toBeInTheDocument();
    });

    it('should render version v1 for version=0', () => {
      render(
        <CertificateMetadataCard
          certificate={{ version: 0, signatureAlgorithm: 'SHA256withRSA' }}
        />
      );
      expect(screen.getByText('v1')).toBeInTheDocument();
    });

    it('should render version v2 for version=1', () => {
      render(
        <CertificateMetadataCard
          certificate={{ version: 1, signatureAlgorithm: 'SHA256withRSA' }}
        />
      );
      expect(screen.getByText('v2')).toBeInTheDocument();
    });

    it('should render version v3 for version=2', () => {
      render(
        <CertificateMetadataCard
          certificate={{ version: 2, signatureAlgorithm: 'SHA256withRSA' }}
        />
      );
      expect(screen.getByText('v3')).toBeInTheDocument();
    });

    it('should render signature algorithm', () => {
      render(<CertificateMetadataCard certificate={{ signatureAlgorithm: 'SHA256withRSA' }} />);
      expect(screen.getByText('SHA256withRSA')).toBeInTheDocument();
    });

    it('should render hash algorithm', () => {
      render(
        <CertificateMetadataCard
          certificate={{
            signatureAlgorithm: 'SHA256withRSA',
            signatureHashAlgorithm: 'SHA-256',
          }}
        />
      );
      expect(screen.getByText('SHA-256')).toBeInTheDocument();
    });

    it('should not render version row when version is undefined', () => {
      render(<CertificateMetadataCard certificate={{ signatureAlgorithm: 'SHA256withRSA' }} />);
      expect(screen.queryByText('Version:')).not.toBeInTheDocument();
    });
  });

  describe('public key information section', () => {
    it('should render public key info section when publicKeyAlgorithm provided', () => {
      render(
        <CertificateMetadataCard
          certificate={{ publicKeyAlgorithm: 'RSA', signatureAlgorithm: 'SHA256withRSA' }}
        />
      );
      expect(screen.getByText('공개키 정보')).toBeInTheDocument();
    });

    it('should render key size with "bits" suffix', () => {
      render(
        <CertificateMetadataCard
          certificate={{ publicKeyAlgorithm: 'RSA', publicKeySize: 2048 }}
        />
      );
      expect(screen.getByText('2048 bits')).toBeInTheDocument();
    });

    it('should render curve name', () => {
      render(
        <CertificateMetadataCard
          certificate={{ publicKeyAlgorithm: 'ECDSA', publicKeyCurve: 'P-256' }}
        />
      );
      expect(screen.getByText('P-256')).toBeInTheDocument();
    });

    it('should not render public key section when no public key fields are provided', () => {
      render(<CertificateMetadataCard certificate={{ signatureAlgorithm: 'SHA256withRSA' }} />);
      expect(screen.queryByText('공개키 정보')).not.toBeInTheDocument();
    });
  });

  describe('key usage section', () => {
    it('should render key usage badges', () => {
      render(
        <CertificateMetadataCard
          certificate={{
            signatureAlgorithm: 'SHA256withRSA',
            keyUsage: ['digitalSignature', 'keyCertSign'],
          }}
        />
      );
      expect(screen.getByText('digitalSignature')).toBeInTheDocument();
      expect(screen.getByText('keyCertSign')).toBeInTheDocument();
    });

    it('should render extended key usage badges', () => {
      render(
        <CertificateMetadataCard
          certificate={{
            signatureAlgorithm: 'SHA256withRSA',
            extendedKeyUsage: ['1.3.6.1.5.5.7.3.2'],
          }}
        />
      );
      expect(screen.getByText('1.3.6.1.5.5.7.3.2')).toBeInTheDocument();
    });

    it('should not render key usage section when both arrays are empty', () => {
      render(
        <CertificateMetadataCard
          certificate={{
            signatureAlgorithm: 'SHA256withRSA',
            keyUsage: [],
            extendedKeyUsage: [],
          }}
        />
      );
      expect(screen.queryByText('Key Usage')).not.toBeInTheDocument();
    });
  });

  describe('CA information section', () => {
    it('should render CA section when isCA is provided', () => {
      render(
        <CertificateMetadataCard
          certificate={{ signatureAlgorithm: 'SHA256withRSA', isCA: true }}
        />
      );
      expect(screen.getByText('CA 정보')).toBeInTheDocument();
    });

    it('should show "TRUE" for CA certificates', () => {
      render(
        <CertificateMetadataCard
          certificate={{ signatureAlgorithm: 'SHA256withRSA', isCA: true }}
        />
      );
      expect(screen.getByText('TRUE')).toBeInTheDocument();
    });

    it('should show "FALSE" for non-CA certificates', () => {
      render(
        <CertificateMetadataCard
          certificate={{ signatureAlgorithm: 'SHA256withRSA', isCA: false }}
        />
      );
      expect(screen.getByText('FALSE')).toBeInTheDocument();
    });

    it('should render path length constraint', () => {
      render(
        <CertificateMetadataCard
          certificate={{ signatureAlgorithm: 'SHA256withRSA', pathLenConstraint: 0 }}
        />
      );
      expect(screen.getByText('0')).toBeInTheDocument();
    });
  });

  describe('identifiers section', () => {
    it('should render SKI when provided', () => {
      render(
        <CertificateMetadataCard
          certificate={{
            signatureAlgorithm: 'SHA256withRSA',
            subjectKeyIdentifier: 'AA:BB:CC:DD',
          }}
        />
      );
      expect(screen.getByText('AA:BB:CC:DD')).toBeInTheDocument();
      expect(screen.getByText('식별자')).toBeInTheDocument();
    });

    it('should render AKI when provided', () => {
      render(
        <CertificateMetadataCard
          certificate={{
            signatureAlgorithm: 'SHA256withRSA',
            authorityKeyIdentifier: 'EE:FF:00:11',
          }}
        />
      );
      expect(screen.getByText('EE:FF:00:11')).toBeInTheDocument();
    });

    it('should not render identifiers section when neither SKI nor AKI provided', () => {
      render(<CertificateMetadataCard certificate={{ signatureAlgorithm: 'SHA256withRSA' }} />);
      expect(screen.queryByText('식별자')).not.toBeInTheDocument();
    });
  });

  describe('distribution points section', () => {
    it('should render CRL distribution point as link', () => {
      render(
        <CertificateMetadataCard
          certificate={{
            signatureAlgorithm: 'SHA256withRSA',
            crlDistributionPoints: ['http://crl.example.com/crl.crl'],
          }}
        />
      );
      const link = screen.getByRole('link', { name: 'http://crl.example.com/crl.crl' });
      expect(link).toBeInTheDocument();
      expect(link).toHaveAttribute('href', 'http://crl.example.com/crl.crl');
    });

    it('should render OCSP responder URL as link', () => {
      render(
        <CertificateMetadataCard
          certificate={{
            signatureAlgorithm: 'SHA256withRSA',
            ocspResponderUrl: 'http://ocsp.example.com',
          }}
        />
      );
      expect(screen.getByRole('link', { name: 'http://ocsp.example.com' })).toBeInTheDocument();
    });

    it('should render distribution points section header', () => {
      render(
        <CertificateMetadataCard
          certificate={{
            signatureAlgorithm: 'SHA256withRSA',
            ocspResponderUrl: 'http://ocsp.example.com',
          }}
        />
      );
      expect(screen.getByText('배포 지점')).toBeInTheDocument();
    });

    it('should not render distribution points section when none provided', () => {
      render(<CertificateMetadataCard certificate={{ signatureAlgorithm: 'SHA256withRSA' }} />);
      expect(screen.queryByText('배포 지점')).not.toBeInTheDocument();
    });
  });
});
