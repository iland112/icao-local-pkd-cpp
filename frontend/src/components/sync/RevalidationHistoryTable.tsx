import { useTranslation } from 'react-i18next';
import { ShieldCheck } from 'lucide-react';
import { cn } from '@/utils/cn';
import { formatDateTime } from '@/utils/dateFormat';
import { useSortableTable } from '@/hooks/useSortableTable';
import { SortableHeader } from '@/components/common/SortableHeader';
import type { RevalidationHistoryItem } from '@/services/api';

interface RevalidationHistoryTableProps {
  items: RevalidationHistoryItem[];
}

export function RevalidationHistoryTable({ items }: RevalidationHistoryTableProps) {
  const { t } = useTranslation(['sync', 'common']);
  const { sortedData, sortConfig, requestSort } = useSortableTable(items);

  if (items.length === 0) {
    return null;
  }

  return (
    <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4">
      <div className="flex items-center gap-2 mb-4">
        <ShieldCheck className="w-5 h-5 text-green-500" />
        <h3 className="text-lg font-semibold text-gray-900 dark:text-white">
          {t('dashboard.revalidationHistory')}
        </h3>
      </div>

      <div className="overflow-x-auto">
        <table className="w-full text-xs">
          <thead className="bg-slate-100 dark:bg-gray-700">
            <tr>
              <SortableHeader label={t('dashboard.executionTime')} sortKey="executedAt" sortConfig={sortConfig} onSort={requestSort}
                className="text-left py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
              <SortableHeader label={t('dashboard.processedCerts')} sortKey="totalProcessed" sortConfig={sortConfig} onSort={requestSort}
                className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
              <SortableHeader label={t('sync:dashboard.newlyExpired')} sortKey="newlyExpired" sortConfig={sortConfig} onSort={requestSort}
                className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
              <SortableHeader label={t('sync:dashboard.newlyValid')} sortKey="newlyValid" sortConfig={sortConfig} onSort={requestSort}
                className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
              <SortableHeader label={t('sync:dashboard.noChange')} sortKey="unchanged" sortConfig={sortConfig} onSort={requestSort}
                className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
              <SortableHeader label={t('sync:dashboard.error')} sortKey="errors" sortConfig={sortConfig} onSort={requestSort}
                className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
              <SortableHeader label={t('sync:dashboard.tcTarget')} sortKey="tcProcessed" sortConfig={sortConfig} onSort={requestSort}
                className="text-right py-2.5 px-3 font-semibold text-blue-700 dark:text-blue-300 whitespace-nowrap" />
              <SortableHeader label="TC VALID" sortKey="tcNewlyValid" sortConfig={sortConfig} onSort={requestSort}
                className="text-right py-2.5 px-3 font-semibold text-blue-700 dark:text-blue-300 whitespace-nowrap" />
              <SortableHeader label={t('sync:dashboard.crlCheck')} sortKey="crlChecked" sortConfig={sortConfig} onSort={requestSort}
                className="text-right py-2.5 px-3 font-semibold text-purple-700 dark:text-purple-300 whitespace-nowrap" />
              <SortableHeader label={t('sync:dashboard.crlRevoked')} sortKey="crlRevoked" sortConfig={sortConfig} onSort={requestSort}
                className="text-right py-2.5 px-3 font-semibold text-purple-700 dark:text-purple-300 whitespace-nowrap" />
              <SortableHeader label={t('sync:reconciliation.duration')} sortKey="durationMs" sortConfig={sortConfig} onSort={requestSort}
                className="text-right py-2.5 px-3 font-semibold text-slate-700 dark:text-gray-200 whitespace-nowrap" />
            </tr>
          </thead>
          <tbody>
            {sortedData.map((item) => (
              <tr
                key={item.id}
                className="border-b border-gray-100 dark:border-gray-700/50 hover:bg-gray-50 dark:hover:bg-gray-700/30"
              >
                <td className="py-2 px-3 text-gray-900 dark:text-white">
                  {formatDateTime(item.executedAt)}
                </td>
                <td className="py-2 px-3 text-right font-mono text-gray-700 dark:text-gray-300">
                  {item.totalProcessed.toLocaleString()}
                </td>
                <td className="py-2 px-3 text-right">
                  <span
                    className={cn(
                      'font-mono font-semibold',
                      item.newlyExpired > 0
                        ? 'text-orange-600 dark:text-orange-400'
                        : 'text-gray-500 dark:text-gray-400'
                    )}
                  >
                    {item.newlyExpired}
                  </span>
                </td>
                <td className="py-2 px-3 text-right">
                  <span
                    className={cn(
                      'font-mono font-semibold',
                      item.newlyValid > 0
                        ? 'text-green-600 dark:text-green-400'
                        : 'text-gray-500 dark:text-gray-400'
                    )}
                  >
                    {item.newlyValid}
                  </span>
                </td>
                <td className="py-2 px-3 text-right font-mono text-gray-500 dark:text-gray-400">
                  {item.unchanged.toLocaleString()}
                </td>
                <td className="py-2 px-3 text-right">
                  <span
                    className={cn(
                      'font-mono font-semibold',
                      item.errors > 0
                        ? 'text-red-600 dark:text-red-400'
                        : 'text-gray-500 dark:text-gray-400'
                    )}
                  >
                    {item.errors}
                  </span>
                </td>
                <td className="py-2 px-3 text-right font-mono text-blue-600 dark:text-blue-400">
                  {(item.tcProcessed ?? 0).toLocaleString()}
                </td>
                <td className="py-2 px-3 text-right">
                  <span className={cn('font-mono font-semibold', (item.tcNewlyValid ?? 0) > 0 ? 'text-green-600 dark:text-green-400' : 'text-gray-500 dark:text-gray-400')}>
                    {(item.tcNewlyValid ?? 0).toLocaleString()}
                  </span>
                </td>
                <td className="py-2 px-3 text-right font-mono text-purple-600 dark:text-purple-400">
                  {(item.crlChecked ?? 0).toLocaleString()}
                </td>
                <td className="py-2 px-3 text-right">
                  <span className={cn('font-mono font-semibold', (item.crlRevoked ?? 0) > 0 ? 'text-red-600 dark:text-red-400' : 'text-gray-500 dark:text-gray-400')}>
                    {item.crlRevoked ?? 0}
                  </span>
                </td>
                <td className="py-2 px-3 text-right text-gray-500 dark:text-gray-400">
                  {item.durationMs}ms
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
