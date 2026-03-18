import { useTranslation } from 'react-i18next';
import { useState, useEffect, useMemo } from 'react';
import { DEFAULT_PAGE_SIZE } from '@/config/pagination';
import { useSortableTable } from '@/hooks/useSortableTable';
import { SortableHeader } from '@/components/common/SortableHeader';
import { Link } from 'react-router-dom';
import {
  History,
  ShieldCheck,
  CheckCircle,
  XCircle,
  AlertCircle,
  ChevronLeft,
  ChevronRight,
  Search,
  Filter,
  Eye,
  Loader2,
  Calendar,
  RefreshCw,
  FileText,
  X,
  Info,
  User,
  AlertTriangle,
} from 'lucide-react';
import { paApi } from '@/services/paApi';
import type { PAHistoryItem, PAStatus } from '@/types';
import { cn } from '@/utils/cn';
import { getFlagSvgPath } from '@/utils/countryCode';
import { getCountryName } from '@/utils/countryNames';
import { formatDateTime } from '@/utils/dateFormat';

// DG1 MRZ Data interface
interface DG1Data {
  surname?: string;
  givenNames?: string;
  documentNumber?: string;
  nationality?: string;
  sex?: string;
  dateOfBirth?: string;
  expirationDate?: string;
}

// DG2 Face Image interface
interface DG2Data {
  faceImages?: Array<{
    imageDataUrl?: string;
    imageFormat?: string;
  }>;
}

// DG Data response interface
interface DGDataResponse {
  hasDg1: boolean;
  hasDg2: boolean;
  dg1?: DG1Data;
  dg2?: DG2Data;
}

