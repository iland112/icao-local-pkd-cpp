import { useState, useEffect } from 'react';
import { ShieldAlert, CheckCircle, XCircle, AlertTriangle, ChevronDown, ChevronRight, Filter } from 'lucide-react';
import { aiAnalysisApi, type ExtensionAnomaly } from '@/services/aiAnalysisApi';

const SEVERITY_STYLE: Record<string, string> = {
  CRITICAL: 'text-red-600 bg-red-50 dark:text-red-400 dark:bg-red-900/30',
  HIGH: 'text-orange-600 bg-orange-50 dark:text-orange-400 dark:bg-orange-900/30',
  MEDIUM: 'text-amber-600 bg-amber-50 dark:text-amber-400 dark:bg-amber-900/30',
  LOW: 'text-green-600 bg-green-50 dark:text-green-400 dark:bg-green-900/30',
};

const SEVERITY_ICON: Record<string, typeof XCircle> = {
  CRITICAL: XCircle,
  HIGH: AlertTriangle,
  MEDIUM: AlertTriangle,
  LOW: CheckCircle,
};

function deriveSeverity(score: number): string {
  if (score >= 0.8) return 'CRITICAL';
  if (score >= 0.5) return 'HIGH';
  if (score >= 0.2) return 'MEDIUM';
  return 'LOW';
}

function totalViolations(a: ExtensionAnomaly): number {
  return a.missing_required.length + a.missing_recommended.length
    + a.forbidden_violations.length + a.key_usage_violations.length;
}

interface Props {
  certType?: string;
  country?: string;
}

