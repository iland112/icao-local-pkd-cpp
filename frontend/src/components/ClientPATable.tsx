import { useTranslation } from 'react-i18next';
import { useState, useEffect, useMemo } from 'react';
import { DEFAULT_PAGE_SIZE } from '@/config/pagination';
import {
  ChevronLeft,
  ChevronRight,
  Loader2,
  Filter,
  Search,
  RefreshCw,
  X,
} from 'lucide-react';
import { paApi } from '@/services/paApi';
import type { TrustMaterialHistoryItem } from '@/types';
import { cn } from '@/utils/cn';
import { getFlagSvgPath } from '@/utils/countryCode';
import { formatDateTime } from '@/utils/dateFormat';
import { useSortableTable } from '@/hooks/useSortableTable';
import { SortableHeader } from '@/components/common/SortableHeader';

type ClientPAStatus = 'REQUESTED' | 'VALID' | 'INVALID' | 'ERROR' | '';

export function ClientPATable() {
  const { t } = useTranslation(['pa', 'common']);
  const [history, setHistory] = useState<TrustMaterialHistoryItem[]>([]);
  const [loading, setLoading] = useState(true);
  const [page, setPage] = useState(0);
  const [total, setTotal] = useState(0);
  const pageSize = DEFAULT_PAGE_SIZE;

  // Filters
  const [countryFilter, setCountryFilter] = useState('');
  const [statusFilter, setStatusFilter] = useState<ClientPAStatus>('');
  const [searchTerm, setSearchTerm] = useState('');
  const [dateFrom, setDateFrom] = useState('');
  const [dateTo, setDateTo] = useState('');
  const [appliedCountry, setAppliedCountry] = useState('');

  useEffect(() => {
    fetchHistory();
  }, [page, appliedCountry]);

  const fetchHistory = async () => {
    setLoading(true);
    try {
      const response = await paApi.getTrustMaterialHistory({
        page,
        size: 100, // Fetch more for client-side filtering
        ...(appliedCountry ? { country: appliedCountry } : {}),
      });
      const resData = response.data;
      setHistory(resData.data ?? []);
      setTotal(resData.total ?? 0);
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to fetch client PA history:', error);
      setHistory([]);
    } finally {
      setLoading(false);
    }
  };

  // Unique countries for dropdown
  const uniqueCountries = useMemo(() => {
    const countries = [...new Set(history.map(h => h.countryCode).filter(Boolean))].sort();
    return countries;
  }, [history]);

  // Client-side filtering (same as server PA)
  const filteredHistory = useMemo(() => {
    return history.filter((item) => {
      const matchesStatus = !statusFilter || item.status === statusFilter;
      const matchesSearch = !searchTerm ||
        item.requestedBy?.toLowerCase().includes(searchTerm.toLowerCase()) ||
        item.countryCode?.toLowerCase().includes(searchTerm.toLowerCase()) ||
        item.id?.toLowerCase().includes(searchTerm.toLowerCase());

      // Date range filter
      let matchesDate = true;
      if (dateFrom || dateTo) {
        const itemDate = item.requestTimestamp ? new Date(item.requestTimestamp) : null;
        if (itemDate) {
          if (dateFrom && itemDate < new Date(dateFrom)) matchesDate = false;
          if (dateTo) {
            const endDate = new Date(dateTo);
            endDate.setHours(23, 59, 59, 999);
            if (itemDate > endDate) matchesDate = false;
          }
        }
      }

      return matchesStatus && matchesSearch && matchesDate;
    });
  }, [history, statusFilter, searchTerm, dateFrom, dateTo]);

  const { sortedData: sortedHistory, sortConfig, requestSort } = useSortableTable<TrustMaterialHistoryItem>(filteredHistory);

  // Paginate sorted+filtered results
  const paginatedHistory = useMemo(() => {
    const start = page * pageSize;
    return sortedHistory.slice(start, start + pageSize);
  }, [sortedHistory, page, pageSize]);

  const clearFilters = () => {
    setCountryFilter('');
    setAppliedCountry('');
    setStatusFilter('');
    setSearchTerm('');
    setDateFrom('');
    setDateTo('');
    setPage(0);
  };

  const hasActiveFilters = statusFilter || appliedCountry || searchTerm || dateFrom || dateTo;
  const totalFiltered = filteredHistory.length;
  const totalPages = Math.ceil(totalFiltered / pageSize) || 1;

  return (
    <div className="space-y-4">
      {/* Filter Card — same layout as Server PA */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4">
        <div className="flex items-center gap-2 mb-3">
          <Filter className="w-4 h-4 text-purple-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">{t('pa:history.filterLabel')}</h3>
        </div>

        <div className="grid grid-cols-2 md:grid-cols-5 gap-3">
          {/* Country */}
          <div>
            <label htmlFor="cpa-country" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('pa:history.filterCountry')}
            </label>
            <select
              id="cpa-country"
              name="countryFilter"
              value={countryFilter}
              onChange={(e) => { setCountryFilter(e.target.value); setAppliedCountry(e.target.value); setPage(0); }}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-purple-500"
            >
              <option value="">{t('pa:history.allCountries')}</option>
              {uniqueCountries.map((c) => (
                <option key={c} value={c}>{c}</option>
              ))}
            </select>
          </div>

          {/* Status */}
          <div>
            <label htmlFor="cpa-status" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('pa:history.filterStatus')}
            </label>
            <select
              id="cpa-status"
              name="statusFilter"
              value={statusFilter}
              onChange={(e) => { setStatusFilter(e.target.value as ClientPAStatus); setPage(0); }}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-purple-500"
            >
              <option value="">{t('pa:history.allStatuses')}</option>
              <option value="REQUESTED">REQUESTED</option>
              <option value="VALID">{t('common:status.valid')}</option>
              <option value="INVALID">{t('common:status.invalid')}</option>
              <option value="ERROR">ERROR</option>
            </select>
          </div>

          {/* Date From */}
          <div>
            <label htmlFor="cpa-date-from" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('pa:history.dateFrom')}
            </label>
            <input
              id="cpa-date-from"
              name="dateFrom"
              type="date"
              value={dateFrom}
              onChange={(e) => { setDateFrom(e.target.value); setPage(0); }}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-purple-500"
            />
          </div>

          {/* Date To */}
          <div>
            <label htmlFor="cpa-date-to" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('pa:history.dateTo')}
            </label>
            <input
              id="cpa-date-to"
              name="dateTo"
              type="date"
              value={dateTo}
              onChange={(e) => { setDateTo(e.target.value); setPage(0); }}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-purple-500"
            />
          </div>

          {/* Search + Actions */}
          <div>
            <label htmlFor="cpa-search" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              {t('common:button.search')}
            </label>
            <div className="flex items-center gap-2">
              <div className="relative flex-1">
                <Search className="absolute left-2.5 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-gray-400" />
                <input
                  id="cpa-search"
                  name="search"
                  type="text"
                  value={searchTerm}
                  onChange={(e) => { setSearchTerm(e.target.value); setPage(0); }}
                  placeholder={t('pa:history.searchPlaceholder')}
                  className="w-full pl-8 pr-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-purple-500"
                />
              </div>
              {hasActiveFilters && (
                <button
                  onClick={clearFilters}
                  className="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
                  title={t('common:button.reset')}
                >
                  <X className="w-4 h-4 text-gray-400" />
                </button>
              )}
              <button
                onClick={fetchHistory}
                disabled={loading}
                className="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
                title={t('common:button.refresh')}
              >
                <RefreshCw className={cn('w-4 h-4 text-gray-500', loading && 'animate-spin')} />
              </button>
            </div>
          </div>
        </div>
      </div>

      {/* Table */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md overflow-hidden">
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-gray-200 dark:border-gray-700 bg-slate-100 dark:bg-slate-800">
                <SortableHeader label={t('common:label.country')} sortKey="countryCode" sortConfig={sortConfig} onSort={requestSort} className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300" />
                <SortableHeader label={t('pa:history.passportNumber')} sortKey="mrzNationality" sortConfig={sortConfig} onSort={requestSort} className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300" />
                <th className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300">CSCA/CRL</th>
                <SortableHeader label={t('common:label.status')} sortKey="status" sortConfig={sortConfig} onSort={requestSort} className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300" />
                <SortableHeader label={t('pa:history.verificationResult')} sortKey="verificationStatus" sortConfig={sortConfig} onSort={requestSort} className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300" />
                <SortableHeader label={t('common:label.timestamp')} sortKey="requestTimestamp" sortConfig={sortConfig} onSort={requestSort} className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300" />
                <SortableHeader label={t('pa:history.requestedBy')} sortKey="requestedBy" sortConfig={sortConfig} onSort={requestSort} className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300" />
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-100 dark:divide-gray-700/50">
              {loading ? (
                <tr><td colSpan={7} className="px-4 py-8 text-center text-gray-400"><Loader2 className="w-5 h-5 animate-spin inline-block mr-2" />{t('common:table.loading')}</td></tr>
              ) : paginatedHistory.length === 0 ? (
                <tr><td colSpan={7} className="px-4 py-8 text-center text-gray-400">{t('common:table.noData')}</td></tr>
              ) : paginatedHistory.map((item) => (
                <tr key={item.id} className="hover:bg-gray-50 dark:hover:bg-gray-700/30">
                  <td className="px-3 py-2 text-center">
                    <div className="flex items-center justify-center gap-1.5">
                      <img src={getFlagSvgPath(item.countryCode)} alt="" className="w-4 h-3 object-cover rounded-sm" onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }} />
                      <span className="font-medium">{item.countryCode}</span>
                    </div>
                  </td>
                  <td className="px-3 py-2 text-center text-xs text-gray-500">
                    {item.mrzDocumentType && <span className="px-1 py-0.5 bg-blue-50 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400 rounded text-[10px] font-medium mr-1">{item.mrzDocumentType}</span>}
                    {item.mrzNationality && <span className="text-gray-600 dark:text-gray-300">{item.mrzNationality}</span>}
                    {!item.mrzNationality && !item.mrzDocumentType && <span className="text-gray-300 dark:text-gray-600">—</span>}
                  </td>
                  <td className="px-3 py-2 text-center text-xs">
                    <span className="text-blue-600">{item.cscaCount} CSCA</span>
                    {item.crlCount > 0 && <span className="ml-1 text-amber-600">{item.crlCount} CRL</span>}
                  </td>
                  <td className="px-3 py-2 text-center">
                    <span className={cn('px-2 py-0.5 text-xs font-medium rounded-full',
                      item.status === 'REQUESTED' ? 'bg-yellow-100 text-yellow-700 dark:bg-yellow-900/30 dark:text-yellow-300' :
                      item.status === 'VALID' ? 'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-300' :
                      item.status === 'INVALID' ? 'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-300' :
                      'bg-gray-100 text-gray-600 dark:bg-gray-700 dark:text-gray-300'
                    )}>
                      {item.status}
                    </span>
                  </td>
                  <td className="px-3 py-2 text-center">
                    {item.verificationStatus ? (
                      <span className={cn('px-2 py-0.5 text-xs font-medium rounded-full',
                        item.verificationStatus === 'VALID' ? 'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-300' :
                        item.verificationStatus === 'INVALID' ? 'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-300' :
                        'bg-gray-100 text-gray-600 dark:bg-gray-700 dark:text-gray-300'
                      )}>
                        {item.verificationStatus}
                      </span>
                    ) : <span className="text-xs text-gray-300 dark:text-gray-500">—</span>}
                  </td>
                  <td className="px-3 py-2 text-center text-xs text-gray-500 dark:text-gray-400">{formatDateTime(item.requestTimestamp)}</td>
                  <td className="px-3 py-2 text-center text-xs text-gray-500 dark:text-gray-400">{item.requestedBy || item.clientIp || '—'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        {/* Pagination */}
        {totalFiltered > pageSize && (
          <div className="flex items-center justify-between px-4 py-3 border-t border-gray-200 dark:border-gray-700">
            <span className="text-xs text-gray-500 dark:text-gray-400">
              {t('common:label.total')} {totalFiltered.toLocaleString()}
              {totalFiltered !== total && <span className="text-gray-400"> / {total.toLocaleString()}</span>}
            </span>
            <div className="flex items-center gap-2">
              <button onClick={() => setPage(p => Math.max(0, p - 1))} disabled={page === 0} className="p-1 rounded hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-30"><ChevronLeft className="w-4 h-4" /></button>
              <span className="text-xs text-gray-500 dark:text-gray-400">{page + 1} / {totalPages}</span>
              <button onClick={() => setPage(p => p + 1)} disabled={(page + 1) * pageSize >= totalFiltered} className="p-1 rounded hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-30"><ChevronRight className="w-4 h-4" /></button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
