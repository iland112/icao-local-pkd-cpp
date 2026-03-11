/**
 * Certificate Display Utility Functions
 *
 * Pure helper functions for formatting, classifying, and displaying
 * X.509 certificate data. Shared between CertificateSearch page
 * and CertificateDetailDialog component.
 *
 * @version 1.0.0
 */

import type { Certificate } from '@/components/CertificateDetailDialog';
import i18n from '@/i18n';
export { formatDate } from '@/utils/dateFormat';

// --- Formatting ---

/** Format X.509 version number (0 → v1, 1 → v2, 2 → v3) */
export function formatVersion(version: number | undefined): string {
  if (version === undefined) return i18n.t('certificate:format.unknown');
  const versionMap: Record<number, string> = { 0: 'v1', 1: 'v2', 2: 'v3' };
  return versionMap[version] || `v${version + 1}`;
}

/** Simplify common signature algorithm OID names for display */
export function formatSignatureAlgorithm(algorithm: string | undefined): string {
  if (!algorithm) return 'N/A';

  const algorithmMap: Record<string, string> = {
    'sha256WithRSAEncryption': 'RSA-SHA256',
    'sha384WithRSAEncryption': 'RSA-SHA384',
    'sha512WithRSAEncryption': 'RSA-SHA512',
    'sha1WithRSAEncryption': 'RSA-SHA1',
    'ecdsa-with-SHA256': 'ECDSA-SHA256',
    'ecdsa-with-SHA384': 'ECDSA-SHA384',
    'ecdsa-with-SHA512': 'ECDSA-SHA512',
  };

  return algorithmMap[algorithm] || algorithm;
}

// --- Classification ---

/** Extract organization unit from LDAP DN (o=xxx) */
export function getOrganizationUnit(dn: string): string {
  const match = dn.match(/o=([^,]+)/i);
  return match ? match[1] : '';
}

/**
 * Get actual certificate type from LDAP DN.
 * Backend may misclassify Link Certificate CSCA as DSC.
 * Uses LDAP DN (o=csca, o=lc, o=dsc, o=mlsc) as source of truth.
 */
export function getActualCertType(cert: Certificate): 'CSCA' | 'DSC' | 'DSC_NC' | 'MLSC' | 'UNKNOWN' {
  const ou = getOrganizationUnit(cert.dn).toLowerCase();

  // Check nc-data FIRST (DSC_NC certificates have both o=dsc AND dc=nc-data)
  if (ou === 'nc-data' || cert.dn.includes('nc-data')) {
    return 'DSC_NC';
  } else if (ou === 'csca' || ou === 'lc') {
    return 'CSCA';
  } else if (ou === 'mlsc') {
    return 'MLSC';
  } else if (ou === 'dsc') {
    return 'DSC';
  }

  // Fallback to backend type field
  return cert.type as 'CSCA' | 'DSC' | 'DSC_NC' | 'MLSC' | 'UNKNOWN';
}

/**
 * Check if certificate is a Link Certificate.
 * Link Certificate: NOT self-signed (subjectDn != issuerDn) AND stored in CSCA category.
 */
export function isLinkCertificate(cert: Certificate): boolean {
  const actualType = getActualCertType(cert);
  return actualType === 'CSCA' && cert.subjectDn !== cert.issuerDn;
}

/**
 * Check if certificate is a Master List Signer Certificate.
 * MLSC: Self-signed (subjectDn = issuerDn) AND stored in o=mlsc.
 */
export function isMasterListSignerCertificate(cert: Certificate): boolean {
  const ou = getOrganizationUnit(cert.dn);
  return cert.subjectDn === cert.issuerDn && ou === 'mlsc';
}

/** Get certificate type description for tooltip display */
export function getCertTypeDescription(certType: string, cert: Certificate): string {
  if (isLinkCertificate(cert)) {
    return i18n.t('certificate:typeDescription.linkCert');
  }
  if (isMasterListSignerCertificate(cert)) {
    return i18n.t('certificate:typeDescription.mlsc');
  }

  const typeKeyMap: Record<string, string> = {
    'CSCA': 'csca',
    'DSC': 'dsc',
    'DSC_NC': 'dscNc',
    'MLSC': 'mlsc',
    'CRL': 'crl',
    'ML': 'ml',
  };

  const key = typeKeyMap[certType];
  if (key) {
    return i18n.t(`certificate:typeDescription.${key}`);
  }

  return certType;
}
