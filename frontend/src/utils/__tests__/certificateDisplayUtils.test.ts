import { describe, it, expect } from 'vitest';
import {
  formatDate,
  formatVersion,
  formatSignatureAlgorithm,
  getOrganizationUnit,
  getActualCertType,
  isLinkCertificate,
  isMasterListSignerCertificate,
  getCertTypeDescription,
} from '../certificateDisplayUtils';
import type { Certificate } from '@/components/CertificateDetailDialog';

/** Helper to create a minimal Certificate object for testing */
function makeCert(overrides: Partial<Certificate> = {}): Certificate {
  return {
    dn: 'cn=abc123,o=dsc,c=KR,dc=data,dc=download,dc=pkd',
    cn: 'abc123',
    sn: '123456',
    country: 'KR',
    type: 'DSC',
    subjectDn: 'CN=Korea DSC,C=KR',
    issuerDn: 'CN=Korea CSCA,C=KR',
    fingerprint: 'abc123def456',
    validFrom: '2025-01-01T00:00:00Z',
    validTo: '2026-01-01T00:00:00Z',
    validity: 'VALID',
    isSelfSigned: false,
    ...overrides,
  };
}

describe('formatDate', () => {
  it('should format ISO date string to Korean locale', () => {
    const result = formatDate('2026-03-04T00:00:00Z');
    // Korean locale format: YYYY. MM. DD.
    expect(result).toMatch(/2026/);
    expect(result).toMatch(/03|3/);
    expect(result).toMatch(/04|4/);
  });
});

describe('formatVersion', () => {
  it('should convert version 0 to v1', () => {
    expect(formatVersion(0)).toBe('v1');
  });

  it('should convert version 1 to v2', () => {
    expect(formatVersion(1)).toBe('v2');
  });

  it('should convert version 2 to v3', () => {
    expect(formatVersion(2)).toBe('v3');
  });

  it('should return "Unknown" for undefined', () => {
    expect(formatVersion(undefined)).toBe('Unknown');
  });

  it('should handle unknown version numbers with fallback', () => {
    expect(formatVersion(5)).toBe('v6');
  });
});

describe('formatSignatureAlgorithm', () => {
  it('should simplify known algorithm names', () => {
    expect(formatSignatureAlgorithm('sha256WithRSAEncryption')).toBe('RSA-SHA256');
    expect(formatSignatureAlgorithm('ecdsa-with-SHA384')).toBe('ECDSA-SHA384');
    expect(formatSignatureAlgorithm('sha512WithRSAEncryption')).toBe('RSA-SHA512');
  });

  it('should return N/A for undefined or empty input', () => {
    expect(formatSignatureAlgorithm(undefined)).toBe('N/A');
    expect(formatSignatureAlgorithm('')).toBe('N/A');
  });

  it('should return original algorithm name for unknown algorithms', () => {
    expect(formatSignatureAlgorithm('rsassaPss')).toBe('rsassaPss');
  });
});

describe('getOrganizationUnit', () => {
  it('should extract organization unit from LDAP DN', () => {
    expect(getOrganizationUnit('cn=abc,o=dsc,c=KR')).toBe('dsc');
    expect(getOrganizationUnit('cn=def,o=csca,c=DE')).toBe('csca');
    expect(getOrganizationUnit('cn=ghi,o=mlsc,c=JP')).toBe('mlsc');
  });

  it('should return empty string when no o= component found', () => {
    expect(getOrganizationUnit('cn=abc,c=KR')).toBe('');
  });

  it('should be case-insensitive for the attribute name', () => {
    expect(getOrganizationUnit('cn=abc,O=CSCA,c=KR')).toBe('CSCA');
  });
});

describe('getActualCertType', () => {
  it('should identify DSC from LDAP DN', () => {
    const cert = makeCert({ dn: 'cn=abc,o=dsc,c=KR,dc=data' });
    expect(getActualCertType(cert)).toBe('DSC');
  });

  it('should identify CSCA from LDAP DN', () => {
    const cert = makeCert({ dn: 'cn=abc,o=csca,c=KR,dc=data', type: 'CSCA' });
    expect(getActualCertType(cert)).toBe('CSCA');
  });

  it('should identify Link Certificates stored in CSCA category', () => {
    const cert = makeCert({ dn: 'cn=abc,o=lc,c=KR,dc=data', type: 'DSC' });
    // lc maps to CSCA
    expect(getActualCertType(cert)).toBe('CSCA');
  });

  it('should identify MLSC from LDAP DN', () => {
    const cert = makeCert({ dn: 'cn=abc,o=mlsc,c=JP,dc=data', type: 'MLSC' });
    expect(getActualCertType(cert)).toBe('MLSC');
  });

  it('should identify DSC_NC from nc-data in DN', () => {
    const cert = makeCert({ dn: 'cn=abc,o=dsc,c=KR,dc=nc-data,dc=download', type: 'DSC' });
    expect(getActualCertType(cert)).toBe('DSC_NC');
  });

  it('should prioritize nc-data over o=dsc', () => {
    const cert = makeCert({ dn: 'cn=abc,o=nc-data,c=KR,dc=data', type: 'DSC' });
    expect(getActualCertType(cert)).toBe('DSC_NC');
  });

  it('should fallback to backend type when DN does not contain known OU', () => {
    const cert = makeCert({ dn: 'cn=abc,c=KR', type: 'CSCA' });
    expect(getActualCertType(cert)).toBe('CSCA');
  });
});

