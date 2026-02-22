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
        <div className="flex flex-wrap gap-2">
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
        <div className="max-h-96 overflow-y-auto">
          {/* Header */}
          <div className="grid grid-cols-[24px_1fr_56px_40px_72px_48px_40px] gap-1 text-xs text-gray-500 dark:text-gray-400 font-medium pb-2 border-b border-gray-200 dark:border-gray-700 sticky top-0 bg-white dark:bg-gray-800 z-10 px-1">
            <div />
            <div>인증서</div>
            <div className="text-center">유형</div>
            <div className="text-center">국가</div>
            <div className="text-center">심각도</div>
            <div className="text-center">위반</div>
            <div className="text-center">점수</div>
          </div>

          {/* Rows */}
          <div className="divide-y divide-gray-100 dark:divide-gray-700">
            {anomalies.slice(0, 100).map((a, idx) => {
              const isExpanded = expandedRows.has(idx);
              const sev = deriveSeverity(a.structural_score);
              const Icon = SEVERITY_ICON[sev] || AlertTriangle;
              const violationCount = totalViolations(a);
              return (
                <div key={idx}>
                  <div
                    className="grid grid-cols-[24px_1fr_56px_40px_72px_48px_40px] gap-1 items-center py-1.5 px-1 cursor-pointer hover:bg-gray-50 dark:hover:bg-gray-750"
                    onClick={() => toggleRow(idx)}
                  >
                    <div>
                      {isExpanded ? (
                        <ChevronDown className="w-3.5 h-3.5 text-gray-400" />
                      ) : (
                        <ChevronRight className="w-3.5 h-3.5 text-gray-400" />
                      )}
                    </div>
                    <div className="text-gray-700 dark:text-gray-300 font-mono text-xs truncate" title={a.fingerprint}>
                      {a.fingerprint.slice(0, 16)}...
                    </div>
                    <div className="text-center">
                      <span className="px-1.5 py-0.5 rounded text-xs bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300">
                        {a.certificate_type || '-'}
                      </span>
                    </div>
                    <div className="text-center text-xs">{a.country_code || '-'}</div>
                    <div className="text-center">
                      <span className={`inline-flex items-center gap-0.5 px-1.5 py-0.5 rounded text-xs font-medium ${SEVERITY_STYLE[sev] || ''}`}>
                        <Icon className="w-3 h-3" />
                        {sev}
                      </span>
                    </div>
                    <div className="text-center text-sm">{violationCount}</div>
                    <div className="text-center text-sm font-medium">
                      {(a.structural_score * 100).toFixed(0)}
                    </div>
                  </div>
                  {isExpanded && (
                    <div className="bg-gray-50 dark:bg-gray-900/30 px-4 py-3">
                      <div className="space-y-1.5">
                        {a.violations_detail.map((v, vi) => (
                          <div key={vi} className="flex items-start gap-2 text-xs">
                            <span className={`shrink-0 px-1.5 py-0.5 rounded font-medium ${
                              v.severity === 'CRITICAL' ? 'text-red-600 bg-red-50 dark:text-red-400 dark:bg-red-900/20' :
                              v.severity === 'HIGH' ? 'text-orange-600 bg-orange-50 dark:text-orange-400 dark:bg-orange-900/20' :
                              'text-amber-600 bg-amber-50 dark:text-amber-400 dark:bg-amber-900/20'
                            }`}>
                              {v.severity}
                            </span>
                            <span className="text-gray-600 dark:text-gray-400 break-words">{v.rule}</span>
                          </div>
                        ))}
                        {a.missing_required.length > 0 && (
                          <div className="text-xs text-red-500 break-words">
                            필수 누락: {a.missing_required.join(', ')}
                          </div>
                        )}
                        {a.forbidden_violations.length > 0 && (
                          <div className="text-xs text-orange-500 break-words">
                            금지 위반: {a.forbidden_violations.join(', ')}
                          </div>
                        )}
                        {a.key_usage_violations.length > 0 && (
                          <div className="text-xs text-amber-500 break-words">
                            키 사용 위반: {a.key_usage_violations.join(', ')}
                          </div>
                        )}
                      </div>
                    </div>
                  )}
                </div>
              );
            })}
          </div>
        </div>
      )}
    </div>
  );
}
