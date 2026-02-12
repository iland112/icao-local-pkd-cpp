import React, { useState, useEffect, useMemo } from 'react';
import { Search, Download, Filter, ChevronDown, ChevronUp, FileText, X, CheckCircle, XCircle, Clock, RefreshCw, Eye, ChevronLeft, ChevronRight, Shield, HelpCircle } from 'lucide-react';
import { getFlagSvgPath } from '@/utils/countryCode';
import { getCountryDisplayName } from '@/utils/countryNames';
import { cn } from '@/utils/cn';
import { TrustChainVisualization } from '@/components/TrustChainVisualization';
import { validationApi } from '@/api/validationApi';
import type { ValidationResult } from '@/types/validation';
import { TreeViewer } from '@/components/TreeViewer';
import type { TreeNode } from '@/components/TreeViewer';

interface DnComponents {
  commonName?: string;
  organization?: string;
  organizationalUnit?: string;
  locality?: string;
  stateOrProvince?: string;
  country?: string;
  email?: string;
  serialNumber?: string;
}

interface Certificate {
  dn: string;
  cn: string;
  sn: string;
  country: string;
  type: string;  // Changed from certType to type (backend uses 'type')
  subjectDn: string;
  issuerDn: string;
  fingerprint: string;
  validFrom: string;
  validTo: string;
  validity: 'VALID' | 'EXPIRED' | 'NOT_YET_VALID' | 'UNKNOWN';
  isSelfSigned: boolean;
  // DSC_NC specific attributes
  pkdConformanceCode?: string;
  pkdConformanceText?: string;
  pkdVersion?: string;

  // DN Components (parsed, clean format)
  subjectDnComponents?: DnComponents;
  issuerDnComponents?: DnComponents;

  // X.509 Metadata (v2.3.0) - 15 fields
  version?: number;                              // 0=v1, 1=v2, 2=v3
  signatureAlgorithm?: string;                   // "sha256WithRSAEncryption"
  signatureHashAlgorithm?: string;               // "SHA-256"
  publicKeyAlgorithm?: string;                   // "RSA", "ECDSA"
  publicKeySize?: number;                        // 2048, 4096 (bits)
  publicKeyCurve?: string;                       // "prime256v1" (ECDSA)
  keyUsage?: string[];                           // ["digitalSignature", "keyCertSign"]
  extendedKeyUsage?: string[];                   // ["serverAuth", "clientAuth"]
  isCA?: boolean;                                // TRUE if CA certificate
  pathLenConstraint?: number;                    // Path length constraint
  subjectKeyIdentifier?: string;                 // SKI (hex)
  authorityKeyIdentifier?: string;               // AKI (hex)
  crlDistributionPoints?: string[];              // CRL URLs
  ocspResponderUrl?: string;                     // OCSP URL
  isCertSelfSigned?: boolean;                    // Self-signed flag from X.509
}

interface SearchCriteria {
  country: string;
  certType: string;
  validity: string;
  source: string;
  searchTerm: string;
  limit: number;
  offset: number;
}

