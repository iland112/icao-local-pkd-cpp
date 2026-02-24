import React from 'react';
import { Search, Download, Filter, ChevronDown, ChevronUp, Archive, Loader2 } from 'lucide-react';
import { getFlagSvgPath } from '@/utils/countryCode';
import { getCountryDisplayName } from '@/utils/countryNames';

export interface SearchCriteria {
  country: string;
  certType: string;
  validity: string;
  source: string;
  searchTerm: string;
  limit: number;
  offset: number;
}

interface CertificateSearchFiltersProps {
  criteria: SearchCriteria;
  setCriteria: React.Dispatch<React.SetStateAction<SearchCriteria>>;
  countries: string[];
  countriesLoading: boolean;
  loading: boolean;
  showFilters: boolean;
  setShowFilters: (show: boolean) => void;
  handleSearch: () => void;
  exportCountry: (country: string, format: 'der' | 'pem') => void;
  exportAll: (format: 'pem' | 'der') => void;
  exportAllLoading: boolean;
}

const CertificateSearchFilters: React.FC<CertificateSearchFiltersProps> = ({
  criteria,
  setCriteria,
  countries,
  countriesLoading,
  loading,
  showFilters,
  setShowFilters,
  handleSearch,
  exportCountry,
  exportAll,
  exportAllLoading,
}) => {
  return (
    <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg mb-4 p-5">
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
                <option value="EXPIRED_VALID">만료-유효</option>
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
            <div className="ml-auto flex gap-2">
              <button
                onClick={() => exportAll('pem')}
                disabled={exportAllLoading}
                className="px-4 py-2 bg-indigo-600 text-white rounded-lg hover:bg-indigo-700 disabled:bg-gray-400 disabled:cursor-not-allowed flex items-center gap-2 text-sm font-medium transition-colors"
              >
                {exportAllLoading ? <Loader2 className="w-4 h-4 animate-spin" /> : <Archive className="w-4 h-4" />}
                전체 내보내기 PEM
              </button>
              <button
                onClick={() => exportAll('der')}
                disabled={exportAllLoading}
                className="px-4 py-2 bg-indigo-700 text-white rounded-lg hover:bg-indigo-800 disabled:bg-gray-400 disabled:cursor-not-allowed flex items-center gap-2 text-sm font-medium transition-colors"
              >
                {exportAllLoading ? <Loader2 className="w-4 h-4 animate-spin" /> : <Archive className="w-4 h-4" />}
                전체 내보내기 DER
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default CertificateSearchFilters;
