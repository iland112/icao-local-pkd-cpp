import { useTranslation } from 'react-i18next';
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
  const { t } = useTranslation(['certificate', 'common']);
  return (
    <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg mb-4 p-5">
      <div className="flex items-center gap-2 mb-3">
        <Filter className="w-4 h-4 text-blue-500" />
        <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">{ t('certificate:search.filterLabel') }</h3>
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
              <label htmlFor="cert-country" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                {t('pa.history.country')}
              </label>
              <div className="relative">
                <select
                  id="cert-country"
                  name="country"
                  value={criteria.country}
                  onChange={(e) => setCriteria({ ...criteria, country: e.target.value })}
                  className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 appearance-none"
                  style={criteria.country && getFlagSvgPath(criteria.country) ? { paddingLeft: '2.5rem' } : {}}
                  disabled={countriesLoading}
                >
                  <option value="">
                    {countriesLoading ? 'Loading...' : countries.length === 0 ? 'No countries' : t('report.crl.allCountries')}
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
              <label htmlFor="cert-type" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                {t('certificate:filters.certType')}
              </label>
              <select
                id="cert-type"
                name="certType"
                value={criteria.certType}
                onChange={(e) => setCriteria({ ...criteria, certType: e.target.value })}
                className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                <option value="">{ t('monitoring:pool.total') }</option>
                <option value="CSCA">CSCA</option>
                <option value="MLSC">MLSC</option>
                <option value="DSC">DSC</option>
                <option value="DSC_NC">DSC_NC</option>
              </select>
            </div>

            {/* Validity */}
            <div>
              <label htmlFor="cert-validity" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                {t('certificate:filters.validity')}
              </label>
              <select
                id="cert-validity"
                name="validity"
                value={criteria.validity}
                onChange={(e) => setCriteria({ ...criteria, validity: e.target.value })}
                className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                <option value="all">{ t('monitoring:pool.total') }</option>
                <option value="VALID">{t('common:status.valid')}</option>
                <option value="EXPIRED_VALID">{ t('certificate:search.validity.expiredValid') }</option>
                <option value="EXPIRED">{t('common:status.expired')}</option>
                <option value="NOT_YET_VALID">{ t('certificate:search.validity.notYetValid') }</option>
              </select>
            </div>

            {/* Source */}
            <div>
              <label htmlFor="cert-source" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                {t('certificate.detail.sourceType')}
              </label>
              <select
                id="cert-source"
                name="source"
                value={criteria.source}
                onChange={(e) => setCriteria({ ...criteria, source: e.target.value })}
                className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              >
                <option value="">{ t('monitoring:pool.total') }</option>
                <option value="LDIF_PARSED">{ t('common:source.LDIF_UPLOAD') }</option>
                <option value="ML_PARSED">Master List</option>
                <option value="FILE_UPLOAD">{ t('nav:menu.fileUpload') }</option>
                <option value="PA_EXTRACTED">{ t('common:source.PA_EXTRACTED_short') }</option>
                <option value="DL_PARSED">{ t('common:source.DL_PARSED_short') }</option>
              </select>
            </div>

            {/* Limit */}
            <div>
              <label htmlFor="cert-limit" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                {t('certificate:filters.displayCount')}
              </label>
              <select
                id="cert-limit"
                name="limit"
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
              <label htmlFor="cert-search" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
                {t('certificate:filters.keywordSearch')}
              </label>
              <div className="relative">
                <Search className="absolute left-2.5 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                <input
                  id="cert-search"
                  name="searchTerm"
                  type="text"
                  placeholder={t('certificate.search.cnSearch')}
                  value={criteria.searchTerm}
                  onChange={(e) => setCriteria({ ...criteria, searchTerm: e.target.value })}
                  onKeyDown={(e) => e.key === 'Enter' && handleSearch()}
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
              {t('certificate:filters.search')}
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
                {t('certificate:filters.exportAllPem')}
              </button>
              <button
                onClick={() => exportAll('der')}
                disabled={exportAllLoading}
                className="px-4 py-2 bg-indigo-700 text-white rounded-lg hover:bg-indigo-800 disabled:bg-gray-400 disabled:cursor-not-allowed flex items-center gap-2 text-sm font-medium transition-colors"
              >
                {exportAllLoading ? <Loader2 className="w-4 h-4 animate-spin" /> : <Archive className="w-4 h-4" />}
                {t('certificate:filters.exportAllDer')}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default CertificateSearchFilters;
