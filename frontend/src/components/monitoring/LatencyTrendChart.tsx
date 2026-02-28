import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from 'recharts';
import { Timer } from 'lucide-react';
import type { HistoryPoint } from '@/services/monitoringApi';

interface Props {
  data: HistoryPoint[];
}

const SERVICE_COLORS: Record<string, string> = {
  'pkd-management': '#3b82f6',
  'pa-service': '#10b981',
  'pkd-relay': '#f59e0b',
  'ai-analysis': '#8b5cf6',
};

const SERVICE_LABELS: Record<string, string> = {
  'pkd-management': 'PKD Management',
  'pa-service': 'PA Service',
  'pkd-relay': 'PKD Relay',
  'ai-analysis': 'AI Analysis',
};

export default function LatencyTrendChart({ data }: Props) {
  // Get all service names from data
  const serviceNames = new Set<string>();
  data.forEach(p => Object.keys(p.latency).forEach(k => serviceNames.add(k)));

  const chartData = data.map(p => ({
    time: new Date(p.timestamp).toLocaleTimeString('ko-KR', { hour: '2-digit', minute: '2-digit', second: '2-digit' }),
    ...p.latency,
  }));

  return (
    <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-5">
      <div className="flex items-center gap-2 mb-4">
        <Timer className="w-5 h-5 text-green-500" />
        <h3 className="font-semibold text-gray-800 dark:text-white">서비스 응답시간 (Latency)</h3>
      </div>

      {chartData.length === 0 ? (
        <div className="flex items-center justify-center h-48 text-gray-400 dark:text-gray-500 text-sm">
          데이터 수집 중...
        </div>
      ) : (
        <ResponsiveContainer width="100%" height={220}>
          <LineChart data={chartData} margin={{ top: 5, right: 10, left: -10, bottom: 0 }}>
            <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
            <XAxis
              dataKey="time"
              tick={{ fontSize: 10 }}
              interval="preserveStartEnd"
              stroke="#9ca3af"
            />
            <YAxis
              tick={{ fontSize: 10 }}
              stroke="#9ca3af"
              label={{ value: 'ms', angle: -90, position: 'insideLeft', style: { fontSize: 10, fill: '#9ca3af' } }}
            />
            <Tooltip
              contentStyle={{ fontSize: 12, borderRadius: 8, border: '1px solid #e5e7eb' }}
              formatter={(value, name) => [`${value}ms`, SERVICE_LABELS[name as string] || name]}
            />
            <Legend
              iconType="circle"
              iconSize={8}
              wrapperStyle={{ fontSize: 11 }}
              formatter={(value: string) => SERVICE_LABELS[value] || value}
            />
            {[...serviceNames].map(name => (
              <Line
                key={name}
                type="monotone"
                dataKey={name}
                stroke={SERVICE_COLORS[name] || '#6b7280'}
                strokeWidth={2}
                dot={false}
                activeDot={{ r: 3 }}
              />
            ))}
          </LineChart>
        </ResponsiveContainer>
      )}
    </div>
  );
}
