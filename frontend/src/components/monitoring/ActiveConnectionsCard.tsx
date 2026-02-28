import { Users, ArrowUpDown, BookOpen, PenTool, Clock } from 'lucide-react';
import type { NginxStatus } from '@/services/monitoringApi';

interface Props {
  nginx: NginxStatus;
  requestsPerSecond: number;
}

export default function ActiveConnectionsCard({ nginx, requestsPerSecond }: Props) {
  return (
    <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-5">
      <div className="flex items-center gap-2 mb-4">
        <Users className="w-5 h-5 text-cyan-500" />
        <h3 className="font-semibold text-gray-800 dark:text-white">동시 접속</h3>
      </div>

      <div className="flex items-baseline gap-1 mb-4">
        <span className="text-3xl font-bold text-gray-900 dark:text-white">
          {nginx.activeConnections}
        </span>
        <span className="text-sm text-gray-500 dark:text-gray-400">active</span>
      </div>

      <div className="grid grid-cols-3 gap-2 mb-3">
        <div className="text-center p-2 bg-blue-50 dark:bg-blue-900/20 rounded-lg">
          <BookOpen className="w-3.5 h-3.5 text-blue-500 mx-auto mb-1" />
          <div className="text-lg font-bold text-gray-900 dark:text-white">{nginx.reading}</div>
          <div className="text-[10px] text-gray-500 dark:text-gray-400">Reading</div>
        </div>
        <div className="text-center p-2 bg-green-50 dark:bg-green-900/20 rounded-lg">
          <PenTool className="w-3.5 h-3.5 text-green-500 mx-auto mb-1" />
          <div className="text-lg font-bold text-gray-900 dark:text-white">{nginx.writing}</div>
          <div className="text-[10px] text-gray-500 dark:text-gray-400">Writing</div>
        </div>
        <div className="text-center p-2 bg-amber-50 dark:bg-amber-900/20 rounded-lg">
          <Clock className="w-3.5 h-3.5 text-amber-500 mx-auto mb-1" />
          <div className="text-lg font-bold text-gray-900 dark:text-white">{nginx.waiting}</div>
          <div className="text-[10px] text-gray-500 dark:text-gray-400">Waiting</div>
        </div>
      </div>

      <div className="space-y-1 text-sm text-gray-600 dark:text-gray-400">
        <div className="flex justify-between">
          <span className="flex items-center gap-1"><ArrowUpDown className="w-3.5 h-3.5" /> 처리량:</span>
          <span className="font-medium text-gray-800 dark:text-gray-200">{requestsPerSecond.toFixed(1)} req/s</span>
        </div>
        <div className="flex justify-between">
          <span>총 요청:</span>
          <span className="font-medium text-gray-800 dark:text-gray-200">{nginx.totalRequests.toLocaleString()}</span>
        </div>
      </div>
    </div>
  );
}
