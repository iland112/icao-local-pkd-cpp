import pkdApi from '@/services/pkdApi';

// --- Types ---

export interface CertificateAnalysis {
  fingerprint: string;
  certificate_type: string | null;
  country_code: string | null;
  anomaly_score: number;
  anomaly_label: 'NORMAL' | 'SUSPICIOUS' | 'ANOMALOUS';
  risk_score: number;
  risk_level: 'LOW' | 'MEDIUM' | 'HIGH' | 'CRITICAL';
  risk_factors: Record<string, number>;
  anomaly_explanations: string[];
  analyzed_at: string | null;
}

export interface AnalysisStatistics {
  total_analyzed: number;
  normal_count: number;
  suspicious_count: number;
  anomalous_count: number;
  risk_distribution: Record<string, number>;
  avg_risk_score: number;
  top_anomalous_countries: {
    country: string;
    total: number;
    anomalous: number;
    anomaly_rate: number;
  }[];
  last_analysis_at: string | null;
  model_version: string;
  forensic_level_distribution: Record<string, number> | null;
  avg_forensic_score: number | null;
}

export interface ForensicDetail {
  fingerprint: string;
  certificate_type: string | null;
  country_code: string | null;
  anomaly_score: number;
  anomaly_label: string;
  risk_score: number;
  risk_level: string;
  risk_factors: Record<string, number>;
  anomaly_explanations: string[];
  forensic_risk_score: number;
  forensic_risk_level: string;
  forensic_findings: {
    score: number;
    level: string;
    findings: { category: string; severity: string; message: string }[];
    categories: Record<string, number>;
  };
  structural_anomaly_score: number;
  issuer_anomaly_score: number;
  temporal_anomaly_score: number;
  analyzed_at: string | null;
}

export interface IssuerProfile {
  issuer_dn: string;
  cert_count: number;
  type_diversity: number;
  types: Record<string, number>;
  dominant_algorithm: string;
  avg_key_size: number;
  compliance_rate: number;
  expired_rate: number;
  risk_indicator: 'LOW' | 'MEDIUM' | 'HIGH';
  country: string;
}

export interface ExtensionAnomaly {
  fingerprint: string;
  certificate_type: string | null;
  country_code: string | null;
  structural_score: number;
  missing_required: string[];
  missing_recommended: string[];
  forbidden_violations: string[];
  key_usage_violations: string[];
  violations_detail: { rule: string; severity: string }[];
}

export interface ForensicSummary {
  total_analyzed: number;
  forensic_level_distribution: Record<string, number>;
  category_avg_scores: Record<string, number>;
  severity_distribution: Record<string, number> | null;
  top_findings: { message: string; count: number }[] | null;
}

export interface AnalysisJobStatus {
  status: 'IDLE' | 'RUNNING' | 'COMPLETED' | 'FAILED';
  progress: number;
  total_certificates: number;
  processed_certificates: number;
  started_at: string | null;
  completed_at: string | null;
  error_message: string | null;
}

export interface AnomalyListResponse {
  success: boolean;
  items: CertificateAnalysis[];
  total: number;
  page: number;
  size: number;
}

export interface CountryMaturity {
  country_code: string;
  country_name: string;
  maturity_score: number;
  algorithm_score: number;
  key_size_score: number;
  compliance_score: number;
  extension_score: number;
  freshness_score: number;
  certificate_count: number;
}

export interface AlgorithmTrend {
  year: number;
  algorithms: Record<string, number>;
  total: number;
}

export interface KeySizeDistribution {
  algorithm: string;
  key_size: number;
  count: number;
  percentage: number;
}

export interface RiskDistribution {
  risk_level: string;
  count: number;
  percentage: number;
  avg_anomaly_score: number;
}

// --- API Functions ---

export const aiAnalysisApi = {
  // Health check
  getHealth: () => pkdApi.get('/ai/health'),

  // Trigger full analysis
  triggerAnalysis: () => pkdApi.post<{ success: boolean; message: string }>('/ai/analyze'),

  // Get analysis job status
  getAnalysisStatus: () => pkdApi.get<AnalysisJobStatus>('/ai/analyze/status'),

  // Get single certificate analysis
  getCertificateAnalysis: (fingerprint: string) =>
    pkdApi.get<CertificateAnalysis>(`/ai/certificate/${fingerprint}`),

  // List anomalies with filters
  getAnomalies: (params?: {
    country?: string;
    type?: string;
    label?: string;
    risk_level?: string;
    page?: number;
    size?: number;
  }) => pkdApi.get<AnomalyListResponse>('/ai/anomalies', { params }),

  // Get overall statistics
  getStatistics: () => pkdApi.get<AnalysisStatistics>('/ai/statistics'),

  // Reports
  getCountryMaturity: () => pkdApi.get<CountryMaturity[]>('/ai/reports/country-maturity'),

  getAlgorithmTrends: () => pkdApi.get<AlgorithmTrend[]>('/ai/reports/algorithm-trends'),

  getKeySizeDistribution: () =>
    pkdApi.get<KeySizeDistribution[]>('/ai/reports/key-size-distribution'),

  getRiskDistribution: () => pkdApi.get<RiskDistribution[]>('/ai/reports/risk-distribution'),

  getCountryReport: (code: string) => pkdApi.get(`/ai/reports/country/${code}`),

  // Forensic endpoints (v2.19.0)
  getCertificateForensic: (fingerprint: string) =>
    pkdApi.get<ForensicDetail>(`/ai/certificate/${fingerprint}/forensic`),

  triggerIncrementalAnalysis: (uploadId?: string) =>
    pkdApi.post<{ success: boolean; message: string }>('/ai/analyze/incremental', { upload_id: uploadId }),

  getIssuerProfiles: () => pkdApi.get<IssuerProfile[]>('/ai/reports/issuer-profiles'),

  getForensicSummary: () => pkdApi.get<ForensicSummary>('/ai/reports/forensic-summary'),

  getExtensionAnomalies: (params?: { type?: string; country?: string; limit?: number }) =>
    pkdApi.get<ExtensionAnomaly[]>('/ai/reports/extension-anomalies', { params }),
};

export default aiAnalysisApi;
