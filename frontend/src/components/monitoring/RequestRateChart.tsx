import { AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';
import { TrendingUp } from 'lucide-react';
import type { HistoryPoint } from '@/services/monitoringApi';

interface Props {
  data: HistoryPoint[];
}

export default function RequestRateChart({ data }: Props) {
  const chartData = data.map(p => ({
    time: new Date(p.timestamp).toLocaleTimeString('ko-KR', { hour: '2-digit', minute: '2-digit', second: '2-digit' }),
    rps: Number(p.nginx.requestsPerSecond.toFixed(1)),
    connections: p.nginx.activeConnections,
  }));

  return (
    <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-5">
      <div className="flex items-center gap-2 mb-4">
        <TrendingUp className="w-5 h-5 text-blue-500" />
        <h3 className="font-semibold text-gray-800 dark:text-white">요청 처리량 트렌드</h3>
        <span className="text-xs text-gray-500 dark:text-gray-400 ml-auto">{data.length} points</span>
      </div>

      {chartData.length === 0 ? (
        <div className="flex items-center justify-center h-48 text-gray-400 dark:text-gray-500 text-sm">
          데이터 수집 중...
        </div>
      ) : (
        <ResponsiveContainer width="100%" height={220}>
          <AreaChart data={chartData} margin={{ top: 5, right: 10, left: -10, bottom: 0 }}>
            <defs>
              <linearGradient id="rpsGrad" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#3b82f6" stopOpacity={0.3} />
                <stop offset="95%" stopColor="#3b82f6" stopOpacity={0} />
              </linearGradient>
              <linearGradient id="connGrad" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#06b6d4" stopOpacity={0.2} />
                <stop offset="95%" stopColor="#06b6d4" stopOpacity={0} />
              </linearGradient>
            </defs>
            <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
            <XAxis
              dataKey="time"
              tick={{ fontSize: 10 }}
              interval="preserveStartEnd"
              stroke="#9ca3af"
            />
            <YAxis
              yAxisId="rps"
              tick={{ fontSize: 10 }}
              stroke="#9ca3af"
              label={{ value: 'req/s', angle: -90, position: 'insideLeft', style: { fontSize: 10, fill: '#9ca3af' } }}
            />
            <YAxis
              yAxisId="conn"
              orientation="right"
              tick={{ fontSize: 10 }}
              stroke="#9ca3af"
              label={{ value: 'conn', angle: 90, position: 'insideRight', style: { fontSize: 10, fill: '#9ca3af' } }}
            />
            <Tooltip
              contentStyle={{ fontSize: 12, borderRadius: 8, border: '1px solid #e5e7eb' }}
              formatter={(value, name) => [
                value,
                name === 'rps' ? '처리량 (req/s)' : '동시 접속',
              ]}
            />
            <Area
              yAxisId="rps"
              type="monotone"
              dataKey="rps"
              stroke="#3b82f6"
              strokeWidth={2}
              fill="url(#rpsGrad)"
            />
            <Area
              yAxisId="conn"
              type="monotone"
              dataKey="connections"
              stroke="#06b6d4"
              strokeWidth={1.5}
              fill="url(#connGrad)"
              strokeDasharray="4 2"
            />
          </AreaChart>
        </ResponsiveContainer>
      )}
    </div>
  );
}