export function PAHistory() {
  const { t } = useTranslation(['pa', 'common']);
  const [history, setHistory] = useState<PAHistoryItem[]>([]);
  const [loading, setLoading] = useState(true);
  const [page, setPage] = useState(0);
  const [totalPages, setTotalPages] = useState(0);
  const [totalElements, setTotalElements] = useState(0);

  // Filters
  const [searchTerm, setSearchTerm] = useState('');
  const [statusFilter, setStatusFilter] = useState<PAStatus | ''>('');
  const [countryFilter, setCountryFilter] = useState('');
  const [dateFrom, setDateFrom] = useState('');
  const [dateTo, setDateTo] = useState('');

  // Modal state
  const [selectedRecord, setSelectedRecord] = useState<PAHistoryItem | null>(null);
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [dgData, setDgData] = useState<DGDataResponse | null>(null);
  const [dgLoading, setDgLoading] = useState(false);
  const [dgError, setDgError] = useState<string | null>(null);

  // Global statistics (independent of pagination)
  const [globalStats, setGlobalStats] = useState<{
    total: number;
    valid: number;
    expiredValid: number;
    invalid: number;
    error: number;
  }>({ total: 0, valid: 0, expiredValid: 0, invalid: 0, error: 0 });

  const pageSize = DEFAULT_PAGE_SIZE;

  useEffect(() => {
    fetchStatistics();
  }, []);

  useEffect(() => {
    fetchHistory();
  }, [page]);

  const fetchStatistics = async () => {
    try {
      const resp = await paApi.getStatistics();
      const data = resp.data;
      const byStatus = data.byStatus || {};
      const valid = byStatus['VALID'] || 0;
      const expiredValid = byStatus['EXPIRED_VALID'] || 0;
      const invalid = byStatus['INVALID'] || 0;
      const error = byStatus['ERROR'] || 0;
      setGlobalStats({
        total: data.totalVerifications || 0,
        valid,
        expiredValid,
        invalid,
        error,
      });
    } catch (err) {
      if (import.meta.env.DEV) console.error('Failed to fetch PA statistics:', err);
    }
  };

  const fetchHistory = async () => {
    setLoading(true);
    try {
      const response = await paApi.getHistory({
        page,
        size: pageSize,
        sort: 'verifiedAt',
        direction: 'DESC',
      });
      // Backend returns: { success, total, page, size, data: [...] }
      const resData = response.data;
      const items = resData.data ?? [];
      const total = resData.total ?? 0;
      setHistory(items);
      setTotalPages(Math.ceil(total / pageSize) || 1);
      setTotalElements(total);
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to fetch PA history:', error);
      setHistory([]);
    } finally {
      setLoading(false);
    }
  };

  // Statistics derived from global stats (not current page)
  const stats = useMemo(() => {
    const { total, valid, expiredValid, invalid, error } = globalStats;
    return {
      total,
      valid,
      expiredValid,
      invalid,
      error,
      validPercent: total > 0 ? Math.round((valid / total) * 100) : 0,
      expiredValidPercent: total > 0 ? Math.round((expiredValid / total) * 100) : 0,
      invalidPercent: total > 0 ? Math.round((invalid / total) * 100) : 0,
      errorPercent: total > 0 ? Math.round((error / total) * 100) : 0,
    };
  }, [globalStats]);

  // Extract unique countries from history
  const uniqueCountries = useMemo(() => {
    const countries = new Set<string>();
    history.forEach((item) => {
      if (item.issuingCountry) {
        countries.add(item.issuingCountry);
      }
    });
    return Array.from(countries).sort();
  }, [history]);

  const getStatusBadge = (status: PAStatus) => {
    const styles: Record<PAStatus, { bg: string; text: string; icon: React.ReactNode }> = {
      VALID: {
        bg: 'bg-green-100 dark:bg-green-900/30',
        text: 'text-green-600 dark:text-green-400',
        icon: <CheckCircle className="w-3 h-3" />,
      },
      EXPIRED_VALID: {
        bg: 'bg-amber-100 dark:bg-amber-900/30',
        text: 'text-amber-600 dark:text-amber-400',
        icon: <AlertTriangle className="w-3 h-3" />,
      },
      INVALID: {
        bg: 'bg-red-100 dark:bg-red-900/30',
        text: 'text-red-600 dark:text-red-400',
        icon: <XCircle className="w-3 h-3" />,
      },
      ERROR: {
        bg: 'bg-yellow-100 dark:bg-yellow-900/30',
        text: 'text-yellow-600 dark:text-yellow-400',
        icon: <AlertCircle className="w-3 h-3" />,
      },
    };

    const style = styles[status];
    if (!style) {
      return <span className="inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium bg-gray-100 text-gray-600">{status}</span>;
    }
    const label: Record<PAStatus, string> = {
      VALID: t('common:status.valid'),
      EXPIRED_VALID: t('common:status.expiredValid'),
      INVALID: t('common:status.invalid'),
      ERROR: t('pa:history.errorVerifications'),
    };

    return (
      <span className={cn('inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium', style.bg, style.text)}>
        {style.icon}
        {label[status]}
      </span>
    );
  };

  const filteredHistory = history.filter((item) => {
    const matchesSearch =
      !searchTerm ||
      item.documentNumber?.toLowerCase().includes(searchTerm.toLowerCase()) ||
      item.issuingCountry?.toLowerCase().includes(searchTerm.toLowerCase());
    const matchesStatus = !statusFilter || item.status === statusFilter;
    const matchesCountry = !countryFilter || item.issuingCountry === countryFilter;

    // Date range filter
    let matchesDateFrom = true;
    let matchesDateTo = true;
    if (dateFrom && item.verificationTimestamp) {
      matchesDateFrom = new Date(item.verificationTimestamp) >= new Date(dateFrom);
    }
    if (dateTo && item.verificationTimestamp) {
      matchesDateTo = new Date(item.verificationTimestamp) <= new Date(dateTo + 'T23:59:59');
    }

    return matchesSearch && matchesStatus && matchesCountry && matchesDateFrom && matchesDateTo;
  });

  const clearFilters = () => {
    setSearchTerm('');
    setStatusFilter('');
    setCountryFilter('');
    setDateFrom('');
    setDateTo('');
  };

  const hasActiveFilters = searchTerm || statusFilter || countryFilter || dateFrom || dateTo;

  const { sortedData: sortedHistory, sortConfig: historySortConfig, requestSort: requestHistorySort } = useSortableTable<PAHistoryItem>(filteredHistory);

  // Open detail modal
  const openDetailModal = async (record: PAHistoryItem) => {
    setSelectedRecord(record);
    setIsModalOpen(true);
    setDgData(null);
    setDgError(null);

    // Load DG data for VALID full verification records (not LOOKUP)
    if (record.status === 'VALID' && record.verificationId && record.verificationType !== 'LOOKUP') {
      setDgLoading(true);
      try {
        const response = await paApi.getDataGroups(record.verificationId);
        setDgData(response.data as DGDataResponse);
      } catch (error) {
        if (import.meta.env.DEV) console.error('Failed to load DG data:', error);
        setDgError(t('pa:history.dgLoadFailed'));
      } finally {
        setDgLoading(false);
      }
    }
  };

  // Close detail modal
  const closeDetailModal = () => {
    setIsModalOpen(false);
    setSelectedRecord(null);
    setDgData(null);
    setDgError(null);
  };

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-purple-500 to-pink-600 shadow-lg">
            <History className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">{t('history.title')}</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              {t('pa:history.pageSubtitle')}
            </p>
          </div>
          <div className="flex gap-2">
            <button
              onClick={fetchHistory}
              disabled={loading}
              className="inline-flex items-center gap-2 px-3 py-2 rounded-lg text-sm font-medium text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            >
              <RefreshCw className={cn('w-4 h-4', loading && 'animate-spin')} />
            </button>
            <Link
              to="/pa/verify"
              className="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-white bg-gradient-to-r from-teal-500 to-cyan-500 hover:from-teal-600 hover:to-cyan-600 transition-all shadow-md hover:shadow-lg"
            >
              <ShieldCheck className="w-4 h-4" />
              {t('pa:history.newVerification')}
            </Link>
          </div>
        </div>
      </div>

      {/* Statistics Cards */}
      <div className="grid grid-cols-2 md:grid-cols-5 gap-3 mb-4">
        {/* Total */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-blue-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-blue-50 dark:bg-blue-900/30">
              <FileText className="w-5 h-5 text-blue-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">{t('pa:history.totalVerifications')}</p>
              <p className="text-xl font-bold text-blue-600 dark:text-blue-400">{stats.total.toLocaleString()}</p>
            </div>
          </div>
        </div>

        {/* Valid */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-green-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-green-50 dark:bg-green-900/30">
              <CheckCircle className="w-5 h-5 text-green-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">{t('pa:history.successVerifications')}</p>
              <p className="text-xl font-bold text-green-600 dark:text-green-400">{stats.valid}</p>
              <p className="text-xs text-gray-400">{stats.validPercent}%</p>
            </div>
          </div>
        </div>

        {/* Expired Valid */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-amber-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-amber-50 dark:bg-amber-900/30">
              <AlertTriangle className="w-5 h-5 text-amber-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">{t('common:status.expiredValid')}</p>
              <p className="text-xl font-bold text-amber-600 dark:text-amber-400">{stats.expiredValid}</p>
              <p className="text-xs text-gray-400">{stats.expiredValidPercent}%</p>
            </div>
          </div>
        </div>

        {/* Invalid */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-red-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-red-50 dark:bg-red-900/30">
              <XCircle className="w-5 h-5 text-red-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">{t('pa:history.failedVerifications')}</p>
              <p className="text-xl font-bold text-red-600 dark:text-red-400">{stats.invalid}</p>
              <p className="text-xs text-gray-400">{stats.invalidPercent}%</p>
            </div>
          </div>
        </div>

        {/* Error */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-yellow-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-yellow-50 dark:bg-yellow-900/30">
              <AlertCircle className="w-5 h-5 text-yellow-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">{t('pa:history.errorVerifications')}</p>
              <p className="text-xl font-bold text-yellow-600 dark:text-yellow-400">{stats.error}</p>
              <p className="text-xs text-gray-400">{stats.errorPercent}%</p>
            </div>
          </div>
        </div>
      </div>

      {/* Filters Card */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md mb-4 p-4">
        <div className="flex items-center gap-2 mb-3">
          <Filter className="w-4 h-4 text-blue-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">{t('pa:history.filterLabel')}</h3>
        </div>

        <div className="grid grid-cols-2 md:grid-cols-5 gap-3">
          {/* Country Filter */}
          <div>
            <label htmlFor="pa-country" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('pa:history.filterCountry')}
            </label>
            <select
              id="pa-country"
              name="countryFilter"
              value={countryFilter}
              onChange={(e) => setCountryFilter(e.target.value)}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="">{t('pa:history.allCountries')}</option>
              {uniqueCountries.map((country) => (
                <option key={country} value={country}>{country}</option>
              ))}
            </select>
          </div>

          {/* Status Filter */}
          <div>
            <label htmlFor="pa-status" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('pa:history.filterStatus')}
            </label>
            <select
              id="pa-status"
              name="statusFilter"
              value={statusFilter}
              onChange={(e) => setStatusFilter(e.target.value as PAStatus | '')}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="">{t('pa:history.allStatuses')}</option>
              <option value="VALID">{t('common:status.valid')}</option>
              <option value="EXPIRED_VALID">{t('common:status.expiredValid')}</option>
              <option value="INVALID">{t('common:status.invalid')}</option>
              <option value="ERROR">{t('pa:history.errorVerifications')}</option>
            </select>
          </div>

          {/* Date From */}
          <div>
            <label htmlFor="pa-date-from" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('pa:history.dateFrom')}
            </label>
            <input
              id="pa-date-from"
              name="dateFrom"
              type="date"
              value={dateFrom}
              onChange={(e) => setDateFrom(e.target.value)}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          {/* Date To */}
          <div>
            <label htmlFor="pa-date-to" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('pa:history.dateTo')}
            </label>
            <input
              id="pa-date-to"
              name="dateTo"
              type="date"
              value={dateTo}
              onChange={(e) => setDateTo(e.target.value)}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          {/* Search & Actions */}
          <div>
            <label htmlFor="pa-search" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('common:button.search')}
            </label>
            <div className="flex gap-2">
              <div className="relative flex-1">
                <Search className="absolute left-2.5 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                <input
                  id="pa-search"
                  name="searchTerm"
                  type="text"
                  placeholder={t('pa:history.searchPlaceholder')}
                  value={searchTerm}
                  onChange={(e) => setSearchTerm(e.target.value)}
                  className="w-full pl-8 pr-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
                />
              </div>
              {hasActiveFilters && (
                <button
                  onClick={clearFilters}
                  className="px-3 py-2 text-xs font-medium text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200 border border-gray-200 dark:border-gray-600 rounded-lg hover:bg-gray-50 dark:hover:bg-gray-700"
                >
                  {t('common:button.reset')}
                </button>
              )}
            </div>
          </div>
        </div>
      </div>

      {/* Table */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center gap-2">
          <FileText className="w-4 h-4 text-purple-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">{t('pa:history.title')}</h3>
          <span className="px-2 py-0.5 text-xs rounded-full bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300">
            {t('pa:history.itemCount', { num: filteredHistory.length })}
          </span>
        </div>

        {loading ? (
          <div className="flex items-center justify-center py-20">
            <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
          </div>
        ) : filteredHistory.length === 0 ? (
          <div className="flex flex-col items-center justify-center py-20 text-gray-500 dark:text-gray-400">
            <AlertCircle className="w-12 h-12 mb-4 opacity-50" />
            <p className="text-lg font-medium">{ t('pa:history.noHistory') }</p>
            <p className="text-sm">{t('pa:history.noHistoryHint')}</p>
            <Link
              to="/pa/verify"
              className="mt-4 inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-white bg-gradient-to-r from-teal-500 to-cyan-500"
            >
              <ShieldCheck className="w-4 h-4" />
              {t('pa:history.goToVerify')}
            </Link>
          </div>
        ) : (
          <>
            <div className="overflow-x-auto">
              <table className="w-full">
                <thead className="bg-slate-100 dark:bg-gray-700">
                  <tr>
                    <SortableHeader label={t('pa:history.verificationId')} sortKey="verificationId" sortConfig={historySortConfig} onSort={requestHistorySort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                    <SortableHeader label={t('common:label.country')} sortKey="issuingCountry" sortConfig={historySortConfig} onSort={requestHistorySort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                    <SortableHeader label={t('pa:result.documentNumber')} sortKey="documentNumber" sortConfig={historySortConfig} onSort={requestHistorySort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                    <SortableHeader label={t('common:label.type')} sortKey="verificationType" sortConfig={historySortConfig} onSort={requestHistorySort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                    <SortableHeader label={t('pa:history.status')} sortKey="status" sortConfig={historySortConfig} onSort={requestHistorySort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                    <SortableHeader label={t('pa:history.verificationTime')} sortKey="verificationTimestamp" sortConfig={historySortConfig} onSort={requestHistorySort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                    <SortableHeader label={t('pa:history.requestedBy')} sortKey="requestedBy" sortConfig={historySortConfig} onSort={requestHistorySort} className="px-3 py-2.5 text-left text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap" />
                    <th className="px-3 py-2.5 text-center text-xs font-semibold text-slate-700 dark:text-gray-200 uppercase tracking-wider whitespace-nowrap">
                      {t('pa:history.viewDetail')}
                    </th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
                  {sortedHistory.map((item) => (
                    <tr
                      key={item.verificationId}
                      className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors"
                    >
                      <td className="px-3 py-2">
                        <span className="font-mono text-xs text-gray-600 dark:text-gray-300">
                          {item.verificationId ? `${item.verificationId.substring(0, 8)}...` : '-'}
                        </span>
                      </td>
                      <td className="px-3 py-2">
                        {item.issuingCountry ? (
                          <div className="flex items-center gap-1.5">
                            {getFlagSvgPath(item.issuingCountry) && (
                              <img
                                src={getFlagSvgPath(item.issuingCountry)}
                                alt={item.issuingCountry}
                                title={getCountryName(item.issuingCountry)}
                                className="w-5 h-3.5 object-cover rounded shadow-sm border border-gray-200 dark:border-gray-600"
                                onError={(e) => {
                                  (e.target as HTMLImageElement).style.display = 'none';
                                }}
                              />
                            )}
                            <span className="text-xs font-medium text-gray-700 dark:text-gray-300">
                              {item.issuingCountry}
                            </span>
                          </div>
                        ) : (
                          <span className="text-gray-400">-</span>
                        )}
                      </td>
                      <td className="px-3 py-2 text-xs text-gray-600 dark:text-gray-300 font-mono">
                        {item.documentNumber || '-'}
                      </td>
                      <td className="px-3 py-2">
                        {item.verificationType === 'LOOKUP' ? (
                          <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium bg-emerald-100 dark:bg-emerald-900/30 text-emerald-600 dark:text-emerald-400">
                            <Search className="w-3 h-3" />
                            {t('pa:history.typeLookup')}
                          </span>
                        ) : (
                          <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium bg-blue-100 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400">
                            <ShieldCheck className="w-3 h-3" />
                            {t('pa:history.typeFull')}
                          </span>
                        )}
                      </td>
                      <td className="px-3 py-2">{getStatusBadge(item.status)}</td>
                      <td className="px-3 py-2">
                        <span className="inline-flex items-center gap-1 text-xs text-gray-500 dark:text-gray-400">
                          <Calendar className="w-3 h-3" />
                          {formatDateTime(item.verificationTimestamp)}
                        </span>
                      </td>
                      <td className="px-3 py-2 text-xs text-gray-500 dark:text-gray-400">
                        {item.requestedBy ? (
                          <span>{item.requestedBy}</span>
                        ) : (
                          <div>
                            <span className="text-gray-400 dark:text-gray-500">{t('pa:history.anonymous')}</span>
                            {item.clientIp && (
                              <span className="ml-1.5 text-xs text-gray-400 dark:text-gray-500 font-mono">
                                ({item.clientIp})
                              </span>
                            )}
                          </div>
                        )}
                      </td>
                      <td className="px-3 py-2 text-right">
                        <button
                          onClick={() => openDetailModal(item)}
                          className="inline-flex items-center gap-1 px-2.5 py-1 rounded-md text-xs font-medium text-blue-600 dark:text-blue-400 hover:bg-blue-50 dark:hover:bg-blue-900/20 transition-colors"
                        >
                          <Eye className="w-3 h-3" />
                          {t('pa:history.viewDetail')}
                        </button>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>

            {/* Pagination */}
            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex items-center justify-between">
              <p className="text-xs text-gray-500 dark:text-gray-400">
                {t('pa:history.paginationInfo', { total: totalElements, from: page * pageSize + 1, to: Math.min((page + 1) * pageSize, totalElements) })}
              </p>
              <div className="flex items-center gap-1">
                <button
                  onClick={() => setPage((p) => Math.max(0, p - 1))}
                  disabled={page === 0}
                  className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
                  aria-label={t('common:button.prev_page')}
                >
                  <ChevronLeft className="w-4 h-4" />
                </button>
                <span className="px-3 text-sm text-gray-600 dark:text-gray-300">
                  {page + 1} / {totalPages || 1}
                </span>
                <button
                  onClick={() => setPage((p) => Math.min(totalPages - 1, p + 1))}
                  disabled={page >= totalPages - 1}
                  className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
                  aria-label={t('common:button.next_page')}
                >
                  <ChevronRight className="w-4 h-4" />
                </button>
              </div>
            </div>
          </>
        )}
      </div>

      {/* Detail Modal */}
      {isModalOpen && selectedRecord && (
        <div className="fixed inset-0 z-[70] flex items-center justify-center">
          {/* Backdrop */}
          <div
            className="absolute inset-0 bg-black/50 backdrop-blur-sm"
            onClick={closeDetailModal}
          />

          {/* Modal Content */}
          <div className="relative bg-white dark:bg-gray-800 rounded-xl shadow-xl w-full max-w-sm sm:max-w-2xl lg:max-w-4xl max-h-[90vh] overflow-y-auto mx-4">
            {/* Modal Header */}
            <div className="sticky top-0 z-10 px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center justify-between bg-white dark:bg-gray-800">
              <div className="flex items-center gap-2.5">
                <div className="p-1.5 rounded-lg bg-gradient-to-br from-blue-500 to-indigo-600">
                  <Info className="w-4 h-4 text-white" />
                </div>
                <h2 className="text-base font-bold text-gray-900 dark:text-white">{t('pa:history.verificationDetail')}</h2>
                {selectedRecord.verificationType === 'LOOKUP' ? (
                  <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium bg-emerald-100 dark:bg-emerald-900/30 text-emerald-600 dark:text-emerald-400">
                    <Search className="w-3 h-3" />
                    {t('pa:history.typeLookup')}
                  </span>
                ) : (
                  <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium bg-blue-100 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400">
                    <ShieldCheck className="w-3 h-3" />
                    {t('pa:history.typeFull')}
                  </span>
                )}
                {getStatusBadge(selectedRecord.status)}
              </div>
              <button
                onClick={closeDetailModal}
                className="p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              >
                <X className="w-4 h-4 text-gray-500" />
              </button>
            </div>

            {/* Modal Body */}
            <div className="p-4 space-y-3">

              {/* Section 1: 기본 정보 - inline row */}
              <div className="grid grid-cols-5 gap-2">
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
                  <p className="text-xs font-semibold text-gray-400 uppercase tracking-wider">{t('pa:history.verificationId')}</p>
                  <p className="font-mono text-xs text-gray-900 dark:text-white truncate mt-0.5" title={selectedRecord.verificationId}>
                    {selectedRecord.verificationId.substring(0, 12)}...
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
                  <p className="text-xs font-semibold text-gray-400 uppercase tracking-wider">{t('pa:history.verificationTime')}</p>
                  <p className="text-xs text-gray-900 dark:text-white mt-0.5">
                    {formatDateTime(selectedRecord.verificationTimestamp)}
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
                  <p className="text-xs font-semibold text-gray-400 uppercase tracking-wider">{t('pa:history.country')}</p>
                  <div className="flex items-center gap-1.5 mt-0.5">
                    {selectedRecord.issuingCountry && getFlagSvgPath(selectedRecord.issuingCountry) && (
                      <img
                        src={getFlagSvgPath(selectedRecord.issuingCountry)}
                        alt={selectedRecord.issuingCountry}
                        title={getCountryName(selectedRecord.issuingCountry)}
                        className="w-5 h-3.5 object-cover rounded shadow-sm border border-gray-200 dark:border-gray-600"
                        onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }}
                      />
                    )}
                    <span className="text-xs font-medium text-gray-900 dark:text-white">
                      {selectedRecord.issuingCountry || '-'}
                    </span>
                  </div>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
                  <p className="text-xs font-semibold text-gray-400 uppercase tracking-wider">{t('pa:history.documentNumber')}</p>
                  <p className="font-mono text-xs font-medium text-blue-600 dark:text-blue-400 mt-0.5">
                    {selectedRecord.documentNumber || '-'}
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
                  <p className="text-xs font-semibold text-gray-400 uppercase tracking-wider">{t('pa:history.requestedBy')}</p>
                  {selectedRecord.requestedBy ? (
                    <p className="text-xs font-medium text-gray-900 dark:text-white truncate mt-0.5">
                      {selectedRecord.requestedBy}
                    </p>
                  ) : (
                    <div className="mt-0.5">
                      <p className="text-xs text-gray-400 dark:text-gray-500">{t('pa:history.anonymous')}</p>
                      {selectedRecord.clientIp && (
                        <p className="text-xs font-mono text-gray-400 dark:text-gray-500 mt-0.5">
                          IP: {selectedRecord.clientIp}
                        </p>
                      )}
                      {selectedRecord.userAgent && (
                        <p className="text-xs text-gray-400 dark:text-gray-500 mt-0.5 truncate" title={selectedRecord.userAgent}>
                          UA: {selectedRecord.userAgent.length > 40 ? selectedRecord.userAgent.substring(0, 40) + '...' : selectedRecord.userAgent}
                        </p>
                      )}
                    </div>
                  )}
                </div>
              </div>

              {/* Section 2: 검증 결과 */}
              <div>
                <div className="flex items-center gap-1.5 mb-2">
                  <div className="w-1 h-3.5 rounded-full bg-green-500" />
                  <h3 className="text-xs font-bold text-gray-800 dark:text-gray-200">{t('pa:history.verificationResult')}</h3>
                </div>

                <div className={cn('grid gap-2', selectedRecord.verificationType === 'LOOKUP' ? 'grid-cols-1' : 'grid-cols-3')}>
                  {/* SOD Signature — hide for LOOKUP */}
                  {selectedRecord.verificationType !== 'LOOKUP' && (
                    <div className={cn(
                      'rounded-lg px-3 py-2.5 border-l-3',
                      selectedRecord.sodSignatureValid
                        ? 'bg-green-50 dark:bg-green-900/20 border-green-500'
                        : 'bg-red-50 dark:bg-red-900/20 border-red-500'
                    )}>
                      <p className="text-xs font-semibold text-gray-500 dark:text-gray-400 mb-1">{t('pa:steps.step3')}</p>
                      <div className="flex items-center gap-1.5">
                        {selectedRecord.sodSignatureValid ? (
                          <CheckCircle className="w-4 h-4 text-green-500" />
                        ) : (
                          <XCircle className="w-4 h-4 text-red-500" />
                        )}
                        <span className={cn(
                          'text-xs font-bold',
                          selectedRecord.sodSignatureValid
                            ? 'text-green-600 dark:text-green-400'
                            : 'text-red-600 dark:text-red-400'
                        )}>
                          {selectedRecord.sodSignatureValid ? t('common:status.valid') : t('common:status.invalid')}
                        </span>
                      </div>
                    </div>
                  )}

                  {/* Certificate Chain */}
                  <div className={cn(
                    'rounded-lg px-3 py-2.5 border-l-3',
                    selectedRecord.trustChainValid
                      ? 'bg-green-50 dark:bg-green-900/20 border-green-500'
                      : 'bg-red-50 dark:bg-red-900/20 border-red-500'
                  )}>
                    <p className="text-xs font-semibold text-gray-500 dark:text-gray-400 mb-1">{t('pa:history.certChainVerification')}</p>
                    <div className="flex items-center gap-1.5">
                      {selectedRecord.trustChainValid ? (
                        <CheckCircle className="w-4 h-4 text-green-500" />
                      ) : (
                        <XCircle className="w-4 h-4 text-red-500" />
                      )}
                      <span className={cn(
                        'text-xs font-bold',
                        selectedRecord.trustChainValid
                          ? 'text-green-600 dark:text-green-400'
                          : 'text-red-600 dark:text-red-400'
                      )}>
                        {selectedRecord.trustChainValid ? t('common:status.valid') : t('common:status.invalid')}
                      </span>
                    </div>
                    {selectedRecord.trustChainMessage && (
                      <p className="text-xs text-gray-500 dark:text-gray-400 mt-1 truncate" title={selectedRecord.trustChainMessage}>
                        {selectedRecord.trustChainMessage}
                      </p>
                    )}
                  </div>

                  {/* Data Group Hash — hide for LOOKUP */}
                  {selectedRecord.verificationType !== 'LOOKUP' && (
                    <div className={cn(
                      'rounded-lg px-3 py-2.5 border-l-3',
                      selectedRecord.dgHashesValid
                        ? 'bg-green-50 dark:bg-green-900/20 border-green-500'
                        : 'bg-red-50 dark:bg-red-900/20 border-red-500'
                    )}>
                      <p className="text-xs font-semibold text-gray-500 dark:text-gray-400 mb-1">{t('pa:steps.step2')}</p>
                      <div className="flex items-center gap-1.5">
                        {selectedRecord.dgHashesValid ? (
                          <CheckCircle className="w-4 h-4 text-green-500" />
                        ) : (
                          <XCircle className="w-4 h-4 text-red-500" />
                        )}
                        <span className={cn(
                          'text-xs font-bold',
                          selectedRecord.dgHashesValid
                            ? 'text-green-600 dark:text-green-400'
                            : 'text-red-600 dark:text-red-400'
                        )}>
                          {selectedRecord.dgHashesValid ? t('common:status.valid') : t('common:status.invalid')}
                        </span>
                      </div>
                    </div>
                  )}
                </div>

                {/* CRL Status - Show when not VALID */}
                {selectedRecord.crlStatus && selectedRecord.crlStatus !== 'VALID' && (
                  <div className={cn(
                    'rounded-lg px-3 py-2 mt-2 flex items-center gap-2',
                    selectedRecord.crlStatus === 'REVOKED' || selectedRecord.crlStatus === 'CRL_INVALID'
                      ? 'bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
                      : 'bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800'
                  )}>
                    {selectedRecord.crlStatus === 'REVOKED' || selectedRecord.crlStatus === 'CRL_INVALID' ? (
                      <AlertTriangle className="w-4 h-4 text-red-500 flex-shrink-0" />
                    ) : (
                      <AlertCircle className="w-4 h-4 text-yellow-500 flex-shrink-0" />
                    )}
                    <span className={cn(
                      'text-xs font-bold',
                      selectedRecord.crlStatus === 'REVOKED' || selectedRecord.crlStatus === 'CRL_INVALID'
                        ? 'text-red-700 dark:text-red-400'
                        : 'text-yellow-700 dark:text-yellow-400'
                    )}>
                      CRL: {selectedRecord.crlStatus?.replace(/_/g, ' ')}
                    </span>
                  </div>
                )}
              </div>

              {/* DSC Non-Conformant Warning Banner */}
              {selectedRecord.dscNonConformant && (
                <div className="bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-lg px-3 py-2.5">
                  <div className="flex items-center gap-2">
                    <AlertTriangle className="w-4 h-4 text-amber-500 flex-shrink-0" />
                    <div>
                      <span className="text-xs font-bold text-amber-700 dark:text-amber-400">
                        {t('pa:history.nonConformantDsc')}
                      </span>
                      {selectedRecord.pkdConformanceCode && (
                        <span className="ml-2 px-1.5 py-0.5 text-xs rounded bg-amber-100 dark:bg-amber-900/40 text-amber-600 dark:text-amber-300 font-mono">
                          {selectedRecord.pkdConformanceCode}
                        </span>
                      )}
                      {selectedRecord.pkdConformanceText ? (
                        <p className="text-xs text-amber-600 dark:text-amber-400 mt-0.5">
                          {selectedRecord.pkdConformanceText}
                        </p>
                      ) : (
                        <p className="text-xs text-amber-600 dark:text-amber-400 mt-0.5">
                          {t('pa:history.ncDataDefaultWarning')}
                        </p>
                      )}
                    </div>
                  </div>
                </div>
              )}

              {/* Section 3: 데이터 그룹 (DG1 + DG2) — hide for LOOKUP */}
              {selectedRecord.status === 'VALID' && selectedRecord.verificationType !== 'LOOKUP' && (
                <div>
                  <div className="flex items-center gap-1.5 mb-2">
                    <div className="w-1 h-3.5 rounded-full bg-purple-500" />
                    <h3 className="text-xs font-bold text-gray-800 dark:text-gray-200">{t('pa:history.dataGroups')}</h3>
                  </div>

                  {dgLoading ? (
                    <div className="flex items-center justify-center py-6 bg-gray-50 dark:bg-gray-700/30 rounded-lg">
                      <Loader2 className="w-4 h-4 animate-spin text-blue-500" />
                      <span className="ml-2 text-xs text-gray-500">{t('common:button.loading')}</span>
                    </div>
                  ) : dgError ? (
                    <div className="bg-yellow-50 dark:bg-yellow-900/20 rounded-lg px-3 py-2 flex items-center gap-2">
                      <AlertCircle className="w-4 h-4 text-yellow-500" />
                      <span className="text-xs text-yellow-700 dark:text-yellow-400">{dgError}</span>
                    </div>
                  ) : (
                    <div className="flex gap-2">
                      {/* DG1 - MRZ */}
                      <div className="flex-1 bg-blue-50 dark:bg-blue-900/20 rounded-lg p-3 border border-blue-100 dark:border-blue-800">
                        <div className="flex items-center gap-1.5 mb-2">
                          <div className="w-1.5 h-1.5 rounded-full bg-blue-500" />
                          <p className="text-xs font-bold text-blue-700 dark:text-blue-300">{t('pa:history.dg1MrzData')}</p>
                        </div>
                        {dgData?.hasDg1 && dgData.dg1 ? (
                          <div className="grid grid-cols-4 gap-1.5">
                            <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5">
                              <p className="text-xs text-gray-400 leading-none">{t('pa:history.surname')}</p>
                              <p className="font-mono text-xs font-medium text-gray-900 dark:text-white truncate mt-0.5">{dgData.dg1.surname || '-'}</p>
                            </div>
                            <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5 col-span-2">
                              <p className="text-xs text-gray-400 leading-none">{t('pa:history.givenNames')}</p>
                              <p className="font-mono text-xs font-medium text-gray-900 dark:text-white truncate mt-0.5">{dgData.dg1.givenNames || '-'}</p>
                            </div>
                            <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5">
                              <p className="text-xs text-gray-400 leading-none">{t('pa:history.gender')}</p>
                              <p className="font-mono text-xs font-medium text-gray-900 dark:text-white mt-0.5">
                                {dgData.dg1.sex === 'M' ? t('pa:result.male') : dgData.dg1.sex === 'F' ? t('pa:result.female') : dgData.dg1.sex || '-'}
                              </p>
                            </div>
                            <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5 col-span-2">
                              <p className="text-xs text-gray-400 leading-none">{t('pa:history.documentNumber')}</p>
                              <p className="font-mono text-xs font-bold text-blue-600 dark:text-blue-400 mt-0.5">{dgData.dg1.documentNumber || '-'}</p>
                            </div>
                            <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5">
                              <p className="text-xs text-gray-400 leading-none">{t('pa:history.nationality')}</p>
                              <p className="font-mono text-xs font-medium text-gray-900 dark:text-white mt-0.5">{dgData.dg1.nationality || '-'}</p>
                            </div>
                            <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5">
                              <p className="text-xs text-gray-400 leading-none">{t('pa:history.dateOfBirth')}</p>
                              <p className="font-mono text-xs font-medium text-gray-900 dark:text-white mt-0.5">{dgData.dg1.dateOfBirth || '-'}</p>
                            </div>
                          </div>
                        ) : (
                          <div className="text-xs text-gray-500 dark:text-gray-400 text-center py-3">
                            {t('pa:history.noDg1Data')}
                          </div>
                        )}
                      </div>

                      {/* DG2 - Face */}
                      <div className="w-40 flex-shrink-0 bg-purple-50 dark:bg-purple-900/20 rounded-lg p-3 border border-purple-100 dark:border-purple-800">
                        <div className="flex items-center gap-1.5 mb-2">
                          <div className="w-1.5 h-1.5 rounded-full bg-purple-500" />
                          <p className="text-xs font-bold text-purple-700 dark:text-purple-300">{t('pa:history.dg2Face')}</p>
                        </div>
                        {dgData?.hasDg2 && dgData.dg2?.faceImages?.[0]?.imageDataUrl ? (
                          <div className="flex flex-col items-center">
                            <img
                              src={dgData.dg2.faceImages[0].imageDataUrl}
                              alt={t('pa:history.dg2Face')}
                              className="w-28 aspect-[3/4] object-cover rounded-lg shadow-md border-2 border-purple-200 dark:border-purple-700"
                            />
                            <span className="mt-1.5 px-1.5 py-0.5 text-xs rounded-full bg-purple-100 dark:bg-purple-900/50 text-purple-600 dark:text-purple-400 font-medium">
                              {dgData.dg2.faceImages[0].imageFormat || 'N/A'}
                            </span>
                          </div>
                        ) : (
                          <div className="flex flex-col items-center justify-center h-28 text-xs text-gray-500 dark:text-gray-400">
                            <User className="w-6 h-6 text-gray-300 dark:text-gray-600 mb-1" />
                            {t('pa:history.noFaceImage')}
                          </div>
                        )}
                      </div>
                    </div>
                  )}
                </div>
              )}
            </div>

            {/* Modal Footer */}
            <div className="sticky bottom-0 px-5 py-3 border-t border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800 flex justify-end">
              <button
                onClick={closeDetailModal}
                className="inline-flex items-center gap-1.5 px-4 py-1.5 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors border border-gray-200 dark:border-gray-600"
              >
                {t('common:button.close')}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default PAHistory;
