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

// --- Formatting ---

/** Format date string to Korean locale (YYYY. MM. DD.) */
export function formatDate(dateStr: string): string {
  return new Date(dateStr).toLocaleDateString('ko-KR', {
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
  });
}

/** Format X.509 version number (0 → v1, 1 → v2, 2 → v3) */
export function formatVersion(version: number | undefined): string {
  if (version === undefined) return 'Unknown';
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
    return 'ICAO Doc 9303 Part 12에 정의된 인증서로, 이전 CSCA와 새 CSCA 사이의 신뢰 체인을 연결합니다.\n\n사용 사례: CSCA 키 교체/갱신, 서명 알고리즘 마이그레이션 (RSA \u2192 ECDSA), 조직 정보 변경, CSCA 인프라 업그레이드';
  }
  if (isMasterListSignerCertificate(cert)) {
    return 'ICAO PKD에서 Master List CMS 구조에 디지털 서명하는데 사용되는 Self-signed 인증서입니다.\n\n특징: Subject DN = Issuer DN, Master List CMS SignerInfo에 포함, digitalSignature key usage (0x80 bit)';
  }

  switch (certType) {
    case 'CSCA':
      return 'ICAO Doc 9303 Part 12에 정의된 Self-signed 루트 인증서로, 여권 전자 칩에 서명하는 DSC를 발급하는 국가 최상위 인증기관입니다.\n\n역할: DSC 발급, 국가 PKI 신뢰 체인의 루트, 여권 검증 시 최상위 신뢰 앵커 (Trust Anchor)';
    case 'DSC':
      return 'ICAO Doc 9303 Part 12에 정의된 인증서로, 여권 전자 칩(eMRTD)의 데이터 그룹(DG1-DG16)에 디지털 서명하는데 사용됩니다.\n\n역할: 여권 데이터 그룹(DG) 서명 (SOD 생성), CSCA에 의해 발급, Passive Authentication 검증 대상, 유효기간: 3개월 ~ 3년';
    case 'DSC_NC':
      return 'ICAO 9303 기술 표준을 완전히 준수하지 않는 DSC입니다. ICAO PKD의 nc-data 컨테이너에 별도 저장됩니다.\n\n\u26a0\ufe0f 주의: 프로덕션 환경에서 사용 권장하지 않음, 일부 검증 시스템에서 거부될 수 있음, ICAO는 2021년부터 nc-data 폐기 권장';
    case 'MLSC':
      return 'ICAO PKD에서 Master List CMS 구조에 디지털 서명하는데 사용되는 Self-signed 인증서입니다.\n\n특징: Subject DN = Issuer DN, Master List CMS SignerInfo에 포함, digitalSignature key usage (0x80 bit)';
    case 'CRL':
      return '인증서 폐지 목록 (Certificate Revocation List) - 폐지된 인증서 목록을 담은 X.509 데이터 구조입니다.';
    case 'ML':
      return 'Master List - ICAO PKD에서 배포하는 국가별 CSCA 인증서 목록이 포함된 CMS SignedData 구조입니다.';
    default:
      return certType;
  }
}
