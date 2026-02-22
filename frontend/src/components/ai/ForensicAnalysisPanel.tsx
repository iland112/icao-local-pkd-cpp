import { useState, useEffect } from 'react';
import { Shield, AlertTriangle, CheckCircle, Info, XCircle } from 'lucide-react';
import aiAnalysisApi, { type ForensicDetail } from '@/services/aiAnalysisApi';

interface ForensicAnalysisPanelProps {
  fingerprint: string;
}

const CATEGORY_LABELS: Record<string, string> = {
  algorithm: '서명 알고리즘',
  key_size: '키 크기',
  compliance: 'ICAO 준수',
  validity: '유효기간',
  extensions: '확장 필드',
  anomaly: 'ML 이상 탐지',
  issuer_reputation: '발급자 평판',
  structural_consistency: '구조 일관성',
  temporal_pattern: '시간 패턴',
  dn_consistency: 'DN 일관성',
};

const CATEGORY_MAX: Record<string, number> = {
  algorithm: 40, key_size: 40, compliance: 20, validity: 15,
  extensions: 15, anomaly: 15, issuer_reputation: 15,
  structural_consistency: 20, temporal_pattern: 10, dn_consistency: 10,
};

const SEVERITY_COLORS: Record<string, string> = {
  CRITICAL: 'text-red-600 bg-red-50 dark:text-red-400 dark:bg-red-900/30',
  HIGH: 'text-orange-600 bg-orange-50 dark:text-orange-400 dark:bg-orange-900/30',
  MEDIUM: 'text-amber-600 bg-amber-50 dark:text-amber-400 dark:bg-amber-900/30',
  LOW: 'text-green-600 bg-green-50 dark:text-green-400 dark:bg-green-900/30',
};

const LEVEL_COLORS: Record<string, string> = {
  CRITICAL: 'bg-red-500',
  HIGH: 'bg-orange-500',
  MEDIUM: 'bg-amber-500',
  LOW: 'bg-green-500',
};

function SeverityIcon({ severity }: { severity: string }) {
  switch (severity) {
    case 'CRITICAL': return <XCircle className="w-4 h-4 text-red-500" />;
    case 'HIGH': return <AlertTriangle className="w-4 h-4 text-orange-500" />;
    case 'MEDIUM': return <Info className="w-4 h-4 text-amber-500" />;
    default: return <CheckCircle className="w-4 h-4 text-green-500" />;
  }
}

export default function ForensicAnalysisPanel({ fingerprint }: ForensicAnalysisPanelProps) {
  const [detail, setDetail] = useState<ForensicDetail | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!fingerprint) return;
    setLoading(true);
    setError(null);
    aiAnalysisApi.getCertificateForensic(fingerprint)
      .then(res => setDetail(res.data))
      .catch(() => setError('포렌식 분석 데이터를 불러올 수 없습니다'))
      .finally(() => setLoading(false));
  }, [fingerprint]);

  if (loading) {
    return (
      <div className="flex items-center justify-center py-12">
        <div className="animate-spin rounded-full h-8 w-8 border-2 border-blue-500 border-t-transparent" />
        <span className="ml-3 text-gray-500">포렌식 분석 로딩 중...</span>
      </div>
    );
  }

  if (error || !detail) {
    return (
      <div className="text-center py-8 text-gray-500 dark:text-gray-400">
        <Shield className="w-10 h-10 mx-auto mb-2 text-gray-300" />
        <p>{error || '분석 데이터가 없습니다. 전체 분석을 먼저 실행하세요.'}</p>
      </div>
    );
  }

  const categories = detail.forensic_findings?.categories || {};
  const findings = detail.forensic_findings?.findings || [];

  return (
    <div className="space-y-4">
      {/* Forensic Score Summary */}
      <div className="flex items-center gap-4 p-4 rounded-lg bg-gray-50 dark:bg-gray-800">
        <div className="flex-shrink-0">
          <div className={`w-14 h-14 rounded-full flex items-center justify-center text-white font-bold text-lg ${LEVEL_COLORS[detail.forensic_risk_level] || 'bg-gray-400'}`}>
            {Math.round(detail.forensic_risk_score)}
          </div>
        </div>
        <div className="flex-1">
          <div className="flex items-center gap-2">
            <span className="text-lg font-semibold dark:text-white">포렌식 리스크</span>
            <span className={`px-2 py-0.5 rounded text-xs font-medium ${SEVERITY_COLORS[detail.forensic_risk_level] || ''}`}>
              {detail.forensic_risk_level}
            </span>
          </div>
          <div className="flex gap-4 mt-1 text-sm text-gray-500 dark:text-gray-400">
            <span>이상 탐지: {(detail.anomaly_score * 100).toFixed(1)}%</span>
            <span>구조 위반: {(detail.structural_anomaly_score * 100).toFixed(1)}%</span>
            <span>발급자 편차: {(detail.issuer_anomaly_score * 100).toFixed(1)}%</span>
          </div>
        </div>
      </div>

      {/* Category Scores Bar Chart */}
      <div className="rounded-lg border border-gray-200 dark:border-gray-700 p-4">
        <h4 className="text-sm font-semibold mb-3 dark:text-white">10개 카테고리별 점수</h4>
        <div className="space-y-2">
          {Object.entries(CATEGORY_LABELS).map(([key, label]) => {
            const score = categories[key] || 0;
            const max = CATEGORY_MAX[key] || 10;
            const pct = Math.min((score / max) * 100, 100);
            const color = pct >= 60 ? 'bg-red-500' : pct >= 30 ? 'bg-amber-500' : 'bg-green-500';
            return (
              <div key={key} className="flex items-center gap-2">
                <span className="text-xs text-gray-600 dark:text-gray-400 w-24 text-right">{label}</span>
                <div className="flex-1 h-4 bg-gray-100 dark:bg-gray-700 rounded-full overflow-hidden">
                  <div className={`h-full ${color} rounded-full transition-all`} style={{ width: `${pct}%` }} />
                </div>
                <span className="text-xs text-gray-500 w-16 text-right">{score}/{max}</span>
              </div>
            );
          })}
        </div>
      </div>

      {/* Findings List */}
      {findings.length > 0 && (
        <div className="rounded-lg border border-gray-200 dark:border-gray-700 p-4">
          <h4 className="text-sm font-semibold mb-3 dark:text-white">
            발견 사항 ({findings.length}건)
          </h4>
          <div className="space-y-2">
            {findings.map((f, idx) => (
              <div key={idx} className={`flex items-start gap-2 px-3 py-2 rounded-md ${SEVERITY_COLORS[f.severity] || ''}`}>
                <SeverityIcon severity={f.severity} />
                <div className="flex-1">
                  <span className="text-sm">{f.message}</span>
                  <span className="ml-2 text-xs opacity-60">[{CATEGORY_LABELS[f.category] || f.category}]</span>
                </div>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Anomaly Explanations */}
      {detail.anomaly_explanations.length > 0 && (
        <div className="rounded-lg border border-gray-200 dark:border-gray-700 p-4">
          <h4 className="text-sm font-semibold mb-3 dark:text-white">ML 이상 탐지 설명</h4>
          <ul className="space-y-1">
            {detail.anomaly_explanations.map((exp, idx) => (
              <li key={idx} className="text-sm text-gray-600 dark:text-gray-400 flex items-center gap-2">
                <span className="w-1.5 h-1.5 rounded-full bg-amber-400 flex-shrink-0" />
                {exp}
              </li>
            ))}
          </ul>
        </div>
      )}
    </div>
  );
}