export default function ExtensionComplianceChecklist({ certType, country }: Props) {
  const [anomalies, setAnomalies] = useState<ExtensionAnomaly[]>([]);
  const [loading, setLoading] = useState(true);
  const [expandedRows, setExpandedRows] = useState<Set<number>>(new Set());
  const [filterType, setFilterType] = useState(certType || '');
  const [filterCountry, setFilterCountry] = useState(country || '');

  useEffect(() => {
    setLoading(true);
    const params: { type?: string; country?: string } = {};
    if (filterType) params.type = filterType;
    if (filterCountry) params.country = filterCountry;
    aiAnalysisApi.getExtensionAnomalies(Object.keys(params).length > 0 ? params : undefined)
      .then(res => setAnomalies(res.data))
      .catch(() => {})
      .finally(() => setLoading(false));
  }, [filterType, filterCountry]);

  const toggleRow = (idx: number) => {
    setExpandedRows(prev => {
      const next = new Set(prev);
      if (next.has(idx)) next.delete(idx);
      else next.add(idx);
      return next;
    });
  };

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

  // Severity distribution based on structural_score
  const severityDist = anomalies.reduce((acc, a) => {
    const sev = deriveSeverity(a.structural_score);
    acc[sev] = (acc[sev] || 0) + 1;
    return acc;
  }, {} as Record<string, number>);

  const certTypes = [...new Set(anomalies.map(a => a.certificate_type).filter((v): v is string => v != null))];
  const countryCodes = [...new Set(anomalies.map(a => a.country_code).filter((v): v is string => v != null))];

  return (
    <div className="rounded-xl border border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800 p-6">
      <div className="flex items-center justify-between mb-4">
        <h3 className="text-base font-semibold flex items-center gap-2 dark:text-white">
          <ShieldAlert className="w-5 h-5 text-orange-500" />
          확장 프로파일 규칙 위반 ({anomalies.length}건)
        </h3>
        <div className="flex gap-2">
          {Object.entries(severityDist).map(([sev, count]) => (
            <span key={sev} className={`px-2 py-0.5 rounded text-xs font-medium ${SEVERITY_STYLE[sev] || SEVERITY_STYLE.MEDIUM}`}>
              {sev} {count}
            </span>
          ))}
        </div>
      </div>

      {/* Filters */}
      <div className="flex gap-3 mb-4">
        <div className="flex items-center gap-1.5">
          <Filter className="w-3.5 h-3.5 text-gray-400" />
          <select
            value={filterType}
            onChange={e => setFilterType(e.target.value)}
            className="text-xs border border-gray-200 dark:border-gray-600 rounded px-2 py-1 bg-white dark:bg-gray-700 dark:text-gray-200"
          >
            <option value="">전체 유형</option>
            {certTypes.map(t => <option key={t} value={t}>{t}</option>)}
          </select>
        </div>
        <select
          value={filterCountry}
          onChange={e => setFilterCountry(e.target.value)}
          className="text-xs border border-gray-200 dark:border-gray-600 rounded px-2 py-1 bg-white dark:bg-gray-700 dark:text-gray-200"
        >
          <option value="">전체 국가</option>
          {countryCodes.map(c => <option key={c} value={c}>{c}</option>)}
        </select>
      </div>

      {anomalies.length === 0 ? (
        <div className="text-center py-8 text-gray-400">
          <CheckCircle className="w-10 h-10 mx-auto mb-2 text-green-400" />
          <p className="text-sm">확장 프로파일 규칙 위반이 없습니다.</p>
        </div>
      ) : (
        <div className="overflow-x-auto max-h-96 overflow-y-auto">
          <table className="w-full text-sm">
            <thead className="sticky top-0 bg-white dark:bg-gray-800 z-10">
              <tr className="text-gray-500 dark:text-gray-400 text-left border-b border-gray-200 dark:border-gray-700">
                <th className="pb-2 font-medium w-8" />
                <th className="pb-2 font-medium">인증서</th>
                <th className="pb-2 font-medium text-center">유형</th>
                <th className="pb-2 font-medium text-center">국가</th>
                <th className="pb-2 font-medium text-center">심각도</th>
                <th className="pb-2 font-medium text-center">위반 수</th>
                <th className="pb-2 font-medium text-center">점수</th>
              </tr>
            </thead>
            <tbody>
              {anomalies.slice(0, 100).map((a, idx) => {
                const isExpanded = expandedRows.has(idx);
                const sev = deriveSeverity(a.structural_score);
                const Icon = SEVERITY_ICON[sev] || AlertTriangle;
                const violationCount = totalViolations(a);
                return (
                  <tr key={`row-${idx}`} className="contents">
                    <td colSpan={7} className="p-0">
                      <table className="w-full">
                        <tbody>
                          <tr
                            className="border-t border-gray-100 dark:border-gray-700 cursor-pointer hover:bg-gray-50 dark:hover:bg-gray-750"
                            onClick={() => toggleRow(idx)}
                          >
                            <td className="py-1.5 w-8">
                              {isExpanded ? (
                                <ChevronDown className="w-3.5 h-3.5 text-gray-400" />
                              ) : (
                                <ChevronRight className="w-3.5 h-3.5 text-gray-400" />
                              )}
                            </td>
                            <td className="py-1.5 text-gray-700 dark:text-gray-300 font-mono text-xs" title={a.fingerprint}>
                              {a.fingerprint.slice(0, 16)}...
                            </td>
                            <td className="py-1.5 text-center">
                              <span className="px-1.5 py-0.5 rounded text-xs bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300">
                                {a.certificate_type || '-'}
                              </span>
                            </td>
                            <td className="py-1.5 text-center text-xs">{a.country_code || '-'}</td>
                            <td className="py-1.5 text-center">
                              <span className={`inline-flex items-center gap-1 px-1.5 py-0.5 rounded text-xs font-medium ${SEVERITY_STYLE[sev] || ''}`}>
                                <Icon className="w-3 h-3" />
                                {sev}
                              </span>
                            </td>
                            <td className="py-1.5 text-center">{violationCount}</td>
                            <td className="py-1.5 text-center font-medium">
                              {(a.structural_score * 100).toFixed(0)}
                            </td>
                          </tr>
                          {isExpanded && (
                            <tr className="bg-gray-50 dark:bg-gray-900/30">
                              <td colSpan={7} className="px-4 py-3">
                                <div className="space-y-1.5">
                                  {a.violations_detail.map((v, vi) => (
                                    <div key={vi} className="flex items-start gap-2 text-xs">
                                      <span className={`px-1.5 py-0.5 rounded font-medium ${
                                        v.severity === 'CRITICAL' ? 'text-red-600 bg-red-50 dark:text-red-400 dark:bg-red-900/20' :
                                        v.severity === 'HIGH' ? 'text-orange-600 bg-orange-50 dark:text-orange-400 dark:bg-orange-900/20' :
                                        'text-amber-600 bg-amber-50 dark:text-amber-400 dark:bg-amber-900/20'
                                      }`}>
                                        {v.severity}
                                      </span>
                                      <span className="text-gray-600 dark:text-gray-400">{v.rule}</span>
                                    </div>
                                  ))}
                                  {a.missing_required.length > 0 && (
                                    <div className="text-xs text-red-500">
                                      필수 누락: {a.missing_required.join(', ')}
                                    </div>
                                  )}
                                  {a.forbidden_violations.length > 0 && (
                                    <div className="text-xs text-orange-500">
                                      금지 위반: {a.forbidden_violations.join(', ')}
                                    </div>
                                  )}
                                  {a.key_usage_violations.length > 0 && (
                                    <div className="text-xs text-amber-500">
                                      키 사용 위반: {a.key_usage_violations.join(', ')}
                                    </div>
                                  )}
                                </div>
                              </td>
                            </tr>
                          )}
                        </tbody>
                      </table>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}