describe('isLinkCertificate', () => {
  it('should return true for non-self-signed cert in CSCA category', () => {
    const cert = makeCert({
      dn: 'cn=abc,o=csca,c=KR,dc=data',
      subjectDn: 'CN=Korea CSCA New,C=KR',
      issuerDn: 'CN=Korea CSCA Old,C=KR',
    });
    expect(isLinkCertificate(cert)).toBe(true);
  });

  it('should return false for self-signed CSCA', () => {
    const cert = makeCert({
      dn: 'cn=abc,o=csca,c=KR,dc=data',
      subjectDn: 'CN=Korea CSCA,C=KR',
      issuerDn: 'CN=Korea CSCA,C=KR',
    });
    expect(isLinkCertificate(cert)).toBe(false);
  });

  it('should return false for DSC certificates', () => {
    const cert = makeCert({
      dn: 'cn=abc,o=dsc,c=KR,dc=data',
      subjectDn: 'CN=Korea DSC,C=KR',
      issuerDn: 'CN=Korea CSCA,C=KR',
    });
    expect(isLinkCertificate(cert)).toBe(false);
  });
});

describe('isMasterListSignerCertificate', () => {
  it('should return true for self-signed cert in mlsc OU', () => {
    const cert = makeCert({
      dn: 'cn=abc,o=mlsc,c=JP,dc=data',
      subjectDn: 'CN=Japan MLSC,C=JP',
      issuerDn: 'CN=Japan MLSC,C=JP',
    });
    expect(isMasterListSignerCertificate(cert)).toBe(true);
  });

  it('should return false for non-self-signed cert in mlsc OU', () => {
    const cert = makeCert({
      dn: 'cn=abc,o=mlsc,c=JP,dc=data',
      subjectDn: 'CN=Japan MLSC,C=JP',
      issuerDn: 'CN=Japan CSCA,C=JP',
    });
    expect(isMasterListSignerCertificate(cert)).toBe(false);
  });

  it('should return false for self-signed cert in non-mlsc OU', () => {
    const cert = makeCert({
      dn: 'cn=abc,o=csca,c=KR,dc=data',
      subjectDn: 'CN=Korea CSCA,C=KR',
      issuerDn: 'CN=Korea CSCA,C=KR',
    });
    expect(isMasterListSignerCertificate(cert)).toBe(false);
  });
});

describe('getCertTypeDescription', () => {
  it('should return Link Certificate description for link certs', () => {
    const cert = makeCert({
      dn: 'cn=abc,o=csca,c=KR,dc=data',
      subjectDn: 'CN=New CSCA,C=KR',
      issuerDn: 'CN=Old CSCA,C=KR',
    });
    const desc = getCertTypeDescription('CSCA', cert);
    expect(desc).toContain('Doc 9303 Part 12');
    expect(desc).toContain('CSCA');
  });

  it('should return DSC description for DSC type', () => {
    const cert = makeCert();
    const desc = getCertTypeDescription('DSC', cert);
    expect(desc).toContain('eMRTD');
    expect(desc).toContain('Passive Authentication');
  });

  it('should return DSC_NC warning description', () => {
    const cert = makeCert({ dn: 'cn=abc,o=dsc,c=KR,dc=data' });
    const desc = getCertTypeDescription('DSC_NC', cert);
    expect(desc).toContain('nc-data');
  });

  it('should return CRL description for CRL type', () => {
    const cert = makeCert();
    const desc = getCertTypeDescription('CRL', cert);
    expect(desc).toContain('Certificate Revocation List');
  });

  it('should return the type string itself for unknown types', () => {
    const cert = makeCert();
    const desc = getCertTypeDescription('UNKNOWN_TYPE', cert);
    expect(desc).toBe('UNKNOWN_TYPE');
  });
});
