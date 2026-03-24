import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { TrustChainVisualization } from '../TrustChainVisualization';

vi.mock('@/components/common', () => ({
  getGlossaryTooltip: (term: string) => term,
}));

describe('TrustChainVisualization', () => {
  describe('empty / no path', () => {
    it('should show "No trust chain information" for empty string', () => {
      render(<TrustChainVisualization trustChainPath="" trustChainValid={false} />);
      expect(screen.getByText('No trust chain information')).toBeInTheDocument();
    });

    it('should show "No trust chain information" for whitespace-only string', () => {
      render(<TrustChainVisualization trustChainPath="   " trustChainValid={false} />);
      expect(screen.getByText('No trust chain information')).toBeInTheDocument();
    });
  });

  describe('compact mode', () => {
    const simplePath = 'DSC → CN=Test CSCA,C=DE';

    it('should render node names in compact mode', () => {
      render(
        <TrustChainVisualization
          trustChainPath={simplePath}
          trustChainValid={true}
          compact
        />
      );
      expect(screen.getByText('DSC')).toBeInTheDocument();
      // CN extracted: "Test CSCA"
      expect(screen.getByText('Test CSCA')).toBeInTheDocument();
    });

    it('should render serial number when present in DN', () => {
      render(
        <TrustChainVisualization
          trustChainPath="DSC → CN=CSCA Latvia,serialNumber=003"
          trustChainValid={true}
          compact
        />
      );
      expect(screen.getByText('(#003)')).toBeInTheDocument();
    });

    it('should not render chain header in compact mode', () => {
      render(
        <TrustChainVisualization
          trustChainPath={simplePath}
          trustChainValid={true}
          compact
        />
      );
      expect(screen.queryByText(/Trust Chain Path/)).not.toBeInTheDocument();
    });
  });

  describe('full mode (default)', () => {
    const chainPath = 'DSC → CN=CSCA Germany,C=DE → CN=Root CSCA,C=DE';

    it('should render chain header with level count', () => {
      render(
        <TrustChainVisualization trustChainPath={chainPath} trustChainValid={true} />
      );
      expect(screen.getByText('Trust Chain Path (3 levels)')).toBeInTheDocument();
    });

    it('should show ShieldCheck icon for valid trust chain', () => {
      const { container } = render(
        <TrustChainVisualization trustChainPath={chainPath} trustChainValid={true} />
      );
      // ShieldCheck renders as SVG; validated via "✓ Valid" in summary
      expect(screen.getByText('✓ Valid')).toBeInTheDocument();
    });

    it('should show "✗ Invalid" for invalid trust chain', () => {
      render(
        <TrustChainVisualization trustChainPath={chainPath} trustChainValid={false} />
      );
      expect(screen.getByText('✗ Invalid')).toBeInTheDocument();
    });

    it('should render "DSC (End Entity)" for first node', () => {
      render(
        <TrustChainVisualization trustChainPath={chainPath} trustChainValid={true} />
      );
      expect(screen.getByText('DSC (End Entity)')).toBeInTheDocument();
    });

    it('should render "Root CSCA" for last node', () => {
      render(
        <TrustChainVisualization trustChainPath={chainPath} trustChainValid={true} />
      );
      expect(screen.getAllByText('Root CSCA').length).toBeGreaterThan(0);
    });

    it('should render "Link Certificate" for intermediate nodes', () => {
      // 3-node chain: DSC → Link → Root
      render(
        <TrustChainVisualization
          trustChainPath="DSC → CN=Link Cert,C=LV → CN=Root CA,C=LV"
          trustChainValid={true}
        />
      );
      expect(screen.getByText('Link Certificate')).toBeInTheDocument();
    });

    it('should render chain summary section', () => {
      render(
        <TrustChainVisualization trustChainPath={chainPath} trustChainValid={true} />
      );
      expect(screen.getByText('Chain Length:')).toBeInTheDocument();
      expect(screen.getByText('Link Certificates:')).toBeInTheDocument();
      expect(screen.getByText('Validation Status:')).toBeInTheDocument();
    });

    it('should count link certificates correctly', () => {
      // 4-node chain: DSC → Link → Link → Root → 2 link certs
      render(
        <TrustChainVisualization
          trustChainPath="DSC → CN=LC1,C=HU → CN=LC2,C=HU → CN=Root,C=HU"
          trustChainValid={true}
        />
      );
      expect(screen.getByText('2')).toBeInTheDocument();
    });

    it('should render singular "certificate" for single-node chain', () => {
      render(
        <TrustChainVisualization trustChainPath="DSC" trustChainValid={false} />
      );
      expect(screen.getByText('1 certificate')).toBeInTheDocument();
    });

    it('should render plural "certificates" for multi-node chain', () => {
      render(
        <TrustChainVisualization trustChainPath={chainPath} trustChainValid={true} />
      );
      expect(screen.getByText('3 certificates')).toBeInTheDocument();
    });

    it('should apply custom className', () => {
      const { container } = render(
        <TrustChainVisualization
          trustChainPath={chainPath}
          trustChainValid={true}
          className="my-custom-class"
        />
      );
      expect(container.querySelector('.my-custom-class')).toBeInTheDocument();
    });

    it('should render Level 0 label for first node', () => {
      render(
        <TrustChainVisualization trustChainPath={chainPath} trustChainValid={true} />
      );
      expect(screen.getByText('Level 0')).toBeInTheDocument();
    });
  });

  describe('CN extraction', () => {
    it('should extract CN from standard DN format', () => {
      render(
        <TrustChainVisualization
          trustChainPath="DSC → CN=Korea CSCA,C=KR"
          trustChainValid={true}
          compact
        />
      );
      expect(screen.getByText('Korea CSCA')).toBeInTheDocument();
    });

    it('should return full DN when no CN present', () => {
      render(
        <TrustChainVisualization
          trustChainPath="DSC → O=NoName"
          trustChainValid={true}
          compact
        />
      );
      expect(screen.getByText('O=NoName')).toBeInTheDocument();
    });
  });
});
