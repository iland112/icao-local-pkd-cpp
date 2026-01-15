import React, { useState, useEffect } from 'react';
import { Search, Download, Filter, ChevronDown, ChevronUp, FileText, X, Shield } from 'lucide-react';
import { getFlagSvgPath } from '@/utils/countryCode';

interface Certificate {
  dn: string;
  cn: string;
  sn: string;
  country: string;
  certType: string;
  subjectDn: string;
  issuerDn: string;
  fingerprint: string;
  validFrom: string;
  validTo: string;
  validity: 'VALID' | 'EXPIRED' | 'NOT_YET_VALID' | 'UNKNOWN';
  isSelfSigned: boolean;
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
      if (criteria.validity) params.append('validity', criteria.validity);
      if (criteria.searchTerm) params.append('searchTerm', criteria.searchTerm);
      params.append('limit', criteria.limit.toString());
      params.append('offset', criteria.offset.toString());

      const response = await fetch(`/api/certificates/search?${params.toString()}`);
      const data = await response.json();

      if (data.success) {
        setCertificates(data.certificates);
        setTotal(data.total);
      } else {
        setError(data.error || 'Search failed');
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to search certificates');
    } finally {
      setLoading(false);
    }
  };

  // Initial load
  useEffect(() => {
    fetchCountries();
    searchCertificates();
  }, [criteria.offset]);

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
  const viewDetails = (cert: Certificate) => {
    setSelectedCert(cert);
    setShowDetailDialog(true);
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

  // Get validity badge
  const getValidityBadge = (validity: string) => {
    switch (validity) {
      case 'VALID':
        return <span className="px-2 py-1 text-xs font-semibold rounded-full bg-green-100 text-green-800">유효</span>;
      case 'EXPIRED':
        return <span className="px-2 py-1 text-xs font-semibold rounded-full bg-red-100 text-red-800">만료</span>;
      case 'NOT_YET_VALID':
        return <span className="px-2 py-1 text-xs font-semibold rounded-full bg-yellow-100 text-yellow-800">유효 전</span>;
      default:
        return <span className="px-2 py-1 text-xs font-semibold rounded-full bg-gray-100 text-gray-800">알 수 없음</span>;
    }
  };

  return (
    <div className="min-h-screen bg-gray-50 p-6">
      <div className="max-w-7xl mx-auto">
        {/* Header */}
        <div className="mb-6">
          <h1 className="text-3xl font-bold text-gray-900 flex items-center gap-3">
            <Shield className="w-8 h-8 text-blue-600" />
            인증서 조회
          </h1>
          <p className="mt-2 text-gray-600">
            LDAP에 저장된 PKD 인증서를 검색하고 내보내기
          </p>
        </div>

        {/* Search Filters */}
        <div className="bg-white rounded-lg shadow-md p-6 mb-6">
          <button
            onClick={() => setShowFilters(!showFilters)}
            className="flex items-center justify-between w-full mb-4"
          >
            <div className="flex items-center gap-2 text-lg font-semibold text-gray-900">
              <Filter className="w-5 h-5" />
              검색 필터
            </div>
            {showFilters ? <ChevronUp className="w-5 h-5" /> : <ChevronDown className="w-5 h-5" />}
          </button>

          {showFilters && (
            <div className="space-y-4">
              <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
                {/* Country */}
                <div>
                  <label className="block text-sm font-medium text-gray-700 mb-2">
                    국가 코드
                  </label>
                  <div className="relative">
                    <select
                      value={criteria.country}
                      onChange={(e) => setCriteria({ ...criteria, country: e.target.value })}
                      className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500 bg-white appearance-none"
                      style={criteria.country && getFlagSvgPath(criteria.country) ? { paddingLeft: '2.5rem' } : {}}
                      disabled={countriesLoading}
                    >
                      <option value="">
                        {countriesLoading ? 'Loading countries...' : countries.length === 0 ? 'No countries available' : '전체 국가'}
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
                    <div className="absolute inset-y-0 right-0 flex items-center pr-2 pointer-events-none">
                      <svg className="h-5 w-5 text-gray-400" viewBox="0 0 20 20" fill="currentColor">
                        <path fillRule="evenodd" d="M5.293 7.293a1 1 0 011.414 0L10 10.586l3.293-3.293a1 1 0 111.414 1.414l-4 4a1 1 0 01-1.414 0l-4-4a1 1 0 010-1.414z" clipRule="evenodd" />
                      </svg>
                    </div>
                  </div>
                </div>

                {/* Certificate Type */}
                <div>
                  <label className="block text-sm font-medium text-gray-700 mb-2">
                    인증서 종류
                  </label>
                  <select
                    value={criteria.certType}
                    onChange={(e) => setCriteria({ ...criteria, certType: e.target.value })}
                    className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
                  >
                    <option value="">전체</option>
                    <option value="CSCA">CSCA</option>
                    <option value="DSC">DSC</option>
                    <option value="DSC_NC">DSC_NC</option>
                    <option value="CRL">CRL</option>
                    <option value="ML">ML</option>
                  </select>
                </div>

                {/* Validity */}
                <div>
                  <label className="block text-sm font-medium text-gray-700 mb-2">
                    유효성
                  </label>
                  <select
                    value={criteria.validity}
                    onChange={(e) => setCriteria({ ...criteria, validity: e.target.value })}
                    className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
                  >
                    <option value="all">전체</option>
                    <option value="VALID">유효</option>
                    <option value="EXPIRED">만료</option>
                    <option value="NOT_YET_VALID">유효 전</option>
                  </select>
                </div>

                {/* Limit */}
                <div>
                  <label className="block text-sm font-medium text-gray-700 mb-2">
                    표시 개수
                  </label>
                  <select
                    value={criteria.limit}
                    onChange={(e) => setCriteria({ ...criteria, limit: Number(e.target.value), offset: 0 })}
                    className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
                  >
                    <option value="10">10</option>
                    <option value="25">25</option>
                    <option value="50">50</option>
                    <option value="100">100</option>
                    <option value="200">200</option>
                  </select>
                </div>
              </div>

              {/* Search Term */}
              <div>
                <label className="block text-sm font-medium text-gray-700 mb-2">
                  키워드 검색 (CN)
                </label>
                <div className="flex gap-2">
                  <input
                    type="text"
                    placeholder="인증서 CN 검색..."
                    value={criteria.searchTerm}
                    onChange={(e) => setCriteria({ ...criteria, searchTerm: e.target.value })}
                    onKeyPress={(e) => e.key === 'Enter' && handleSearch()}
                    className="flex-1 px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
                  />
                  <button
                    onClick={handleSearch}
                    disabled={loading}
                    className="px-6 py-2 bg-blue-600 text-white rounded-md hover:bg-blue-700 disabled:bg-gray-400 disabled:cursor-not-allowed flex items-center gap-2"
                  >
                    <Search className="w-4 h-4" />
                    검색
                  </button>
                </div>
              </div>

              {/* Export Country Button */}
              {criteria.country && (
                <div className="flex gap-2 pt-2 border-t">
                  <button
                    onClick={() => exportCountry(criteria.country, 'pem')}
                    className="px-4 py-2 bg-green-600 text-white rounded-md hover:bg-green-700 flex items-center gap-2"
                  >
                    <Download className="w-4 h-4" />
                    {criteria.country} 전체 내보내기 (PEM ZIP)
                  </button>
                  <button
                    onClick={() => exportCountry(criteria.country, 'der')}
                    className="px-4 py-2 bg-green-700 text-white rounded-md hover:bg-green-800 flex items-center gap-2"
                  >
                    <Download className="w-4 h-4" />
                    {criteria.country} 전체 내보내기 (DER ZIP)
                  </button>
                </div>
              )}
            </div>
          )}
        </div>

        {/* Results */}
        <div className="bg-white rounded-lg shadow-md overflow-hidden">
          {/* Results Header */}
          <div className="px-6 py-4 border-b border-gray-200 bg-gray-50">
            <div className="flex items-center justify-between">
              <div className="text-sm text-gray-600">
                총 <span className="font-semibold text-gray-900">{total.toLocaleString()}</span>개 인증서
                {total > 0 && (
                  <span className="ml-2">
                    ({criteria.offset + 1}-{Math.min(criteria.offset + criteria.limit, total)})
                  </span>
                )}
              </div>
              <div className="flex items-center gap-2">
                <button
                  onClick={handlePrevPage}
                  disabled={criteria.offset === 0}
                  className="px-3 py-1 text-sm border border-gray-300 rounded-md hover:bg-gray-50 disabled:opacity-50 disabled:cursor-not-allowed"
                >
                  이전
                </button>
                <button
                  onClick={handleNextPage}
                  disabled={criteria.offset + criteria.limit >= total}
                  className="px-3 py-1 text-sm border border-gray-300 rounded-md hover:bg-gray-50 disabled:opacity-50 disabled:cursor-not-allowed"
                >
                  다음
                </button>
              </div>
            </div>
          </div>

          {/* Loading/Error States */}
          {loading && (
            <div className="p-12 text-center">
              <div className="inline-block animate-spin rounded-full h-12 w-12 border-4 border-blue-600 border-t-transparent"></div>
              <p className="mt-4 text-gray-600">검색 중...</p>
            </div>
          )}

          {error && (
            <div className="p-12 text-center">
              <div className="text-red-600 font-semibold">{error}</div>
            </div>
          )}

          {/* Results Table */}
          {!loading && !error && certificates.length > 0 && (
            <div className="overflow-x-auto">
              <table className="w-full">
                <thead className="bg-gray-50 border-b border-gray-200">
                  <tr>
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      국가
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      종류
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      CN
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      Serial
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      유효기간
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      상태
                    </th>
                    <th className="px-6 py-3 text-right text-xs font-medium text-gray-500 uppercase tracking-wider">
                      작업
                    </th>
                  </tr>
                </thead>
                <tbody className="bg-white divide-y divide-gray-200">
                  {certificates.map((cert, index) => (
                    <tr key={index} className="hover:bg-gray-50">
                      <td className="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                        <div className="flex items-center gap-2">
                          {getFlagSvgPath(cert.country) && (
                            <img
                              src={getFlagSvgPath(cert.country)}
                              alt={cert.country}
                              className="w-6 h-4 object-cover rounded shadow-sm border border-gray-300"
                              onError={(e) => {
                                e.currentTarget.style.display = 'none';
                              }}
                            />
                          )}
                          <span>{cert.country}</span>
                        </div>
                      </td>
                      <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900">
                        <span className="px-2 py-1 text-xs font-semibold rounded bg-blue-100 text-blue-800">
                          {cert.certType}
                        </span>
                      </td>
                      <td className="px-6 py-4 text-sm text-gray-900 max-w-xs truncate" title={cert.cn}>
                        {cert.cn}
                      </td>
                      <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-500 font-mono">
                        {cert.sn.substring(0, 12)}...
                      </td>
                      <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-500">
                        {formatDate(cert.validFrom)} ~ {formatDate(cert.validTo)}
                      </td>
                      <td className="px-6 py-4 whitespace-nowrap">
                        {getValidityBadge(cert.validity)}
                      </td>
                      <td className="px-6 py-4 whitespace-nowrap text-right text-sm font-medium">
                        <div className="flex items-center justify-end gap-2">
                          <button
                            onClick={() => viewDetails(cert)}
                            className="text-blue-600 hover:text-blue-900"
                            title="상세보기"
                          >
                            <FileText className="w-4 h-4" />
                          </button>
                          <button
                            onClick={() => exportCertificate(cert.dn, 'pem')}
                            className="text-green-600 hover:text-green-900"
                            title="PEM 내보내기"
                          >
                            <Download className="w-4 h-4" />
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
            <div className="p-12 text-center text-gray-500">
              검색 결과가 없습니다.
            </div>
          )}
        </div>
      </div>

      {/* Certificate Detail Dialog */}
      {showDetailDialog && selectedCert && (
        <div className="fixed inset-0 bg-black bg-opacity-50 flex items-center justify-center z-50 p-4">
          <div className="bg-white rounded-lg shadow-xl max-w-4xl w-full max-h-[90vh] flex flex-col">
            {/* Dialog Header */}
            <div className="bg-white border-b border-gray-200 px-6 py-4 flex items-center justify-between">
              <h2 className="text-xl font-bold text-gray-900 flex items-center gap-2">
                <Shield className="w-6 h-6 text-blue-600" />
                Certificate Editor
              </h2>
              <button
                onClick={() => setShowDetailDialog(false)}
                className="text-gray-400 hover:text-gray-600"
              >
                <X className="w-6 h-6" />
              </button>
            </div>

            {/* Tabs */}
            <div className="border-b border-gray-200 bg-gray-50">
              <div className="flex">
                <button
                  onClick={() => setDetailTab('general')}
                  className={`px-6 py-3 text-sm font-medium border-b-2 transition-colors ${
                    detailTab === 'general'
                      ? 'border-blue-600 text-blue-600 bg-white'
                      : 'border-transparent text-gray-600 hover:text-gray-900 hover:bg-gray-100'
                  }`}
                >
                  General
                </button>
                <button
                  onClick={() => setDetailTab('details')}
                  className={`px-6 py-3 text-sm font-medium border-b-2 transition-colors ${
                    detailTab === 'details'
                      ? 'border-blue-600 text-blue-600 bg-white'
                      : 'border-transparent text-gray-600 hover:text-gray-900 hover:bg-gray-100'
                  }`}
                >
                  Details
                </button>
              </div>
            </div>

            {/* Dialog Content */}
            <div className="flex-1 overflow-y-auto p-6">
              {/* General Tab */}
              {detailTab === 'general' && (
                <div className="space-y-6">
                  {/* Issued To Section */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 mb-3 pb-2 border-b">Issued To</h3>
                    <div className="space-y-3">
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600">Common name (CN):</span>
                        <span className="text-sm text-gray-900 break-all">{selectedCert.cn}</span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600">Organization (O):</span>
                        <span className="text-sm text-gray-900">
                          {selectedCert.subjectDn.match(/O=([^,]+)/)?.[1] || '-'}
                        </span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600">Organizational unit (OU):</span>
                        <span className="text-sm text-gray-900">
                          {selectedCert.subjectDn.match(/OU=([^,]+)/)?.[1] || '-'}
                        </span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600">Serial number:</span>
                        <span className="text-sm text-gray-900 font-mono break-all">{selectedCert.sn}</span>
                      </div>
                    </div>
                  </div>

                  {/* Issued By Section */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 mb-3 pb-2 border-b">Issued By</h3>
                    <div className="space-y-3">
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600">Common name (CN):</span>
                        <span className="text-sm text-gray-900 break-all">
                          {selectedCert.issuerDn.match(/CN=([^,]+)/)?.[1] || '-'}
                        </span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600">Organization (O):</span>
                        <span className="text-sm text-gray-900">
                          {selectedCert.issuerDn.match(/O=([^,]+)/)?.[1] || '-'}
                        </span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600">Organizational unit (OU):</span>
                        <span className="text-sm text-gray-900">
                          {selectedCert.issuerDn.match(/OU=([^,]+)/)?.[1] || '-'}
                        </span>
                      </div>
                    </div>
                  </div>

                  {/* Validity Section */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 mb-3 pb-2 border-b">Validity</h3>
                    <div className="space-y-3">
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600">Issued on:</span>
                        <span className="text-sm text-gray-900">{formatDate(selectedCert.validFrom)}</span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600">Expires on:</span>
                        <span className="text-sm text-gray-900">{formatDate(selectedCert.validTo)}</span>
                      </div>
                    </div>
                  </div>

                  {/* Fingerprints Section */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 mb-3 pb-2 border-b">Fingerprints</h3>
                    <div className="space-y-3">
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600">SHA1 fingerprint:</span>
                        <span className="text-sm text-gray-900 font-mono break-all">
                          {selectedCert.fingerprint.substring(0, 40) || 'N/A'}
                        </span>
                      </div>
                      <div className="grid grid-cols-[140px_1fr] gap-2">
                        <span className="text-sm text-gray-600">MD5 fingerprint:</span>
                        <span className="text-sm text-gray-900 font-mono break-all">
                          {selectedCert.fingerprint.substring(0, 32) || 'N/A'}
                        </span>
                      </div>
                    </div>
                  </div>
                </div>
              )}

              {/* Details Tab */}
              {detailTab === 'details' && (
                <div className="space-y-4">
                  {/* Certificate Hierarchy */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 mb-3 pb-2 border-b">Certificate Hierarchy</h3>
                    <div className="bg-gray-50 p-4 rounded border">
                      <div className="text-sm font-mono text-blue-600 cursor-pointer hover:underline">
                        {selectedCert.cn}
                      </div>
                    </div>
                  </div>

                  {/* Certificate Fields Tree */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 mb-3 pb-2 border-b">Certificate Fields</h3>
                    <div className="bg-gray-50 p-4 rounded border max-h-96 overflow-y-auto">
                      <div className="space-y-2 text-sm font-mono">
                        <details open>
                          <summary className="cursor-pointer font-semibold text-gray-900 hover:text-blue-600">
                            Certificate
                          </summary>
                          <div className="ml-4 mt-2 space-y-2">
                            <details>
                              <summary className="cursor-pointer text-gray-700 hover:text-blue-600">Version</summary>
                              <div className="ml-4 text-gray-600">V3</div>
                            </details>
                            <details>
                              <summary className="cursor-pointer text-gray-700 hover:text-blue-600">Serial Number</summary>
                              <div className="ml-4 text-gray-600 break-all">{selectedCert.sn}</div>
                            </details>
                            <details>
                              <summary className="cursor-pointer text-gray-700 hover:text-blue-600">Signature</summary>
                              <div className="ml-4 space-y-1">
                                <div className="text-gray-600">Algorithm: RSA-PSS</div>
                                <div className="text-gray-600">Hash Algorithm: sha256</div>
                              </div>
                            </details>
                            <details>
                              <summary className="cursor-pointer text-gray-700 hover:text-blue-600">Issuer</summary>
                              <div className="ml-4 text-gray-600 break-all">{selectedCert.issuerDn}</div>
                            </details>
                            <details open>
                              <summary className="cursor-pointer text-gray-700 hover:text-blue-600">Validity</summary>
                              <div className="ml-4 space-y-1">
                                <div className="text-gray-600">Not Before: {formatDate(selectedCert.validFrom)}</div>
                                <div className="text-gray-600">Not After: {formatDate(selectedCert.validTo)}</div>
                              </div>
                            </details>
                            <details>
                              <summary className="cursor-pointer text-gray-700 hover:text-blue-600">Subject</summary>
                              <div className="ml-4 text-gray-600 break-all">{selectedCert.subjectDn}</div>
                            </details>
                            <details>
                              <summary className="cursor-pointer text-gray-700 hover:text-blue-600">Subject Public Key Info</summary>
                              <div className="ml-4 space-y-1">
                                <div className="text-gray-600">Subject Public Key Algorithm: RSA</div>
                                <div className="text-gray-600">Subject Public Key: (2048 bits)</div>
                              </div>
                            </details>
                          </div>
                        </details>
                      </div>
                    </div>
                  </div>

                  {/* Field Value */}
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 mb-3 pb-2 border-b">Field Value</h3>
                    <div className="bg-gray-50 p-4 rounded border">
                      <div className="text-sm text-gray-600 italic">Select a field from the tree above to view its value</div>
                    </div>
                  </div>
                </div>
              )}
            </div>

            {/* Dialog Footer */}
            <div className="border-t border-gray-200 px-6 py-4 bg-gray-50 flex justify-between items-center">
              <div className="flex gap-2">
                <button
                  onClick={() => exportCertificate(selectedCert.dn, 'pem')}
                  className="px-4 py-2 text-sm bg-white border border-gray-300 text-gray-700 rounded hover:bg-gray-50"
                >
                  Save Certificate...
                </button>
              </div>
              <div className="flex gap-2">
                <button
                  onClick={() => setShowDetailDialog(false)}
                  className="px-4 py-2 text-sm bg-blue-600 text-white rounded hover:bg-blue-700"
                >
                  OK
                </button>
                <button
                  onClick={() => setShowDetailDialog(false)}
                  className="px-4 py-2 text-sm bg-white border border-gray-300 text-gray-700 rounded hover:bg-gray-50"
                >
                  Cancel
                </button>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default CertificateSearch;
