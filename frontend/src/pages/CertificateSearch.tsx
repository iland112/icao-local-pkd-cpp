import React, { useState, useEffect, useMemo, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import { DEFAULT_PAGE_SIZE } from '@/config/pagination';
import { useSearchParams } from 'react-router-dom';
import { useSortableTable } from '@/hooks/useSortableTable';
import { SortableHeader } from '@/components/common/SortableHeader';
import { Download, FileText, CheckCircle, XCircle, Clock, RefreshCw, Eye, ChevronLeft, ChevronRight, Shield, HelpCircle } from 'lucide-react';
import { getFlagSvgPath } from '@/utils/countryCode';
import { getCountryName } from '@/utils/countryNames';
import { cn } from '@/utils/cn';
import {
  formatDate,
  formatVersion,
  formatSignatureAlgorithm,
  getActualCertType,
  isMasterListSignerCertificate,
} from '@/utils/certificateDisplayUtils';
import { getGlossaryTooltip } from '@/components/common';
import { toast } from '@/stores/toastStore';
import { validationApi } from '@/api/validationApi';
import { certificateApi } from '@/services/pkdApi';
import pkdApi from '@/services/pkdApi';
import type { ValidationResult } from '@/types/validation';
import CertificateDetailDialog from '@/components/CertificateDetailDialog';
import type { Certificate } from '@/components/CertificateDetailDialog';
import CertificateSearchFilters from '@/components/CertificateSearchFilters';
import type { SearchCriteria } from '@/components/CertificateSearchFilters';

const CertificateSearch: React.FC = () => {
  const { t } = useTranslation(['certificate', 'common']);
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
    limit: DEFAULT_PAGE_SIZE,
    offset: 0,
  });

  // AbortController ref for cancelling stale search requests
  const searchAbortRef = useRef<AbortController | null>(null);

  const { sortedData: sortedCerts, sortConfig: certSortConfig, requestSort: requestCertSort } = useSortableTable<Certificate>(certificates);

  // UI state
  const [showFilters, setShowFilters] = useState(true);
  const [selectedCert, setSelectedCert] = useState<Certificate | null>(null);
  const [showDetailDialog, setShowDetailDialog] = useState(false);
  const [detailTab, setDetailTab] = useState<'general' | 'details' | 'doc9303' | 'forensic'>('general');
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
    // Abort previous pending request
    if (searchAbortRef.current) {
      searchAbortRef.current.abort();
    }
    const abortController = new AbortController();
    searchAbortRef.current = abortController;

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

      const response = await pkdApi.get('/certificates/search', { params, signal: abortController.signal });
      const data = response.data as Record<string, unknown>;

      if (data.success) {
        const certs = data.certificates as Certificate[];
        setCertificates(certs);
        setTotal(data.total as number);
        // Store statistics from backend (if available)
        if (data.stats) {
          setApiStats(data.stats as { total: number; valid: number; expired: number; notYetValid: number; unknown: number });
        }
        // Auto-open detail dialog when navigated with fingerprint query param
        if (autoOpenFingerprintRef.current && certs.length > 0) {
          const match = certs.find(c => c.fingerprint === autoOpenFingerprintRef.current);
          autoOpenFingerprintRef.current = null;
          if (match) {
            viewDetails(match);
          }
        }
      } else {
        setError((data.error as string) || 'Search failed');
      }
    } catch (err) {
      // Ignore aborted requests — a newer request superseded this one
      if (err instanceof Error && err.name === 'CanceledError') return;
      setError(err instanceof Error ? err.message : 'Failed to search certificates');
    } finally {
      // Only clear loading if this is still the active request
      if (searchAbortRef.current === abortController) {
        setLoading(false);
      }
    }
  };

  const [searchParams, setSearchParams] = useSearchParams();
  const autoOpenFingerprintRef = useRef<string | null>(null);

  // Initial load - fetch countries once + handle fingerprint query param
  useEffect(() => {
    fetchCountries();

    // Auto-lookup by fingerprint from URL query param (e.g. from Trust Chain report)
    const fp = searchParams.get('fingerprint');
    if (fp) {
      // Clear the query param to avoid re-triggering
      setSearchParams({}, { replace: true });
      // Mark fingerprint for auto-open when search results arrive
      autoOpenFingerprintRef.current = fp;
      // Set search term to fingerprint and trigger search
      setCriteria(prev => ({ ...prev, searchTerm: fp, offset: 0 }));
    }
  }, []);

  // Search when criteria changes (AbortController covers pagination too via criteria.offset)
  useEffect(() => {
    searchCertificates();

    // Cleanup: abort in-flight request when criteria changes or component unmounts
    return () => {
      if (searchAbortRef.current) {
        searchAbortRef.current.abort();
      }
    };
  }, [criteria.country, criteria.certType, criteria.validity, criteria.source, criteria.searchTerm, criteria.limit, criteria.offset]);

  // Handle search button click
  const handleSearch = () => {
    setCriteria({ ...criteria, offset: 0 });
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
        toast.error(t('search.exportFailed'), t('search.exportError'));
      }
    } catch (err) {
      toast.error(t('search.exportFailed'), err instanceof Error ? err.message : t('common:error.unknownError'));
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
        toast.error(t('search.exportFailed'), t('search.exportError'));
      }
    } catch (err) {
      toast.error(t('search.exportFailed'), err instanceof Error ? err.message : t('common:error.unknownError'));
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
        toast.error(t('certificate:search.exportFailed'), errorData?.error || response.statusText);
      }
    } catch (err) {
      toast.error(t('search.exportFailed'), err instanceof Error ? err.message : t('common:error.unknownError'));
    } finally {
      setExportAllLoading(false);
    }
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

  // Get certificate type badge with tooltip
  const getCertTypeBadge = (certType: string) => {
    const colorMap: Record<string, string> = {
      'CSCA': 'bg-blue-100 dark:bg-blue-900/40 text-blue-800 dark:text-blue-300',
      'MLSC': 'bg-purple-100 dark:bg-purple-900/40 text-purple-800 dark:text-purple-300',
      'DSC': 'bg-green-100 dark:bg-green-900/40 text-green-800 dark:text-green-300',
      'DSC_NC': 'bg-orange-100 dark:bg-orange-900/40 text-orange-800 dark:text-orange-300',
      'CRL': 'bg-red-100 dark:bg-red-900/40 text-red-800 dark:text-red-300',
      'ML': 'bg-indigo-100 dark:bg-indigo-900/40 text-indigo-800 dark:text-indigo-300',
    };

    const colors = colorMap[certType] || 'bg-gray-100 dark:bg-gray-700 text-gray-800 dark:text-gray-300';

    return (
      <span
        className={`inline-flex items-center gap-1 px-1.5 py-0.5 text-xs font-semibold rounded ${colors} cursor-help`}
        title={getGlossaryTooltip(certType)}
      >
        {certType}
        <HelpCircle className="w-2.5 h-2.5 opacity-40" />
      </span>
    );
  };

  // Get validity badge
  const getValidityBadge = (validity: string) => {
    const configs: Record<string, { icon: React.ReactNode; label: string; colors: string }> = {
      'VALID': { icon: <CheckCircle className="w-3 h-3" />, label: t('search.validity.valid'), colors: 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400' },
      'EXPIRED_VALID': { icon: <CheckCircle className="w-3 h-3" />, label: t('search.validity.expiredValid'), colors: 'bg-amber-100 dark:bg-amber-900/30 text-amber-700 dark:text-amber-400' },
      'EXPIRED': { icon: <XCircle className="w-3 h-3" />, label: t('search.validity.expired'), colors: 'bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400' },
      'NOT_YET_VALID': { icon: <Clock className="w-3 h-3" />, label: t('search.validity.notYetValid'), colors: 'bg-yellow-100 dark:bg-yellow-900/30 text-yellow-700 dark:text-yellow-400' },
    };
    const config = configs[validity] || { icon: null, label: t('search.validity.unknown'), colors: 'bg-gray-100 dark:bg-gray-700 text-gray-700 dark:text-gray-400' };
    return (
      <span className={`inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium ${config.colors}`}>
        {config.icon}
        {config.label}
      </span>
    );
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
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">{t('search.title')}</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              {t('search.subtitle')}
            </p>
          </div>
          <button
            onClick={() => {
              searchCertificates();
            }}
            disabled={loading}
            className="inline-flex items-center gap-2 px-3 py-2 rounded-lg text-sm font-medium text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            aria-label={t('common:button.refresh')}
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
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 border-l-4 border-blue-500">
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
                    title={getCountryName(criteria.country)}
                    className="w-6 h-4 object-cover rounded shadow-sm border border-gray-300 dark:border-gray-500"
                    onError={(e) => {
                      e.currentTarget.style.display = 'none';
                    }}
                  />
                )}
                <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">
                  {criteria.country ? t('search.countryTotalCerts', { country: criteria.country }) : t('search.totalCerts')}
                </p>
              </div>
              <p className="text-2xl font-bold text-blue-600 dark:text-blue-400">{stats.total.toLocaleString()}</p>
            </div>
          </div>

          {/* Breakdown Cards - Nested */}
          <div className="grid grid-cols-1 sm:grid-cols-3 gap-3 pt-4 border-t border-gray-200 dark:border-gray-700">
            {/* Valid */}
            <div className="bg-green-50 dark:bg-green-900/20 rounded-lg p-3 border border-green-200 dark:border-green-700">
              <div className="flex items-center gap-2 mb-1">
                <CheckCircle className="w-4 h-4 text-green-500" />
                <p className="text-xs text-gray-600 dark:text-gray-400 font-medium">{t('search.validity.valid')}</p>
              </div>
              <p className="text-lg font-bold text-green-600 dark:text-green-400">{stats.valid.toLocaleString()}</p>
              <p className="text-xs text-gray-500 dark:text-gray-400">{stats.validPercent}%</p>
            </div>

            {/* Expired */}
            <div className="bg-red-50 dark:bg-red-900/20 rounded-lg p-3 border border-red-200 dark:border-red-700">
              <div className="flex items-center gap-2 mb-1">
                <XCircle className="w-4 h-4 text-red-500" />
                <p className="text-xs text-gray-600 dark:text-gray-400 font-medium">{t('search.validity.expired')}</p>
              </div>
              <p className="text-lg font-bold text-red-600 dark:text-red-400">{stats.expired.toLocaleString()}</p>
              <p className="text-xs text-gray-500 dark:text-gray-400">{stats.expiredPercent}%</p>
            </div>

            {/* Not Yet Valid */}
            <div className="bg-yellow-50 dark:bg-yellow-900/20 rounded-lg p-3 border border-yellow-200 dark:border-yellow-700">
              <div className="flex items-center gap-2 mb-1">
                <Clock className="w-4 h-4 text-yellow-500" />
                <p className="text-xs text-gray-600 dark:text-gray-400 font-medium">{t('search.validity.notYetValid')}</p>
              </div>
              <p className="text-lg font-bold text-yellow-600 dark:text-yellow-400">{stats.notYetValid.toLocaleString()}</p>
            </div>
          </div>
        </div>
      </div>

      {/* Results Table */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg overflow-hidden">
        {/* Table Header */}
        <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center justify-between">
          <div className="flex items-center gap-2">
            <FileText className="w-4 h-4 text-blue-500" />
            <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">{t('search.searchResults')}</h3>
            <span className="px-2 py-0.5 text-xs rounded-full bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300">
              {t('search.resultCount', { num: certificates.length })}
            </span>
          </div>
          <div className="flex items-center gap-2">
            <button
              onClick={handlePrevPage}
              disabled={criteria.offset === 0}
              className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
              aria-label={t('common:pagination.prev')}
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
              aria-label={t('common:pagination.next')}
            >
              <ChevronRight className="w-4 h-4" />
            </button>
          </div>
        </div>

        {/* Loading State */}
        {loading && (
          <div className="flex items-center justify-center py-20" aria-live="polite">
            <RefreshCw className="w-8 h-8 animate-spin text-blue-500" />
            <p className="ml-3 text-gray-600 dark:text-gray-400">{t('search.searching')}</p>
          </div>
        )}

        {/* Error State */}
        {error && (
          <div className="p-12 text-center" role="alert">
            <div className="text-red-600 dark:text-red-400 font-semibold">{error}</div>
          </div>
        )}

        {/* Results Table */}
        {!loading && !error && certificates.length > 0 && (
          <div className="overflow-x-auto">
            <table className="w-full">
              <thead className="bg-slate-100 dark:bg-gray-700">
                <tr>
                  <SortableHeader label={t('search.country')} sortKey="country" sortConfig={certSortConfig} onSort={requestCertSort} className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                  <SortableHeader label={t('search.certType')} sortKey="certType" sortConfig={certSortConfig} onSort={requestCertSort} className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                  <SortableHeader label={t('search.issuer')} sortKey="issuerDn" sortConfig={certSortConfig} onSort={requestCertSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider" />
                  <SortableHeader label={t('search.version')} sortKey="version" sortConfig={certSortConfig} onSort={requestCertSort} className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                  <SortableHeader label={t('search.signatureAlgorithm')} sortKey="signatureAlgorithm" sortConfig={certSortConfig} onSort={requestCertSort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                  <SortableHeader label={t('search.validPeriod')} sortKey="validFrom" sortConfig={certSortConfig} onSort={requestCertSort} className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                  <SortableHeader label={t('common:label.status')} sortKey="validity" sortConfig={certSortConfig} onSort={requestCertSort} className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                  <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                    {t('search.action')}
                  </th>
                </tr>
              </thead>
              <tbody className="bg-white dark:bg-gray-800 divide-y divide-gray-200 dark:divide-gray-700">
                {sortedCerts.map((cert) => (
                  <tr
                    key={cert.fingerprint}
                    className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors"
                  >
                    <td className="px-3 py-2 whitespace-nowrap text-center">
                      <div className="flex items-center justify-center gap-1.5">
                        {getFlagSvgPath(cert.country) && (
                          <img
                            src={getFlagSvgPath(cert.country)}
                            alt={cert.country}
                            title={getCountryName(cert.country)}
                            className="w-5 h-3.5 object-cover rounded shadow-sm border border-gray-300 dark:border-gray-500"
                            onError={(e) => {
                              e.currentTarget.style.display = 'none';
                            }}
                          />
                        )}
                        <span className="text-xs font-medium text-gray-900 dark:text-gray-100">{cert.country}</span>
                      </div>
                    </td>
                    <td className="px-3 py-2 whitespace-nowrap text-center">
                      <div className="flex items-center justify-center gap-1">
                        {getCertTypeBadge(getActualCertType(cert))}
                        {getActualCertType(cert) === 'CSCA' && !isMasterListSignerCertificate(cert) && (
                          cert.isSelfSigned ? (
                            <span className="inline-flex items-center px-1.5 py-0.5 text-xs font-semibold rounded bg-cyan-100 dark:bg-cyan-900/40 text-cyan-800 dark:text-cyan-300">
                              SS
                            </span>
                          ) : (
                            <span className="inline-flex items-center px-1.5 py-0.5 text-xs font-semibold rounded bg-orange-100 dark:bg-orange-900/40 text-orange-800 dark:text-orange-300">
                              LC
                            </span>
                          )
                        )}
                      </div>
                    </td>
                    <td className="px-3 py-2 text-xs text-gray-900 dark:text-gray-100 max-w-[200px] truncate" title={cert.issuerDnComponents?.organization || cert.issuerDnComponents?.commonName || 'N/A'}>
                      {cert.issuerDnComponents?.organization || cert.issuerDnComponents?.commonName || 'N/A'}
                    </td>
                    <td className="px-3 py-2 whitespace-nowrap text-center text-xs text-gray-600 dark:text-gray-300">
                      {formatVersion(cert.version)}
                    </td>
                    <td className="px-3 py-2 text-xs text-gray-600 dark:text-gray-300 whitespace-nowrap" title={cert.signatureAlgorithm || 'N/A'}>
                      {formatSignatureAlgorithm(cert.signatureAlgorithm)}
                    </td>
                    <td className="px-3 py-2 whitespace-nowrap text-center text-xs text-gray-600 dark:text-gray-300">
                      {formatDate(cert.validFrom)} ~ {formatDate(cert.validTo)}
                    </td>
                    <td className="px-3 py-2 whitespace-nowrap text-center">
                      {getValidityBadge(cert.validity)}
                    </td>
                    <td className="px-3 py-2 whitespace-nowrap text-right">
                      <div className="flex items-center justify-end gap-1">
                        <button
                          onClick={() => viewDetails(cert)}
                          className="inline-flex items-center gap-1 px-2.5 py-1 rounded-md text-xs font-medium text-blue-600 dark:text-blue-400 hover:bg-blue-50 dark:hover:bg-blue-900/20 transition-colors"
                          title={t('search.viewDetail')}
                        >
                          <Eye className="w-3.5 h-3.5" />
                          {t('search.detail')}
                        </button>
                        <button
                          onClick={() => exportCertificate(cert.dn, 'pem')}
                          className="inline-flex items-center gap-1 px-2.5 py-1 rounded-md text-xs font-medium text-green-600 dark:text-green-400 hover:bg-green-50 dark:hover:bg-green-900/20 transition-colors"
                          title={t('search.exportPem')}
                        >
                          <Download className="w-3.5 h-3.5" />
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
            <p className="text-lg font-medium">{t('search.noResults')}</p>
            <p className="text-sm">{t('search.noResultsHint')}</p>
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
          getCertTypeBadge={getCertTypeBadge}
        />
      )}
    </div>
  );
};

export default CertificateSearch;
