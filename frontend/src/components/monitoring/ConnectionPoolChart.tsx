import { BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer, Cell } from 'recharts';
import { Database } from 'lucide-react';
import type { ServiceLoadMetrics } from '@/services/monitoringApi';

interface Props {
  services: ServiceLoadMetrics[];
}

const SERVICE_SHORT: Record<string, string> = {
  'pkd-management': 'PKD Mgmt',
  'pa-service': 'PA Svc',
  'pkd-relay': 'PKD Relay',
  'ai-analysis': 'AI',
};

export default function ConnectionPoolChart({ services }: Props) {
  const chartData = services.flatMap(svc => {
    const items = [];
    const label = SERVICE_SHORT[svc.name] || svc.name;
    if (svc.dbPool) {
      const inUse = svc.dbPool.total - svc.dbPool.available;
      items.push({
        name: `${label} DB`,
        inUse,
        available: svc.dbPool.available,
        max: svc.dbPool.max,
        usagePercent: svc.dbPool.max > 0 ? Math.round((inUse / svc.dbPool.max) * 100) : 0,
      });
    }
    if (svc.ldapPool) {
      const inUse = svc.ldapPool.total - svc.ldapPool.available;
      items.push({
        name: `${label} LDAP`,
        inUse,
        available: svc.ldapPool.available,
        max: svc.ldapPool.max,
        usagePercent: svc.ldapPool.max > 0 ? Math.round((inUse / svc.ldapPool.max) * 100) : 0,
      });
    }
    return items;
  });

  if (chartData.length === 0) {
    return (
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-5">
        <div className="flex items-center gap-2 mb-4">
          <Database className="w-5 h-5 text-indigo-500" />
          <h3 className="font-semibold text-gray-800 dark:text-white">커넥션 풀 사용량</h3>
        </div>
        <div className="flex items-center justify-center h-48 text-gray-400 dark:text-gray-500 text-sm">
          풀 데이터 수집 중...
        </div>
      </div>
    );
  }

  const getBarColor = (percent: number) => {
    if (percent < 50) return '#22c55e';
    if (percent < 80) return '#f59e0b';
    return '#ef4444';
  };

  return (
    <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-5">
      <div className="flex items-center gap-2 mb-4">
        <Database className="w-5 h-5 text-indigo-500" />
        <h3 className="font-semibold text-gray-800 dark:text-white">커넥션 풀 사용량</h3>
      </div>

      <ResponsiveContainer width="100%" height={220}>
        <BarChart data={chartData} layout="vertical" margin={{ top: 5, right: 10, left: 10, bottom: 0 }}>
          <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" horizontal={false} />
          <XAxis type="number" tick={{ fontSize: 10 }} stroke="#9ca3af" />
          <YAxis
            type="category"
            dataKey="name"
            tick={{ fontSize: 10 }}
            stroke="#9ca3af"
            width={80}
          />
          <Tooltip
            contentStyle={{ fontSize: 12, borderRadius: 8, border: '1px solid #e5e7eb' }}
            formatter={(value, name) => [
              value,
              name === 'inUse' ? '사용 중' : '여유',
            ]}
          />
          <Legend
            iconSize={10}
            wrapperStyle={{ fontSize: 11 }}
            formatter={(value: string) => (value === 'inUse' ? '사용 중' : '여유')}
          />
          <Bar dataKey="inUse" stackId="pool" barSize={16}>
            {chartData.map((entry, idx) => (
              <Cell key={idx} fill={getBarColor(entry.usagePercent)} />
            ))}
          </Bar>
          <Bar dataKey="available" stackId="pool" fill="#e5e7eb" barSize={16} />
        </BarChart>
      </ResponsiveContainer>
    </div>
  );
}
