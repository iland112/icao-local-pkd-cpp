import { useTranslation } from 'react-i18next';
import { CheckCircle, XCircle } from 'lucide-react';
import { cn } from '@/utils/cn';
import { Dialog } from '@/components/common/Dialog';
import type { RevalidationResult } from '@/services/api';

interface RevalidationResultDialogProps {
  result: RevalidationResult;
  onClose: () => void;
}

export function RevalidationResultDialog({ result, onClose }: RevalidationResultDialogProps) {
  const { t } = useTranslation(['sync', 'common']);

  return (
    <Dialog
      isOpen={true}
      onClose={onClose}
      title={t('sync:dashboard.revalidationComplete')}
      size="lg"
    >
      <div className="space-y-4">
        {/* Status Banner */}
        <div className={cn(
          'flex items-center gap-3 p-3 rounded-lg',
          result.success
            ? 'bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800'
            : 'bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
        )}>
          {result.success ? (
            <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400 flex-shrink-0" />
          ) : (
            <XCircle className="w-5 h-5 text-red-600 dark:text-red-400 flex-shrink-0" />
          )}
          <span className={cn(
            'text-sm font-medium',
            result.success
              ? 'text-green-800 dark:text-green-300'
              : 'text-red-800 dark:text-red-300'
          )}>
            {result.success
              ? t('sync:dashboard.certsValidated', { num: result.totalProcessed })
              : t('sync:dashboard.revalidationError')}
          </span>
          <span className="ml-auto text-xs text-gray-500 dark:text-gray-400">
            {result.durationMs < 1000
              ? `${result.durationMs}ms`
              : `${(result.durationMs / 1000).toFixed(1)}${t('common:time.seconds')}`}
          </span>
        </div>

        {/* Step 1: Expiration Check */}
        <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
          <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">
            {t('sync:dashboard.step1ExpiryCheck')}
          </h4>
          <div className="grid grid-cols-3 gap-2 text-center">
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
              <p className="text-xs text-gray-500 dark:text-gray-400">{ t('common:label.processing') }</p>
              <p className="text-base font-bold text-gray-900 dark:text-white">{result.totalProcessed.toLocaleString()}</p>
            </div>
            <div className={cn('rounded-lg p-2', result.newlyExpired > 0 ? 'bg-orange-50 dark:bg-orange-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
              <p className="text-xs text-gray-500 dark:text-gray-400">{t('sync:dashboard.newlyExpired')}</p>
              <p className={cn('text-base font-bold', result.newlyExpired > 0 ? 'text-orange-600 dark:text-orange-400' : 'text-gray-900 dark:text-white')}>{result.newlyExpired}</p>
            </div>
            <div className={cn('rounded-lg p-2', result.newlyValid > 0 ? 'bg-green-50 dark:bg-green-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
              <p className="text-xs text-gray-500 dark:text-gray-400">{t('sync:dashboard.newlyValid')}</p>
              <p className={cn('text-base font-bold', result.newlyValid > 0 ? 'text-green-600 dark:text-green-400' : 'text-gray-900 dark:text-white')}>{result.newlyValid}</p>
            </div>
          </div>
          <div className="grid grid-cols-2 gap-2 mt-2 text-center">
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
              <p className="text-xs text-gray-500 dark:text-gray-400">{t('sync:dashboard.noChange')}</p>
              <p className="text-base font-bold text-gray-900 dark:text-white">{result.unchanged.toLocaleString()}</p>
            </div>
            <div className={cn('rounded-lg p-2', result.errors > 0 ? 'bg-red-50 dark:bg-red-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
              <p className="text-xs text-gray-500 dark:text-gray-400">{ t('sync:dashboard.error') }</p>
              <p className={cn('text-base font-bold', result.errors > 0 ? 'text-red-600 dark:text-red-400' : 'text-gray-900 dark:text-white')}>{result.errors}</p>
            </div>
          </div>
        </div>

        {/* Step 2: Trust Chain Re-validation */}
        <div className="border border-blue-200 dark:border-blue-800 rounded-lg p-4">
          <h4 className="text-sm font-semibold text-blue-700 dark:text-blue-300 mb-3">
            {t('sync:dashboard.step2TrustChain')}
          </h4>
          <div className="grid grid-cols-4 gap-2 text-center">
            <div className="bg-blue-50 dark:bg-blue-900/20 rounded-lg p-2">
              <p className="text-xs text-gray-500 dark:text-gray-400">{t('sync:dashboard.target')}</p>
              <p className="text-base font-bold text-blue-700 dark:text-blue-300">{(result.tcProcessed ?? 0).toLocaleString()}</p>
            </div>
            <div className={cn('rounded-lg p-2', (result.tcNewlyValid ?? 0) > 0 ? 'bg-green-50 dark:bg-green-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
              <p className="text-xs text-gray-500 dark:text-gray-400">{t('sync:dashboard.validTransition')}</p>
              <p className={cn('text-base font-bold', (result.tcNewlyValid ?? 0) > 0 ? 'text-green-600 dark:text-green-400' : 'text-gray-900 dark:text-white')}>{(result.tcNewlyValid ?? 0).toLocaleString()}</p>
            </div>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
              <p className="text-xs text-gray-500 dark:text-gray-400">{t('sync:dashboard.stillPending')}</p>
              <p className="text-base font-bold text-gray-900 dark:text-white">{(result.tcStillPending ?? 0).toLocaleString()}</p>
            </div>
            <div className={cn('rounded-lg p-2', (result.tcErrors ?? 0) > 0 ? 'bg-red-50 dark:bg-red-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
              <p className="text-xs text-gray-500 dark:text-gray-400">{ t('sync:dashboard.error') }</p>
              <p className={cn('text-base font-bold', (result.tcErrors ?? 0) > 0 ? 'text-red-600 dark:text-red-400' : 'text-gray-900 dark:text-white')}>{result.tcErrors ?? 0}</p>
            </div>
          </div>
        </div>

        {/* Step 3: CRL Re-check */}
        <div className="border border-purple-200 dark:border-purple-800 rounded-lg p-4">
          <h4 className="text-sm font-semibold text-purple-700 dark:text-purple-300 mb-3">
            {t('sync:dashboard.step3CrlCheck')}
          </h4>
          <div className="grid grid-cols-5 gap-2 text-center">
            <div className="bg-purple-50 dark:bg-purple-900/20 rounded-lg p-2">
              <p className="text-xs text-gray-500 dark:text-gray-400">{t('sync:dashboard.checked')}</p>
              <p className="text-base font-bold text-purple-700 dark:text-purple-300">{(result.crlChecked ?? 0).toLocaleString()}</p>
            </div>
            <div className={cn('rounded-lg p-2', (result.crlRevoked ?? 0) > 0 ? 'bg-red-50 dark:bg-red-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
              <p className="text-xs text-gray-500 dark:text-gray-400">{t('sync:dashboard.revoked')}</p>
              <p className={cn('text-base font-bold', (result.crlRevoked ?? 0) > 0 ? 'text-red-600 dark:text-red-400' : 'text-gray-900 dark:text-white')}>{result.crlRevoked ?? 0}</p>
            </div>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
              <p className="text-xs text-gray-500 dark:text-gray-400">{t('sync:dashboard.crlUnavailable')}</p>
              <p className="text-base font-bold text-gray-900 dark:text-white">{(result.crlUnavailable ?? 0).toLocaleString()}</p>
            </div>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
              <p className="text-xs text-gray-500 dark:text-gray-400">{t('sync:dashboard.crlExpired')}</p>
              <p className="text-base font-bold text-gray-900 dark:text-white">{result.crlExpired ?? 0}</p>
            </div>
            <div className={cn('rounded-lg p-2', (result.crlErrors ?? 0) > 0 ? 'bg-red-50 dark:bg-red-900/20' : 'bg-gray-50 dark:bg-gray-700/50')}>
              <p className="text-xs text-gray-500 dark:text-gray-400">{ t('sync:dashboard.error') }</p>
              <p className={cn('text-base font-bold', (result.crlErrors ?? 0) > 0 ? 'text-red-600 dark:text-red-400' : 'text-gray-900 dark:text-white')}>{result.crlErrors ?? 0}</p>
            </div>
          </div>
        </div>

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
