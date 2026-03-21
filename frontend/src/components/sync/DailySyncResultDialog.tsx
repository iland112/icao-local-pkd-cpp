import { useTranslation } from 'react-i18next';
import {
  CheckCircle,
  XCircle,
  Activity,
  Play,
  ShieldCheck,
  RotateCcw,
} from 'lucide-react';
import { cn } from '@/utils/cn';
import { Dialog } from '@/components/common/Dialog';
import type { SyncConfigResponse } from '@/services/api';

interface DailySyncResultDialogProps {
  result: { success: boolean; message: string };
  config: SyncConfigResponse | null;
  onClose: () => void;
}

export function DailySyncResultDialog({ result, config, onClose }: DailySyncResultDialogProps) {
  const { t } = useTranslation(['sync', 'common']);

  return (
    <Dialog
      isOpen={true}
      onClose={onClose}
      title={t('sync:dashboard.manualSync')}
      size="md"
    >
      <div className="space-y-4">
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
              ? t('sync:dashboard.dailySyncTriggered')
              : result.message}
          </span>
        </div>

        {result.success && (
          <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
            <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 flex items-center gap-2">
              <Activity className="w-4 h-4 text-blue-500" />
              {t('sync:dashboard.executedTasks')}
            </h4>
            <div className="space-y-2">
              {[
                { icon: <Play className="w-3.5 h-3.5 text-blue-500" />, label: t('sync:dashboard.taskSyncCheck') },
                { icon: <ShieldCheck className="w-3.5 h-3.5 text-purple-500" />, label: t('sync:dashboard.taskRevalidation'), note: config?.revalidateCertsOnSync ? t('dashboard.enabled') : t('dashboard.disabled') },
                { icon: <RotateCcw className="w-3.5 h-3.5 text-orange-500" />, label: t('sync:dashboard.taskReconciliation'), note: config?.autoReconcile ? t('dashboard.enabled') : t('dashboard.disabled') },
              ].map((step, i) => (
                <div key={i} className="flex items-center gap-3 p-2 bg-gray-50 dark:bg-gray-700/50 rounded-lg">
                  <div className="flex items-center justify-center w-6 h-6 bg-white dark:bg-gray-600 rounded-full text-xs font-bold text-gray-600 dark:text-gray-300 shadow-sm">
                    {i + 1}
                  </div>
                  {step.icon}
                  <span className="text-sm text-gray-700 dark:text-gray-300 flex-1">{step.label}</span>
                  {step.note && (
                    <span className={cn(
                      'text-xs px-1.5 py-0.5 rounded font-medium',
                      step.note === t('dashboard.enabled')
                        ? 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400'
                        : 'bg-gray-200 dark:bg-gray-600 text-gray-500 dark:text-gray-400'
                    )}>
                      {step.note}
                    </span>
                  )}
                </div>
              ))}
            </div>
            <p className="mt-3 text-xs text-gray-500 dark:text-gray-400">
              {t('sync:dashboard.backgroundRunning')}
            </p>
          </div>
        )}

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
