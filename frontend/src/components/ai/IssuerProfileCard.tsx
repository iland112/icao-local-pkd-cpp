import { useState, useEffect } from 'react';
import { Users, AlertTriangle } from 'lucide-react';
import { BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, Cell } from 'recharts';
import aiAnalysisApi, { type IssuerProfile } from '@/services/aiAnalysisApi';
const RISK_COLORS: Record<string, string> = {
  HIGH: '#ef4444',
  MEDIUM: '#f59e0b',
  LOW: '#22c55e',
};

const RISK_BADGE: Record<string, string> = {
  HIGH: 'text-red-600 bg-red-50 dark:text-red-400 dark:bg-red-900/30',
  MEDIUM: 'text-amber-600 bg-amber-50 dark:text-amber-400 dark:bg-amber-900/30',
  LOW: 'text-green-600 bg-green-50 dark:text-green-400 dark:bg-green-900/30',
};

function truncateDn(dn: string, maxLen = 60): string {
  if (dn.length <= maxLen) return dn;
  return dn.slice(0, maxLen) + '...';
}

export default function IssuerProfileCard() {
  const [profiles, setProfiles] = useState<IssuerProfile[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    aiAnalysisApi.getIssuerProfiles()
      .then(res => setProfiles(res.data))
      .catch(() => {})
      .finally(() => setLoading(false));
  }, []);

  if (loading) {
    return (
      <div className="rounded-xl border border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800 p-6">
        <div className="animate-pulse space-y-4">
          <div className="h-5 bg-gray-200 dark:bg-gray-700 rounded w-1/3" />
          <div className="h-40 bg-gray-200 dark:bg-gray-700 rounded" />
        </div>
      </div>
    );
  }

  if (profiles.length === 0) {
    return null;
  }

  // Top 15 issuers by cert count
  const chartData = profiles
    .sort((a, b) => b.cert_count - a.cert_count)
    .slice(0, 15)
    .map(p => ({
      name: truncateDn(p.issuer_dn, 40),
      count: p.cert_count,
      compliance: Math.round(p.compliance_rate * 100),
      risk: p.risk_indicator,
      country: p.country,
    }));

  const riskSummary = {
    HIGH: profiles.filter(p => p.risk_indicator === 'HIGH').length,
    MEDIUM: profiles.filter(p => p.risk_indicator === 'MEDIUM').length,
    LOW: profiles.filter(p => p.risk_indicator === 'LOW').length,
  };

  return (
    <div className="rounded-xl border border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800 p-6">
      <div className="flex items-center justify-between mb-4">
        <h3 className="text-base font-semibold flex items-center gap-2 dark:text-white">
          <Users className="w-5 h-5 text-blue-500" />
          발급자 프로파일 ({profiles.length}개)
        </h3>
        <div className="flex gap-2">
          {Object.entries(riskSummary).map(([level, count]) => (
            count > 0 && (
              <span key={level} className={`px-2 py-0.5 rounded text-xs font-medium ${RISK_BADGE[level]}`}>
                {level} {count}
              </span>
            )
          ))}
        </div>
      </div>

      {/* Bar chart: top issuers by cert count */}
      <div className="h-64">
        <ResponsiveContainer width="100%" height="100%">
          <BarChart data={chartData} layout="vertical" margin={{ left: 10, right: 20, top: 5, bottom: 5 }}>
            <CartesianGrid strokeDasharray="3 3" opacity={0.3} />
            <XAxis type="number" tick={{ fontSize: 11 }} />
            <YAxis type="category" dataKey="name" tick={{ fontSize: 10 }} width={180} />
            <Tooltip
              content={({ active, payload }) => {
                if (!active || !payload?.length) return null;
                const d = payload[0].payload;
                return (
                  <div className="bg-white dark:bg-gray-800 p-3 rounded-lg shadow-lg border text-sm">
                    <p className="font-medium dark:text-white">{d.name}</p>
                    <p className="text-gray-500">인증서: {d.count}개</p>
                    <p className="text-gray-500">준수율: {d.compliance}%</p>
                    <p className="text-gray-500">국가: {d.country}</p>
                    <p className={d.risk === 'HIGH' ? 'text-red-500' : d.risk === 'MEDIUM' ? 'text-amber-500' : 'text-green-500'}>
                      리스크: {d.risk}
                    </p>
                  </div>
                );
              }}
            />
            <Bar dataKey="count" name="인증서 수">
              {chartData.map((entry, idx) => (
                <Cell key={idx} fill={RISK_COLORS[entry.risk] || RISK_COLORS.LOW} />
              ))}
            </Bar>
          </BarChart>
        </ResponsiveContainer>
      </div>

      {/* Table: top risk issuers */}
      {riskSummary.HIGH > 0 && (
        <div className="mt-4">
          <h4 className="text-sm font-medium text-gray-600 dark:text-gray-400 mb-2">
            <AlertTriangle className="w-4 h-4 inline mr-1 text-red-500" />
            고위험 발급자
          </h4>
          <div className="overflow-x-auto">
            <table className="w-full text-sm">
              <thead>
                <tr className="text-gray-500 dark:text-gray-400 text-left">
                  <th className="pb-1 font-medium">발급자 DN</th>
                  <th className="pb-1 font-medium text-center">인증서</th>
                  <th className="pb-1 font-medium text-center">준수율</th>
                  <th className="pb-1 font-medium text-center">만료율</th>
                </tr>
              </thead>
              <tbody>
                {profiles
                  .filter(p => p.risk_indicator === 'HIGH')
                  .slice(0, 5)
                  .map((p, idx) => (
                    <tr key={idx} className="border-t border-gray-100 dark:border-gray-700">
                      <td className="py-1.5 text-gray-700 dark:text-gray-300" title={p.issuer_dn}>
                        {truncateDn(p.issuer_dn, 50)}
                      </td>
                      <td className="py-1.5 text-center">{p.cert_count}</td>
                      <td className="py-1.5 text-center text-red-500">
                        {(p.compliance_rate * 100).toFixed(0)}%
                      </td>
                      <td className="py-1.5 text-center text-amber-500">
                        {(p.expired_rate * 100).toFixed(0)}%
                      </td>
                    </tr>
                  ))}
              </tbody>
            </table>
          </div>
        </div>
      )}
    </div>
  );
}
