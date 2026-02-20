import React, { useState, useEffect, useMemo } from 'react';
import { Download, FileText, CheckCircle, XCircle, Clock, RefreshCw, Eye, ChevronLeft, ChevronRight, Shield, HelpCircle } from 'lucide-react';
import { getFlagSvgPath } from '@/utils/countryCode';
import { cn } from '@/utils/cn';
import { validationApi } from '@/api/validationApi';
import { certificateApi } from '@/services/pkdApi';
import pkdApi from '@/services/pkdApi';
import type { ValidationResult } from '@/types/validation';
import CertificateDetailDialog from '@/components/CertificateDetailDialog';
import type { Certificate } from '@/components/CertificateDetailDialog';
import CertificateSearchFilters from '@/components/CertificateSearchFilters';
import type { SearchCriteria } from '@/components/CertificateSearchFilters';

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
  const [detailTab, setDetailTab] = useState<'general' | 'details' | 'doc9303'>('general');
  const [validationResult, setValidationResult] = useState<ValidationResult | null>(null);
  const [validationLoading, setValidationLoading] = useState(false);

  // Fetch available countries
  const fetchCountries = async () => {
    setCountriesLoading(true);
    try {
      const response = await certificateApi.getCountries();
      const { success, countries } = response.data;

      if (success && countries) {
        setCountries(countries);
      }
    } catch (err) {
      if (import.meta.env.DEV) console.error('Failed to fetch countries:', err);
    } finally {
      setCountriesLoading(false);
    }
  };

  // Search certificates
  const searchCertificates = async () => {
    setLoading(true);
    setError(null);

    try {
      const params: Record<string, string> = {
        limit: criteria.limit.toString(),
        offset: criteria.offset.toString(),
      };
      if (criteria.country) params.country = criteria.country;
      if (criteria.certType) params.certType = criteria.certType;
      if (criteria.validity && criteria.validity !== 'all') params.validity = criteria.validity;
      if (criteria.source) params.source = criteria.source;
      if (criteria.searchTerm) params.searchTerm = criteria.searchTerm;

      const response = await pkdApi.get('/certificates/search', { params });
      const data = response.data as Record<string, unknown>;

      if (data.success) {
        setCertificates(data.certificates as Certificate[]);
        setTotal(data.total as number);
        // Store statistics from backend (if available)
        if (data.stats) {
          setApiStats(data.stats as { total: number; valid: number; expired: number; notYetValid: number; unknown: number });
        }
      } else {
        setError((data.error as string) || 'Search failed');
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
      if (import.meta.env.DEV) console.error('Failed to fetch validation result:', err);
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

  // Export all LDAP-stored data (DIT ZIP)
  const [exportAllLoading, setExportAllLoading] = useState(false);
  const exportAll = async (format: 'pem' | 'der') => {
    try {
      setExportAllLoading(true);
      const response = await fetch(`/api/certificates/export/all?format=${format}`);

      if (response.ok) {
        const blob = await response.blob();
        const contentDisposition = response.headers.get('Content-Disposition');
        const filename = contentDisposition?.match(/filename="(.+)"/)?.[1] || `ICAO-PKD-Export.zip`;

        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        window.URL.revokeObjectURL(url);
        document.body.removeChild(a);
      } else {
        const errorData = await response.json().catch(() => null);
        alert('Export failed: ' + (errorData?.error || response.statusText));
      }
    } catch (err) {
      alert('Export failed: ' + (err instanceof Error ? err.message : 'Unknown error'));
    } finally {
      setExportAllLoading(false);
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
      case 'EXPIRED_VALID':
        return (
          <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-amber-100 dark:bg-amber-900/30 text-amber-700 dark:text-amber-400">
            <CheckCircle className="w-3 h-3 mr-1" />
            만료-유효
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
      <CertificateSearchFilters
        criteria={criteria}
        setCriteria={setCriteria}
        countries={countries}
        countriesLoading={countriesLoading}
        loading={loading}
        showFilters={showFilters}
        setShowFilters={setShowFilters}
        handleSearch={handleSearch}
        exportCountry={exportCountry}
        exportAll={exportAll}
        exportAllLoading={exportAllLoading}
      />

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
      {selectedCert && (
        <CertificateDetailDialog
          selectedCert={selectedCert}
          showDetailDialog={showDetailDialog}
          setShowDetailDialog={setShowDetailDialog}
          detailTab={detailTab}
          setDetailTab={setDetailTab}
          validationResult={validationResult}
          validationLoading={validationLoading}
          exportCertificate={exportCertificate}
          formatDate={formatDate}
          formatVersion={formatVersion}
          isLinkCertificate={isLinkCertificate}
          isMasterListSignerCertificate={isMasterListSignerCertificate}
          getActualCertType={getActualCertType}
          getCertTypeBadge={getCertTypeBadge}
        />
      )}
    </div>
  );
};

export default CertificateSearch;
