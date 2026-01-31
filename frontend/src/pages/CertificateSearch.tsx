import React, { useState, useEffect, useMemo } from 'react';
import { Search, Download, Filter, ChevronDown, ChevronUp, FileText, X, Shield, ShieldCheck, CheckCircle, XCircle, Clock, RefreshCw, Eye, ChevronLeft, ChevronRight, HardDrive, AlertTriangle } from 'lucide-react';
import { getFlagSvgPath } from '@/utils/countryCode';
import { cn } from '@/utils/cn';
import { TrustChainVisualization } from '@/components/TrustChainVisualization';
import { validationApi } from '@/api/validationApi';
import type { ValidationResult } from '@/types/validation';

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
}

interface SearchCriteria {
  country: string;
  certType: string;
  validity: string;
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
  }, [criteria.country, criteria.certType, criteria.validity, criteria.searchTerm, criteria.limit, criteria.offset]);

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

  // Get certificate type badge
  const getCertTypeBadge = (certType: string) => {
    switch (certType) {
      case 'CSCA':
        return (
          <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-blue-100 dark:bg-blue-900/40 text-blue-800 dark:text-blue-300 border border-blue-200 dark:border-blue-700">
            CSCA
          </span>
        );
      case 'MLSC':
        return (
          <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-purple-100 dark:bg-purple-900/40 text-purple-800 dark:text-purple-300 border border-purple-200 dark:border-purple-700">
            MLSC
          </span>
        );
      case 'DSC':
        return (
          <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-green-100 dark:bg-green-900/40 text-green-800 dark:text-green-300 border border-green-200 dark:border-green-700">
            DSC
          </span>
        );
      case 'DSC_NC':
        return (
          <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-orange-100 dark:bg-orange-900/40 text-orange-800 dark:text-orange-300 border border-orange-200 dark:border-orange-700">
            DSC_NC
          </span>
        );
      case 'CRL':
        return (
          <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-red-100 dark:bg-red-900/40 text-red-800 dark:text-red-300 border border-red-200 dark:border-red-700">
            CRL
          </span>
        );
      case 'ML':
        return (
          <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-indigo-100 dark:bg-indigo-900/40 text-indigo-800 dark:text-indigo-300 border border-indigo-200 dark:border-indigo-700">
            ML
          </span>
        );
      default:
        return (
          <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-gray-100 dark:bg-gray-700 text-gray-800 dark:text-gray-300 border border-gray-200 dark:border-gray-600">
            {certType}
          </span>
        );
    }
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
            <div className="grid grid-cols-2 md:grid-cols-5 gap-3">
              {/* Country */}
              <div>
                <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                  국가 코드
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
                        {country}
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
                    CN
                  </th>
                  <th className="px-6 py-3 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider border-r border-gray-200 dark:border-gray-600">
                    Serial
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
                    <td className="px-6 py-4 text-sm text-gray-900 dark:text-gray-100 max-w-xs truncate border-r border-gray-100 dark:border-gray-700" title={cert.cn}>
                      {cert.cn}
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-600 dark:text-gray-300 font-mono border-r border-gray-100 dark:border-gray-700">
                      {cert.sn.substring(0, 12)}...
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
                    {selectedCert.cn}
                  </p>
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
                <div className="space-y-6">
                  {/* Certificate Type Section */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                      Certificate Type
                    </h3>
                    <div className="space-y-3">
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">Type:</span>
                        <div className="flex items-center gap-2">
                          {/* Use actual cert type from LDAP DN */}
                          {getCertTypeBadge(getActualCertType(selectedCert))}
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
                        </div>
                      </div>
                      {isLinkCertificate(selectedCert) && (
                        <div className="bg-cyan-50 dark:bg-cyan-900/20 border border-cyan-200 dark:border-cyan-700 rounded-lg p-3 space-y-2">
                          <div className="flex items-start gap-2">
                            <Shield className="w-4 h-4 text-cyan-600 dark:text-cyan-400 flex-shrink-0 mt-0.5" />
                            <div className="text-xs text-cyan-800 dark:text-cyan-300">
                              <p className="font-semibold mb-1">연결 인증서 (Link Certificate)</p>
                              <p className="mb-2">
                                ICAO Doc 9303 Part 12에 정의된 인증서로, 이전 CSCA와 새 CSCA 사이의 신뢰 체인을 연결합니다.
                                Subject DN과 Issuer DN이 서로 다르며, 이전 CSCA의 개인키로 새 CSCA 공개키에 서명합니다.
                              </p>
                              <p className="font-semibold mb-1">사용 사례:</p>
                              <ul className="list-disc list-inside space-y-0.5 ml-2">
                                <li>CSCA 키 교체/갱신 (Key Rollover)</li>
                                <li>서명 알고리즘 마이그레이션 (예: RSA → ECDSA)</li>
                                <li>조직 정보 변경 (Organization DN change)</li>
                                <li>CSCA 인프라 업그레이드</li>
                              </ul>
                            </div>
                          </div>
                        </div>
                      )}
                      {isMasterListSignerCertificate(selectedCert) && (
                        <div className="bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-700 rounded-lg p-3 space-y-2">
                          <div className="flex items-start gap-2">
                            <FileText className="w-4 h-4 text-purple-600 dark:text-purple-400 flex-shrink-0 mt-0.5" />
                            <div className="text-xs text-purple-800 dark:text-purple-300">
                              <p className="font-semibold mb-1">마스터 리스트 서명 인증서 (MLSC)</p>
                              <p className="mb-2">
                                ICAO PKD에서 Master List CMS 구조에 디지털 서명하는데 사용되는 Self-signed 인증서입니다.
                                국가 PKI 기관이 발급하며, digitalSignature key usage (0x80 bit)를 가집니다.
                              </p>
                              <p className="font-semibold mb-1">특징:</p>
                              <ul className="list-disc list-inside space-y-0.5 ml-2">
                                <li>Self-signed 인증서 (Subject DN = Issuer DN)</li>
                                <li>Master List CMS SignerInfo에 포함</li>
                                <li>LDAP: o=mlsc,c=UN 에 저장</li>
                                <li>Database: certificate_type='MLSC', country_code='UN'</li>
                              </ul>
                            </div>
                          </div>
                        </div>
                      )}
                      {/* CSCA (Self-signed) description */}
                      {getActualCertType(selectedCert) === 'CSCA' && !isLinkCertificate(selectedCert) && !isMasterListSignerCertificate(selectedCert) && (
                        <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-700 rounded-lg p-3 space-y-2">
                          <div className="flex items-start gap-2">
                            <ShieldCheck className="w-4 h-4 text-blue-600 dark:text-blue-400 flex-shrink-0 mt-0.5" />
                            <div className="text-xs text-blue-800 dark:text-blue-300">
                              <p className="font-semibold mb-1">국가 서명 인증기관 (CSCA - Country Signing CA)</p>
                              <p className="mb-2">
                                ICAO Doc 9303 Part 12에 정의된 Self-signed 루트 인증서로, 여권 전자 칩에 서명하는 DSC를 발급하는 국가 최상위 인증기관입니다.
                                각 국가는 독자적인 CSCA를 운영하며, ICAO PKD를 통해 전 세계에 배포합니다.
                              </p>
                              <p className="font-semibold mb-1">역할:</p>
                              <ul className="list-disc list-inside space-y-0.5 ml-2">
                                <li>DSC (Document Signer Certificate) 발급</li>
                                <li>국가 PKI 신뢰 체인의 루트</li>
                                <li>여권 검증 시 최상위 신뢰 앵커 (Trust Anchor)</li>
                                <li>Self-signed (Subject DN = Issuer DN)</li>
                              </ul>
                            </div>
                          </div>
                        </div>
                      )}
                      {/* DSC description */}
                      {getActualCertType(selectedCert) === 'DSC' && (
                        <div className="bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-700 rounded-lg p-3 space-y-2">
                          <div className="flex items-start gap-2">
                            <HardDrive className="w-4 h-4 text-green-600 dark:text-green-400 flex-shrink-0 mt-0.5" />
                            <div className="text-xs text-green-800 dark:text-green-300">
                              <p className="font-semibold mb-1">문서 서명 인증서 (DSC - Document Signer Certificate)</p>
                              <p className="mb-2">
                                ICAO Doc 9303 Part 12에 정의된 인증서로, 여권 전자 칩(eMRTD)의 데이터 그룹(DG1-DG16)에 디지털 서명하는데 사용됩니다.
                                CSCA가 발급하며, Passive Authentication 검증 시 사용됩니다.
                              </p>
                              <p className="font-semibold mb-1">역할:</p>
                              <ul className="list-disc list-inside space-y-0.5 ml-2">
                                <li>여권 데이터 그룹(DG) 서명 (SOD 생성)</li>
                                <li>CSCA에 의해 발급 (Issuer = CSCA)</li>
                                <li>Passive Authentication 검증 대상</li>
                                <li>유효기간: 일반적으로 3개월 ~ 3년</li>
                              </ul>
                            </div>
                          </div>
                        </div>
                      )}
                      {/* DSC_NC description */}
                      {getActualCertType(selectedCert) === 'DSC_NC' && (
                        <div className="bg-orange-50 dark:bg-orange-900/20 border border-orange-200 dark:border-orange-700 rounded-lg p-3 space-y-2">
                          <div className="flex items-start gap-2">
                            <AlertTriangle className="w-4 h-4 text-orange-600 dark:text-orange-400 flex-shrink-0 mt-0.5" />
                            <div className="text-xs text-orange-800 dark:text-orange-300">
                              <p className="font-semibold mb-1">비준수 문서 서명 인증서 (DSC_NC - Non-Conformant DSC)</p>
                              <p className="mb-2">
                                ICAO 9303 기술 표준을 완전히 준수하지 않는 DSC입니다.
                                ICAO PKD의 nc-data 컨테이너에 별도 저장되며, 일부 국가의 레거시 시스템 호환성을 위해 유지됩니다.
                              </p>
                              <p className="font-semibold mb-1">비준수 이유 (예시):</p>
                              <ul className="list-disc list-inside space-y-0.5 ml-2">
                                <li>필수 X.509 확장(Extension) 누락 또는 잘못된 설정</li>
                                <li>Key Usage, Extended Key Usage 미준수</li>
                                <li>Subject DN 또는 Issuer DN 형식 오류</li>
                                <li>유효기간(Validity Period) 정책 위반</li>
                                <li>서명 알고리즘 비권장 또는 보안 취약</li>
                              </ul>
                              <p className="font-semibold mb-1 mt-2">주의사항:</p>
                              <ul className="list-disc list-inside space-y-0.5 ml-2">
                                <li>⚠️ 프로덕션 환경에서 사용 권장하지 않음</li>
                                <li>⚠️ 일부 검증 시스템에서 거부될 수 있음</li>
                                <li>📌 LDAP 저장: dc=nc-data 컨테이너</li>
                                <li>📌 ICAO는 2021년부터 nc-data 폐기 권장</li>
                              </ul>
                            </div>
                          </div>
                        </div>
                      )}
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">Self-signed:</span>
                        <span className="text-sm text-gray-900 dark:text-white">
                          {selectedCert.isSelfSigned ? (
                            <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400">
                              <CheckCircle className="w-3 h-3 mr-1" />
                              Yes
                            </span>
                          ) : (
                            <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-gray-100 dark:bg-gray-700 text-gray-700 dark:text-gray-400">
                              No
                            </span>
                          )}
                        </span>
                      </div>
                    </div>
                  </div>

                  {/* Issued To Section */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">Issued To</h3>
                    <div className="space-y-3">
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">Common name (CN):</span>
                        <span className="text-sm text-gray-900 dark:text-white break-all">{selectedCert.cn}</span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">Organization (O):</span>
                        <span className="text-sm text-gray-900 dark:text-white">
                          {selectedCert.subjectDn.match(/O=([^,]+)/)?.[1] || '-'}
                        </span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">Organizational unit (OU):</span>
                        <span className="text-sm text-gray-900 dark:text-white">
                          {selectedCert.subjectDn.match(/OU=([^,]+)/)?.[1] || '-'}
                        </span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">Serial number:</span>
                        <span className="text-sm text-gray-900 dark:text-white font-mono break-all">{selectedCert.sn}</span>
                      </div>
                    </div>
                  </div>

                  {/* Issued By Section */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">Issued By</h3>
                    <div className="space-y-3">
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">Common name (CN):</span>
                        <span className="text-sm text-gray-900 dark:text-white break-all">
                          {selectedCert.issuerDn.match(/CN=([^,]+)/)?.[1] || '-'}
                        </span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">Organization (O):</span>
                        <span className="text-sm text-gray-900 dark:text-white">
                          {selectedCert.issuerDn.match(/O=([^,]+)/)?.[1] || '-'}
                        </span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">Organizational unit (OU):</span>
                        <span className="text-sm text-gray-900 dark:text-white">
                          {selectedCert.issuerDn.match(/OU=([^,]+)/)?.[1] || '-'}
                        </span>
                      </div>
                    </div>
                  </div>

                  {/* Validity Section */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">Validity</h3>
                    <div className="space-y-3">
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">Issued on:</span>
                        <span className="text-sm text-gray-900 dark:text-white">{formatDate(selectedCert.validFrom)}</span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">Expires on:</span>
                        <span className="text-sm text-gray-900 dark:text-white">{formatDate(selectedCert.validTo)}</span>
                      </div>
                    </div>
                  </div>

                  {/* Fingerprints Section */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">Fingerprints</h3>
                    <div className="space-y-3">
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">SHA1 fingerprint:</span>
                        <span className="text-sm text-gray-900 dark:text-white font-mono break-all">
                          {selectedCert.fingerprint.substring(0, 40) || 'N/A'}
                        </span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600 dark:text-gray-400">MD5 fingerprint:</span>
                        <span className="text-sm text-gray-900 dark:text-white font-mono break-all">
                          {selectedCert.fingerprint.substring(0, 32) || 'N/A'}
                        </span>
                      </div>
                    </div>
                  </div>

                  {/* PKD Conformance Section (DSC_NC only) */}
                  {getActualCertType(selectedCert) === 'DSC_NC' && (selectedCert.pkdConformanceCode || selectedCert.pkdConformanceText || selectedCert.pkdVersion) && (
                    <div>
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                        PKD Conformance Information
                      </h3>
                      <div className="space-y-3">
                        {selectedCert.pkdConformanceCode && (
                          <div className="grid grid-cols-[140px_1fr] gap-2">
                            <span className="text-sm text-gray-600 dark:text-gray-400">Conformance Code:</span>
                            <span className="text-sm text-gray-900 dark:text-white font-mono">
                              {selectedCert.pkdConformanceCode}
                            </span>
                          </div>
                        )}
                        {selectedCert.pkdVersion && (
                          <div className="grid grid-cols-[140px_1fr] gap-2">
                            <span className="text-sm text-gray-600 dark:text-gray-400">PKD Version:</span>
                            <span className="text-sm text-gray-900 dark:text-white">
                              {selectedCert.pkdVersion}
                            </span>
                          </div>
                        )}
                        {selectedCert.pkdConformanceText && (
                          <div className="grid grid-cols-[140px_1fr] gap-2">
                            <span className="text-sm text-gray-600 dark:text-gray-400">Conformance Text:</span>
                            <div className="text-sm text-gray-900 dark:text-white">
                              <div className="bg-orange-50 dark:bg-orange-900/10 border border-orange-200 dark:border-orange-700 rounded p-3">
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
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                        Trust Chain Validation
                      </h3>
                      {validationLoading ? (
                        <div className="flex items-center gap-2 p-3 bg-gray-50 dark:bg-gray-800/50 rounded-lg border border-gray-200 dark:border-gray-700">
                          <RefreshCw className="w-4 h-4 animate-spin text-blue-500" />
                          <span className="text-sm text-gray-500 dark:text-gray-400">검증 결과 로드 중...</span>
                        </div>
                      ) : validationResult ? (
                        <div className={`rounded-lg border p-4 space-y-3 ${
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
                                <span className="inline-flex items-center gap-1 px-2.5 py-1 rounded-full text-xs font-semibold bg-green-100 dark:bg-green-900/40 text-green-800 dark:text-green-300">
                                  <CheckCircle className="w-3.5 h-3.5" />
                                  신뢰 체인 유효
                                </span>
                              ) : validationResult.validationStatus === 'PENDING' ? (
                                <span className="inline-flex items-center gap-1 px-2.5 py-1 rounded-full text-xs font-semibold bg-yellow-100 dark:bg-yellow-900/40 text-yellow-800 dark:text-yellow-300">
                                  <Clock className="w-3.5 h-3.5" />
                                  검증 대기 (만료됨)
                                </span>
                              ) : (
                                <span className="inline-flex items-center gap-1 px-2.5 py-1 rounded-full text-xs font-semibold bg-red-100 dark:bg-red-900/40 text-red-800 dark:text-red-300">
                                  <XCircle className="w-3.5 h-3.5" />
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
                              <span className="text-xs text-gray-500 dark:text-gray-400 block mb-1.5">신뢰 체인 경로:</span>
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
                        <p className="text-sm text-gray-500 dark:text-gray-400 p-3 bg-gray-50 dark:bg-gray-800/50 rounded-lg border border-gray-200 dark:border-gray-700">
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
                      Trust Chain Validation
                    </h3>
                    {validationLoading ? (
                      <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600 flex items-center justify-center gap-2">
                        <RefreshCw className="w-4 h-4 animate-spin text-blue-500" />
                        <span className="text-sm text-gray-600 dark:text-gray-400">Loading validation result...</span>
                      </div>
                    ) : validationResult ? (
                      <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600 space-y-3">
                        {/* Validation Status */}
                        <div className="flex items-center gap-2">
                          <span className="text-sm text-gray-600 dark:text-gray-400">Status:</span>
                          {validationResult.trustChainValid ? (
                            <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400">
                              <CheckCircle className="w-3 h-3 mr-1" />
                              Valid
                            </span>
                          ) : (
                            <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400">
                              <XCircle className="w-3 h-3 mr-1" />
                              Invalid
                            </span>
                          )}
                        </div>

                        {/* Trust Chain Path Visualization */}
                        {validationResult.trustChainPath && (
                          <div>
                            <span className="text-sm text-gray-600 dark:text-gray-400 mb-2 block">Trust Chain Path:</span>
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
                            <span className="text-sm text-gray-600 dark:text-gray-400">Message:</span>
                            <p className="text-sm text-gray-700 dark:text-gray-300 mt-1">{validationResult.trustChainMessage}</p>
                          </div>
                        )}
                      </div>
                    ) : (
                      <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600">
                        <p className="text-sm text-gray-600 dark:text-gray-400">
                          No validation result available for this certificate.
                        </p>
                      </div>
                    )}
                  </div>

                  {/* Link Certificate Information */}
                  {isLinkCertificate(selectedCert) && (
                    <div>
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                        Link Certificate Information
                      </h3>
                      <div className="bg-cyan-50 dark:bg-cyan-900/20 border border-cyan-200 dark:border-cyan-700 rounded-lg p-4 space-y-3">
                        <div className="flex items-start gap-2">
                          <Shield className="w-5 h-5 text-cyan-600 dark:text-cyan-400 mt-0.5" />
                          <div className="flex-1">
                            <h4 className="text-sm font-semibold text-cyan-800 dark:text-cyan-300 mb-2">
                              Purpose
                            </h4>
                            <p className="text-xs text-cyan-700 dark:text-cyan-400 leading-relaxed">
                              Link Certificates create a cryptographic trust chain between different CSCA certificates. They are typically used when:
                            </p>
                            <ul className="mt-2 ml-4 space-y-1 text-xs text-cyan-700 dark:text-cyan-400">
                              <li className="list-disc">• A country updates their CSCA infrastructure</li>
                              <li className="list-disc">• Organizational details change (e.g., organization name)</li>
                              <li className="list-disc">• Certificate policies are updated</li>
                              <li className="list-disc">• Migration to new cryptographic algorithms</li>
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
                        Master List Signer Certificate Information
                      </h3>
                      <div className="bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-700 rounded-lg p-4 space-y-3">
                        <div className="flex items-start gap-2">
                          <FileText className="w-5 h-5 text-purple-600 dark:text-purple-400 mt-0.5" />
                          <div className="flex-1">
                            <h4 className="text-sm font-semibold text-purple-800 dark:text-purple-300 mb-2">
                              Purpose
                            </h4>
                            <p className="text-xs text-purple-700 dark:text-purple-400 leading-relaxed">
                              Master List Signer Certificates (MLSC) are used to digitally sign Master List CMS structures. These certificates:
                            </p>
                            <ul className="mt-2 ml-4 space-y-1 text-xs text-purple-700 dark:text-purple-400">
                              <li className="list-disc">• Are self-signed certificates</li>
                              <li className="list-disc">• Have digitalSignature key usage (0x80 bit)</li>
                              <li className="list-disc">• Are embedded in Master List CMS as signer certificates</li>
                              <li className="list-disc">• Are issued by national PKI authorities</li>
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
                              <span className="text-purple-600 dark:text-purple-400 font-medium">Storage:</span>
                              <span className="text-purple-800 dark:text-purple-300">
                                Stored as CSCA type in database, but in <code className="bg-purple-100 dark:bg-purple-900/50 px-1 py-0.5 rounded">o=mlsc</code> organizational unit in LDAP
                              </span>
                            </div>
                            <div className="grid grid-cols-[120px_1fr] gap-2">
                              <span className="text-purple-600 dark:text-purple-400 font-medium">Self-signed:</span>
                              <span className="text-purple-800 dark:text-purple-300">
                                {selectedCert.isSelfSigned ? 'Yes (Subject DN = Issuer DN)' : 'No'}
                              </span>
                            </div>
                          </div>
                        </div>
                      </div>
                    </div>
                  )}

                  {/* Certificate Hierarchy */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">Certificate Hierarchy</h3>
                    <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600">
                      <div className="text-sm font-mono text-blue-600 dark:text-blue-400 cursor-pointer hover:underline">
                        {selectedCert.cn}
                      </div>
                    </div>
                  </div>

                  {/* Certificate Fields Tree */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">Certificate Fields</h3>
                    <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600 max-h-96 overflow-y-auto">
                      <div className="space-y-2 text-sm font-mono">
                        <details open>
                          <summary className="cursor-pointer font-semibold text-gray-900 dark:text-white hover:text-blue-600 dark:hover:text-blue-400">
                            Certificate
                          </summary>
                          <div className="ml-4 mt-2 space-y-2">
                            <details>
                              <summary className="cursor-pointer text-gray-700 dark:text-gray-300 hover:text-blue-600 dark:hover:text-blue-400">Version</summary>
                              <div className="ml-4 text-gray-600 dark:text-gray-400">V3</div>
                            </details>
                            <details>
                              <summary className="cursor-pointer text-gray-700 dark:text-gray-300 hover:text-blue-600 dark:hover:text-blue-400">Serial Number</summary>
                              <div className="ml-4 text-gray-600 dark:text-gray-400 break-all">{selectedCert.sn}</div>
                            </details>
                            <details>
                              <summary className="cursor-pointer text-gray-700 dark:text-gray-300 hover:text-blue-600 dark:hover:text-blue-400">Issuer</summary>
                              <div className="ml-4 text-gray-600 dark:text-gray-400 break-all">{selectedCert.issuerDn}</div>
                            </details>
                            <details open>
                              <summary className="cursor-pointer text-gray-700 dark:text-gray-300 hover:text-blue-600 dark:hover:text-blue-400">Validity</summary>
                              <div className="ml-4 space-y-1">
                                <div className="text-gray-600 dark:text-gray-400">Not Before: {formatDate(selectedCert.validFrom)}</div>
                                <div className="text-gray-600 dark:text-gray-400">Not After: {formatDate(selectedCert.validTo)}</div>
                              </div>
                            </details>
                            <details>
                              <summary className="cursor-pointer text-gray-700 dark:text-gray-300 hover:text-blue-600 dark:hover:text-blue-400">Subject</summary>
                              <div className="ml-4 text-gray-600 dark:text-gray-400 break-all">{selectedCert.subjectDn}</div>
                            </details>
                          </div>
                        </details>
                      </div>
                    </div>
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
                Save Certificate...
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
