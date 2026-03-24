import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { DuplicateCertificatesTree } from '../DuplicateCertificatesTree';
import type { UploadDuplicate } from '@/types';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string, opts?: Record<string, unknown>) => {
      const map: Record<string, string> = {
        'upload:duplicateTree.noDuplicates': '중복 없음',
        'upload:duplicateTree.countrySummary': `${opts?.certNum}개 인증서, ${opts?.dupNum}개 중복`,
        'upload:duplicateTree.summary': `${opts?.countryNum}개 국가, ${opts?.certNum}개 인증서, ${opts?.dupNum}개 중복`,
      };
      return map[key] ?? key;
    },
    i18n: { language: 'ko' },
  }),
}));

// TreeViewer renders a complex tree — stub it for simplicity
vi.mock('../TreeViewer', () => {
  const renderNodes = (nodes: Array<{ id: string; name: string; children?: any[] }>): any => {
    return nodes.map((node) => (
      <div key={node.id} data-testid={`tree-node-${node.id}`}>
        {node.name}
        {node.children && renderNodes(node.children)}
      </div>
    ));
  };
  return {
    TreeViewer: ({ data }: { data: unknown[] }) => (
      <div data-testid="tree-viewer">
        {renderNodes(data as Array<{ id: string; name: string; children?: any[] }>)}
      </div>
    ),
  };
});

const makeDuplicate = (overrides: Partial<UploadDuplicate> = {}): UploadDuplicate => ({
  id: 1,
  certificateId: 'cert-id-1',
  fingerprint: 'abcdef1234567890abcdef1234567890abcdef12',
  subjectDn: 'CN=Test DSC,C=KR',
  certificateType: 'DSC',
  country: 'KR',
  sourceType: 'LDIF_PARSED',
  detectedAt: '2026-01-01T00:00:00Z',
  firstUploadId: 'upload-id-1',
  ...overrides,
});

describe('DuplicateCertificatesTree', () => {
  it('should show "중복 없음" message when no duplicates', () => {
    render(<DuplicateCertificatesTree duplicates={[]} />);
    expect(screen.getByText('중복 없음')).toBeInTheDocument();
  });

  it('should render tree viewer when duplicates exist', () => {
    render(<DuplicateCertificatesTree duplicates={[makeDuplicate()]} />);
    expect(screen.getByTestId('tree-viewer')).toBeInTheDocument();
  });

  it('should render summary banner with group counts', () => {
    const dups = [
      makeDuplicate({ country: 'KR', certificateId: 'cert-1' }),
      makeDuplicate({ country: 'DE', certificateId: 'cert-2' }),
    ];
    render(<DuplicateCertificatesTree duplicates={dups} />);

    // Summary with 2 countries, 2 certs, 2 dups
    expect(screen.getByText(/2개 국가/)).toBeInTheDocument();
  });

  it('should group duplicates by country', () => {
    const dups = [
      makeDuplicate({ country: 'KR', certificateId: 'cert-1' }),
      makeDuplicate({ country: 'KR', certificateId: 'cert-2' }),
      makeDuplicate({ country: 'DE', certificateId: 'cert-3' }),
    ];
    render(<DuplicateCertificatesTree duplicates={dups} />);

    expect(screen.getByTestId('tree-node-country-KR')).toBeInTheDocument();
    expect(screen.getByTestId('tree-node-country-DE')).toBeInTheDocument();
  });

  it('should use "Unknown" country for entries with empty country', () => {
    const dup = makeDuplicate({ country: '' });
    render(<DuplicateCertificatesTree duplicates={[dup]} />);

    expect(screen.getByTestId('tree-node-country-Unknown')).toBeInTheDocument();
  });

  it('should truncate long fingerprints in tree node names', () => {
    const longFp = 'abcdefgh12345678901234567890abcdefgh12345678';
    const dup = makeDuplicate({ fingerprint: longFp, certificateId: 'cert-fp' });
    render(<DuplicateCertificatesTree duplicates={[dup]} />);

    // The node name should contain truncated fingerprint (first 8 + ... + last 8)
    const nodeEl = screen.getByTestId('tree-node-KR-cert-fp');
    expect(nodeEl.textContent).toContain('...');
  });

  it('should group multiple duplicates under the same certificate', () => {
    const dups = [
      makeDuplicate({ certificateId: 'same-cert', sourceType: 'LDIF_PARSED' }),
      makeDuplicate({ id: 2, certificateId: 'same-cert', sourceType: 'ML_PARSED' }),
    ];
    render(<DuplicateCertificatesTree duplicates={dups} />);

    // Only one tree node per certificate (cert grouped)
    expect(screen.getByTestId('tree-node-KR-same-cert')).toBeInTheDocument();
  });

  it('should render CSCA icon for CSCA certificate type', () => {
    const dup = makeDuplicate({ certificateType: 'CSCA', certificateId: 'csca-1' });
    render(<DuplicateCertificatesTree duplicates={[dup]} />);

    const nodeEl = screen.getByTestId('tree-node-KR-csca-1');
    expect(nodeEl.textContent).toContain('[CSCA]');
  });

  it('should sort countries by total duplicate count (descending)', () => {
    const dups = [
      // DE: 1 dup
      makeDuplicate({ country: 'DE', certificateId: 'de-cert-1' }),
      // KR: 3 dups for same cert
      makeDuplicate({ id: 2, country: 'KR', certificateId: 'kr-cert-1' }),
      makeDuplicate({ id: 3, country: 'KR', certificateId: 'kr-cert-1' }),
      makeDuplicate({ id: 4, country: 'KR', certificateId: 'kr-cert-1' }),
    ];
    render(<DuplicateCertificatesTree duplicates={dups} />);

    const treeViewer = screen.getByTestId('tree-viewer');
    const children = treeViewer.querySelectorAll('[data-testid^="tree-node-country"]');
    // First should be KR (3 dups), then DE (1 dup)
    expect(children[0].getAttribute('data-testid')).toBe('tree-node-country-KR');
    expect(children[1].getAttribute('data-testid')).toBe('tree-node-country-DE');
  });
});
