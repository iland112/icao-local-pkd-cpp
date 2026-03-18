import { useTranslation } from 'react-i18next';
import { Users, ArrowUpDown, BookOpen, PenTool, Clock, Link } from 'lucide-react';
import type { NginxStatus } from '@/services/monitoringApi';

interface Props {
  nginx: NginxStatus;
  requestsPerSecond: number;
  uniqueUsers: number;
}

export default function ActiveConnectionsCard({ nginx, requestsPerSecond, uniqueUsers }: Props) {
  const { t } = useTranslation(['monitoring', 'common']);
  return (
    <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-4 h-full">
      <div className="flex items-center gap-2 mb-4">
        <Users className="w-5 h-5 text-cyan-500" />
        <h3 className="font-semibold text-gray-800 dark:text-white">{ t('monitoring:activeConnections') }</h3>
      </div>

      {/* Primary: Unique Users */}
      <div className="flex items-baseline gap-1 mb-1">
        <span className="text-3xl font-bold text-gray-900 dark:text-white">
          {uniqueUsers}
        </span>
        <span className="text-sm text-gray-500 dark:text-gray-400">{t('monitoring:dashboard.activeNow')}</span>
      </div>
      <div className="text-xs text-gray-400 dark:text-gray-500 mb-4">{t('monitoring:connections.uniqueIpBasis')}</div>

      {/* Secondary: TCP Connections */}
      <div className="flex items-center gap-1.5 mb-3 px-3 py-2 bg-gray-50 dark:bg-gray-700/50 rounded-lg">
        <Link className="w-3.5 h-3.5 text-gray-400" />
        <span className="text-sm text-gray-600 dark:text-gray-300">
          {t('monitoring:dashboard.tcpConnections')}: <span className="font-semibold text-gray-800 dark:text-white">{nginx.activeConnections}</span>
        </span>
      </div>

      <div className="grid grid-cols-3 gap-2 mb-3">
        <div className="text-center p-2 bg-blue-50 dark:bg-blue-900/20 rounded-lg">
          <BookOpen className="w-3.5 h-3.5 text-blue-500 mx-auto mb-1" />
          <div className="text-lg font-bold text-gray-900 dark:text-white">{nginx.reading}</div>
          <div className="text-xs text-gray-500 dark:text-gray-400">Reading</div>
        </div>
        <div className="text-center p-2 bg-green-50 dark:bg-green-900/20 rounded-lg">
          <PenTool className="w-3.5 h-3.5 text-green-500 mx-auto mb-1" />
          <div className="text-lg font-bold text-gray-900 dark:text-white">{nginx.writing}</div>
          <div className="text-xs text-gray-500 dark:text-gray-400">Writing</div>
        </div>
        <div className="text-center p-2 bg-amber-50 dark:bg-amber-900/20 rounded-lg">
          <Clock className="w-3.5 h-3.5 text-amber-500 mx-auto mb-1" />
          <div className="text-lg font-bold text-gray-900 dark:text-white">{nginx.waiting}</div>
          <div className="text-xs text-gray-500 dark:text-gray-400">Waiting</div>
        </div>
      </div>

      <div className="space-y-1 text-sm text-gray-600 dark:text-gray-400">
        <div className="flex justify-between">
          <span className="flex items-center gap-1"><ArrowUpDown className="w-3.5 h-3.5" /> {t('monitoring:connections.throughput')}</span>
          <span className="font-medium text-gray-800 dark:text-gray-200">{requestsPerSecond.toFixed(1)} req/s</span>
        </div>
        <div className="flex justify-between">
          <span>{t('monitoring:connections.totalRequests')}</span>
          <span className="font-medium text-gray-800 dark:text-gray-200">{nginx.totalRequests.toLocaleString()}</span>
        </div>
      </div>
    </div>
  );
}
