import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import {
  exportDuplicatesToCsv,
  exportDscNcReportToCsv,
  exportDuplicateStatisticsToCsv,
  exportAiAnalysisReportToCsv,
  exportCrlReportToCsv,
} from '../csvExport';

// Track what gets downloaded
let lastBlobContent: string | null = null;
let lastFilename: string | null = null;

const OriginalBlob = globalThis.Blob;

beforeEach(() => {
  lastBlobContent = null;
  lastFilename = null;

  // Mock Blob to capture content
  globalThis.Blob = class MockBlob extends OriginalBlob {
    constructor(parts?: BlobPart[], options?: BlobPropertyBag) {
      super(parts, options);
      lastBlobContent = parts ? parts.map(p => String(p)).join('') : '';
    }
  } as typeof Blob;

  // Mock anchor element click to capture download
  const mockLink = {
    setAttribute: vi.fn((attr: string, value: string) => {
      if (attr === 'download') lastFilename = value;
    }),
    set href(_val: string) { /* noop */ },
    get href() { return 'blob:mock-url'; },
    set download(val: string) { lastFilename = val; },
    get download() { return lastFilename || ''; },
    style: { visibility: '' },
    click: vi.fn(),
  };

  vi.spyOn(document, 'createElement').mockReturnValue(mockLink as unknown as HTMLElement);
  vi.spyOn(document.body, 'appendChild').mockReturnValue(mockLink as unknown as Node);
  vi.spyOn(document.body, 'removeChild').mockReturnValue(mockLink as unknown as Node);
});

afterEach(() => {
  globalThis.Blob = OriginalBlob;
});

describe('exportDuplicatesToCsv', () => {
  it('should not export when duplicates array is empty', () => {
    exportDuplicatesToCsv([]);
    expect(lastBlobContent).toBeNull();
  });

  it('should generate CSV with BOM header for UTF-8 Excel compatibility', () => {
    const duplicates = [
      {
        id: 1,
        certificateId: 'cert-001',
        certificateType: 'DSC' as const,
        country: 'KR',
        subjectDn: 'CN=Test,C=KR',
        fingerprint: 'abc123',
        sourceType: 'LDIF_PARSED',
        sourceCountry: 'KR',
        sourceFileName: 'test.ldif',
        sourceEntryDn: 'cn=abc123,o=dsc,c=KR',
        detectedAt: '2026-01-15T10:30:00Z',
        firstUploadId: 'upload-001',
        firstUploadFileName: 'first.ldif',
        firstUploadTimestamp: '2026-01-10T08:00:00Z',
      },
    ];

    exportDuplicatesToCsv(duplicates);

    expect(lastBlobContent).not.toBeNull();
    // BOM character at the start
    expect(lastBlobContent!.startsWith('\uFEFF')).toBe(true);
    // Contains headers
    expect(lastBlobContent).toContain('Duplicate ID');
    expect(lastBlobContent).toContain('Certificate Type');
    expect(lastBlobContent).toContain('Fingerprint (SHA-256)');
    // Contains data
    expect(lastBlobContent).toContain('KR');
    expect(lastBlobContent).toContain('abc123');
  });

  it('should escape double quotes in CSV values', () => {
    const duplicates = [
      {
        id: 1,
        certificateId: 'cert-001',
        certificateType: 'DSC' as const,
        country: 'KR',
        subjectDn: 'CN="Test Cert",C=KR',
        fingerprint: 'abc123',
        sourceType: 'LDIF_PARSED',
        detectedAt: '2026-01-15T10:30:00Z',
        firstUploadId: 'upload-001',
      },
    ];

    exportDuplicatesToCsv(duplicates);

    // Double quotes should be escaped as ""
    expect(lastBlobContent).toContain('""Test Cert""');
  });

  it('should use custom filename when provided', () => {
    const duplicates = [
      {
        id: 1,
        certificateId: 'cert-001',
        certificateType: 'CSCA' as const,
        country: 'DE',
        subjectDn: 'CN=Test',
        fingerprint: 'def456',
        sourceType: 'ML_PARSED',
        detectedAt: '2026-01-15T10:30:00Z',
        firstUploadId: 'upload-002',
      },
    ];

    exportDuplicatesToCsv(duplicates, 'custom-report.csv');
    expect(lastFilename).toBe('custom-report.csv');
  });
});

