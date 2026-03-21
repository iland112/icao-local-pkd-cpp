import { useTranslation } from 'react-i18next';
import { useState, useEffect } from 'react';
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

export function ClientPATable() {
  const { t } = useTranslation(['pa', 'common']);
  const [history, setHistory] = useState<TrustMaterialHistoryItem[]>([]);
  const [loading, setLoading] = useState(true);
  const [page, setPage] = useState(0);
  const [total, setTotal] = useState(0);
  const pageSize = DEFAULT_PAGE_SIZE;

  // Filters
  const [countryFilter, setCountryFilter] = useState('');
  const [appliedCountry, setAppliedCountry] = useState('');

  useEffect(() => {
    fetchHistory();
  }, [page, appliedCountry]);

  const fetchHistory = async () => {
    setLoading(true);
    try {
      const response = await paApi.getTrustMaterialHistory({
        page,
        size: pageSize,
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

  const applyFilter = () => {
    setAppliedCountry(countryFilter.toUpperCase());
    setPage(0);
  };

  const clearFilter = () => {
    setCountryFilter('');
    setAppliedCountry('');
    setPage(0);
  };

  const totalPages = Math.ceil(total / pageSize) || 1;

  return (
    <div className="space-y-4">
      {/* Filter Card */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4">
        <div className="flex items-center gap-2 mb-3">
          <Filter className="w-4 h-4 text-purple-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">{t('common:label.searchFilter')}</h3>
        </div>
        <div className="flex items-center gap-3">
          <div className="flex-1 max-w-xs">
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">{t('common:label.country')}</label>
            <input
              type="text"
              value={countryFilter}
              onChange={(e) => setCountryFilter(e.target.value)}
              onKeyDown={(e) => e.key === 'Enter' && applyFilter()}
              placeholder="KR, US, JP..."
              className="w-full px-3 py-1.5 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-gray-100 focus:ring-2 focus:ring-purple-500"
            />
          </div>
          <div className="flex items-center gap-2 pt-5">
            <button
              onClick={applyFilter}
              className="inline-flex items-center gap-1.5 px-3 py-1.5 text-sm font-medium text-white bg-purple-500 hover:bg-purple-600 rounded-lg transition-colors"
            >
              <Search className="w-3.5 h-3.5" />
              {t('common:button.search')}
            </button>
            {appliedCountry && (
              <button
                onClick={clearFilter}
                className="inline-flex items-center gap-1.5 px-3 py-1.5 text-sm font-medium text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-lg transition-colors"
              >
                <X className="w-3.5 h-3.5" />
                {t('common:button.reset')}
              </button>
            )}
            <button
              onClick={fetchHistory}
              disabled={loading}
              className="p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            >
              <RefreshCw className={cn('w-4 h-4 text-gray-500', loading && 'animate-spin')} />
            </button>
          </div>
        </div>
      </div>

      {/* Table */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md overflow-hidden">
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-gray-200 dark:border-gray-700 bg-slate-100 dark:bg-slate-800">
                <th className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300">{t('common:label.country')}</th>
                <th className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300">{t('pa:history.passportNumber')}</th>
                <th className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300">CSCA/CRL</th>
                <th className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300">{t('common:label.status')}</th>
                <th className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300">{t('pa:history.verificationResult')}</th>
                <th className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300">{t('common:label.timestamp')}</th>
                <th className="px-3 py-2 text-center text-xs font-semibold text-gray-600 dark:text-gray-300">{t('pa:history.requestedBy')}</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-100 dark:divide-gray-700/50">
              {loading ? (
                <tr><td colSpan={7} className="px-4 py-8 text-center text-gray-400"><Loader2 className="w-5 h-5 animate-spin inline-block mr-2" />{t('common:table.loading')}</td></tr>
              ) : history.length === 0 ? (
                <tr><td colSpan={7} className="px-4 py-8 text-center text-gray-400">{t('common:table.noData')}</td></tr>
              ) : history.map((item) => (
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
        {total > pageSize && (
          <div className="flex items-center justify-between px-4 py-3 border-t border-gray-200 dark:border-gray-700">
            <span className="text-xs text-gray-500 dark:text-gray-400">{t('common:label.total')} {total.toLocaleString()}</span>
            <div className="flex items-center gap-2">
              <button onClick={() => setPage(p => Math.max(0, p - 1))} disabled={page === 0} className="p-1 rounded hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-30"><ChevronLeft className="w-4 h-4" /></button>
              <span className="text-xs text-gray-500 dark:text-gray-400">{page + 1} / {totalPages}</span>
              <button onClick={() => setPage(p => p + 1)} disabled={(page + 1) * pageSize >= total} className="p-1 rounded hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-30"><ChevronRight className="w-4 h-4" /></button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
