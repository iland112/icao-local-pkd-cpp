import { useTranslation } from 'react-i18next';
import { CheckCircle, AlertTriangle, XCircle, ArrowRightLeft } from 'lucide-react';
import { cn } from '@/utils/cn';
import { Dialog } from '@/components/common/Dialog';
import type { SyncStatusResponse } from '@/types';

interface SyncCheckResultDialogProps {
  result: SyncStatusResponse;
  onClose: () => void;
}

export function SyncCheckResultDialog({ result, onClose }: SyncCheckResultDialogProps) {
  const { t } = useTranslation(['sync', 'common', 'ai']);

  return (
    <Dialog
      isOpen={true}
      onClose={onClose}
      title={t('sync:dashboard.syncCheckComplete')}
      size="lg"
    >
      <div className="space-y-4">
        {/* Status Banner */}
        <div className={cn(
          'flex items-center gap-3 p-3 rounded-lg',
          result.status === 'SYNCED'
            ? 'bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800'
            : result.status === 'DISCREPANCY'
              ? 'bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800'
              : 'bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
        )}>
          {result.status === 'SYNCED' ? (
            <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400 flex-shrink-0" />
          ) : result.status === 'DISCREPANCY' ? (
            <AlertTriangle className="w-5 h-5 text-yellow-600 dark:text-yellow-400 flex-shrink-0" />
          ) : (
            <XCircle className="w-5 h-5 text-red-600 dark:text-red-400 flex-shrink-0" />
          )}
          <span className={cn(
            'text-sm font-medium',
            result.status === 'SYNCED'
              ? 'text-green-800 dark:text-green-300'
              : result.status === 'DISCREPANCY'
                ? 'text-yellow-800 dark:text-yellow-300'
                : 'text-red-800 dark:text-red-300'
          )}>
            {result.status === 'SYNCED'
              ? t('sync:dashboard.dbLdapSynced')
              : result.status === 'DISCREPANCY'
                ? t('sync:dashboard.discrepanciesDetected', { num: result.discrepancies?.total ?? 0 })
                : t('sync:dashboard.syncCheckError')}
          </span>
          {result.checkDurationMs && (
            <span className="ml-auto text-xs text-gray-500 dark:text-gray-400">
              {result.checkDurationMs < 1000
                ? `${result.checkDurationMs}ms`
                : `${(result.checkDurationMs / 1000).toFixed(1)}${t('common:time.seconds')}`}
            </span>
          )}
        </div>

        {/* DB vs LDAP Counts */}
        {result.dbCounts && result.ldapCounts && (
          <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
            <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 flex items-center gap-2">
              <ArrowRightLeft className="w-4 h-4 text-purple-500" />
              {t('sync:dashboard.dbLdapCertComparison')}
            </h4>
            <div className="overflow-x-auto">
              <table className="w-full text-xs">
                <thead className="bg-slate-100 dark:bg-gray-700">
                  <tr>
                    <th className="text-center py-2 px-3 font-semibold text-slate-700 dark:text-gray-200">{ t('ai:dashboard.filterType') }</th>
                    <th className="text-center py-2 px-3 font-semibold text-blue-600 dark:text-blue-400">DB</th>
                    <th className="text-center py-2 px-3 font-semibold text-green-600 dark:text-green-400">LDAP</th>
                    <th className="text-center py-2 px-3 font-semibold text-slate-700 dark:text-gray-200">{ t('sync:dashboard.difference') }</th>
                  </tr>
                </thead>
                <tbody>
                  {([
                    { key: 'csca' as const, label: 'CSCA' },
                    { key: 'mlsc' as const, label: 'MLSC' },
                    { key: 'dsc' as const, label: 'DSC' },
                    { key: 'dscNc' as const, label: 'DSC_NC' },
                    { key: 'crl' as const, label: 'CRL' },
                  ]).map(({ key, label }) => {
                    const dbCount = result.dbCounts?.[key] ?? 0;
                    const ldapCount = result.ldapCounts?.[key] ?? 0;
                    const diff = result.discrepancies?.[key] ?? 0;
                    return (
                      <tr key={key} className="border-b border-gray-100 dark:border-gray-700/50">
                        <td className="py-2 px-3 text-gray-700 dark:text-gray-300 font-medium">{label}</td>
                        <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                          {dbCount.toLocaleString()}
                        </td>
                        <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
                          {ldapCount.toLocaleString()}
                        </td>
                        <td className="py-2 px-3 text-right">
                          {diff !== 0 ? (
                            <span className={cn('font-mono font-semibold', diff > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400')}>
                              {diff > 0 ? '+' : ''}{diff}
                            </span>
                          ) : (
                            <span className="text-green-600 dark:text-green-400 font-semibold">✓</span>
                          )}
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
          </div>
        )}

        {/* Close Button */}
        <div className="flex justify-end pt-2">
          <button
            onClick={onClose}
            className="px-4 py-2 text-sm font-medium text-white bg-blue-600 rounded-lg hover:bg-blue-700 transition-colors"
          >
            {t('common:confirm.title')}
          </button>
        </div>
      </div>
    </Dialog>
  );
}