describe('exportDscNcReportToCsv', () => {
  it('should not export when certificates array is empty', () => {
    exportDscNcReportToCsv([]);
    expect(lastBlobContent).toBeNull();
  });

  it('should include all 14 columns in the CSV header', () => {
    const certs = [
      {
        fingerprint: 'fp001',
        countryCode: 'JP',
        subjectDn: 'CN=Japan DSC',
        issuerDn: 'CN=Japan CSCA',
        serialNumber: '123456',
        notBefore: '2025-01-01T00:00:00Z',
        notAfter: '2026-01-01T00:00:00Z',
        validity: 'VALID',
        signatureAlgorithm: 'sha256WithRSAEncryption',
        publicKeyAlgorithm: 'RSA',
        publicKeySize: 2048,
        pkdConformanceCode: 'NC001',
        pkdConformanceText: 'Non-conformant reason',
        pkdVersion: '100',
      },
    ];

    exportDscNcReportToCsv(certs);

    expect(lastBlobContent).toContain('Country');
    expect(lastBlobContent).toContain('Subject DN');
    expect(lastBlobContent).toContain('Conformance Code');
    expect(lastBlobContent).toContain('Fingerprint (SHA-256)');
    expect(lastBlobContent).toContain('JP');
    expect(lastBlobContent).toContain('NC001');
  });
});

describe('exportDuplicateStatisticsToCsv', () => {
  it('should generate CSV with type counts and percentages', () => {
    const byType = { DSC: 50, CSCA: 30, CRL: 20 };
    const total = 100;

    exportDuplicateStatisticsToCsv(byType, total);

    expect(lastBlobContent).toContain('Certificate Type');
    expect(lastBlobContent).toContain('Duplicate Count');
    expect(lastBlobContent).toContain('Percentage');
    expect(lastBlobContent).toContain('DSC');
    expect(lastBlobContent).toContain('50.00%');
    expect(lastBlobContent).toContain('TOTAL');
    expect(lastBlobContent).toContain('100.00%');
  });

  it('should filter out zero-count types', () => {
    const byType = { DSC: 10, CSCA: 0, CRL: 5 };
    const total = 15;

    exportDuplicateStatisticsToCsv(byType, total);

    expect(lastBlobContent).toContain('DSC');
    expect(lastBlobContent).toContain('CRL');
    // CSCA with 0 should not appear (except potentially in TOTAL line context)
    const lines = lastBlobContent!.split('\n');
    const dataLines = lines.slice(1, -1); // skip header and TOTAL
    const cscaLine = dataLines.find(l => l.startsWith('CSCA'));
    expect(cscaLine).toBeUndefined();
  });
});

describe('exportAiAnalysisReportToCsv', () => {
  it('should not export when items array is empty', () => {
    exportAiAnalysisReportToCsv([]);
    expect(lastBlobContent).toBeNull();
  });

  it('should sort risk factors by value descending and take top 5', () => {
    const items = [
      {
        fingerprint: 'fp001',
        certificate_type: 'DSC',
        country_code: 'KR',
        anomaly_score: 0.85,
        anomaly_label: 'anomalous',
        risk_score: 75.5,
        risk_level: 'HIGH',
        risk_factors: {
          algorithm: 30,
          key_size: 20,
          compliance: 15,
          validity: 10,
          extensions: 5,
          anomaly: 1,
        },
        anomaly_explanations: ['Weak algorithm detected', 'Short key size'],
        analyzed_at: '2026-03-01T12:00:00Z',
      },
    ];

    exportAiAnalysisReportToCsv(items);

    expect(lastBlobContent).toContain('Risk Score');
    expect(lastBlobContent).toContain('75.5');
    expect(lastBlobContent).toContain('0.850');
    // Top 5 risk factors should be present (sorted by value desc)
    expect(lastBlobContent).toContain('algorithm(30)');
    expect(lastBlobContent).toContain('key_size(20)');
  });
});

describe('exportCrlReportToCsv', () => {
  it('should format boolean storedInLdap as Yes/No', () => {
    const crls = [
      {
        countryCode: 'US',
        issuerDn: 'CN=US CRL Issuer',
        thisUpdate: '2026-01-01',
        nextUpdate: '2026-07-01',
        crlNumber: '42',
        status: 'VALID',
        revokedCount: 10,
        signatureAlgorithm: 'sha256WithRSAEncryption',
        fingerprint: 'crl-fp-001',
        storedInLdap: true,
        createdAt: '2026-01-01T00:00:00Z',
      },
      {
        countryCode: 'DE',
        issuerDn: 'CN=DE CRL Issuer',
        thisUpdate: '2026-02-01',
        nextUpdate: '',
        crlNumber: '',
        status: 'EXPIRED',
        revokedCount: 0,
        signatureAlgorithm: 'sha384WithRSAEncryption',
        fingerprint: 'crl-fp-002',
        storedInLdap: false,
        createdAt: '2026-02-01T00:00:00Z',
      },
    ];

    exportCrlReportToCsv(crls);

    expect(lastBlobContent).toContain('Stored in LDAP');
    expect(lastBlobContent).toContain('Yes');
    expect(lastBlobContent).toContain('No');
    expect(lastBlobContent).toContain('US');
    expect(lastBlobContent).toContain('DE');
  });
});
