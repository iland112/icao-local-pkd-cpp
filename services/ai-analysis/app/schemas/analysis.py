from datetime import datetime
from typing import Optional

from pydantic import BaseModel


class CertificateAnalysis(BaseModel):
    fingerprint: str
    certificate_type: str | None = None
    country_code: str | None = None
    anomaly_score: float
    anomaly_label: str
    risk_score: float
    risk_level: str
    risk_factors: dict
    anomaly_explanations: list[str]
    analyzed_at: datetime | None = None

    class Config:
        from_attributes = True


class AnalysisStatistics(BaseModel):
    total_analyzed: int
    normal_count: int
    suspicious_count: int
    anomalous_count: int
    risk_distribution: dict[str, int]
    avg_risk_score: float
    top_anomalous_countries: list[dict]
    last_analysis_at: datetime | None = None
    model_version: str


class AnalysisJobStatus(BaseModel):
    status: str  # IDLE / RUNNING / COMPLETED / FAILED
    progress: float  # 0.0 ~ 1.0
    total_certificates: int
    processed_certificates: int
    started_at: datetime | None = None
    completed_at: datetime | None = None
    error_message: str | None = None


class AnomalyListResponse(BaseModel):
    success: bool
    items: list[CertificateAnalysis]
    total: int
    page: int
    size: int


class CountryMaturity(BaseModel):
    country_code: str
    country_name: str
    maturity_score: float
    algorithm_score: float
    key_size_score: float
    compliance_score: float
    extension_score: float
    freshness_score: float
    certificate_count: int


class AlgorithmTrend(BaseModel):
    year: int
    algorithms: dict[str, int]  # {"sha256WithRSAEncryption": 1234, ...}
    total: int


class KeySizeDistribution(BaseModel):
    algorithm: str
    key_size: int
    count: int
    percentage: float


class RiskDistribution(BaseModel):
    risk_level: str
    count: int
    percentage: float
    avg_anomaly_score: float


class CountryDetail(BaseModel):
    country_code: str
    country_name: str
    total_certificates: int
    type_distribution: dict[str, int]
    algorithm_distribution: dict[str, int]
    key_size_distribution: dict[str, int]
    risk_distribution: dict[str, int]
    anomaly_distribution: dict[str, int]
    maturity: CountryMaturity | None = None
    top_anomalies: list[CertificateAnalysis]
