import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import {
  Loader2,
  CalendarClock,
  ShieldCheck,
  RotateCcw,
  Save,
  X as CloseIcon,
} from 'lucide-react';
import { cn } from '@/utils/cn';
import { Dialog } from '@/components/common/Dialog';
import { syncServiceApi, type SyncConfigResponse, type UpdateSyncConfigRequest } from '@/services/api';

interface SyncConfigDialogProps {
  config: SyncConfigResponse;
  onClose: () => void;
  onSaved: (savedConfig: SyncConfigResponse) => void;
  onError: (message: string) => void;
  onRefresh: () => void;
}

export function SyncConfigDialog({ config, onClose, onSaved, onError, onRefresh }: SyncConfigDialogProps) {
  const { t } = useTranslation(['sync', 'common']);

  const [editedConfig, setEditedConfig] = useState<UpdateSyncConfigRequest>({
    dailySyncEnabled: config.dailySyncEnabled,
    dailySyncHour: config.dailySyncHour,
    dailySyncMinute: config.dailySyncMinute,
    autoReconcile: config.autoReconcile,
    revalidateCertsOnSync: config.revalidateCertsOnSync,
    maxReconcileBatchSize: config.maxReconcileBatchSize,
  });
  const [saving, setSaving] = useState(false);

  const handleSaveConfig = async () => {
    setSaving(true);
    try {
      const res = await syncServiceApi.updateConfig(editedConfig);
      await onRefresh();
      onClose();
      if (res.data?.config) {
        onSaved(res.data.config);
      }
    } catch (err) {
      if (import.meta.env.DEV) console.error('Failed to update config:', err);
      onError(t('dashboard.configUpdateFailed'));
    } finally {
      setSaving(false);
    }
  };

  return (
    <Dialog
      isOpen={true}
      onClose={onClose}
      title={t('sync:dashboard.editServiceConfig')}
      size="lg"
    >
      <div className="space-y-6 py-4">
        {/* Daily Sync Schedule */}
        <div className="space-y-3">
          <div className="flex items-center justify-between">
            <label className="flex items-center gap-2 text-sm font-medium text-gray-700 dark:text-gray-300">
              <CalendarClock className="w-5 h-5 text-purple-500" />
              {t('sync:reconciliation.dailySync')}
            </label>
            <button
              type="button"
              onClick={() =>
                setEditedConfig((prev) => ({
                  ...prev,
                  dailySyncEnabled: !prev.dailySyncEnabled,
                }))
              }
              className={cn(
                'relative inline-flex h-6 w-11 items-center rounded-full transition-colors',
                editedConfig.dailySyncEnabled
                  ? 'bg-blue-600'
                  : 'bg-gray-200 dark:bg-gray-700'
              )}
            >
              <span
                className={cn(
                  'inline-block h-4 w-4 transform rounded-full bg-white transition-transform',
                  editedConfig.dailySyncEnabled ? 'translate-x-6' : 'translate-x-1'
                )}
              />
            </button>
          </div>

          {editedConfig.dailySyncEnabled && (
            <div className="flex items-center gap-3 pl-7">
              <div className="flex items-center gap-2">
                <label className="text-sm text-gray-600 dark:text-gray-400">{t('common:time.hour')}</label>
                <input
                  type="number"
                  min="0"
                  max="23"
                  value={editedConfig.dailySyncHour ?? 0}
                  onChange={(e) =>
                    setEditedConfig((prev) => ({
                      ...prev,
                      dailySyncHour: parseInt(e.target.value) || 0,
                    }))
                  }
                  className="w-16 px-2 py-1 text-center border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white"
                />
              </div>
              <span className="text-gray-400">:</span>
              <div className="flex items-center gap-2">
                <label className="text-sm text-gray-600 dark:text-gray-400">{ t('common:label.minutes') }</label>
                <input
                  type="number"
                  min="0"
                  max="59"
                  value={editedConfig.dailySyncMinute ?? 0}
                  onChange={(e) =>
                    setEditedConfig((prev) => ({
                      ...prev,
                      dailySyncMinute: parseInt(e.target.value) || 0,
                    }))
                  }
                  className="w-16 px-2 py-1 text-center border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white"
                />
              </div>
            </div>
          )}
        </div>

        {/* Auto Re-validation */}
        <div className="flex items-center justify-between">
          <label className="flex items-center gap-2 text-sm font-medium text-gray-700 dark:text-gray-300">
            <ShieldCheck className="w-5 h-5 text-green-500" />
            {t('sync:dashboard.autoRevalidation')}
          </label>
          <button
            type="button"
            onClick={() =>
              setEditedConfig((prev) => ({
                ...prev,
                revalidateCertsOnSync: !prev.revalidateCertsOnSync,
              }))
            }
            className={cn(
              'relative inline-flex h-6 w-11 items-center rounded-full transition-colors',
              editedConfig.revalidateCertsOnSync
                ? 'bg-green-600'
                : 'bg-gray-200 dark:bg-gray-700'
            )}
          >
            <span
              className={cn(
                'inline-block h-4 w-4 transform rounded-full bg-white transition-transform',
                editedConfig.revalidateCertsOnSync ? 'translate-x-6' : 'translate-x-1'
              )}
            />
          </button>
        </div>

        {/* Auto Reconcile */}
        <div className="flex items-center justify-between">
          <label className="flex items-center gap-2 text-sm font-medium text-gray-700 dark:text-gray-300">
            <RotateCcw className="w-5 h-5 text-orange-500" />
            {t('sync:dashboard.autoReconcileLabel')}
          </label>
          <button
            type="button"
            onClick={() =>
              setEditedConfig((prev) => ({
                ...prev,
                autoReconcile: !prev.autoReconcile,
              }))
            }
            className={cn(
              'relative inline-flex h-6 w-11 items-center rounded-full transition-colors',
              editedConfig.autoReconcile
                ? 'bg-orange-600'
                : 'bg-gray-200 dark:bg-gray-700'
            )}
          >
            <span
              className={cn(
                'inline-block h-4 w-4 transform rounded-full bg-white transition-transform',
                editedConfig.autoReconcile ? 'translate-x-6' : 'translate-x-1'
              )}
            />
          </button>
        </div>

        {/* Action Buttons */}
        <div className="flex items-center justify-end gap-3 pt-4 border-t border-gray-200 dark:border-gray-700">
          <button
            onClick={onClose}
            disabled={saving}
            className="flex items-center gap-2 px-4 py-2 text-sm font-medium text-gray-700 dark:text-gray-300 bg-gray-100 dark:bg-gray-700 rounded-lg hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors disabled:opacity-50"
          >
            <CloseIcon className="w-4 h-4" />
            {t('common:button.cancel')}
          </button>
          <button
            onClick={handleSaveConfig}
            disabled={saving}
            className="flex items-center gap-2 px-4 py-2 text-sm font-medium text-white bg-blue-600 rounded-lg hover:bg-blue-700 transition-colors disabled:opacity-50"
          >
            {saving ? (
              <>
                <Loader2 className="w-4 h-4 animate-spin" />
                {t('common:label.saving')}
              </>
            ) : (
              <>
                <Save className="w-4 h-4" />
                {t('common:button.save')}
              </>
            )}
          </button>
        </div>
      </div>
    </Dialog>
  );
}
