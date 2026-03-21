import { useTranslation } from 'react-i18next';
import {
  CheckCircle,
  CalendarClock,
  ShieldCheck,
  RotateCcw,
} from 'lucide-react';
import { cn } from '@/utils/cn';
import { Dialog } from '@/components/common/Dialog';
import type { SyncConfigResponse } from '@/services/api';

interface ConfigSaveResultDialogProps {
  result: SyncConfigResponse;
  onClose: () => void;
}

export function ConfigSaveResultDialog({ result, onClose }: ConfigSaveResultDialogProps) {
  const { t } = useTranslation(['sync', 'common', 'admin']);

  return (
    <Dialog
      isOpen={true}
      onClose={onClose}
      title={t('sync:dashboard.configSaved')}
      size="md"
    >
      <div className="space-y-4">
        <div className="flex items-center gap-3 p-3 rounded-lg bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800">
          <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400 flex-shrink-0" />
          <span className="text-sm font-medium text-green-800 dark:text-green-300">
            {t('sync:dashboard.configSavedMsg')}
          </span>
        </div>

        <div className="border border-gray-200 dark:border-gray-700 rounded-lg divide-y divide-gray-200 dark:divide-gray-700">
          <div className="flex items-center justify-between p-3">
            <div className="flex items-center gap-2">
              <CalendarClock className="w-4 h-4 text-purple-500" />
              <span className="text-sm text-gray-700 dark:text-gray-300">{ t('admin:operationAudit.dailySync') }</span>
            </div>
            <span className={cn('text-sm font-semibold', result.dailySyncEnabled ? 'text-blue-600 dark:text-blue-400' : 'text-gray-400 dark:text-gray-500')}>
              {result.dailySyncEnabled ? t('dashboard.daily', { time: result.dailySyncTime }) : t('dashboard.disabled')}
            </span>
          </div>
          <div className="flex items-center justify-between p-3">
            <div className="flex items-center gap-2">
              <ShieldCheck className="w-4 h-4 text-green-500" />
              <span className="text-sm text-gray-700 dark:text-gray-300">{t('dashboard.autoRevalidation')}</span>
            </div>
            <span className={cn('text-sm font-semibold', result.revalidateCertsOnSync ? 'text-green-600 dark:text-green-400' : 'text-gray-400 dark:text-gray-500')}>
              {result.revalidateCertsOnSync ? t('dashboard.enabled') : t('dashboard.disabled')}
            </span>
          </div>
          <div className="flex items-center justify-between p-3">
            <div className="flex items-center gap-2">
              <RotateCcw className="w-4 h-4 text-orange-500" />
              <span className="text-sm text-gray-700 dark:text-gray-300">{t('dashboard.autoReconcile')}</span>
            </div>
            <span className={cn('text-sm font-semibold', result.autoReconcile ? 'text-orange-600 dark:text-orange-400' : 'text-gray-400 dark:text-gray-500')}>
              {result.autoReconcile ? t('dashboard.enabled') : t('dashboard.disabled')}
            </span>
          </div>
        </div>

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