const CertificateSearch: React.FC = () => {
  const [certificates, setCertificates] = useState<Certificate[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [total, setTotal] = useState(0);
  const [countries, setCountries] = useState<string[]>([]);
  const [countriesLoading, setCountriesLoading] = useState(false);
  const [apiStats, setApiStats] = useState<{ total: number; valid: number; expired: number; notYetValid: number; unknown: number } | null>(null);

  // Search criteria
  const [criteria, setCriteria] = useState<SearchCriteria>({
    country: '',
    certType: '',
    validity: 'all',
    source: '',
    searchTerm: '',
    limit: 50,
    offset: 0,
  });

  // UI state
  const [showFilters, setShowFilters] = useState(true);
  const [selectedCert, setSelectedCert] = useState<Certificate | null>(null);
  const [showDetailDialog, setShowDetailDialog] = useState(false);
  const [detailTab, setDetailTab] = useState<'general' | 'details'>('general');
  const [validationResult, setValidationResult] = useState<ValidationResult | null>(null);
  const [validationLoading, setValidationLoading] = useState(false);

  // Fetch available countries
  const fetchCountries = async () => {
    setCountriesLoading(true);
    try {
      console.log('Fetching countries from /api/certificates/countries...');
      const response = await fetch('/api/certificates/countries');
      const data = await response.json();

      console.log('Countries API response:', data);

      if (data.success && data.countries) {
        setCountries(data.countries);
        console.log(`Loaded ${data.countries.length} countries`);
      } else {
        console.error('Countries API returned unsuccessful:', data);
      }
    } catch (err) {
      console.error('Failed to fetch countries:', err);
    } finally {
      setCountriesLoading(false);
    }
  };

  // Search certificates
  const searchCertificates = async () => {
    setLoading(true);
    setError(null);

    try {
      const params = new URLSearchParams();
      if (criteria.country) params.append('country', criteria.country);
      if (criteria.certType) params.append('certType', criteria.certType);
      if (criteria.validity && criteria.validity !== 'all') params.append('validity', criteria.validity);
      if (criteria.source) params.append('source', criteria.source);
      if (criteria.searchTerm) params.append('searchTerm', criteria.searchTerm);
      params.append('limit', criteria.limit.toString());
      params.append('offset', criteria.offset.toString());

      const response = await fetch(`/api/certificates/search?${params.toString()}`);
      const data = await response.json();

      if (data.success) {
        setCertificates(data.certificates);
        setTotal(data.total);
        // Store statistics from backend (if available)
        if (data.stats) {
          setApiStats(data.stats);
        }
      } else {
        setError(data.error || 'Search failed');
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to search certificates');
    } finally {
      setLoading(false);
    }
  };

  // Initial load - fetch countries once
  useEffect(() => {
    fetchCountries();
  }, []);

  // Search when criteria changes
  useEffect(() => {
    searchCertificates();
  }, [criteria.country, criteria.certType, criteria.validity, criteria.source, criteria.searchTerm, criteria.limit, criteria.offset]);

  // Handle search button click
  const handleSearch = () => {
    setCriteria({ ...criteria, offset: 0 });
    searchCertificates();
  };

  // Handle pagination
  const handlePrevPage = () => {
    if (criteria.offset >= criteria.limit) {
      setCriteria({ ...criteria, offset: criteria.offset - criteria.limit });
    }
  };

  const handleNextPage = () => {
    if (criteria.offset + criteria.limit < total) {
      setCriteria({ ...criteria, offset: criteria.offset + criteria.limit });
    }
  };

  // View certificate details
  const viewDetails = async (cert: Certificate) => {
    setSelectedCert(cert);
    setShowDetailDialog(true);
    setValidationResult(null);
    setValidationLoading(true);

    // Fetch validation result by fingerprint
    try {
      const response = await validationApi.getCertificateValidation(cert.fingerprint);
      if (response.success && response.validation) {
        setValidationResult(response.validation);
      }
    } catch (err) {
      console.error('Failed to fetch validation result:', err);
    } finally {
      setValidationLoading(false);
    }
  };

  // Export single certificate
  const exportCertificate = async (dn: string, format: 'der' | 'pem') => {
    try {
      const params = new URLSearchParams({ dn, format });
      const response = await fetch(`/api/certificates/export/file?${params.toString()}`);

      if (response.ok) {
        const blob = await response.blob();
        const contentDisposition = response.headers.get('Content-Disposition');
        const filename = contentDisposition?.match(/filename="(.+)"/)?.[1] || `certificate.${format}`;

        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        window.URL.revokeObjectURL(url);
        document.body.removeChild(a);
      } else {
        alert('Export failed');
      }
    } catch (err) {
      alert('Export failed: ' + (err instanceof Error ? err.message : 'Unknown error'));
    }
  };

  // Export country certificates (ZIP)
  const exportCountry = async (country: string, format: 'der' | 'pem') => {
    try {
      const params = new URLSearchParams({ country, format });
      const response = await fetch(`/api/certificates/export/country?${params.toString()}`);

      if (response.ok) {
        const blob = await response.blob();
        const contentDisposition = response.headers.get('Content-Disposition');
        const filename = contentDisposition?.match(/filename="(.+)"/)?.[1] || `${country}_certificates.zip`;

        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        window.URL.revokeObjectURL(url);
        document.body.removeChild(a);
      } else {
        alert('Export failed');
      }
    } catch (err) {
      alert('Export failed: ' + (err instanceof Error ? err.message : 'Unknown error'));
    }
  };

  // Format date
  const formatDate = (dateStr: string) => {
    return new Date(dateStr).toLocaleDateString('ko-KR', {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
    });
  };

  // Format X.509 version
  const formatVersion = (version: number | undefined): string => {
    if (version === undefined) return 'Unknown';
    const versionMap: { [key: number]: string } = { 0: 'v1', 1: 'v2', 2: 'v3' };
    return versionMap[version] || `v${version + 1}`;
  };

  // Format signature algorithm for display
  const formatSignatureAlgorithm = (algorithm: string | undefined): string => {
    if (!algorithm) return 'N/A';

    // Simplify common algorithm names
    const algorithmMap: { [key: string]: string } = {
      'sha256WithRSAEncryption': 'RSA-SHA256',
      'sha384WithRSAEncryption': 'RSA-SHA384',
      'sha512WithRSAEncryption': 'RSA-SHA512',
      'sha1WithRSAEncryption': 'RSA-SHA1',
      'ecdsa-with-SHA256': 'ECDSA-SHA256',
      'ecdsa-with-SHA384': 'ECDSA-SHA384',
      'ecdsa-with-SHA512': 'ECDSA-SHA512',
    };

    return algorithmMap[algorithm] || algorithm;
  };

  // Build trust chain tree from validation result
  const buildTrustChainTree = (trustChainPath: string): TreeNode[] => {
    // Parse trust chain path: "DSC → CSCA Latvia → Root CSCA"
    const parts = trustChainPath.split('→').map(s => s.trim());

    if (parts.length === 0) return [];

    // Build tree structure (reverse order - root at top)
    const nodes: TreeNode[] = [];
    const reversedParts = [...parts].reverse();

    reversedParts.forEach((part, index) => {
      const isRoot = index === 0;
      const isLeaf = index === reversedParts.length - 1;

      // Extract meaningful info from DN
      let displayName = part;
      let icon = 'shield-check';

      // Detect certificate type
      if (part.startsWith('DSC')) {
        icon = 'hard-drive';
        displayName = part.replace('DSC', 'Document Signer Certificate');
      } else if (part.includes('CSCA') || part.includes('CN=')) {
        icon = isRoot ? 'shield-check' : 'shield';
        // Extract CN from DN if present
        const cnMatch = part.match(/CN=([^,]+)/);
        if (cnMatch) {
          displayName = cnMatch[1];
        }
      }

      nodes.push({
        id: `chain-${index}`,
        name: isRoot ? '🔒 Root CA' : isLeaf ? '📄 Certificate' : '🔗 Intermediate CA',
        value: displayName,
        icon: icon as any,
      });
    });

    return nodes;
  };

  // Build certificate tree data for react-arborist
  const buildCertificateTree = (cert: Certificate): TreeNode[] => {
    const children: TreeNode[] = [];

    // Version
    children.push({
      id: 'version',
      name: 'Version',
      value: formatVersion(cert.version),
      icon: 'file-text',
    });

    // Serial Number
    children.push({
      id: 'serial',
      name: 'Serial Number',
      value: cert.sn,
      icon: 'hash',
    });

    // Signature
    if (cert.signatureAlgorithm || cert.signatureHashAlgorithm) {
      const sigChildren: TreeNode[] = [];
      if (cert.signatureAlgorithm) {
        sigChildren.push({ id: 'sig-algo', name: 'Algorithm', value: cert.signatureAlgorithm });
      }
      if (cert.signatureHashAlgorithm) {
        sigChildren.push({ id: 'hash-algo', name: 'Hash', value: cert.signatureHashAlgorithm });
      }
      children.push({
        id: 'signature',
        name: 'Signature',
        children: sigChildren,
        icon: 'shield',
      });
    }

    // Issuer
    children.push({
      id: 'issuer',
      name: 'Issuer',
      value: cert.issuerDn,
      icon: 'user',
    });

    // Validity
    children.push({
      id: 'validity',
      name: 'Validity',
      children: [
        { id: 'valid-from', name: 'Not Before', value: formatDate(cert.validFrom) },
        { id: 'valid-to', name: 'Not After', value: formatDate(cert.validTo) },
      ],
      icon: 'calendar',
    });

    // Subject
    children.push({
      id: 'subject',
      name: 'Subject',
      value: cert.subjectDn,
      icon: 'user',
    });

    // Public Key
    if (cert.publicKeyAlgorithm || cert.publicKeySize) {
      const pkChildren: TreeNode[] = [];
      if (cert.publicKeyAlgorithm) {
        pkChildren.push({ id: 'pk-algo', name: 'Algorithm', value: cert.publicKeyAlgorithm });
      }
      if (cert.publicKeySize) {
        pkChildren.push({ id: 'pk-size', name: 'Key Size', value: `${cert.publicKeySize} bits` });
      }
      if (cert.publicKeyCurve) {
        pkChildren.push({ id: 'pk-curve', name: 'Curve', value: cert.publicKeyCurve });
      }
      children.push({
        id: 'public-key',
        name: 'Public Key',
        children: pkChildren,
        icon: 'key',
      });
    }

    // Extensions
    const extChildren: TreeNode[] = [];

    // Key Usage
    if (cert.keyUsage && cert.keyUsage.length > 0) {
      extChildren.push({
        id: 'key-usage',
        name: 'Key Usage',
        value: cert.keyUsage.join(', '),
        icon: 'lock',
      });
    }

    // Extended Key Usage
    if (cert.extendedKeyUsage && cert.extendedKeyUsage.length > 0) {
      extChildren.push({
        id: 'ext-key-usage',
        name: 'Extended Key Usage',
        value: cert.extendedKeyUsage.join(', '),
        icon: 'lock',
      });
    }

    // Basic Constraints
    if (cert.isCA !== undefined || cert.pathLenConstraint !== undefined) {
      const bcChildren: TreeNode[] = [];
      if (cert.isCA !== undefined) {
        bcChildren.push({ id: 'is-ca', name: 'CA', value: cert.isCA ? 'TRUE' : 'FALSE' });
      }
      if (cert.pathLenConstraint !== undefined) {
        bcChildren.push({ id: 'path-len', name: 'Path Length', value: String(cert.pathLenConstraint) });
      }
      extChildren.push({
        id: 'basic-constraints',
        name: 'Basic Constraints',
        children: bcChildren,
        icon: 'shield-check',
      });
    }

    // Subject Key Identifier
    if (cert.subjectKeyIdentifier) {
      extChildren.push({
        id: 'ski',
        name: 'Subject Key Identifier',
        value: cert.subjectKeyIdentifier,
        copyable: true,
        icon: 'hash',
      });
    }

    // Authority Key Identifier
    if (cert.authorityKeyIdentifier) {
      extChildren.push({
        id: 'aki',
        name: 'Authority Key Identifier',
        value: cert.authorityKeyIdentifier,
        copyable: true,
        icon: 'hash',
      });
    }

    // CRL Distribution Points
    if (cert.crlDistributionPoints && cert.crlDistributionPoints.length > 0) {
      const crlChildren: TreeNode[] = cert.crlDistributionPoints.map((url, index) => ({
        id: `crl-${index}`,
        name: `URL ${index + 1}`,
        value: url,
        linkUrl: url,
        icon: 'link-2',
      }));
      extChildren.push({
        id: 'crl-dist',
        name: 'CRL Distribution Points',
        children: crlChildren,
        icon: 'file-text',
      });
    }

    // OCSP Responder
    if (cert.ocspResponderUrl) {
      extChildren.push({
        id: 'ocsp',
        name: 'OCSP Responder',
        value: cert.ocspResponderUrl,
        linkUrl: cert.ocspResponderUrl,
        icon: 'link-2',
      });
    }

    if (extChildren.length > 0) {
      children.push({
        id: 'extensions',
        name: 'Extensions',
        children: extChildren,
        icon: 'settings',
      });
    }

    return [{
      id: 'certificate',
      name: 'Certificate',
      children,
      icon: 'file-text',
    }];
  };

  // Helper: Extract organization unit from DN (o=xxx)
  const getOrganizationUnit = (dn: string): string => {
    const match = dn.match(/o=([^,]+)/i);
    return match ? match[1] : '';
  };

  // Helper: Get actual certificate type from LDAP DN
  // Backend may misclassify Link Certificate CSCA as DSC
  // Use LDAP DN (o=csca, o=lc, o=dsc, o=mlsc) as source of truth
  const getActualCertType = (cert: Certificate): 'CSCA' | 'DSC' | 'DSC_NC' | 'MLSC' | 'UNKNOWN' => {
    const ou = getOrganizationUnit(cert.dn).toLowerCase();

    // Check nc-data FIRST (DSC_NC certificates have both o=dsc AND dc=nc-data)
    if (ou === 'nc-data' || cert.dn.includes('nc-data')) {
      return 'DSC_NC';
    } else if (ou === 'csca' || ou === 'lc') {
      return 'CSCA';  // Both o=csca and o=lc are CSCA certificates
    } else if (ou === 'mlsc') {
      return 'MLSC';
    } else if (ou === 'dsc') {
      return 'DSC';
    }

    // Fallback to backend type field
    return cert.type as 'CSCA' | 'DSC' | 'DSC_NC' | 'MLSC' | 'UNKNOWN';
  };

  // Helper: Check if certificate is a Link Certificate
  // Link Certificate: NOT self-signed (subjectDn != issuerDn) AND stored in CSCA category
  const isLinkCertificate = (cert: Certificate): boolean => {
    const actualType = getActualCertType(cert);
    return actualType === 'CSCA' && cert.subjectDn !== cert.issuerDn;
  };

  // Helper: Check if certificate is a Master List Signer Certificate
  // MLSC: Self-signed (subjectDn = issuerDn) AND stored in o=mlsc
  const isMasterListSignerCertificate = (cert: Certificate): boolean => {
    const ou = getOrganizationUnit(cert.dn);
    return cert.subjectDn === cert.issuerDn && ou === 'mlsc';
  };

  // Calculate statistics
  const stats = useMemo(() => {
    // Use backend statistics if available, otherwise fallback to client-side calculation
    if (apiStats && apiStats.total > 0) {
      return {
        total: apiStats.total,
        valid: apiStats.valid,
        expired: apiStats.expired,
        notYetValid: apiStats.notYetValid,
        unknown: apiStats.unknown,
        validPercent: apiStats.total > 0 ? Math.round((apiStats.valid / apiStats.total) * 100) : 0,
        expiredPercent: apiStats.total > 0 ? Math.round((apiStats.expired / apiStats.total) * 100) : 0,
      };
    }

    // Fallback: Calculate from current page certificates (legacy behavior)
    const valid = certificates.filter((c) => c.validity === 'VALID').length;
    const expired = certificates.filter((c) => c.validity === 'EXPIRED').length;
    const notYetValid = certificates.filter((c) => c.validity === 'NOT_YET_VALID').length;
    const unknown = certificates.filter((c) => c.validity === 'UNKNOWN').length;

    return {
      total,
      valid,
      expired,
      notYetValid,
      unknown,
      validPercent: total > 0 ? Math.round((valid / total) * 100) : 0,
      expiredPercent: total > 0 ? Math.round((expired / total) * 100) : 0,
    };
  }, [apiStats, certificates, total]);

  // Get certificate type description for tooltip
  const getCertTypeDescription = (certType: string, cert: Certificate): string => {
    if (isLinkCertificate(cert)) {
      return 'ICAO Doc 9303 Part 12에 정의된 인증서로, 이전 CSCA와 새 CSCA 사이의 신뢰 체인을 연결합니다.\n\n사용 사례: CSCA 키 교체/갱신, 서명 알고리즘 마이그레이션 (RSA → ECDSA), 조직 정보 변경, CSCA 인프라 업그레이드';
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
        return 'ICAO 9303 기술 표준을 완전히 준수하지 않는 DSC입니다. ICAO PKD의 nc-data 컨테이너에 별도 저장됩니다.\n\n⚠️ 주의: 프로덕션 환경에서 사용 권장하지 않음, 일부 검증 시스템에서 거부될 수 있음, ICAO는 2021년부터 nc-data 폐기 권장';
      case 'MLSC':
        return 'ICAO PKD에서 Master List CMS 구조에 디지털 서명하는데 사용되는 Self-signed 인증서입니다.\n\n특징: Subject DN = Issuer DN, Master List CMS SignerInfo에 포함, digitalSignature key usage (0x80 bit)';
      case 'CRL':
        return '인증서 폐지 목록 (Certificate Revocation List) - 폐지된 인증서 목록을 담은 X.509 데이터 구조입니다.';
      case 'ML':
        return 'Master List - ICAO PKD에서 배포하는 국가별 CSCA 인증서 목록이 포함된 CMS SignedData 구조입니다.';
      default:
        return certType;
    }
  };

  // Get certificate type badge with tooltip
  const getCertTypeBadge = (certType: string, cert?: Certificate) => {
    const badges: Record<string, React.ReactElement> = {
      'CSCA': (
        <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-blue-100 dark:bg-blue-900/40 text-blue-800 dark:text-blue-300 border border-blue-200 dark:border-blue-700">
          CSCA
        </span>
      ),
      'MLSC': (
        <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-purple-100 dark:bg-purple-900/40 text-purple-800 dark:text-purple-300 border border-purple-200 dark:border-purple-700">
          MLSC
        </span>
      ),
      'DSC': (
        <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-green-100 dark:bg-green-900/40 text-green-800 dark:text-green-300 border border-green-200 dark:border-green-700">
          DSC
        </span>
      ),
      'DSC_NC': (
        <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-orange-100 dark:bg-orange-900/40 text-orange-800 dark:text-orange-300 border border-orange-200 dark:border-orange-700">
          DSC_NC
        </span>
      ),
      'CRL': (
        <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-red-100 dark:bg-red-900/40 text-red-800 dark:text-red-300 border border-red-200 dark:border-red-700">
          CRL
        </span>
      ),
      'ML': (
        <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-indigo-100 dark:bg-indigo-900/40 text-indigo-800 dark:text-indigo-300 border border-indigo-200 dark:border-indigo-700">
          ML
        </span>
      ),
    };

    const badge = badges[certType] || (
      <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-gray-100 dark:bg-gray-700 text-gray-800 dark:text-gray-300 border border-gray-200 dark:border-gray-600">
        {certType}
      </span>
    );

    // Add tooltip icon if certificate object is provided
    if (cert) {
      const description = getCertTypeDescription(certType, cert);
      return (
        <div className="inline-flex items-center gap-1.5">
          {badge}
          <div className="relative group">
            <HelpCircle className="w-4 h-4 text-gray-400 dark:text-gray-500 hover:text-gray-600 dark:hover:text-gray-300 cursor-help transition-colors" />
            <div className="absolute left-1/2 -translate-x-1/2 top-full mt-2 hidden group-hover:block z-50 w-80">
              <div className="bg-gray-900 dark:bg-gray-100 text-white dark:text-gray-900 text-xs rounded-lg p-3 shadow-lg">
                <div className="absolute left-1/2 -translate-x-1/2 bottom-full w-0 h-0 border-l-4 border-r-4 border-b-4 border-transparent border-b-gray-900 dark:border-b-gray-100"></div>
                <div className="whitespace-pre-line leading-relaxed">{description}</div>
              </div>
            </div>
          </div>
        </div>
      );
    }

    return badge;
  };

  // Get validity badge
  const getValidityBadge = (validity: string) => {
    switch (validity) {
      case 'VALID':
        return (
          <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400">
            <CheckCircle className="w-3 h-3 mr-1" />
            유효
          </span>
        );
      case 'EXPIRED':
        return (
          <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400">
            <XCircle className="w-3 h-3 mr-1" />
            만료
          </span>
        );
      case 'NOT_YET_VALID':
        return (
          <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-yellow-100 dark:bg-yellow-900/30 text-yellow-700 dark:text-yellow-400">
            <Clock className="w-3 h-3 mr-1" />
            유효 전
          </span>
        );
      default:
        return (
          <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-gray-100 dark:bg-gray-700 text-gray-700 dark:text-gray-400">
            알 수 없음
          </span>
        );
    }
  };

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-blue-500 to-indigo-600 shadow-lg">
            <Shield className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">인증서 조회</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              LDAP에 저장된 PKD 인증서를 검색하고 내보내기
            </p>
          </div>
          <button
            onClick={() => {
              setCriteria({ ...criteria, offset: 0 });
              searchCertificates();
            }}
            disabled={loading}
            className="inline-flex items-center gap-2 px-3 py-2 rounded-lg text-sm font-medium text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
          >
            <RefreshCw className={cn('w-4 h-4', loading && 'animate-spin')} />
          </button>
        </div>
      </div>

      {/* Filters Card */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md mb-4 p-4">
        <div className="flex items-center gap-2 mb-3">
          <Filter className="w-4 h-4 text-blue-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">검색 필터</h3>
          <button
            onClick={() => setShowFilters(!showFilters)}
            className="ml-auto p-1 hover:bg-gray-100 dark:hover:bg-gray-700 rounded transition-colors"
          >
            {showFilters ? <ChevronUp className="w-4 h-4" /> : <ChevronDown className="w-4 h-4" />}
          </button>
        </div>

        {showFilters && (
          <div className="space-y-3">
            <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-7 gap-3">
              {/* Country - wider column */}
              <div className="lg:col-span-2">
                <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                  국가
                </label>
                <div className="relative">
                  <select
                    value={criteria.country}
                    onChange={(e) => setCriteria({ ...criteria, country: e.target.value })}
                    className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 appearance-none"
                    style={criteria.country && getFlagSvgPath(criteria.country) ? { paddingLeft: '2.5rem' } : {}}
                    disabled={countriesLoading}
                  >
                    <option value="">
                      {countriesLoading ? 'Loading...' : countries.length === 0 ? 'No countries' : '전체 국가'}
                    </option>
                    {countries.map((country) => (
                      <option key={country} value={country}>
                        {getCountryDisplayName(country)}
                      </option>
                    ))}
                  </select>
                  {criteria.country && getFlagSvgPath(criteria.country) && (
                    <img
                      src={getFlagSvgPath(criteria.country)}
                      alt={criteria.country}
                      className="absolute left-2 top-1/2 transform -translate-y-1/2 w-6 h-4 object-cover rounded shadow-sm border border-gray-300 pointer-events-none"
                      onError={(e) => {
                        e.currentTarget.style.display = 'none';
                      }}
                    />
                  )}
                </div>
              </div>

              {/* Certificate Type */}
              <div>
                <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                  인증서 종류
                </label>
                <select
                  value={criteria.certType}
                  onChange={(e) => setCriteria({ ...criteria, certType: e.target.value })}
                  className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                >
                  <option value="">전체</option>
                  <option value="CSCA">CSCA</option>
                  <option value="MLSC">MLSC</option>
                  <option value="DSC">DSC</option>
                  <option value="DSC_NC">DSC_NC</option>
                </select>
              </div>

              {/* Validity */}
              <div>
                <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                  유효성
                </label>
                <select
                  value={criteria.validity}
                  onChange={(e) => setCriteria({ ...criteria, validity: e.target.value })}
                  className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                >
                  <option value="all">전체</option>
                  <option value="VALID">유효</option>
                  <option value="EXPIRED">만료</option>
                  <option value="NOT_YET_VALID">유효 전</option>
                </select>
              </div>

              {/* Source */}
              <div>
                <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                  출처
                </label>
                <select
                  value={criteria.source}
                  onChange={(e) => setCriteria({ ...criteria, source: e.target.value })}
                  className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                >
                  <option value="">전체</option>
                  <option value="LDIF_PARSED">LDIF 업로드</option>
                  <option value="ML_PARSED">Master List</option>
                  <option value="FILE_UPLOAD">파일 업로드</option>
                  <option value="PA_EXTRACTED">PA 검증 추출</option>
                  <option value="DL_PARSED">편차 목록</option>
                </select>
              </div>

              {/* Limit */}
              <div>
                <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                  표시 개수
                </label>
                <select
                  value={criteria.limit}
                  onChange={(e) => setCriteria({ ...criteria, limit: Number(e.target.value), offset: 0 })}
                  className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                >
                  <option value="10">10</option>
                  <option value="25">25</option>
                  <option value="50">50</option>
                  <option value="100">100</option>
                  <option value="200">200</option>
                </select>
              </div>

              {/* Search */}
              <div>
                <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                  키워드 검색
                </label>
                <div className="relative">
                  <Search className="absolute left-2.5 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                  <input
                    type="text"
                    placeholder="CN 검색..."
                    value={criteria.searchTerm}
                    onChange={(e) => setCriteria({ ...criteria, searchTerm: e.target.value })}
                    onKeyPress={(e) => e.key === 'Enter' && handleSearch()}
                    className="w-full pl-8 pr-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                  />
                </div>
              </div>
            </div>

            {/* Action Buttons */}
            <div className="flex gap-2 pt-2 border-t border-gray-200 dark:border-gray-700">
              <button
                onClick={handleSearch}
                disabled={loading}
                className="px-4 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700 disabled:bg-gray-400 disabled:cursor-not-allowed flex items-center gap-2 text-sm font-medium transition-colors"
              >
                <Search className="w-4 h-4" />
                검색
              </button>
              {criteria.country && (
                <>
                  <button
                    onClick={() => exportCountry(criteria.country, 'pem')}
                    className="px-4 py-2 bg-green-600 text-white rounded-lg hover:bg-green-700 flex items-center gap-2 text-sm font-medium transition-colors"
                  >
                    <Download className="w-4 h-4" />
                    {criteria.country} PEM ZIP
                  </button>
                  <button
                    onClick={() => exportCountry(criteria.country, 'der')}
                    className="px-4 py-2 bg-green-700 text-white rounded-lg hover:bg-green-800 flex items-center gap-2 text-sm font-medium transition-colors"
                  >
                    <Download className="w-4 h-4" />
                    {criteria.country} DER ZIP
                  </button>
                </>
              )}
            </div>
          </div>
        )}
      </div>

      {/* Statistics Cards - Hierarchical Layout */}
      <div className="mb-4">
        {/* Main Total Card */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-5 border-l-4 border-blue-500">
          <div className="flex items-center gap-3 mb-4">
            <div className="p-2 rounded-lg bg-blue-50 dark:bg-blue-900/30">
              <Shield className="w-6 h-6 text-blue-500" />
            </div>
            <div className="flex-1">
              <div className="flex items-center gap-2">
                {criteria.country && getFlagSvgPath(criteria.country) && (
                  <img
                    src={getFlagSvgPath(criteria.country)}
                    alt={criteria.country}
                    className="w-6 h-4 object-cover rounded shadow-sm border border-gray-300 dark:border-gray-500"
                    onError={(e) => {
                      e.currentTarget.style.display = 'none';
                    }}
                  />
                )}
                <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">
                  {criteria.country ? `${criteria.country} 총 인증서` : '총 인증서'}
                </p>
              </div>
              <p className="text-2xl font-bold text-blue-600 dark:text-blue-400">{stats.total.toLocaleString()}</p>
            </div>
          </div>

          {/* Breakdown Cards - Nested */}
          <div className="grid grid-cols-3 gap-3 pt-4 border-t border-gray-200 dark:border-gray-700">
            {/* Valid */}
            <div className="bg-green-50 dark:bg-green-900/20 rounded-lg p-3 border border-green-200 dark:border-green-700">
              <div className="flex items-center gap-2 mb-1">
                <CheckCircle className="w-4 h-4 text-green-500" />
                <p className="text-xs text-gray-600 dark:text-gray-400 font-medium">유효</p>
              </div>
              <p className="text-lg font-bold text-green-600 dark:text-green-400">{stats.valid.toLocaleString()}</p>
              <p className="text-xs text-gray-500 dark:text-gray-400">{stats.validPercent}%</p>
            </div>

            {/* Expired */}
            <div className="bg-red-50 dark:bg-red-900/20 rounded-lg p-3 border border-red-200 dark:border-red-700">
              <div className="flex items-center gap-2 mb-1">
                <XCircle className="w-4 h-4 text-red-500" />
                <p className="text-xs text-gray-600 dark:text-gray-400 font-medium">만료</p>
              </div>
              <p className="text-lg font-bold text-red-600 dark:text-red-400">{stats.expired.toLocaleString()}</p>
              <p className="text-xs text-gray-500 dark:text-gray-400">{stats.expiredPercent}%</p>
            </div>

            {/* Not Yet Valid */}
            <div className="bg-yellow-50 dark:bg-yellow-900/20 rounded-lg p-3 border border-yellow-200 dark:border-yellow-700">
              <div className="flex items-center gap-2 mb-1">
                <Clock className="w-4 h-4 text-yellow-500" />
                <p className="text-xs text-gray-600 dark:text-gray-400 font-medium">유효 전</p>
              </div>
              <p className="text-lg font-bold text-yellow-600 dark:text-yellow-400">{stats.notYetValid.toLocaleString()}</p>
            </div>
          </div>
        </div>
      </div>

      {/* Results Table */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        {/* Table Header */}
        <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center justify-between">
          <div className="flex items-center gap-2">
            <FileText className="w-4 h-4 text-blue-500" />
            <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">검색 결과</h3>
            <span className="px-2 py-0.5 text-xs rounded-full bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300">
              {certificates.length}건
            </span>
          </div>
          <div className="flex items-center gap-2">
            <button
              onClick={handlePrevPage}
              disabled={criteria.offset === 0}
              className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
            >
              <ChevronLeft className="w-4 h-4" />
            </button>
            <span className="text-xs text-gray-500 dark:text-gray-400">
              {criteria.offset + 1}-{Math.min(criteria.offset + criteria.limit, total)} / {total.toLocaleString()}
            </span>
            <button
              onClick={handleNextPage}
              disabled={criteria.offset + criteria.limit >= total}
              className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
            >
              <ChevronRight className="w-4 h-4" />
            </button>
          </div>
        </div>

        {/* Loading State */}
        {loading && (
          <div className="flex items-center justify-center py-20">
            <RefreshCw className="w-8 h-8 animate-spin text-blue-500" />
            <p className="ml-3 text-gray-600 dark:text-gray-400">검색 중...</p>
          </div>
        )}

        {/* Error State */}
        {error && (
          <div className="p-12 text-center">
            <div className="text-red-600 dark:text-red-400 font-semibold">{error}</div>
          </div>
        )}

        {/* Results Table */}
        {!loading && !error && certificates.length > 0 && (
          <div className="overflow-x-auto">
            <table className="w-full border-collapse">
              <thead className="bg-slate-100 dark:bg-gray-700/50 border-b-2 border-gray-300 dark:border-gray-600">
                <tr>
                  <th className="px-6 py-3 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider border-r border-gray-200 dark:border-gray-600">
                    국가
                  </th>
                  <th className="px-6 py-3 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider border-r border-gray-200 dark:border-gray-600">
                    종류
                  </th>
                  <th className="px-6 py-3 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider border-r border-gray-200 dark:border-gray-600">
                    발급 기관
                  </th>
                  <th className="px-6 py-3 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider border-r border-gray-200 dark:border-gray-600">
                    버전
                  </th>
                  <th className="px-6 py-3 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider border-r border-gray-200 dark:border-gray-600">
                    서명 알고리즘
                  </th>
                  <th className="px-6 py-3 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider border-r border-gray-200 dark:border-gray-600">
                    유효기간
                  </th>
                  <th className="px-6 py-3 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider border-r border-gray-200 dark:border-gray-600">
                    상태
                  </th>
                  <th className="px-6 py-3 text-right text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider">
                    작업
                  </th>
                </tr>
              </thead>
              <tbody className="bg-white dark:bg-gray-800 divide-y divide-gray-200 dark:divide-gray-700">
                {certificates.map((cert, index) => (
                  <tr
                    key={index}
                    className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors border-b border-gray-100 dark:border-gray-700"
                  >
                    <td className="px-6 py-4 whitespace-nowrap text-center border-r border-gray-100 dark:border-gray-700">
                      <div className="flex items-center justify-center gap-2">
                        {getFlagSvgPath(cert.country) && (
                          <img
                            src={getFlagSvgPath(cert.country)}
                            alt={cert.country}
                            className="w-6 h-4 object-cover rounded shadow-sm border border-gray-300 dark:border-gray-500"
                            onError={(e) => {
                              e.currentTarget.style.display = 'none';
                            }}
                          />
                        )}
                        <span className="text-sm font-medium text-gray-900 dark:text-gray-100">{cert.country}</span>
                      </div>
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap text-center border-r border-gray-100 dark:border-gray-700">
                      <div className="flex items-center justify-center gap-1.5">
                        {/* Use actual cert type from LDAP DN */}
                        {getCertTypeBadge(getActualCertType(cert))}
                        {/* Additional badges for CSCA */}
                        {getActualCertType(cert) === 'CSCA' && !isMasterListSignerCertificate(cert) && (
                          <>
                            {cert.isSelfSigned ? (
                              <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-cyan-100 dark:bg-cyan-900/40 text-cyan-800 dark:text-cyan-300 border border-cyan-200 dark:border-cyan-700">
                                SS
                              </span>
                            ) : (
                              <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-orange-100 dark:bg-orange-900/40 text-orange-800 dark:text-orange-300 border border-orange-200 dark:border-orange-700">
                                LC
                              </span>
                            )}
                          </>
                        )}
                      </div>
                    </td>
                    <td className="px-6 py-4 text-sm text-gray-900 dark:text-gray-100 max-w-xs truncate border-r border-gray-100 dark:border-gray-700" title={cert.issuerDnComponents?.organization || cert.issuerDnComponents?.commonName || 'N/A'}>
                      {cert.issuerDnComponents?.organization || cert.issuerDnComponents?.commonName || 'N/A'}
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap text-center text-sm text-gray-600 dark:text-gray-300 border-r border-gray-100 dark:border-gray-700">
                      {formatVersion(cert.version)}
                    </td>
                    <td className="px-6 py-4 text-sm text-gray-600 dark:text-gray-300 border-r border-gray-100 dark:border-gray-700">
                      <span className="truncate block max-w-[200px]" title={cert.signatureAlgorithm || 'N/A'}>
                        {formatSignatureAlgorithm(cert.signatureAlgorithm)}
                      </span>
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-600 dark:text-gray-300 text-center border-r border-gray-100 dark:border-gray-700">
                      <div className="flex flex-col gap-0.5">
                        <span className="text-xs">{formatDate(cert.validFrom)}</span>
                        <span className="text-xs">~</span>
                        <span className="text-xs">{formatDate(cert.validTo)}</span>
                      </div>
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap text-center border-r border-gray-100 dark:border-gray-700">
                      {getValidityBadge(cert.validity)}
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap text-right">
                      <div className="flex items-center justify-end gap-2">
                        <button
                          onClick={() => viewDetails(cert)}
                          className="inline-flex items-center gap-1 px-3 py-1.5 rounded-lg text-sm font-medium text-blue-600 dark:text-blue-400 hover:bg-blue-50 dark:hover:bg-blue-900/30 transition-colors border border-transparent hover:border-blue-200 dark:hover:border-blue-700"
                          title="상세보기"
                        >
                          <Eye className="w-4 h-4" />
                          상세
                        </button>
                        <button
                          onClick={() => exportCertificate(cert.dn, 'pem')}
                          className="inline-flex items-center gap-1 px-3 py-1.5 rounded-lg text-sm font-medium text-green-600 dark:text-green-400 hover:bg-green-50 dark:hover:bg-green-900/30 transition-colors border border-transparent hover:border-green-200 dark:hover:border-green-700"
                          title="PEM 내보내기"
                        >
                          <Download className="w-4 h-4" />
                          PEM
                        </button>
                      </div>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}

        {/* No Results */}
        {!loading && !error && certificates.length === 0 && (
          <div className="flex flex-col items-center justify-center py-20 text-gray-500 dark:text-gray-400">
            <Shield className="w-12 h-12 mb-4 opacity-50" />
            <p className="text-lg font-medium">검색 결과가 없습니다.</p>
            <p className="text-sm">필터를 조정하여 다시 검색하세요.</p>
          </div>
        )}
      </div>

      {/* Certificate Detail Dialog */}
      {showDetailDialog && selectedCert && (
        <div className="fixed inset-0 z-50 flex items-center justify-center">
          {/* Backdrop */}
          <div
            className="absolute inset-0 bg-black/50 backdrop-blur-sm"
            onClick={() => setShowDetailDialog(false)}
          />

          {/* Dialog Content */}
          <div className="relative bg-white dark:bg-gray-800 rounded-2xl shadow-2xl w-full max-w-4xl mx-4 max-h-[90vh] flex flex-col">
            {/* Header */}
            <div className="flex items-center justify-between px-5 py-3 border-b border-gray-200 dark:border-gray-700">
              <div className="flex items-center gap-3">
                <div className="p-2 rounded-lg bg-gradient-to-br from-blue-500 to-indigo-600">
                  <Shield className="w-5 h-5 text-white" />
                </div>
                <div>
                  <h2 className="text-lg font-semibold text-gray-900 dark:text-white">
                    인증서 상세 정보
                  </h2>
                  <p className="text-sm text-gray-500 dark:text-gray-400 truncate max-w-md">
                    {selectedCert.country} - {selectedCert.subjectDnComponents?.organization || selectedCert.cn}
                  </p>
                  {/* Certificate Type Badges */}
                  <div className="flex items-center gap-2 mt-2">
                    {getCertTypeBadge(getActualCertType(selectedCert), selectedCert)}
                    {isLinkCertificate(selectedCert) && (
                      <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-cyan-100 dark:bg-cyan-900/40 text-cyan-800 dark:text-cyan-300 border border-cyan-200 dark:border-cyan-700">
                        Link Certificate
                      </span>
                    )}
                    {isMasterListSignerCertificate(selectedCert) && (
                      <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-purple-100 dark:bg-purple-900/40 text-purple-800 dark:text-purple-300 border border-purple-200 dark:border-purple-700">
                        Master List Signer
                      </span>
                    )}
                    {selectedCert.isSelfSigned && (
                      <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400">
                        <CheckCircle className="w-3 h-3 mr-1" />
                        Self-signed
                      </span>
                    )}
                  </div>
                </div>
              </div>
              <button
                onClick={() => setShowDetailDialog(false)}
                className="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              >
                <X className="w-5 h-5 text-gray-500" />
              </button>
            </div>

            {/* Tabs */}
            <div className="border-b border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-700/50">
              <div className="flex">
                <button
                  onClick={() => setDetailTab('general')}
                  className={cn(
                    'px-6 py-3 text-sm font-medium border-b-2 transition-colors',
                    detailTab === 'general'
                      ? 'border-blue-600 text-blue-600 dark:text-blue-400 bg-white dark:bg-gray-800'
                      : 'border-transparent text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-gray-200 hover:bg-gray-100 dark:hover:bg-gray-700'
                  )}
                >
                  General
                </button>
                <button
                  onClick={() => setDetailTab('details')}
                  className={cn(
                    'px-6 py-3 text-sm font-medium border-b-2 transition-colors',
                    detailTab === 'details'
                      ? 'border-blue-600 text-blue-600 dark:text-blue-400 bg-white dark:bg-gray-800'
                      : 'border-transparent text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-gray-200 hover:bg-gray-100 dark:hover:bg-gray-700'
                  )}
                >
                  Details
                </button>
              </div>
            </div>

            {/* Content */}
            <div className="flex-1 overflow-y-auto p-6">
              {/* General Tab */}
              {detailTab === 'general' && (
                <div className="space-y-4">
                  {/* Issued To/By in 2-column layout */}
                  <div className="grid grid-cols-2 gap-4">
                    {/* Issued To */}
                    <div>
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Issued To</h3>
                      <div className="space-y-2">
                        <div className="grid grid-cols-[80px_1fr] gap-2">
                          <span className="text-xs text-gray-600 dark:text-gray-400">CN:</span>
                          <span className="text-xs text-gray-900 dark:text-white break-all">
                            {selectedCert.subjectDnComponents?.commonName || selectedCert.cn}
                          </span>
                        </div>
                        <div className="grid grid-cols-[80px_1fr] gap-2">
                          <span className="text-xs text-gray-600 dark:text-gray-400">Organization:</span>
                          <span className="text-xs text-gray-900 dark:text-white">
                            {selectedCert.subjectDnComponents?.organization || '-'}
                          </span>
                        </div>
                        <div className="grid grid-cols-[80px_1fr] gap-2">
                          <span className="text-xs text-gray-600 dark:text-gray-400">Org. Unit:</span>
                          <span className="text-xs text-gray-900 dark:text-white">
                            {selectedCert.subjectDnComponents?.organizationalUnit || '-'}
                          </span>
                        </div>
                        <div className="grid grid-cols-[80px_1fr] gap-2">
                          <span className="text-xs text-gray-600 dark:text-gray-400">Serial:</span>
                          <span className="text-xs text-gray-900 dark:text-white font-mono break-all">
                            {selectedCert.subjectDnComponents?.serialNumber || selectedCert.sn}
                          </span>
                        </div>
                      </div>
                    </div>

                    {/* Issued By */}
                    <div>
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Issued By</h3>
                      <div className="space-y-2">
                        <div className="grid grid-cols-[80px_1fr] gap-2">
                          <span className="text-xs text-gray-600 dark:text-gray-400">CN:</span>
                          <span className="text-xs text-gray-900 dark:text-white break-all">
                            {selectedCert.issuerDnComponents?.commonName || '-'}
                          </span>
                        </div>
                        <div className="grid grid-cols-[80px_1fr] gap-2">
                          <span className="text-xs text-gray-600 dark:text-gray-400">Organization:</span>
                          <span className="text-xs text-gray-900 dark:text-white">
                            {selectedCert.issuerDnComponents?.organization || '-'}
                          </span>
                        </div>
                        <div className="grid grid-cols-[80px_1fr] gap-2">
                          <span className="text-xs text-gray-600 dark:text-gray-400">Org. Unit:</span>
                          <span className="text-xs text-gray-900 dark:text-white">
                            {selectedCert.issuerDnComponents?.organizationalUnit || '-'}
                          </span>
                        </div>
                      </div>
                    </div>
                  </div>

                  {/* Validity and Fingerprints in 2-column layout */}
                  <div className="grid grid-cols-2 gap-4">
                    {/* Validity */}
                    <div>
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Validity</h3>
                      <div className="space-y-2">
                        <div className="grid grid-cols-[80px_1fr] gap-2">
                          <span className="text-xs text-gray-600 dark:text-gray-400">Issued on:</span>
                          <span className="text-xs text-gray-900 dark:text-white">{formatDate(selectedCert.validFrom)}</span>
                        </div>
                        <div className="grid grid-cols-[80px_1fr] gap-2">
                          <span className="text-xs text-gray-600 dark:text-gray-400">Expires on:</span>
                          <span className="text-xs text-gray-900 dark:text-white">{formatDate(selectedCert.validTo)}</span>
                        </div>
                      </div>
                    </div>

                    {/* Fingerprints */}
                    <div>
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Fingerprints</h3>
                      <div className="space-y-2">
                        <div className="grid grid-cols-[80px_1fr] gap-2">
                          <span className="text-xs text-gray-600 dark:text-gray-400">SHA1:</span>
                          <span className="text-xs text-gray-900 dark:text-white font-mono break-all">
                            {selectedCert.fingerprint.substring(0, 40) || 'N/A'}
                          </span>
                        </div>
                        <div className="grid grid-cols-[80px_1fr] gap-2">
                          <span className="text-xs text-gray-600 dark:text-gray-400">MD5:</span>
                          <span className="text-xs text-gray-900 dark:text-white font-mono break-all">
                            {selectedCert.fingerprint.substring(0, 32) || 'N/A'}
                          </span>
                        </div>
                      </div>
                    </div>
                  </div>

                  {/* PKD Conformance Section (DSC_NC only) */}
                  {getActualCertType(selectedCert) === 'DSC_NC' && (selectedCert.pkdConformanceCode || selectedCert.pkdConformanceText || selectedCert.pkdVersion) && (
                    <div>
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">
                        PKD Conformance Information
                      </h3>
                      <div className="space-y-2">
                        {selectedCert.pkdConformanceCode && (
                          <div className="grid grid-cols-[120px_1fr] gap-2">
                            <span className="text-xs text-gray-600 dark:text-gray-400">Conformance Code:</span>
                            <span className="text-xs text-gray-900 dark:text-white font-mono">
                              {selectedCert.pkdConformanceCode}
                            </span>
                          </div>
                        )}
                        {selectedCert.pkdVersion && (
                          <div className="grid grid-cols-[120px_1fr] gap-2">
                            <span className="text-xs text-gray-600 dark:text-gray-400">PKD Version:</span>
                            <span className="text-xs text-gray-900 dark:text-white">
                              {selectedCert.pkdVersion}
                            </span>
                          </div>
                        )}
                        {selectedCert.pkdConformanceText && (
                          <div className="grid grid-cols-[120px_1fr] gap-2">
                            <span className="text-xs text-gray-600 dark:text-gray-400">Conformance Text:</span>
                            <div className="text-xs text-gray-900 dark:text-white">
                              <div className="bg-orange-50 dark:bg-orange-900/10 border border-orange-200 dark:border-orange-700 rounded p-2">
                                <pre className="whitespace-pre-wrap break-words text-xs font-mono">
                                  {selectedCert.pkdConformanceText}
                                </pre>
                              </div>
                            </div>
                          </div>
                        )}
                      </div>
                    </div>
                  )}

                  {/* Trust Chain Summary Card (DSC / DSC_NC only) */}
                  {(getActualCertType(selectedCert) === 'DSC' || getActualCertType(selectedCert) === 'DSC_NC') && (
                    <div>
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">
                        Trust Chain Validation
                      </h3>
                      {validationLoading ? (
                        <div className="flex items-center gap-2 p-2.5 bg-gray-50 dark:bg-gray-800/50 rounded-lg border border-gray-200 dark:border-gray-700">
                          <RefreshCw className="w-4 h-4 animate-spin text-blue-500" />
                          <span className="text-xs text-gray-500 dark:text-gray-400">검증 결과 로드 중...</span>
                        </div>
                      ) : validationResult ? (
                        <div className={`rounded-lg border p-3 space-y-2 ${
                          validationResult.trustChainValid
                            ? 'bg-green-50 dark:bg-green-900/10 border-green-200 dark:border-green-800'
                            : validationResult.validationStatus === 'PENDING'
                            ? 'bg-yellow-50 dark:bg-yellow-900/10 border-yellow-200 dark:border-yellow-800'
                            : 'bg-red-50 dark:bg-red-900/10 border-red-200 dark:border-red-800'
                        }`}>
                          {/* Status Badge Row */}
                          <div className="flex items-center justify-between">
                            <div className="flex items-center gap-2">
                              {validationResult.trustChainValid ? (
                                <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-semibold bg-green-100 dark:bg-green-900/40 text-green-800 dark:text-green-300">
                                  <CheckCircle className="w-3 h-3" />
                                  신뢰 체인 유효
                                </span>
                              ) : validationResult.validationStatus === 'PENDING' ? (
                                <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-semibold bg-yellow-100 dark:bg-yellow-900/40 text-yellow-800 dark:text-yellow-300">
                                  <Clock className="w-3 h-3" />
                                  검증 대기 (만료됨)
                                </span>
                              ) : (
                                <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-semibold bg-red-100 dark:bg-red-900/40 text-red-800 dark:text-red-300">
                                  <XCircle className="w-3 h-3" />
                                  신뢰 체인 유효하지 않음
                                </span>
                              )}
                            </div>
                            <button
                              onClick={() => setDetailTab('details')}
                              className="text-xs text-blue-600 dark:text-blue-400 hover:underline flex items-center gap-1"
                            >
                              자세히 보기 <ChevronRight className="w-3 h-3" />
                            </button>
                          </div>

                          {/* Trust Chain Path (Compact) */}
                          {validationResult.trustChainPath && (
                            <div>
                              <span className="text-xs text-gray-500 dark:text-gray-400 block mb-1">신뢰 체인 경로:</span>
                              <TrustChainVisualization
                                trustChainPath={validationResult.trustChainPath}
                                trustChainValid={validationResult.trustChainValid}
                                compact={true}
                              />
                            </div>
                          )}

                          {/* Validation Message */}
                          {validationResult.trustChainMessage && (
                            <p className={`text-xs ${
                              validationResult.trustChainValid
                                ? 'text-green-700 dark:text-green-400'
                                : validationResult.validationStatus === 'PENDING'
                                ? 'text-yellow-700 dark:text-yellow-400'
                                : 'text-red-700 dark:text-red-400'
                            }`}>
                              {validationResult.trustChainMessage}
                            </p>
                          )}
                        </div>
                      ) : (
                        <p className="text-xs text-gray-500 dark:text-gray-400 p-2.5 bg-gray-50 dark:bg-gray-800/50 rounded-lg border border-gray-200 dark:border-gray-700">
                          이 인증서에 대한 신뢰 체인 검증 결과가 없습니다.
                        </p>
                      )}
                    </div>
                  )}
                </div>
              )}

              {/* Details Tab */}
              {detailTab === 'details' && (
                <div className="space-y-4">
                  {/* Trust Chain Validation (Sprint 3 Task 3.6) */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                      신뢰 체인 검증
                    </h3>
                    {validationLoading ? (
                      <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600 flex items-center justify-center gap-2">
                        <RefreshCw className="w-4 h-4 animate-spin text-blue-500" />
                        <span className="text-sm text-gray-600 dark:text-gray-400">검증 결과 로드 중...</span>
                      </div>
                    ) : validationResult ? (
                      <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600 space-y-3">
                        {/* Validation Status */}
                        <div className="flex items-center gap-2">
                          <span className="text-sm text-gray-600 dark:text-gray-400">상태:</span>
                          {validationResult.trustChainValid ? (
                            <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400">
                              <CheckCircle className="w-3 h-3 mr-1" />
                              유효
                            </span>
                          ) : (
                            <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400">
                              <XCircle className="w-3 h-3 mr-1" />
                              유효하지 않음
                            </span>
                          )}
                        </div>

                        {/* Trust Chain Path Visualization */}
                        {validationResult.trustChainPath && (
                          <div>
                            <span className="text-sm text-gray-600 dark:text-gray-400 mb-2 block">신뢰 체인 경로:</span>
                            <TrustChainVisualization
                              trustChainPath={validationResult.trustChainPath}
                              trustChainValid={validationResult.trustChainValid}
                              compact={false}
                            />
                          </div>
                        )}

                        {/* Message */}
                        {validationResult.trustChainMessage && (
                          <div>
                            <span className="text-sm text-gray-600 dark:text-gray-400">메시지:</span>
                            <p className="text-sm text-gray-700 dark:text-gray-300 mt-1">{validationResult.trustChainMessage}</p>
                          </div>
                        )}
                      </div>
                    ) : (
                      <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600">
                        <p className="text-sm text-gray-600 dark:text-gray-400">
                          이 인증서에 대한 검증 결과가 없습니다.
                        </p>
                      </div>
                    )}
                  </div>

                  {/* Link Certificate Information */}
                  {isLinkCertificate(selectedCert) && (
                    <div>
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                        링크 인증서 정보
                      </h3>
                      <div className="bg-cyan-50 dark:bg-cyan-900/20 border border-cyan-200 dark:border-cyan-700 rounded-lg p-4 space-y-3">
                        <div className="flex items-start gap-2">
                          <Shield className="w-5 h-5 text-cyan-600 dark:text-cyan-400 mt-0.5" />
                          <div className="flex-1">
                            <h4 className="text-sm font-semibold text-cyan-800 dark:text-cyan-300 mb-2">
                              목적
                            </h4>
                            <p className="text-xs text-cyan-700 dark:text-cyan-400 leading-relaxed">
                              링크 인증서는 서로 다른 CSCA 인증서 간의 암호화 신뢰 체인을 생성합니다. 일반적으로 다음과 같은 경우에 사용됩니다:
                            </p>
                            <ul className="mt-2 ml-4 space-y-1 text-xs text-cyan-700 dark:text-cyan-400">
                              <li className="list-disc">• 국가가 CSCA 인프라를 업데이트할 때</li>
                              <li className="list-disc">• 조직 정보가 변경될 때 (예: 조직명 변경)</li>
                              <li className="list-disc">• 인증서 정책이 업데이트될 때</li>
                              <li className="list-disc">• 새로운 암호화 알고리즘으로 마이그레이션할 때</li>
                            </ul>
                          </div>
                        </div>
                        <div className="border-t border-cyan-200 dark:border-cyan-700 pt-3">
                          <div className="grid grid-cols-[120px_1fr] gap-2 text-xs">
                            <span className="text-cyan-600 dark:text-cyan-400 font-medium">LDAP DN:</span>
                            <span className="text-cyan-800 dark:text-cyan-300 font-mono break-all">{selectedCert.dn}</span>
                          </div>
                        </div>
                      </div>
                    </div>
                  )}

                  {/* Master List Signer Certificate Information */}
                  {isMasterListSignerCertificate(selectedCert) && (
                    <div>
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                        Master List 서명 인증서 정보
                      </h3>
                      <div className="bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-700 rounded-lg p-4 space-y-3">
                        <div className="flex items-start gap-2">
                          <FileText className="w-5 h-5 text-purple-600 dark:text-purple-400 mt-0.5" />
                          <div className="flex-1">
                            <h4 className="text-sm font-semibold text-purple-800 dark:text-purple-300 mb-2">
                              목적
                            </h4>
                            <p className="text-xs text-purple-700 dark:text-purple-400 leading-relaxed">
                              Master List 서명 인증서(MLSC)는 Master List CMS 구조에 디지털 서명하는 데 사용됩니다. 이러한 인증서의 특징:
                            </p>
                            <ul className="mt-2 ml-4 space-y-1 text-xs text-purple-700 dark:text-purple-400">
                              <li className="list-disc">• 자체 서명 인증서</li>
                              <li className="list-disc">• digitalSignature 키 사용 (0x80 비트)</li>
                              <li className="list-disc">• Master List CMS에 서명 인증서로 포함됨</li>
                              <li className="list-disc">• 국가 PKI 기관에서 발급</li>
                            </ul>
                          </div>
                        </div>
                        <div className="border-t border-purple-200 dark:border-purple-700 pt-3">
                          <div className="space-y-2 text-xs">
                            <div className="grid grid-cols-[120px_1fr] gap-2">
                              <span className="text-purple-600 dark:text-purple-400 font-medium">LDAP DN:</span>
                              <span className="text-purple-800 dark:text-purple-300 font-mono break-all">{selectedCert.dn}</span>
                            </div>
                            <div className="grid grid-cols-[120px_1fr] gap-2">
                              <span className="text-purple-600 dark:text-purple-400 font-medium">저장 위치:</span>
                              <span className="text-purple-800 dark:text-purple-300">
                                데이터베이스에는 CSCA 타입으로 저장되지만, LDAP에서는 <code className="bg-purple-100 dark:bg-purple-900/50 px-1 py-0.5 rounded">o=mlsc</code> 조직 단위에 저장
                              </span>
                            </div>
                            <div className="grid grid-cols-[120px_1fr] gap-2">
                              <span className="text-purple-600 dark:text-purple-400 font-medium">자체 서명:</span>
                              <span className="text-purple-800 dark:text-purple-300">
                                {selectedCert.isSelfSigned ? '예 (Subject DN = Issuer DN)' : '아니오'}
                              </span>
                            </div>
                          </div>
                        </div>
                      </div>
                    </div>
                  )}

                  {/* Trust Chain Hierarchy (only show for non-self-signed certificates with trust chain) */}
                  {validationResult && validationResult.trustChainPath && validationResult.trustChainPath.trim() !== '' && !selectedCert.isSelfSigned && (
                    <div>
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                        신뢰 체인 계층
                      </h3>
                      <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg border border-gray-200 dark:border-gray-600 overflow-hidden">
                        <TreeViewer
                          data={buildTrustChainTree(validationResult.trustChainPath)}
                          height="200px"
                        />
                      </div>
                      {validationResult.trustChainValid ? (
                        <div className="mt-2 flex items-center gap-2 text-sm text-green-600 dark:text-green-400">
                          <CheckCircle className="w-4 h-4" />
                          <span>신뢰 체인이 유효합니다</span>
                        </div>
                      ) : (
                        <div className="mt-2 flex items-center gap-2 text-sm text-red-600 dark:text-red-400">
                          <XCircle className="w-4 h-4" />
                          <span>신뢰 체인 검증 실패</span>
                        </div>
                      )}
                    </div>
                  )}

                  {/* Certificate Fields Tree */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">인증서 필드</h3>
                    <TreeViewer
                      data={buildCertificateTree(selectedCert)}
                      height="400px"
                    />
                  </div>
                </div>
              )}
            </div>

            {/* Footer */}
            <div className="flex justify-between items-center gap-3 px-5 py-3 border-t border-gray-200 dark:border-gray-700">
              <button
                onClick={() => exportCertificate(selectedCert.dn, 'pem')}
                className="px-4 py-2 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 bg-white dark:bg-gray-700 border border-gray-300 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-600 transition-colors"
              >
                인증서 저장...
              </button>
              <button
                onClick={() => setShowDetailDialog(false)}
                className="px-4 py-2 rounded-lg text-sm font-medium text-white bg-blue-600 hover:bg-blue-700 transition-colors"
              >
                닫기
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default CertificateSearch;
